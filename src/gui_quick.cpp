#include "core.hpp"
#include "codex_sync.h"
#include "google_drive.hpp"
#include "job_coordinator.hpp"
#include "change_monitor.hpp"
#include "disclaimer.hpp"
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QFileDialog>
#include <QMessageBox>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QClipboard>
#include <QDateTime>
#include <QStandardPaths>
#include <QSysInfo>
#include <QDir>
#include <QTimer>
#include <QElapsedTimer>
#include <QSaveFile>
#include <QStyleHints>
#include <QIcon>
#include <QDesktopServices>
#include <QFileInfo>
#include <QSettings>
#include <QCryptographicHash>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QCloseEvent>
#include <QPointer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLocale>
#include "gui_translator.hpp"
#include <sodium.h>
using cxs::Json;
static QString q(const std::string& s){return QString::fromUtf8(s.data(),static_cast<qsizetype>(s.size()));}
static std::string s(const QString& v){return v.toUtf8().toStdString();}
static void startupReport(const QJsonObject& update){auto filename=qEnvironmentVariable("CXS_STARTUP_REPORT");if(filename.isEmpty())return;static QJsonObject report;for(auto it=update.begin();it!=update.end();++it)report[it.key()]=it.value();QSaveFile output(filename);if(output.open(QIODevice::WriteOnly)){output.write(QJsonDocument(report).toJson());output.commit();}}
static QString applicationData(){return q(cxs::utf8(cxs::application_data_directory()));}
static QString preferencesPath(const QString& smoke){
    if(!smoke.isEmpty())return smoke+QStringLiteral("/notices.ini");
    cxs::private_directory(cxs::application_data_directory());
    const auto current=applicationData()+QStringLiteral("/notices.ini");
    if(!cxs::portable_mode()&&!QFileInfo::exists(current)){
        const auto legacy=QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)+QStringLiteral("/notices.ini");
        if(QFileInfo::exists(legacy)&&!QFile::copy(legacy,current))throw std::runtime_error("cannot preserve existing preferences");
    }
    return current;
}
class Bridge final:public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap configuration READ configuration NOTIFY configurationChanged)
    Q_PROPERTY(QString configFile READ configFile NOTIFY configurationChanged)
    Q_PROPERTY(QString keyFile READ keyFile NOTIFY configurationChanged)
    Q_PROPERTY(QString deviceName READ deviceName CONSTANT)
    Q_PROPERTY(QString platform READ platform CONSTANT)
    Q_PROPERTY(bool portableEdition READ portableEdition CONSTANT)
    Q_PROPERTY(QString dataDirectory READ dataDirectory CONSTANT)
    Q_PROPERTY(QString applicationVersion READ applicationVersion CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString lastActivity READ lastActivity NOTIFY stateChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool apiRunning READ apiRunning NOTIFY stateChanged)
    Q_PROPERTY(QString apiToken READ apiToken NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap conversationHistory READ conversationHistory NOTIFY stateChanged)
    Q_PROPERTY(bool googleAuthorized READ googleAuthorized NOTIFY stateChanged)
    Q_PROPERTY(bool googleClientReady READ googleClientReady NOTIFY configurationChanged)
    Q_PROPERTY(bool noticeAcknowledged READ noticeAcknowledged NOTIFY stateChanged)
    Q_PROPERTY(QString disclaimerText READ disclaimerText NOTIFY languageChanged)
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)
    Q_PROPERTY(bool englishLanguage READ englishLanguage NOTIFY languageChanged)
    Q_PROPERTY(int themeMode READ themeMode NOTIFY appearanceChanged)
    Q_PROPERTY(QString terminalCommand READ terminalCommand CONSTANT)
    Q_PROPERTY(QVariantList snapshots READ snapshots NOTIFY stateChanged)
    Q_PROPERTY(QString googleLoginMessage READ googleLoginMessage NOTIFY stateChanged)
    Q_PROPERTY(QString googleConnectionState READ googleConnectionState NOTIFY stateChanged)
    Q_PROPERTY(QString googleConnectionLabel READ googleConnectionLabel NOTIFY stateChanged)
    Q_PROPERTY(QString googleFolderUrl READ googleFolderUrl NOTIFY stateChanged)
    Q_PROPERTY(QString googleStorageLabel READ googleStorageLabel NOTIFY configurationChanged)
    Q_PROPERTY(QString encryptionLabel READ encryptionLabel NOTIFY configurationChanged)
    Q_PROPERTY(QString googleSnapshotStatus READ googleSnapshotStatus NOTIFY stateChanged)
    Q_PROPERTY(QString syncMode READ syncMode NOTIFY configurationChanged)
    Q_PROPERTY(QVariantMap dataTypes READ dataTypes NOTIFY configurationChanged)
    Q_PROPERTY(QVariantMap itemCatalog READ itemCatalog NOTIFY catalogChanged)
    Q_PROPERTY(bool configurationSaved READ configurationSaved NOTIFY configurationChanged)
    Q_PROPERTY(bool autoSync READ autoSync NOTIFY stateChanged)
    Q_PROPERTY(QString autoTrigger READ autoTrigger NOTIFY stateChanged)
    Q_PROPERTY(int autoIntervalMinutes READ autoIntervalMinutes NOTIFY stateChanged)
    Q_PROPERTY(int autoThresholdMB READ autoThresholdMB NOTIFY stateChanged)
    Q_PROPERTY(QString autoPending READ autoPending NOTIFY stateChanged)
    Q_PROPERTY(bool startOnLogin READ startOnLogin NOTIFY stateChanged)
    Q_PROPERTY(bool closeToTray READ closeToTray NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap syncProgress READ syncProgress NOTIFY stateChanged)
    Q_PROPERTY(QString progressLabel READ progressLabel NOTIFY stateChanged)
    Q_PROPERTY(QString progressDetail READ progressDetail NOTIFY stateChanged)
    Q_PROPERTY(double progressValue READ progressValue NOTIFY stateChanged)
    Q_PROPERTY(bool progressIndeterminate READ progressIndeterminate NOTIFY stateChanged)
    QString folderUrl;QFileSystemWatcher stateWatcher;ChangeMonitor sourceMonitor;bool externalBusy=false,autoDirty=true,autoDue=false;qulonglong pendingBytes=0,pendingFiles=0;
    QVariantMap progressData;QString progressId;QElapsedTimer progressClock;
    static bool tracksProgress(const QString& op){return op=="backup"||op=="resume"||op=="sync"||op=="restore"||op=="rollback";}
    void readProgress(){if(progressId.isEmpty())return;try{auto data=Json::parse(cxs::read_bytes(cxs::path(config.at("state"))/"progress.json",8192));if(data.value("operation_id",std::string{})!=s(progressId))return;progressData=QJsonDocument::fromJson(QByteArray::fromStdString(data.dump())).object().toVariantMap();if(!inFlight){externalBusy=cxs::JobCoordinator::busy(cxs::path(config.at("state")));if(!externalBusy)progressTimer.stop();}}catch(...){}}
    void attachProgress(){if(inFlight)return;try{auto state=cxs::path(config.at("state"));auto data=Json::parse(cxs::read_bytes(state/"progress.json",8192));const bool owned=cxs::JobCoordinator::busy(state);if(owned){progressId=q(data.at("operation_id"));externalBusy=true;progressTimer.start();}else externalBusy=false;progressData=QJsonDocument::fromJson(QByteArray::fromStdString(data.dump())).object().toVariantMap();emit stateChanged();}catch(...){}}
    void watchState(){auto folders=stateWatcher.directories();if(!folders.empty())stateWatcher.removePaths(folders);auto state=cxs::path(config.at("state"));cxs::private_directory(state);stateWatcher.addPath(q(cxs::utf8(state)));attachProgress();}
    void refreshSourceMonitor(){sourceMonitor.clear();if(autoEnabled)sourceMonitor.configure(config);}
    QTimer autoTimer,sourceDebounce,progressTimer;QSystemTrayIcon tray;QMenu trayMenu;QPointer<QQuickWindow> mainWindow;
    bool inFlight=false,dirty=false,automatic=false,autoEnabled=false,exitPending=false,loginTask=false,downloadPending=false;
    QString loginMessage,googleState,googleVerifiedAt,downloadParent,task;int googleSnapshotCount=-1;
    QString autoSetting()const{return QStringLiteral("auto_sync/")+keySetting().section('/',1);}
    QString scheduleSetting(const QString& field)const{return QStringLiteral("auto_schedule/")+keySetting().section('/',1)+"/"+field;}
    void updateAutoTimer(){autoTimer.setInterval(autoTrigger()=="time"?autoIntervalMinutes()*60000:300000);sourceDebounce.setInterval(autoTrigger()=="amount"?10000:1500);}
    void changed(){dirty=true;folderUrl.clear();googleState.clear();googleVerifiedAt.clear();googleSnapshotCount=-1;loginMessage.clear();if(autoEnabled)setAutoSync(false);emit configurationChanged();emit stateChanged();}
    bool remoteReady()const{auto r=config.value("remote",Json::object());auto provider=r.value("provider",std::string("local"));return provider=="google_drive"?googleAuthorized():provider=="local"?r.contains("directory")&&!r.value("directory",std::string()).empty():r.contains("url")&&!r.value("url",std::string()).empty()&&r.value("url",std::string()).find("your-server.example")==std::string::npos;}
    void showWindow(){if(mainWindow){mainWindow->showNormal();mainWindow->raise();mainWindow->requestActivate();}}
    void quitSafely(){autoTimer.stop();sourceDebounce.stop();if(busy()){exitPending=true;statusText=QCoreApplication::translate("Bridge","当前操作完成后退出");emit stateChanged();return;}stopApi();qApp->quit();}
    bool eventFilter(QObject* object,QEvent* event)override{if(object==mainWindow&&event->type()==QEvent::Close){if(closeToTray()&&QSystemTrayIcon::isSystemTrayAvailable()&&!exitPending){mainWindow->hide();static_cast<QCloseEvent*>(event)->ignore();return true;}if(busy()){quitSafely();static_cast<QCloseEvent*>(event)->ignore();return true;}}return QObject::eventFilter(object,event);}
    Json config=cxs::discover();QString filename,key,keyPassword,username,password,googleSecret,statusText=QCoreApplication::translate("Bridge","尚未配置"),activity=QCoreApplication::translate("Bridge","还没有同步记录"),log;
    QFutureWatcher<QString> watcher;QProcess process;QString secret;QVariantList snapshotList;QVariantMap historyCoverage;
    QSettings notices;int backendOperations=0;Json catalogData=Json::object();
    EnglishTranslator englishTranslator;QString languageSelection=QStringLiteral("zh_CN");int selectedTheme=0;
    void applyLanguage(){if(englishLanguage())qApp->installTranslator(&englishTranslator);else qApp->removeTranslator(&englishTranslator);for(auto action:trayMenu.actions()){if(action==trayMenu.actions().first())action->setText(QCoreApplication::translate("Bridge","打开 CodexSync"));else action->setText(QCoreApplication::translate("Bridge","退出"));}emit languageChanged();emit stateChanged();}
    void applyTheme(){QGuiApplication::styleHints()->setColorScheme(selectedTheme==1?Qt::ColorScheme::Light:selectedTheme==2?Qt::ColorScheme::Dark:Qt::ColorScheme::Unknown);emit appearanceChanged();}
    QString keySetting()const{if(cxs::portable_mode())return QStringLiteral("key_files/portable");return QStringLiteral("key_files/")+QString::fromLatin1(QCryptographicHash::hash(QFileInfo(filename).absoluteFilePath().toUtf8(),QCryptographicHash::Sha256).toHex());}
    QString storedPath(const QString& file)const{return cxs::portable_mode()?QDir(applicationData()).relativeFilePath(file):file;}
    QString restoredPath(const QString& file)const{if(file.isEmpty())return {};if(!cxs::portable_mode())return file;auto relative=QDir::cleanPath(file);if(QDir::isAbsolutePath(relative)||relative==".."||relative.startsWith("../"))return {};return QDir(applicationData()).absoluteFilePath(relative);}
    void rememberConfiguration(){if(filename.isEmpty())return;notices.setValue(QStringLiteral("last_config"),storedPath(filename));if(!key.isEmpty())notices.setValue(keySetting(),storedPath(key));notices.sync();}
    bool requireNotice(){if(noticeAcknowledged())return true;emit noticeRequested();return false;}
    bool save(){if(filename.isEmpty()||cxs::portable_mode())filename=q(cxs::utf8(cxs::default_configuration_path()));cxs::normalize_application_config(config);
        try{auto text=config.dump(2);cxs::write_atomic(cxs::path(s(filename)),cxs::Bytes(text.begin(),text.end()));dirty=false;rememberConfiguration();emit configurationChanged();return true;}catch(const std::exception& e){error(q(e.what()));return false;}}
    void error(const QString& message){statusText=QCoreApplication::translate("Bridge","操作失败");log+=message+QStringLiteral("\n");emit stateChanged();QMessageBox::warning(nullptr,QStringLiteral("CodexSync"),message);}
public:
    explicit Bridge(const QString& file,const QString& smoke={}):notices(preferencesPath(smoke),QSettings::IniFormat){languageSelection=notices.value(QStringLiteral("appearance/language"),QStringLiteral("zh_CN")).toString();if(languageSelection!="zh_CN"&&languageSelection!="en"&&languageSelection!="system")languageSelection="zh_CN";selectedTheme=notices.value(QStringLiteral("appearance/theme"),0).toInt();if(selectedTheme<0||selectedTheme>2)selectedTheme=0;applyLanguage();applyTheme();statusText=QCoreApplication::translate("Bridge","尚未配置");activity=QCoreApplication::translate("Bridge","还没有同步记录");if(!file.isEmpty())load(file,true);else{auto selected=restoredPath(notices.value(QStringLiteral("last_config")).toString());if(selected.isEmpty())selected=q(cxs::utf8(cxs::default_configuration_path()));if(QFileInfo::exists(selected))load(selected,true);}
        cxs::runtime_log(config.contains("state")?cxs::path(config.at("state")):cxs::application_data_directory()/"state","gui_started");
        QObject::connect(&stateWatcher,&QFileSystemWatcher::directoryChanged,this,[this]{attachProgress();});
        sourceDebounce.setSingleShot(true);connect(&sourceDebounce,&QTimer::timeout,this,[this]{if(autoDirty)automaticTick();});
        sourceMonitor.changed=[this]{autoDirty=true;if(autoEnabled&&autoTrigger()!="time"&&!sourceDebounce.isActive())sourceDebounce.start();};refreshSourceMonitor();watchState();
        updateAutoTimer();autoTimer.setTimerType(Qt::VeryCoarseTimer);
        connect(&autoTimer,&QTimer::timeout,this,[this]{autoDirty=true;autoDue=true;automaticTick();});
        progressTimer.setInterval(250);connect(&progressTimer,&QTimer::timeout,this,[this]{readProgress();emit stateChanged();});
        tray.setIcon(QIcon(QStringLiteral(":/ui/codexsync.png")));tray.setToolTip(QStringLiteral("CodexSync 0.2"));
        trayMenu.addAction(QCoreApplication::translate("Bridge","打开 CodexSync"),this,[this]{showWindow();});trayMenu.addAction(QCoreApplication::translate("Bridge","退出"),this,[this]{quitSafely();});tray.setContextMenu(&trayMenu);
        connect(&tray,&QSystemTrayIcon::activated,this,[this](auto reason){if(reason==QSystemTrayIcon::DoubleClick||reason==QSystemTrayIcon::Trigger)showWindow();});
        if(autoEnabled)autoTimer.start();
        connect(&watcher,&QFutureWatcher<QString>::finished,this,[this]{progressTimer.stop();readProgress();auto text=watcher.result();
            if(tracksProgress(task)&&!progressData.isEmpty()){auto response=Json::parse(s(text),nullptr,false);if(!response.is_discarded()&&response.value("ok",false))progressData["phase"]=response.value("result",Json::object()).value("no_changes",false)?"no_changes":"completed";else if(progressData.value("phase")!="paused"&&progressData.value("phase")!="cancelled")progressData["phase"]="failed";if(progressClock.isValid())progressData["elapsed_ms"]=progressClock.elapsed();}log+=text+QStringLiteral("\n");try{auto value=Json::parse(s(text));if(value.value("ok",false)){statusText=QCoreApplication::translate("Bridge","操作完成");if(!loginTask)activity=QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"))+QCoreApplication::translate("Bridge"," · 操作已完成");auto result=value.at("result");if(result.contains("projects")&&result.contains("skills")){catalogData=result;emit catalogChanged();statusText=QCoreApplication::translate("Bridge","已识别 %1 个对话、%2 个项目、%3 个插件、%4 个 skills").arg(result.at("conversations").size()).arg(result.at("projects").size()).arg(result.at("plugins").size()).arg(result.at("skills").size());}if((task=="backup"||task=="resume")&&result.contains("snapshot")&&!result.value("no_changes",false)){if(config.value("remote",Json::object()).value("provider",std::string())=="google_drive")googleSnapshotCount=-1;statusText=QCoreApplication::translate("Bridge","备份完成 · %1 个文件").arg(result.value("files",uint64_t(0)));activity=QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))+QCoreApplication::translate("Bridge"," · 备份成功");if(result.contains("conversation_history")&&!result["conversation_history"].value("coverage_complete",true)&&!result["conversation_history"].value("selection_limited",false))statusText+=QCoreApplication::translate("Bridge"," · 存在缺失历史引用");notices.setValue(QStringLiteral("last_activity/")+keySetting().section('/',1),activity);notices.sync();}if(result.contains("active")&&result.contains("archived")){historyCoverage=QJsonDocument::fromJson(QByteArray::fromStdString(result.dump())).object().toVariantMap();statusText=result.value("coverage_complete",false)?QCoreApplication::translate("Bridge","历史核查完成"):QCoreApplication::translate("Bridge","历史核查发现缺失引用");activity=QCoreApplication::translate("Bridge","历史对话与归档已核查");}if(result.value("no_changes",false))statusText=QCoreApplication::translate("Bridge","没有变化 · 正文读取 0 字节");if(result.contains("storage")){folderUrl=q(result["storage"].value("folder_url",std::string()));}if(result.contains("snapshots")){if(config.value("remote",Json::object()).value("provider",std::string())=="google_drive")googleSnapshotCount=static_cast<int>(result.at("snapshots").size());snapshotList.clear();for(auto& item:result.at("snapshots")){QVariantMap m;m["id"]=q(item.at("snapshot"));m["time"]=QDateTime::fromSecsSinceEpoch(item.at("created_unix").get<qint64>()).toString(QStringLiteral("yyyy-MM-dd HH:mm"));m["entries"]=item.at("entries").get<qulonglong>();m["conflicts"]=static_cast<int>(item.at("conflicts").size());snapshotList.push_back(m);}}}else{statusText=QCoreApplication::translate("Bridge","操作失败");activity=q(value.value("error",std::string("查看操作详情")));}}catch(...){statusText=QCoreApplication::translate("Bridge","无法解析操作结果");}if(loginTask)finishGoogleTask(text);
            if(automatic){automatic=false;auto response=Json::parse(s(text),nullptr,false);if(response.is_discarded()||!response.value("ok",false)){setAutoSync(false);statusText=QCoreApplication::translate("Bridge","自动同步已暂停，请检查操作详情");}}
            if(downloadPending){downloadPending=false;auto response=Json::parse(s(text),nullptr,false);if(!response.is_discarded()&&response.value("ok",false)){auto list=response["result"].value("snapshots",Json::array());if(list.empty())statusText=QCoreApplication::translate("Bridge","远端尚无可下载快照");else{auto newest=*std::max_element(list.begin(),list.end(),[](auto& a,auto& b){return a.at("created_unix")<b.at("created_unix");});auto id=newest.at("snapshot").get<std::string>();auto output=s(downloadParent);QTimer::singleShot(0,this,[this,id,output]{execute({{"op","restore"},{"snapshot",id},{"output",output}});});}}}
            bool thresholdReached=false;
            if(task=="changes"){auto response=Json::parse(s(text),nullptr,false);if(response.is_object()&&response.value("ok",false)){const auto& result=response.at("result");pendingBytes=result.value("bytes",uint64_t(0));pendingFiles=result.value("files",uint64_t(0));thresholdReached=result.value("pending_job",false)||pendingBytes>=uint64_t(autoThresholdMB())*1024*1024;statusText=autoPending();}}
            else if(task=="backup"||task=="resume"){auto response=Json::parse(s(text),nullptr,false);if(response.is_object()&&response.value("ok",false)){pendingBytes=0;pendingFiles=0;}}
            inFlight=false;emit stateChanged();if(autoEnabled&&thresholdReached)QTimer::singleShot(0,this,[this]{startAutomaticBackup();});else if(autoEnabled&&autoDirty&&autoTrigger()!="time"&&!sourceDebounce.isActive())sourceDebounce.start();else if(autoEnabled&&autoDirty&&autoDue)QTimer::singleShot(0,this,[this]{automaticTick();});if(exitPending)QTimer::singleShot(0,this,[this]{quitSafely();});});
        connect(&process,&QProcess::stateChanged,this,[this]{emit stateChanged();});connect(&process,&QProcess::readyReadStandardOutput,this,[this]{log+=QString::fromUtf8(process.readAllStandardOutput());emit stateChanged();});connect(&process,&QProcess::errorOccurred,this,[this]{statusText=QCoreApplication::translate("Bridge","API 启动失败");emit stateChanged();});
    }
    ~Bridge(){stopApi();watcher.waitForFinished();}
    QVariantMap configuration()const{return QJsonDocument::fromJson(QByteArray::fromStdString(config.dump())).object().toVariantMap();}
    QVariantMap syncProgress()const{return progressData;}
    QString progressLabel()const{
        const auto phase=progressData.value("phase").toString();
        if(phase=="snapshotting")return QCoreApplication::translate("Bridge","创建 Windows 快照 · 可能需要 UAC 确认");
        if(phase=="snapshot_released")return QCoreApplication::translate("Bridge","释放本次 Windows 快照");
        if(phase=="checking")return QCoreApplication::translate("Bridge","核对文件元数据");
        if(phase=="checking_remote")return QCoreApplication::translate("Bridge","核对云端已有备份");
        if(phase=="capturing")return QCoreApplication::translate("Bridge","捕获变化文件");
        if(phase=="prepared")return QCoreApplication::translate("Bridge","准备上传已捕获的文件");
        if(phase=="resuming")return QCoreApplication::translate("Bridge","继续已捕获的快照");
        if(phase=="no_changes")return QCoreApplication::translate("Bridge","没有变化");
        if(phase=="paused")return QCoreApplication::translate("Bridge","任务已暂停");
        if(phase=="cancelled")return QCoreApplication::translate("Bridge","任务已取消 · 捕获结果已保留");
        if(phase=="scanning")return QCoreApplication::translate("Bridge","扫描文件");
        if(phase=="uploading")return QCoreApplication::translate("Bridge","上传文件");
        if(phase=="downloading")return QCoreApplication::translate("Bridge","下载与恢复");
        if(phase=="applying")return QCoreApplication::translate("Bridge","写入本地更改");
        if(phase=="merging")return QCoreApplication::translate("Bridge","核对远端与合并");
        if(phase=="committing"||phase=="completed"&&busy())return QCoreApplication::translate("Bridge","提交并验证快照");
        if(phase=="completed")return QCoreApplication::translate("Bridge","同步完成");
        if(phase=="failed")return QCoreApplication::translate("Bridge","同步失败 · 查看操作详情");
        return QCoreApplication::translate("Bridge","准备同步");
    }
    static QString byteLabel(qulonglong bytes){const char* units[]={"B","KiB","MiB","GiB","TiB"};double number=static_cast<double>(bytes);int unit=0;while(number>=1024&&unit<4){number/=1024;++unit;}return QString::number(number,'f',unit?1:0)+" "+QString::fromLatin1(units[unit]);}
    double progressValue()const{
        const auto phase=progressData.value("phase").toString();if((phase=="completed"||phase=="no_changes")&&!busy())return 1;
        auto total=progressData.value("bytes_total").toDouble(),done=progressData.value("bytes_done").toDouble();
        if(!total){total=progressData.value("total").toDouble();done=progressData.value("done").toDouble();}
        return total>0?std::clamp(done/total,0.0,0.99):0;
    }
    bool progressIndeterminate()const{auto phase=progressData.value("phase").toString();return busy()&&(phase=="checking"||phase=="checking_remote"||phase=="prepared"||phase=="resuming"||phase=="preparing"||phase=="scanning"||phase=="merging"||phase=="committing"||phase=="completed"||progressData.value("total").toULongLong()==0&&progressData.value("bytes_total").toULongLong()==0);}
    QString progressDetail()const{
        const auto phase=progressData.value("phase").toString();QStringList parts;
        const auto files=progressData.value("files_done").toULongLong(),totalFiles=progressData.value("files_total").toULongLong();
        if(phase=="scanning")parts<<QCoreApplication::translate("Bridge","已扫描 %1 个文件").arg(files);
        else if(totalFiles)parts<<(phase=="uploading"||phase=="committing"?QCoreApplication::translate("Bridge","范围：%1 个文件").arg(totalFiles):QCoreApplication::translate("Bridge","文件：%1 / %2").arg(files).arg(totalFiles));
        auto done=progressData.value("bytes_done").toULongLong(),total=progressData.value("bytes_total").toULongLong();
        parts<<(total?byteLabel(done)+" / "+byteLabel(total):byteLabel(done));
        if(phase=="uploading")parts<<(config.value("payload_mode",std::string("encrypted"))=="encrypted"?QCoreApplication::translate("Bridge","加密块：%1 / %2"):QCoreApplication::translate("Bridge","已传文件：%1 / %2")).arg(progressData.value("done").toULongLong()).arg(progressData.value("total").toULongLong());
        const auto transferred=progressData.value("transferred_bytes").toULongLong();
        auto ms=busy()&&progressClock.isValid()?progressClock.elapsed():progressData.value("elapsed_ms").toLongLong();
        parts<<QCoreApplication::translate("Bridge","核对 %1 · 读取 %2 个文件 / %3 · 复用 %4 · 续传 %5 个块").arg(progressData.value("metadata_checked").toULongLong()).arg(progressData.value("body_files").toULongLong()).arg(byteLabel(progressData.value("body_bytes").toULongLong())).arg(progressData.value("reused_files").toULongLong()).arg(progressData.value("resumed_objects").toULongLong());
        parts<<QCoreApplication::translate("Bridge","实际传输：%1").arg(byteLabel(transferred));
        if(transferred&&ms>0)parts<<QCoreApplication::translate("Bridge","平均 %1/s").arg(byteLabel(static_cast<qulonglong>(transferred*1000.0/ms)));
        parts<<QCoreApplication::translate("Bridge","耗时 %1 秒").arg(ms/1000);return parts.join(QStringLiteral(" · "));
    }
    QString applicationVersion()const{return QCoreApplication::applicationVersion();}
    void finishGoogleTask(const QString& text){auto response=Json::parse(s(text),nullptr,false);bool valid=response.is_object();auto result=valid?response.value("result",Json::object()):Json::object();bool ok=valid&&response.value("ok",false)&&result.is_object()&&result.value("connection_verified",false)&&result.contains("snapshots_count")&&result["snapshots_count"].is_number_unsigned();bool saved=valid&&response.value("credentials_saved",false);if(ok){folderUrl=q(result.value("storage",Json::object()).value("folder_url",std::string()));googleState="connected";googleVerifiedAt=QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));googleSnapshotCount=response.at("result").value("snapshots_count",0);loginMessage=QCoreApplication::translate("Bridge","Google 登录成功，已实际验证网盘连接（%1）。授权已加密保存；登录不会上传数据。").arg(googleVerifiedAt);statusText=QCoreApplication::translate("Bridge","Google 连接验证成功");}else{googleState=saved||task=="google_verify"?"connection_failed":"authorization_failed";loginMessage=(saved?QCoreApplication::translate("Bridge","授权已加密保存，但网盘连接验证失败："):task=="google_verify"?QCoreApplication::translate("Bridge","网盘连接验证失败："):QCoreApplication::translate("Bridge","Google 登录失败："))+(valid?q(response.value("error",std::string("连接未通过验证，请重试"))):QCoreApplication::translate("Bridge","无法解析结果"));statusText=googleConnectionLabel();googleSnapshotCount=-1;}loginTask=false;if(task=="google_login")showWindow();startupReport({{"google_connection_state",googleConnectionState()},{"google_connection_label",googleConnectionLabel()},{"google_snapshot_status",googleSnapshotStatus()}});auto screenshot=qEnvironmentVariable("CXS_CONNECTION_SCREENSHOT");if(ok&&!screenshot.isEmpty()&&mainWindow)QTimer::singleShot(300,this,[this,screenshot]{if(mainWindow)mainWindow->grabWindow().save(screenshot);});}
    QString googleLoginMessage()const{if(googleConnectionState()=="connected")return QCoreApplication::translate("Bridge","Google 登录成功，已实际验证网盘连接（%1）。授权已加密保存；登录不会上传数据。").arg(googleVerifiedAt);if(!loginMessage.isEmpty())return loginMessage;return googleAuthorized()?QCoreApplication::translate("Bridge","发现本机加密凭据，尚未验证是否有效；点击“验证连接”。"):QCoreApplication::translate("Bridge","尚未登录 Google，请先完成浏览器授权。");}
    QString googleConnectionState()const{return googleState.isEmpty()?(googleAuthorized()?QStringLiteral("credentials_saved"):QStringLiteral("not_signed_in")):googleState;}
    QString googleConnectionLabel()const{auto state=googleConnectionState();if(state=="connected")return QCoreApplication::translate("Bridge","登录成功 · 连接已验证");if(state=="authorizing")return QCoreApplication::translate("Bridge","正在登录");if(state=="verifying")return QCoreApplication::translate("Bridge","正在验证连接");if(state=="connection_failed")return QCoreApplication::translate("Bridge","连接验证失败");if(state=="authorization_failed")return QCoreApplication::translate("Bridge","登录失败");return state=="credentials_saved"?QCoreApplication::translate("Bridge","凭据已保存 · 未验证"):QCoreApplication::translate("Bridge","未登录");}
    QString googleFolderUrl()const{return folderUrl;}
    QString encryptionLabel()const{auto mode=config.value("payload_mode",std::string("encrypted"));return mode=="original"?QCoreApplication::translate("Bridge","不加密 · 原样保存"):mode=="selective"?QCoreApplication::translate("Bridge","部分加密"):QCoreApplication::translate("Bridge","全部加密");}
    QString googleStorageLabel()const{return config.value("remote",Json::object()).value("storage_mode",std::string("visible"))=="visible"?QCoreApplication::translate("Bridge","可见文件夹"):QCoreApplication::translate("Bridge","隐藏应用数据");}
    QString googleSnapshotStatus()const{if(googleSnapshotCount<0)return QCoreApplication::translate("Bridge","同步状态：远端快照数量待查询，登录不代表已同步。");return googleSnapshotCount==0?QCoreApplication::translate("Bridge","同步状态：远端 0 个快照，尚未完成备份。"):QCoreApplication::translate("Bridge","同步状态：远端有 %1 个快照；不代表当前本地数据已全部同步。").arg(googleSnapshotCount);}
    QString syncMode()const{return q(config.value("sync_mode",std::string("upload")));}
    QVariantMap dataTypes()const{Json values;for(auto name:{"conversations","settings","credentials","plugins","skills","other"})values[name]=config.value("data_types",Json::object()).value(name,true);return QJsonDocument::fromJson(QByteArray::fromStdString(values.dump())).object().toVariantMap();}
    bool configurationSaved()const{return !dirty&&!filename.isEmpty()&&QFileInfo(filename).isFile();}
    bool autoSync()const{return autoEnabled;}
    QString autoTrigger()const{auto mode=notices.value(scheduleSetting("trigger"),QStringLiteral("changes")).toString();return mode=="time"||mode=="amount"?mode:QStringLiteral("changes");}
    int autoIntervalMinutes()const{return std::clamp(notices.value(scheduleSetting("minutes"),5).toInt(),1,1440);}
    int autoThresholdMB()const{return std::clamp(notices.value(scheduleSetting("megabytes"),64).toInt(),1,1048576);}
    QString autoPending()const{return QCoreApplication::translate("Bridge","待备份变更：%1 个文件 · %2 MiB").arg(pendingFiles).arg(double(pendingBytes)/(1024*1024),0,'f',2);}
    bool closeToTray()const{return notices.value(QStringLiteral("close_to_tray"),false).toBool();}
    bool startOnLogin()const{
#ifdef _WIN32
        QSettings startup(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),QSettings::NativeFormat);return !startup.value(QStringLiteral("CodexSync")).toString().isEmpty();
#else
        auto file=QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)+QStringLiteral("/autostart/codexsync.desktop");QFile input(file);return input.open(QIODevice::ReadOnly)&&!input.readAll().contains("Hidden=true");
#endif
    }
    Q_INVOKABLE void setStartOnLogin(bool enabled){
#ifdef _WIN32
        QSettings startup(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),QSettings::NativeFormat);if(enabled)startup.setValue(QStringLiteral("CodexSync"),QStringLiteral("\"")+QDir::toNativeSeparators(QCoreApplication::applicationFilePath())+QStringLiteral("\" --start-minimized"));else startup.remove(QStringLiteral("CodexSync"));startup.sync();if(startup.status()!=QSettings::NoError){error(QCoreApplication::translate("Bridge","无法保存当前用户的自启动设置"));return;}
#else
        auto file=QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)+QStringLiteral("/autostart/codexsync.desktop");QDir().mkpath(QFileInfo(file).absolutePath());QSaveFile output(file);if(!output.open(QIODevice::WriteOnly)){error(QCoreApplication::translate("Bridge","无法保存自启动设置"));return;}auto executable=QCoreApplication::applicationFilePath();executable.replace("\\","\\\\").replace("\"","\\\"").replace("$","\\$").replace("`","\\`").replace("%","%%");output.write((QStringLiteral("[Desktop Entry]\nType=Application\nName=CodexSync\nExec=\"")+executable+QStringLiteral("\" --start-minimized\nHidden=")+(enabled?QStringLiteral("false"):QStringLiteral("true"))+QStringLiteral("\n")).toUtf8());if(!output.commit()){error(QCoreApplication::translate("Bridge","无法保存自启动设置"));return;}
#endif
        emit stateChanged();}
    Q_INVOKABLE void setCloseToTray(bool enabled){notices.setValue(QStringLiteral("close_to_tray"),enabled);notices.sync();emit stateChanged();}
    void attachWindow(QQuickWindow* window,bool minimized=false){mainWindow=window;window->installEventFilter(this);if(QSystemTrayIcon::isSystemTrayAvailable()){tray.show();if(minimized)window->hide();}}
    Q_INVOKABLE void setSyncMode(const QString& mode){if(busy()||(mode!="upload"&&mode!="bidirectional"&&mode!="download"))return;config["sync_mode"]=s(mode);changed();}
    QVariantMap itemCatalog()const{return QJsonDocument::fromJson(QByteArray::fromStdString(catalogData.dump())).object().toVariantMap();}
    Q_INVOKABLE void refreshCatalog(){if(!busy())execute({{"op","catalog"}});}
    void detectItemsForSmoke(){catalogData=cxs::selection_catalog(config);emit catalogChanged();}
    Q_INVOKABLE bool catalogItemSelected(const QString& category,const QString& identifier)const {
        if(category=="projects"){for(const auto& root:config.at("roots"))if(root.value("project_id",std::string{})==s(identifier)&&root.value("enabled",true))return true;return false;}
        const auto rule=config.value("selection",Json::object()).value(s(category),Json::object());
        if(rule.value("mode",std::string("all"))=="all")return true;
        const auto ids=rule.value("ids",Json::array());return std::find(ids.begin(),ids.end(),s(identifier))!=ids.end();
    }
    Q_INVOKABLE void setCatalogItem(const QString& category,const QString& identifier,bool enabled){
        if(busy()||apiRunning())return;const auto group=s(category),id=s(identifier);
        if(group!="conversations"&&group!="projects"&&group!="plugins"&&group!="skills")return;
        if(!catalogData.contains(group))return;
        auto item=std::find_if(catalogData.at(group).begin(),catalogData.at(group).end(),[&](const Json& value){return value.at("id")==id;});
        if(item==catalogData.at(group).end()||!item->value("available",true))return;
        if(group=="projects"){
            auto next=config;
            for(auto& root:next["roots"])if(root.value("project_id",std::string{})==id)root["enabled"]=enabled;
            if(enabled)for(const auto& value:item->at("paths")){
                const auto folder=QDir::cleanPath(q(value.get<std::string>()));if(!QFileInfo(folder).isDir()){error(QCoreApplication::translate("Bridge","项目目录不存在，无法同步：")+folder);return;}
                bool existing=false;for(const auto& root:next.at("roots")){
                    const auto path=QDir::cleanPath(q(root.at("path").get<std::string>()));
                    if(path.compare(folder,Qt::CaseInsensitive)==0&&root.value("project_id",std::string{})==id){existing=true;break;}
                    if(path.compare(folder,Qt::CaseInsensitive)==0||path.startsWith(folder+"/",Qt::CaseInsensitive)||folder.startsWith(path+"/",Qt::CaseInsensitive)){error(QCoreApplication::translate("Bridge","项目与现有同步目录重叠，请先调整目录映射：")+folder);return;}
                }
                const auto state=QDir::cleanPath(q(next.at("state").get<std::string>()));
                if(folder.compare(state,Qt::CaseInsensitive)==0||folder.startsWith(state+"/",Qt::CaseInsensitive)||state.startsWith(folder+"/",Qt::CaseInsensitive)){error(QCoreApplication::translate("Bridge","项目目录不能包含 CodexSync 自身的数据目录。"));return;}
                if(!existing){auto hash=QCryptographicHash::hash(folder.toUtf8(),QCryptographicHash::Sha256).toHex().left(24);next["roots"].push_back({{"id","project_"+s(QString::fromLatin1(hash))},{"path",s(folder)},{"project_id",id},{"enabled",true}});}
            }
            config=std::move(next);historyCoverage.clear();changed();return;
        }
        auto& rule=config["selection"][group];if(!rule.is_object())rule=Json::object();
        if(rule.value("mode",std::string("all"))=="all"){
            rule={{"mode","selected"},{"ids",Json::array()}};
            for(const auto& value:catalogData.at(group))if(value.value("available",true))rule["ids"].push_back(value.at("id"));
        }
        auto& ids=rule["ids"];auto found=std::find(ids.begin(),ids.end(),id);
        if(enabled&&found==ids.end())ids.push_back(id);else if(!enabled&&found!=ids.end())ids.erase(found);
        historyCoverage.clear();changed();
    }
    Q_INVOKABLE void setCatalogAll(const QString& category,bool enabled){if(busy()||apiRunning()||(category!="conversations"&&category!="plugins"&&category!="skills"))return;config["selection"][s(category)]={{"mode",enabled?"all":"selected"},{"ids",Json::array()}};historyCoverage.clear();changed();}
    Q_INVOKABLE void setRootEnabled(int index,bool enabled){if(busy()||index<0||static_cast<size_t>(index)>=config["roots"].size())return;config["roots"][index]["enabled"]=enabled;historyCoverage.clear();changed();}
    Q_INVOKABLE void setRootRules(int index,const QString& includes,const QString& excludes){if(busy()||index<0||static_cast<size_t>(index)>=config["roots"].size())return;try{auto next=config;for(const auto& field:{QStringLiteral("include"),QStringLiteral("exclude")}){Json rules=Json::array();for(const auto& line:(field=="include"?includes:excludes).split('\n')){auto value=line.trimmed();value.replace('\\','/');while(value.endsWith('/'))value.chop(1);if(!value.isEmpty())rules.push_back(s(value));}next["roots"][index][s(field)]=rules;}cxs::validate_selection_rules(next);config=std::move(next);historyCoverage.clear();changed();}catch(const std::exception& e){error(q(e.what()));}}
    Q_INVOKABLE void setDataType(const QString& category,bool enabled){if(busy())return;const QStringList allowed={"conversations","settings","credentials","plugins","skills","other"};if(!allowed.contains(category))return;config["data_types"][s(category)]=enabled;historyCoverage.clear();changed();}
    Q_INVOKABLE void setAutoTrigger(const QString& mode){if(busy()||(mode!="changes"&&mode!="time"&&mode!="amount"))return;notices.setValue(scheduleSetting("trigger"),mode);notices.sync();autoDue=false;autoDirty=true;pendingBytes=0;pendingFiles=0;updateAutoTimer();emit stateChanged();}
    Q_INVOKABLE void setAutoIntervalMinutes(int minutes){if(busy())return;notices.setValue(scheduleSetting("minutes"),std::clamp(minutes,1,1440));notices.sync();updateAutoTimer();emit stateChanged();}
    Q_INVOKABLE void setAutoThresholdMB(int megabytes){if(busy())return;notices.setValue(scheduleSetting("megabytes"),std::clamp(megabytes,1,1048576));notices.sync();emit stateChanged();}
    Q_INVOKABLE void setAutoSync(bool enabled){if(enabled&&(!configurationSaved()||!noticeAcknowledged()||syncMode()!="upload"||key.isEmpty()||!remoteReady()||busy()||apiRunning()))return;autoEnabled=enabled;autoDirty=true;autoDue=false;refreshSourceMonitor();updateAutoTimer();if(enabled){autoTimer.start();setCloseToTray(true);if(autoTrigger()=="amount")sourceDebounce.start();}else{autoTimer.stop();sourceDebounce.stop();}if(!filename.isEmpty()){notices.setValue(autoSetting(),enabled);notices.sync();}emit stateChanged();}
    void startAutomaticBackup(){if(!autoEnabled||exitPending||busy()||apiRunning()||!noticeAcknowledged())return;autoDirty=false;autoDue=false;automatic=true;execute({{"op","backup"}},true);}
    void automaticTick(){if(!autoEnabled||exitPending||!autoDirty||busy()||apiRunning()||!configurationSaved()||!noticeAcknowledged()||syncMode()!="upload"||key.isEmpty()||!remoteReady())return;if(autoTrigger()=="time"&&!autoDue)return;if(autoTrigger()=="amount"){autoDirty=false;autoDue=false;automatic=true;execute({{"op","changes"}},true);}else startAutomaticBackup();}
    Q_INVOKABLE void runSelectedSync(){if(syncMode()=="upload")run(QStringLiteral("backup"));else if(syncMode()=="bidirectional")run(QStringLiteral("sync"));else{if(busy()||apiRunning()||!requireNotice())return;auto parent=QFileDialog::getExistingDirectory(nullptr,QCoreApplication::translate("Bridge","选择独立恢复目录的父目录"));if(parent.isEmpty())return;downloadParent=parent+QStringLiteral("/CodexSync-restore-")+QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));downloadPending=true;execute({{"op","history"}});if(!busy())downloadPending=false;}}
    bool progressTimerRunning()const{return progressTimer.isActive();}
#ifdef CXS_GUI_CHECKS
    void prepareProgressFixture(const QString& folder){prepareBackgroundFixture(folder);auto base=cxs::path(s(folder));for(int i=0;i<8;++i){cxs::Bytes sample(512*1024,static_cast<unsigned char>(i+1));cxs::write_atomic(base/"source"/("sample-"+std::to_string(i)+".bin"),sample);}auto url=qEnvironmentVariable("CXS_PROGRESS_TEST_URL");if(!url.startsWith("http://127.0.0.1:"))throw std::runtime_error("progress test requires its isolated loopback WebDAV server");config["remote"]={{"provider","webdav"},{"url",s(url)},{"allow_loopback_http",true}};save();}
    bool checkProgressIdentity(){auto file=cxs::path(config.at("state"))/"progress.json";auto original=cxs::read_bytes(file,8192);auto stale=Json::parse(original);stale["operation_id"]="previous-task";stale["phase"]="failed";auto text=stale.dump();cxs::write_atomic(file,cxs::Bytes(text.begin(),text.end()));auto previous=progressData;readProgress();const bool ok=previous==progressData;cxs::write_atomic(file,original);return ok;}
    void prepareRestoreProgressCheck(){auto state=cxs::path(config.at("state"));cxs::fs::rename(state/"cache",state/"retained-upload-cache");cxs::private_directory(state/"cache");}
    void prepareBackgroundFixture(const QString& folder){auto base=cxs::path(s(folder));cxs::private_directory(base);auto source=base/"source";cxs::private_directory(source);std::string sample="CodexSync isolated background fixture\n";cxs::write_atomic(source/"tiny.txt",cxs::Bytes(sample.begin(),sample.end()));cxs::generate_key(base/"master.key");Json fixture={{"format",1},{"device",cxs::random_id()},{"state",cxs::utf8(base/"state")},{"roots",Json::array({{{"id","fixture"},{"path",cxs::utf8(source)}}})},{"remote",{{"provider","local"},{"directory",cxs::utf8(base/"repository")}}},{"exclude",Json::array()}};auto text=fixture.dump(2);cxs::write_atomic(base/"sync.json",cxs::Bytes(text.begin(),text.end()));load(q(cxs::utf8(base/"sync.json")));key=q(cxs::utf8(base/"master.key"));rememberConfiguration();emit configurationChanged();}
    bool checkGoogleStates(){googleState.clear();if(googleConnectionState()!="not_signed_in")return false;googleState="credentials_saved";if(googleConnectionLabel()!=QCoreApplication::translate("Bridge","凭据已保存 · 未验证"))return false;task="google_login";finishGoogleTask(q(Json{{"ok",true},{"credentials_saved",true},{"result",{{"authorized",true}}}}.dump()));if(googleConnectionState()!="connection_failed")return false;finishGoogleTask(q(Json{{"ok",false},{"credentials_saved",true},{"error","fixture connection failure"}}.dump()));if(googleConnectionState()!="connection_failed"||!googleLoginMessage().contains(QCoreApplication::translate("Bridge","验证失败")))return false;finishGoogleTask(q(Json{{"ok",false},{"credentials_saved",false},{"error","fixture authorization failure"}}.dump()));if(googleConnectionState()!="authorization_failed")return false;finishGoogleTask(QStringLiteral("not json"));if(googleConnectionState()!="authorization_failed")return false;finishGoogleTask(q(Json{{"ok",true},{"result",{{"connection_verified",true},{"snapshots_count",size_t(0)}}}}.dump()));if(googleConnectionState()!="connected"||!googleSnapshotStatus().contains(QCoreApplication::translate("Bridge","尚未完成备份")))return false;finishGoogleTask(q(Json{{"ok",true},{"result",{{"connection_verified",true},{"snapshots_count",size_t(3)}}}}.dump()));if(!googleSnapshotStatus().contains(QCoreApplication::translate("Bridge","不代表当前本地数据已全部同步")))return false;changed();return googleConnectionState()=="not_signed_in";}
    void setCheckInterval(int milliseconds){autoTimer.setTimerType(Qt::PreciseTimer);autoTimer.setInterval(milliseconds);}
    void reconcileChangesForCheck(){autoDirty=true;automaticTick();}
#endif
    bool noticeAcknowledged()const{return notices.value(QStringLiteral("read_notice_version")).toString()==q(std::string(cxs::disclaimer_version));}
    QString disclaimerText()const{return q(std::string(englishLanguage()?cxs::disclaimer_en_text:cxs::disclaimer_text)).section('\n',1).trimmed();}
    QString language()const{return languageSelection;}
    bool englishLanguage()const{return languageSelection=="en"||(languageSelection=="system"&&QLocale::system().language()!=QLocale::Chinese);}
    int themeMode()const{return selectedTheme;}
    void initializeAppearance(int theme,const QString& language){if(theme>=0){selectedTheme=theme;applyTheme();}if(!language.isEmpty()){languageSelection=language;applyLanguage();statusText=filename.isEmpty()?QCoreApplication::translate("Bridge","尚未配置"):QCoreApplication::translate("Bridge","配置已加载");if(!notices.contains(QStringLiteral("last_activity/")+keySetting().section('/',1)))activity=QCoreApplication::translate("Bridge","尚无备份完成记录");}}
    Q_INVOKABLE void setLanguage(const QString& language){if(busy()||(language!="en"&&language!="zh_CN"&&language!="system")||languageSelection==language)return;languageSelection=language;notices.setValue(QStringLiteral("appearance/language"),language);notices.sync();applyLanguage();statusText=googleConnectionState()=="connected"?googleConnectionLabel():filename.isEmpty()?QCoreApplication::translate("Bridge","尚未配置"):QCoreApplication::translate("Bridge","配置已加载");emit stateChanged();}
    int backendOperationCount()const{return backendOperations;}
    Q_INVOKABLE bool acknowledgeNotice(){notices.setValue(QStringLiteral("read_notice_version"),q(std::string(cxs::disclaimer_version)));notices.sync();if(notices.status()!=QSettings::NoError){error(QCoreApplication::translate("Bridge","无法保存本机阅读记录，请检查配置目录权限。"));return false;}emit stateChanged();return true;}
    bool googleAuthorized()const{try{return !cxs::env("CXS_GOOGLE_ACCESS_TOKEN").empty()||!cxs::env("CXS_GOOGLE_REFRESH_TOKEN").empty()||QFileInfo::exists(q(cxs::utf8(cxs::google_credential_path(config))));}catch(...){return false;}}
    bool googleClientReady()const{return cxs::google_client_ready(config);}
    QVariantMap conversationHistory()const{return historyCoverage;}
    QString terminalCommand()const{
        auto executable=QCoreApplication::applicationDirPath()+QStringLiteral("/codex-sync");
#ifdef _WIN32
        executable+=QStringLiteral(".exe");
        return QStringLiteral("& '")+executable.replace(QStringLiteral("'"),QStringLiteral("''"))+QStringLiteral("' --help");
#else
        return QStringLiteral("'")+executable.replace(QStringLiteral("'"),QStringLiteral("'\\''"))+QStringLiteral("' --help");
#endif
    }
    bool portableEdition()const{return cxs::portable_mode();}QString dataDirectory()const{return applicationData();}
    QString configFile()const{return filename;}QString keyFile()const{return key;}QString deviceName()const{return QSysInfo::machineHostName();}QString platform()const{return QSysInfo::prettyProductName();}
    QString status()const{return statusText;}QString lastActivity()const{return activity;}QString logText()const{return log;}bool busy()const{return inFlight||externalBusy;}bool apiRunning()const{return process.state()!=QProcess::NotRunning;}QString apiToken()const{return secret;}QVariantList snapshots()const{return snapshotList;}
    void load(const QString& file,bool startup=false){try{config=Json::parse(cxs::read_bytes(cxs::path(s(file)),1024*1024));cxs::normalize_application_config(config);cxs::resolve_history_roots(config);catalogData=Json::object();emit catalogChanged();historyCoverage.clear();folderUrl.clear();googleState.clear();googleVerifiedAt.clear();googleSnapshotCount=-1;loginMessage.clear();filename=cxs::portable_mode()?q(cxs::utf8(cxs::default_configuration_path())):QFileInfo(file).absoluteFilePath();key=restoredPath(notices.value(keySetting()).toString());if(key.isEmpty())key=q(cxs::utf8(cxs::default_key_path()));if(!QFileInfo(key).isFile())key.clear();dirty=cxs::portable_mode()&&QFileInfo(file).absoluteFilePath()!=filename;statusText=QCoreApplication::translate("Bridge","配置已加载");activity=notices.value(QStringLiteral("last_activity/")+keySetting().section('/',1),QCoreApplication::translate("Bridge","尚无备份完成记录")).toString();autoEnabled=notices.value(autoSetting(),false).toBool()&&syncMode()=="upload";autoDue=false;pendingBytes=0;pendingFiles=0;updateAutoTimer();if(autoEnabled)autoTimer.start();else autoTimer.stop();rememberConfiguration();if(qEnvironmentVariableIsSet("CXS_STARTUP_DIAGNOSTIC"))qInfo().noquote()<<"CONFIG_LOADED"<<filename<<"key_selected="<<!key.isEmpty();watchState();refreshSourceMonitor();emit stateChanged();emit configurationChanged();}catch(const std::exception& e){QFile probe(file);bool readable=probe.open(QIODevice::ReadOnly);startupReport({{"phase","config_error"},{"error",q(e.what())},{"qt_readable",readable},{"qt_error",probe.errorString()}});qWarning().noquote()<<"Configuration load:"<<q(e.what());if(startup){statusText=QCoreApplication::translate("Bridge","配置加载失败");activity=q(e.what());log+=activity+QStringLiteral("\n");emit stateChanged();}else error(q(e.what()));}}
    Q_INVOKABLE void openLogs(){auto state=config.contains("state")?cxs::path(config.at("state")):cxs::application_data_directory()/"state";auto folder=state/"logs";cxs::private_directory(folder);QDesktopServices::openUrl(QUrl::fromLocalFile(q(cxs::utf8(folder))));}
    Q_INVOKABLE void openConfig(){if(busy())return;auto file=QFileDialog::getOpenFileName(nullptr,QCoreApplication::translate("Bridge","打开同步配置"),{},QStringLiteral("JSON (*.json)"));if(!file.isEmpty())load(file);}
    Q_INVOKABLE void saveConfig(){if(!busy())save();}
    Q_INVOKABLE void setKeyPassword(const QString& value){keyPassword=value;}
    Q_INVOKABLE bool encryptionRootAll(const QString& root)const{for(const auto& rule:config.value("encryption_rules",Json::array()))if(rule.at("root")==s(root)&&rule.at("path")=="")return true;return false;}
    Q_INVOKABLE QString encryptionRuleText(const QString& root)const{QStringList paths;for(const auto& rule:config.value("encryption_rules",Json::array()))if(rule.at("root")==s(root)&&rule.at("path")!="")paths<<q(rule.at("path"));return paths.join('\n');}
    Q_INVOKABLE void setEncryptionRules(const QString& root,bool all,const QString& paths){if(busy())return;try{auto next=config;Json rules=Json::array();for(const auto& rule:config.value("encryption_rules",Json::array()))if(rule.at("root")!=s(root))rules.push_back(rule);if(all)rules.push_back({{"root",s(root)},{"path",""}});else for(auto line:paths.split('\n')){line=line.trimmed();line.replace('\\','/');while(line.endsWith('/'))line.chop(1);if(!line.isEmpty())rules.push_back({{"root",s(root)},{"path",s(line)}});}next["encryption_rules"]=rules;cxs::normalize_application_config(next);config=std::move(next);changed();}catch(const std::exception& e){error(q(e.what()));}}
    Q_INVOKABLE void setSetting(const QString& name,const QString& value){if(busy())return;auto n=s(name);if(n=="payload_mode"){if(value!="encrypted"&&value!="original"&&value!="selective")return;config[n]=s(value);}else if(n=="provider"){config["remote"]["provider"]=s(value);if(value=="google_drive"&&!config["remote"].contains("storage_mode"))config["remote"]["storage_mode"]="visible";if(value!=QStringLiteral("local"))config["remote"].erase("directory");}else if(n=="url"){config["remote"].erase("directory");config["remote"]["url"]=s(value);}else if(n=="directory"){if(value.isEmpty())config["remote"].erase("directory");else config["remote"]["directory"]=s(value);}else if(n=="client_id"||n=="repository")config["remote"][n]=s(value);else if(n=="storage_mode"){if(value!="visible"&&value!="appdata")return;config["remote"]["storage_mode"]=s(value);config["remote"].erase("credential_file");}else if(n=="google_client_secret")googleSecret=value;else if(n=="state"){if(cxs::portable_mode())return;config["state"]=s(value);}else if(n=="username")username=value;else if(n=="password")password=value;changed();}
    Q_INVOKABLE void updateRoot(int index,const QString& id,const QString& path){if(busy()||index<0||static_cast<size_t>(index)>=config["roots"].size())return;auto prior=config["roots"][index]["id"];for(auto& rule:config["encryption_rules"])if(rule.at("root")==prior)rule["root"]=s(id);config["roots"][index]["id"]=s(id);config["roots"][index]["path"]=s(path);catalogData=Json::object();emit catalogChanged();historyCoverage.clear();changed();}
    Q_INVOKABLE void removeRoot(int index){if(!busy()&&index>=0&&static_cast<size_t>(index)<config["roots"].size()){auto prior=config["roots"][index]["id"];Json rules=Json::array();for(const auto& rule:config.value("encryption_rules",Json::array()))if(rule.at("root")!=prior)rules.push_back(rule);config["encryption_rules"]=rules;config["roots"].erase(config["roots"].begin()+index);historyCoverage.clear();changed();}}
    Q_INVOKABLE void addRoot(){if(busy())return;auto folder=QFileDialog::getExistingDirectory(nullptr,QCoreApplication::translate("Bridge","添加数据目录"));if(folder.isEmpty())return;config["roots"].push_back({{"id","extra_"+std::to_string(config["roots"].size())},{"path",s(folder)}});historyCoverage.clear();changed();}
    Q_INVOKABLE void discoverRoots(){if(busy())return;for(auto& found:cxs::discover()["roots"]){bool present=false;for(auto& current:config["roots"])if(current.at("id")==found.at("id")||current.at("path")==found.at("path")){present=true;break;}if(!present)config["roots"].push_back(found);}cxs::resolve_history_roots(config);historyCoverage.clear();changed();}
    Q_INVOKABLE void chooseKey(){if(busy()||apiRunning())return;auto file=QFileDialog::getOpenFileName(nullptr,QCoreApplication::translate("Bridge","选择同步主密钥"));if(!file.isEmpty()){try{auto destination=cxs::application_data_directory()/"secrets"/("imported-"+cxs::random_id()+".key");auto bytes=cxs::read_bytes(cxs::path(s(file)),256);try{cxs::write_atomic(destination,bytes);}catch(...){sodium_memzero(bytes.data(),bytes.size());throw;}sodium_memzero(bytes.data(),bytes.size());setAutoSync(false);key=q(cxs::utf8(destination));rememberConfiguration();emit configurationChanged();}catch(const std::exception& e){error(q(e.what()));}}}
    Q_INVOKABLE void createKey(){if(busy()||apiRunning())return;auto destination=cxs::default_key_path();if(cxs::fs::exists(destination))destination=destination.parent_path()/("master-"+cxs::random_id()+".key");auto file=q(cxs::utf8(destination));try{cxs::generate_key(destination,s(keyPassword));setAutoSync(false);key=file;rememberConfiguration();emit configurationChanged();QMessageBox::information(nullptr,QCoreApplication::translate("Bridge","主密钥已生成"),QCoreApplication::translate("Bridge","其他设备需要使用同一主密钥。请保存安全副本，不要将密钥放进同步目录。"));}catch(const std::exception& e){error(q(e.what()));}}
    Q_INVOKABLE void authorizeGoogle(){startGoogleTask(true);}
    Q_INVOKABLE void verifyGoogleConnection(){startGoogleTask(false);}
    void startGoogleTask(bool authorize){
        if(busy()||apiRunning()||!requireNotice()||!save())return;
        if(!authorize&&!googleAuthorized())return;
        Json secrets;
        try{if(key.isEmpty())throw std::runtime_error("请选择加密主密钥");secrets["key_file"]=s(key);if(!keyPassword.isEmpty())secrets["key_password"]=s(keyPassword);if(!googleSecret.isEmpty())secrets["google_client_secret"]=s(googleSecret);}
        catch(const std::exception& e){googleState="connection_failed";loginMessage=q(e.what());error(loginMessage);return;}
        progressData.clear();progressId.clear();inFlight=true;loginTask=true;task=authorize?"google_login":"google_verify";googleState=authorize?"authorizing":"verifying";googleSnapshotCount=-1;
        loginMessage=authorize?QCoreApplication::translate("Bridge","正在打开浏览器，等待你的授权…"):QCoreApplication::translate("Bridge","正在实际访问所选 Google Drive 存储，只查询快照，不上传数据…");auto current=config;statusText=googleConnectionLabel();++backendOperations;emit stateChanged();
        watcher.setFuture(QtConcurrent::run([this,authorize,current=std::move(current),secrets=std::move(secrets)]()mutable{
            auto clear=[&]{for(auto field:{"key_hex","key_password","google_client_secret"})if(secrets.contains(field)){auto& value=secrets[field].get_ref<std::string&>();sodium_memzero(value.data(),value.size());}};
            bool saved=false;
            try{if(authorize){cxs::google_authorize(current,secrets,[](const std::string& url){bool opened=false;QMetaObject::invokeMethod(qApp,[&]{opened=QDesktopServices::openUrl(QUrl(q(url)));},Qt::BlockingQueuedConnection);if(!opened)throw std::runtime_error("cannot open the Google authorization browser");},[this](const std::string& phase){auto current=q(phase);QMetaObject::invokeMethod(this,[this,current]{if(current=="callback_received"||current=="token_exchange"){loginMessage=QCoreApplication::translate("Bridge","已收到授权，正在换取令牌并加密保存，请稍候…");statusText=QCoreApplication::translate("Bridge","正在完成 Google 登录");}else if(current=="authorized"){googleState="verifying";loginMessage=QCoreApplication::translate("Bridge","授权已加密保存，正在实际验证网盘连接…");statusText=googleConnectionLabel();}emit stateChanged();},Qt::QueuedConnection);});saved=true;}
                cxs::GoogleDrive drive(current,secrets);auto count=drive.list("snapshots").size();auto location=drive.location();clear();return q(Json{{"ok",true},{"credentials_saved",true},{"result",{{"connection_verified",true},{"snapshots_count",count},{"storage",location}}}}.dump());}
            catch(const std::exception& e){clear();return q(Json{{"ok",false},{"credentials_saved",saved},{"error",e.what()}}.dump());}
        }));emit stateChanged();
    }
    Q_INVOKABLE void controlTask(const QString& action){try{auto id=progressData.value("job_id").toString();if(id.isEmpty())id=progressData.value("operation_id").toString();cxs::execute({{"op",s(action)},{"config",s(filename)},{"job_id",s(id)}});}catch(const std::exception& e){error(q(e.what()));}}
    Q_INVOKABLE void run(const QString& operation){if(busy()||!requireNotice())return;Json request={{"op",s(operation)}};if(operation==QStringLiteral("sync")){if(QMessageBox::question(nullptr,QCoreApplication::translate("Bridge","确认离线同步"),QCoreApplication::translate("Bridge","请先关闭 Codex。冲突版本会保留，删除不会传播。\n\nCodex 已关闭，继续？"),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;request["offline"]=true;}execute(std::move(request));}
    Q_INVOKABLE void restore(const QString& snapshot){if(busy()||!requireNotice())return;auto folder=QFileDialog::getExistingDirectory(nullptr,QCoreApplication::translate("Bridge","选择恢复目录的父目录"));if(folder.isEmpty())return;execute({{"op","restore"},{"snapshot",s(snapshot)},{"output",s(folder+QStringLiteral("/CodexSync-restore-")+QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")))}});}
    void execute(Json request,bool scheduled=false){if(busy()||apiRunning()||!requireNotice()||(!scheduled&&!save()))return;try{request["config"]=s(filename);if(!key.isEmpty()&&request.at("op")!="catalog"&&request.at("op")!="scan"&&request.at("op")!="conversations"){request["key_file"]=s(key);if(!keyPassword.isEmpty())request["key_password"]=s(keyPassword);}if(!username.isEmpty())request["dav_user"]=s(username);if(!password.isEmpty())request["dav_password"]=s(password);}catch(const std::exception& e){if(scheduled){automatic=false;setAutoSync(false);statusText=QCoreApplication::translate("Bridge","自动同步已暂停");log+=q(e.what())+"\n";emit stateChanged();}else error(q(e.what()));return;}
        inFlight=true;task=q(request.value("op",std::string()));if(task!="changes"){progressData.clear();progressId.clear();}if(tracksProgress(task)){progressId=q(cxs::random_id());request["progress_id"]=s(progressId);progressClock.start();progressData={{"phase","preparing"},{"operation_id",progressId}};progressTimer.start();}statusText=QCoreApplication::translate("Bridge","正在处理");++backendOperations;watcher.setFuture(QtConcurrent::run([request=std::move(request)]()mutable{auto input=request.dump();char* result=nullptr;cxs_run(input.c_str(),&result);sodium_memzero(input.data(),input.size());for(auto field:{"key_hex","key_password","dav_user","dav_password"})if(request.contains(field)){auto& value=request[field].get_ref<std::string&>();sodium_memzero(value.data(),value.size());}if(!result)return QStringLiteral("{\"ok\":false,\"error\":\"allocation failure\"}");auto text=QString::fromUtf8(result);cxs_free(result);return text;}));emit stateChanged();}
    Q_INVOKABLE void startApi(int port){if(apiRunning()||busy()||!requireNotice()||!save())return;auto environment=QProcessEnvironment::systemEnvironment();secret=q(cxs::random_id()+cxs::random_id());environment.insert(QStringLiteral("CXS_API_TOKEN"),secret);environment.insert(QStringLiteral("CXS_KEY_FILE"),key);environment.insert(QStringLiteral("CXS_KEY_PASSWORD"),keyPassword);environment.insert(QStringLiteral("CXS_DAV_USER"),username);environment.insert(QStringLiteral("CXS_DAV_PASSWORD"),password);process.setProcessEnvironment(environment);auto executable=QCoreApplication::applicationDirPath()+QStringLiteral("/codex-sync");
#ifdef _WIN32
        executable+=QStringLiteral(".exe");
#endif
        ++backendOperations;process.start(executable,{QStringLiteral("serve"),QStringLiteral("--config"),filename,QStringLiteral("--port"),QString::number(port)});emit stateChanged();}
    Q_INVOKABLE void stopApi(){if(process.state()!=QProcess::NotRunning){process.terminate();if(!process.waitForFinished(1500)){process.kill();process.waitForFinished(1500);}}secret.clear();emit stateChanged();}
    Q_INVOKABLE void copyToken(){QApplication::clipboard()->setText(secret);}
    Q_INVOKABLE void copyTerminalCommand(){QApplication::clipboard()->setText(terminalCommand());}
    Q_INVOKABLE void setTheme(int mode){if(mode<0||mode>2)return;selectedTheme=mode;notices.setValue(QStringLiteral("appearance/theme"),mode);notices.sync();applyTheme();}
signals:void languageChanged();void appearanceChanged();void catalogChanged();void configurationChanged();void stateChanged();void noticeRequested();
};
static void captureAutomaticSettings(QApplication& app,Bridge& bridge,QQuickWindow* window,const QString& folder,std::function<void()> done){
    window->setProperty("currentPage",5);window->resize(780,680);bridge.setAutoTrigger("amount");
    QTimer::singleShot(300,&app,[&app,&bridge,window,folder,done]{
        auto card=window->findChild<QQuickItem*>("autoScheduleCard");auto scroll=window->findChild<QObject*>("settingsScroll");auto item=scroll?scroll->property("contentItem").value<QObject*>():nullptr;
        if(!card||!item){qCritical("Automatic schedule controls missing");app.exit(2);return;}
        item->setProperty("contentY",std::clamp(card->mapToScene(QPointF()).y()-160+item->property("contentY").toDouble(),0.0,std::max(0.0,item->property("contentHeight").toDouble()-item->property("height").toDouble())));
        QTimer::singleShot(300,&app,[&app,&bridge,window,folder,done]{
            auto field=window->findChild<QQuickItem*>("autoThresholdMB");
            if(!field||!field->isVisible()||field->mapToScene(QPointF(field->width(),field->height())).x()>window->width()||field->mapToScene(QPointF(field->width(),field->height())).y()>window->height()){qCritical("Quantity control clipped");app.exit(2);return;}
            window->grabWindow().save(folder+"/automatic-amount.png");bridge.setAutoTrigger("time");bridge.setAutoIntervalMinutes(7);
            QTimer::singleShot(300,&app,[&app,&bridge,window,folder,done]{
                auto field=window->findChild<QQuickItem*>("autoIntervalMinutes");if(!field||!field->isVisible()||field->property("value").toInt()!=7||field->mapToScene(QPointF(field->width(),field->height())).y()>window->height()){qCritical("Time control clipped or not bound");app.exit(2);return;}
                window->grabWindow().save(folder+"/automatic-time.png");done();
            });
        });
    });
}
#ifdef CXS_GUI_CHECKS
#include "gui_checks.inc"
#endif
int main(int argc,char** argv){QApplication app(argc,argv);app.setApplicationName(QStringLiteral("CodexSyncNative"));app.setOrganizationName(QStringLiteral("CodexSync"));
    app.setApplicationVersion(QStringLiteral("0.2"));
#ifdef _WIN32
    QFont chinese(QStringLiteral("Microsoft YaHei UI"));chinese.setPixelSize(14);chinese.setWeight(QFont::DemiBold);app.setFont(chinese);
#endif
    app.setWindowIcon(QIcon(QStringLiteral(":/ui/codexsync.png")));
#ifdef _WIN32
    QQuickStyle::setStyle(QStringLiteral("FluentWinUI3"));
#else
    // Debian's Qt packages ship Fusion, but do not ship the FluentWinUI3 style.
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
#endif
    QString config,smoke,background,language,progressCheck;bool minimized=false;int theme=-1;auto args=app.arguments();for(int i=1;i<args.size();++i){if(args[i]==QStringLiteral("--config")&&i+1<args.size())config=args[++i];else if(args[i]==QStringLiteral("--ui-smoke")&&i+1<args.size())smoke=args[++i];else if(args[i]==QStringLiteral("--ui-light"))theme=1;else if(args[i]==QStringLiteral("--ui-dark"))theme=2;else if(args[i]==QStringLiteral("--language")&&i+1<args.size())language=args[++i];else if(args[i]==QStringLiteral("--start-minimized"))minimized=true;else if(args[i]==QStringLiteral("--progress-check")&&i+1<args.size())progressCheck=args[++i];else if(args[i]==QStringLiteral("--background-check")&&i+1<args.size())background=args[++i];}
    if(!smoke.isEmpty())QTimer::singleShot(8000,&app,[&]{if(auto message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){qCritical().noquote()<<"Startup diagnostic:"<<message->text();message->reject();}});
    QLocalServer instanceServer;
    if(smoke.isEmpty()&&background.isEmpty()&&progressCheck.isEmpty()){
        auto name=QStringLiteral("CodexSync-")+QString::fromLatin1(QCryptographicHash::hash(applicationData().toUtf8(),QCryptographicHash::Sha256).toHex().left(24));
        QLocalSocket existing;existing.connectToServer(name);if(existing.waitForConnected(300)){existing.write("show");existing.waitForBytesWritten(300);return 0;}
        instanceServer.setSocketOptions(QLocalServer::UserAccessOption);if(!instanceServer.listen(name)){qCritical("Cannot establish the single-instance channel");return 2;}
    }
    Bridge bridge(background.isEmpty()?config:QString(),background.isEmpty()?(progressCheck.isEmpty()?smoke:progressCheck):background);if(!background.isEmpty()){
#ifdef CXS_GUI_CHECKS
        runBackgroundChecks(app,bridge,background);return app.exec();
#else
        qCritical("Background checks are only available in test builds");return 2;
#endif
    }if(!language.isEmpty()&&language!="en"&&language!="zh_CN"&&language!="system"){qCritical("Invalid GUI language");return 2;}bridge.initializeAppearance(theme,language);QQmlApplicationEngine engine;QObject::connect(&bridge,&Bridge::languageChanged,&engine,&QQmlApplicationEngine::retranslate);engine.rootContext()->setContextProperty(QStringLiteral("ui"),&bridge);engine.load(QUrl(QStringLiteral("qrc:/ui/Main.qml")));if(engine.rootObjects().isEmpty())return 1;auto window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());bridge.attachWindow(window,minimized);if(qEnvironmentVariable("CXS_STARTUP_PAGE")=="encryption")window->setProperty("currentPage",6);if(qEnvironmentVariableIsSet("CXS_STARTUP_REPORT")||qEnvironmentVariableIsSet("CXS_STARTUP_SCREENSHOT"))QTimer::singleShot(1500,&app,[&bridge,window]{startupReport({{"external_task",bridge.busy()},{"job_id",bridge.syncProgress().value("job_id").toString()},{"task_phase",bridge.syncProgress().value("phase").toString()},{"backend_operations",bridge.backendOperationCount()},{"progress_timer",bridge.progressTimerRunning()}});auto output=qEnvironmentVariable("CXS_STARTUP_SCREENSHOT");if(!output.isEmpty())window->grabWindow().save(output);});if(!progressCheck.isEmpty()){
#ifdef CXS_GUI_CHECKS
        runProgressChecks(app,bridge,window,progressCheck);return app.exec();
#else
        qCritical("Progress checks are only available in test builds");return 2;
#endif
    }if(smoke.isEmpty()&&bridge.configurationSaved()&&bridge.configuration().value("remote").toMap().value("provider").toString()=="google_drive"&&bridge.googleAuthorized()&&bridge.noticeAcknowledged()&&!bridge.keyFile().isEmpty())QTimer::singleShot(0,&bridge,[&bridge]{bridge.verifyGoogleConnection();});startupReport({{"phase","gui_ready"},{"config_loaded",bridge.configurationSaved()},{"key_selected",!bridge.keyFile().isEmpty()},{"status",bridge.status()},{"startup_error",bridge.logText()},{"visible",window->isVisible()}});if(qEnvironmentVariableIsSet("CXS_STARTUP_DIAGNOSTIC"))qInfo()<<"GUI_LOADED version="<<bridge.applicationVersion()<<"visible="<<window->isVisible();QObject::connect(&instanceServer,&QLocalServer::newConnection,&app,[&]{while(auto socket=instanceServer.nextPendingConnection()){auto show=[window,socket]{if(socket->readAll()==QByteArray("show")){window->showNormal();window->raise();window->requestActivate();}socket->disconnectFromServer();};QObject::connect(socket,&QLocalSocket::readyRead,&app,show);QObject::connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);if(socket->bytesAvailable())show();}});
    if(!smoke.isEmpty())QTimer::singleShot(15000,&app,[&]{if(!QFileInfo::exists(smoke+QStringLiteral("/gui-ready.json"))){if(auto message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))qCritical().noquote()<<"UI smoke modal failure:"<<message->text();else qCritical("UI smoke timeout");app.exit(2);}});
    if(!smoke.isEmpty())QTimer::singleShot(1000,&app,[&]{
        QDir().mkpath(smoke);
        auto button=window->findChild<QObject*>(QStringLiteral("noticeContinue"));
        if(bridge.noticeAcknowledged()||!window->property("noticeVisible").toBool()||!button||button->property("enabled").toBool()){qCritical("First-run notice or unchecked-button contract failed");app.exit(2);return;}
        int requested=0;auto connection=QObject::connect(&bridge,&Bridge::noticeRequested,&app,[&]{++requested;});
        bridge.run(QStringLiteral("scan"));bridge.authorizeGoogle();bridge.restore(QStringLiteral("fixture"));bridge.startApi(12306);QObject::disconnect(connection);
        if(requested!=4||bridge.backendOperationCount()!=0||bridge.busy()||bridge.apiRunning()){qCritical("Notice operation gate failed");app.exit(2);return;}
        window->grabWindow().save(smoke+QStringLiteral("/disclaimer.png"));window->resize(760,580);
        QTimer::singleShot(300,&app,[&]{
            window->grabWindow().save(smoke+QStringLiteral("/disclaimer-compact.png"));window->resize(1120,790);
            if(!QMetaObject::invokeMethod(window,"acknowledgeNoticeForSmoke",Qt::DirectConnection)||!bridge.noticeAcknowledged()){qCritical("Explicit notice acknowledgement failed");app.exit(2);return;}
            bridge.detectItemsForSmoke();Bridge replay({},smoke);if(!replay.noticeAcknowledged()||replay.configFile()!=bridge.configFile()||replay.keyFile()!=bridge.keyFile()){qCritical("Notice/configuration/key-path persistence failed");app.exit(2);return;}
            auto finish=[&]{
                window->setProperty("currentPage",5);const auto originalLanguage=bridge.language();const auto originalTheme=bridge.themeMode();
                bridge.setLanguage(originalLanguage=="en"?QStringLiteral("zh_CN"):QStringLiteral("en"));
                auto primary=window->findChild<QObject*>(QStringLiteral("startSyncButton"));
                if(!primary||primary->property("text").toString()!=QCoreApplication::translate("Main","开始同步")){qCritical("Live language switch failed");app.exit(2);return;}
                bridge.setLanguage(originalLanguage);bridge.setTheme(originalTheme);
                {Bridge appearanceReplay({},smoke);if(appearanceReplay.language()!=originalLanguage||appearanceReplay.themeMode()!=originalTheme){qCritical("Language or theme persistence failed");app.exit(2);return;}}
            QTimer::singleShot(100,&app,[&]{auto scroll=window->findChild<QObject*>(QStringLiteral("settingsScroll"));if(scroll){auto item=scroll->property("contentItem").value<QObject*>();if(item)item->setProperty("contentY",std::max(0.0,item->property("contentHeight").toDouble()-item->property("height").toDouble()));}});
            QTimer::singleShot(200,&app,[&]{window->grabWindow().save(smoke+QStringLiteral("/settings-bottom.png"));});
            QTimer::singleShot(300,&app,[&]{window->setProperty("currentPage",0);window->resize(780,680);QTimer::singleShot(350,&app,[&]{
                window->grabWindow().save(smoke+QStringLiteral("/compact.png"));window->resize(1120,790);QMetaObject::invokeMethod(window,"openSelectionForSmoke",Qt::DirectConnection);window->requestUpdate();window->grabWindow();QTimer::singleShot(400,&app,[&]{
                    auto rendered=window->grabWindow();auto selector=window->findChild<QObject*>(QStringLiteral("contentSelector"));
                    if(!selector||selector->property("opacity").toDouble()<0.99||!window->property("selectionVisible").toBool()||window->property("selectionCount").toInt()!=static_cast<int>(bridge.itemCatalog().value("conversations").toList().size())){qCritical()<<"Conversation selector is missing, invisible or incomplete"<<(selector?selector->property("opacity"):QVariant())<<window->isExposed();app.exit(2);return;}
                    rendered.save(smoke+QStringLiteral("/selection.png"));
                    bool trayCheck=false;if(QSystemTrayIcon::isSystemTrayAvailable()){bridge.setCloseToTray(true);window->close();trayCheck=!window->isVisible();bridge.setCloseToTray(false);window->show();if(!trayCheck){qCritical("Tray close lifecycle failed");app.exit(2);return;}}
                    QSaveFile ready(smoke+QStringLiteral("/gui-ready.json"));ready.open(QIODevice::WriteOnly);
                ready.write(QJsonDocument(QJsonObject{{"appearance_persistence_verified",true},{"language_switch_verified",true},{"language",bridge.language()},{"english",bridge.englishLanguage()},{"theme_mode",bridge.themeMode()},{"selection_render_verified",true},{"primary_sync_visible_all_pages",true},{"default_api_port",12306},{"portable",bridge.portableEdition()},{"tray_available",QSystemTrayIcon::isSystemTrayAvailable()},{"tray_close_verified",trayCheck},{"version",bridge.applicationVersion()},{"framework",QStringLiteral("Qt Quick")},{"style",QQuickStyle::name()},{"pages",7},{"screenshots",15},{"auto_schedule_controls_verified",true},{"encryption_page_verified",true},{"backend_operations",bridge.backendOperationCount()},{"notice_first_run",true},{"notice_unchecked_disabled",true},{"notice_operation_gate",true},{"notice_acknowledgement_persisted",true},{"configuration_remembered",true},{"key_selected",!bridge.keyFile().isEmpty()},{"google_client_ready",bridge.googleClientReady()},{"google_authorized",bridge.googleAuthorized()}}).toJson(QJsonDocument::Compact));ready.commit();app.exit(0);
                });
            });});
            };
            auto step=new QTimer(&app);step->setSingleShot(true);auto pageIndex=std::make_shared<int>(0);
            QObject::connect(step,&QTimer::timeout,&app,[&,step,pageIndex,finish]{
                const auto page=*pageIndex;if(page==2){bridge.setSetting("provider","google_drive");bridge.setSetting("storage_mode",qEnvironmentVariable("CXS_SMOKE_GOOGLE_MODE","visible"));}if(page==6){bridge.setSetting("payload_mode",qEnvironmentVariable("CXS_SMOKE_ENCRYPTION_MODE","selective"));const auto roots=bridge.configuration().value("roots").toList();if(!roots.empty())bridge.setEncryptionRules(roots.front().toMap().value("id").toString(),false,"sessions\nskills/private-skill\nauth.json");}window->setProperty("currentPage",page);
                QTimer::singleShot(400,&app,[&,step,pageIndex,finish,page]{
                    auto primary=window->findChild<QQuickItem*>(QStringLiteral("startSyncButton"));
                    if(!primary||!primary->isVisible()||!primary->property("highlighted").toBool()||primary->mapToScene(QPointF(primary->width(),primary->height())).x()>window->width()){qCritical("Primary sync action is missing, hidden or clipped");app.exit(2);return;}
                    if(page==4){auto loader=window->findChild<QObject*>(QStringLiteral("pageLoader"));auto loaded=loader?loader->property("item").value<QObject*>():nullptr;auto port=loaded?loaded->findChild<QObject*>(QStringLiteral("apiPort")):nullptr;if(!port||port->property("value").toInt()!=12306){qCritical()<<"Default API port contract failed"<<port<<window->property("currentPage");app.exit(2);return;}}
                    const QStringList names={"overview","roots","connection","history","api","settings","encryption"};window->grabWindow().save(smoke+"/"+names[page]+".png");
                    if(page==6){auto mode=window->findChild<QObject*>("encryptionMode");auto password=window->findChild<QObject*>("keyPassword");if(!mode||!password||password->property("echoMode").toInt()!=2){qCritical("Encryption controls or password masking missing");app.exit(2);return;}}if(++*pageIndex<7)step->start(100);else{step->deleteLater();auto dialog=window->findChild<QObject*>(QStringLiteral("operationDetails"));if(!dialog||!QMetaObject::invokeMethod(dialog,"open",Qt::DirectConnection)){qCritical("Runtime log dialog unavailable");app.exit(2);return;}QTimer::singleShot(400,&app,[&,dialog,finish]{auto button=window->findChild<QQuickItem*>(QStringLiteral("openRuntimeLogs"));if(!button||!button->isVisible()||button->mapToScene(QPointF(button->width(),button->height())).x()>window->width()){qCritical("Runtime logs action hidden or clipped");app.exit(2);return;}window->grabWindow().save(smoke+"/logs-details.png");QMetaObject::invokeMethod(dialog,"close",Qt::DirectConnection);auto scroll=window->findChild<QObject*>("pageScroll");auto item=scroll?scroll->property("contentItem").value<QObject*>():nullptr;if(item)item->setProperty("contentY",std::max(0.0,item->property("contentHeight").toDouble()-item->property("height").toDouble()));QTimer::singleShot(300,&app,[&,finish]{window->grabWindow().save(smoke+"/encryption-bottom.png");captureAutomaticSettings(app,bridge,window,smoke,finish);});});}
                });
            });step->start(0);
        });
    });
    return app.exec();}
#include "gui_quick.moc"
