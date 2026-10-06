#include "core.hpp"
#include "codex_sync.h"
#include "google_drive.hpp"
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
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
#include <QSaveFile>
#include <QStyleHints>
#include <QIcon>
#include <QDesktopServices>
#include <QFileInfo>
#include <sodium.h>
using cxs::Json;
static QString q(const std::string& s){return QString::fromUtf8(s.data(),static_cast<qsizetype>(s.size()));}
static std::string s(const QString& v){return v.toUtf8().toStdString();}
class Bridge final:public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap configuration READ configuration NOTIFY configurationChanged)
    Q_PROPERTY(QString configFile READ configFile NOTIFY configurationChanged)
    Q_PROPERTY(QString keyFile READ keyFile NOTIFY configurationChanged)
    Q_PROPERTY(QString deviceName READ deviceName CONSTANT)
    Q_PROPERTY(QString platform READ platform CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString lastActivity READ lastActivity NOTIFY stateChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool apiRunning READ apiRunning NOTIFY stateChanged)
    Q_PROPERTY(QString apiToken READ apiToken NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap conversationHistory READ conversationHistory NOTIFY stateChanged)
    Q_PROPERTY(bool googleAuthorized READ googleAuthorized NOTIFY stateChanged)
    Q_PROPERTY(QString terminalCommand READ terminalCommand CONSTANT)
    Q_PROPERTY(QVariantList snapshots READ snapshots NOTIFY stateChanged)
    Json config=cxs::discover();QString filename,key,username,password,googleSecret,statusText=QStringLiteral("尚未配置"),activity=QStringLiteral("还没有同步记录"),log;
    QFutureWatcher<QString> watcher;QProcess process;QString secret;QVariantList snapshotList;QVariantMap historyCoverage;
    bool save(){if(filename.isEmpty())filename=QFileDialog::getSaveFileName(nullptr,QStringLiteral("保存同步配置"),QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+QStringLiteral("/codex-sync.json"),QStringLiteral("JSON (*.json)"));if(filename.isEmpty())return false;
        try{auto text=config.dump(2);cxs::write_atomic(cxs::path(s(filename)),cxs::Bytes(text.begin(),text.end()));emit configurationChanged();return true;}catch(const std::exception& e){error(q(e.what()));return false;}}
    void error(const QString& message){statusText=QStringLiteral("操作失败");log+=message+QStringLiteral("\n");emit stateChanged();QMessageBox::warning(nullptr,QStringLiteral("CodexSync"),message);}
public:
    explicit Bridge(const QString& file){if(!file.isEmpty())load(file);
        connect(&watcher,&QFutureWatcher<QString>::finished,this,[this]{auto text=watcher.result();log+=text+QStringLiteral("\n");try{auto value=Json::parse(s(text));if(value.value("ok",false)){statusText=QStringLiteral("操作完成");activity=QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"))+QStringLiteral(" · 操作已完成");auto result=value.at("result");if(result.contains("active")&&result.contains("archived")){historyCoverage=QJsonDocument::fromJson(QByteArray::fromStdString(result.dump())).object().toVariantMap();statusText=result.value("coverage_complete",false)?QStringLiteral("历史核查完成"):QStringLiteral("历史核查发现缺失引用");activity=QStringLiteral("历史对话与归档已核查");}if(result.contains("snapshots")){snapshotList.clear();for(auto& item:result.at("snapshots")){QVariantMap m;m["id"]=q(item.at("snapshot"));m["time"]=QDateTime::fromSecsSinceEpoch(item.at("created_unix").get<qint64>()).toString(QStringLiteral("yyyy-MM-dd HH:mm"));m["entries"]=item.at("entries").get<qulonglong>();m["conflicts"]=static_cast<int>(item.at("conflicts").size());snapshotList.push_back(m);}}}else{statusText=QStringLiteral("操作失败");activity=q(value.value("error",std::string("查看操作详情")));}}catch(...){statusText=QStringLiteral("无法解析操作结果");}emit stateChanged();});
        connect(&process,&QProcess::stateChanged,this,[this]{emit stateChanged();});connect(&process,&QProcess::readyReadStandardOutput,this,[this]{log+=QString::fromUtf8(process.readAllStandardOutput());emit stateChanged();});connect(&process,&QProcess::errorOccurred,this,[this]{statusText=QStringLiteral("API 启动失败");emit stateChanged();});
    }
    ~Bridge(){stopApi();watcher.waitForFinished();}
    QVariantMap configuration()const{return QJsonDocument::fromJson(QByteArray::fromStdString(config.dump())).object().toVariantMap();}
    bool googleAuthorized()const{try{return !cxs::env("CXS_GOOGLE_ACCESS_TOKEN").empty()||!cxs::env("CXS_GOOGLE_REFRESH_TOKEN").empty()||QFileInfo::exists(q(cxs::utf8(cxs::google_credential_path(config))));}catch(...){return false;}}
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
    QString configFile()const{return filename;}QString keyFile()const{return key;}QString deviceName()const{return QSysInfo::machineHostName();}QString platform()const{return QSysInfo::prettyProductName();}
    QString status()const{return statusText;}QString lastActivity()const{return activity;}QString logText()const{return log;}bool busy()const{return watcher.isRunning();}bool apiRunning()const{return process.state()!=QProcess::NotRunning;}QString apiToken()const{return secret;}QVariantList snapshots()const{return snapshotList;}
    void load(const QString& file){try{config=Json::parse(cxs::read_bytes(cxs::path(s(file)),1024*1024));cxs::resolve_history_roots(config);historyCoverage.clear();filename=file;emit stateChanged();emit configurationChanged();}catch(const std::exception& e){error(q(e.what()));}}
    Q_INVOKABLE void openConfig(){if(busy())return;auto file=QFileDialog::getOpenFileName(nullptr,QStringLiteral("打开同步配置"),{},QStringLiteral("JSON (*.json)"));if(!file.isEmpty())load(file);}
    Q_INVOKABLE void saveConfig(){if(!busy())save();}
    Q_INVOKABLE void setSetting(const QString& name,const QString& value){if(busy())return;auto n=s(name);if(n=="provider"){config["remote"]["provider"]=s(value);if(value!=QStringLiteral("local"))config["remote"].erase("directory");}else if(n=="url"){config["remote"].erase("directory");config["remote"]["url"]=s(value);}else if(n=="directory"){if(value.isEmpty())config["remote"].erase("directory");else config["remote"]["directory"]=s(value);}else if(n=="client_id"||n=="repository")config["remote"][n]=s(value);else if(n=="google_client_secret")googleSecret=value;else if(n=="state")config["state"]=s(value);else if(n=="username")username=value;else if(n=="password")password=value;emit configurationChanged();emit stateChanged();}
    Q_INVOKABLE void updateRoot(int index,const QString& id,const QString& path){if(busy()||index<0||static_cast<size_t>(index)>=config["roots"].size())return;config["roots"][index]={{"id",s(id)},{"path",s(path)}};historyCoverage.clear();emit stateChanged();emit configurationChanged();}
    Q_INVOKABLE void removeRoot(int index){if(!busy()&&index>=0&&static_cast<size_t>(index)<config["roots"].size()){config["roots"].erase(config["roots"].begin()+index);historyCoverage.clear();emit stateChanged();emit configurationChanged();}}
    Q_INVOKABLE void addRoot(){if(busy())return;auto folder=QFileDialog::getExistingDirectory(nullptr,QStringLiteral("添加数据目录"));if(folder.isEmpty())return;config["roots"].push_back({{"id","extra_"+std::to_string(config["roots"].size())},{"path",s(folder)}});historyCoverage.clear();emit stateChanged();emit configurationChanged();}
    Q_INVOKABLE void discoverRoots(){if(busy())return;for(auto& found:cxs::discover()["roots"]){bool present=false;for(auto& current:config["roots"])if(current.at("id")==found.at("id")||current.at("path")==found.at("path")){present=true;break;}if(!present)config["roots"].push_back(found);}cxs::resolve_history_roots(config);historyCoverage.clear();emit configurationChanged();emit stateChanged();}
    Q_INVOKABLE void chooseKey(){auto file=QFileDialog::getOpenFileName(nullptr,QStringLiteral("选择同步主密钥"));if(!file.isEmpty()){key=file;emit configurationChanged();}}
    Q_INVOKABLE void createKey(){auto file=QFileDialog::getSaveFileName(nullptr,QStringLiteral("保存同步主密钥"),{},QStringLiteral("Key (*.key)"));if(file.isEmpty())return;try{cxs::generate_key(cxs::path(s(file)));key=file;emit configurationChanged();QMessageBox::information(nullptr,QStringLiteral("主密钥已生成"),QStringLiteral("其他设备需要使用同一主密钥。请保存安全副本，不要将密钥放进同步目录。"));}catch(const std::exception& e){error(q(e.what()));}}
    Q_INVOKABLE void authorizeGoogle(){
        if(busy()||!save())return;
        Json secrets;
        try{if(!key.isEmpty()){auto data=cxs::read_bytes(cxs::path(s(key)),256);secrets["key_hex"]=std::string(data.begin(),data.end());sodium_memzero(data.data(),data.size());}if(!googleSecret.isEmpty())secrets["google_client_secret"]=s(googleSecret);cxs::google_client_id(config.at("remote"));}
        catch(const std::exception& e){error(q(e.what()));return;}
        auto current=config;statusText=QStringLiteral("等待 Google 授权");emit stateChanged();
        watcher.setFuture(QtConcurrent::run([current=std::move(current),secrets=std::move(secrets)]()mutable{
            auto clear=[&]{for(auto field:{"key_hex","google_client_secret"})if(secrets.contains(field)){auto& value=secrets[field].get_ref<std::string&>();sodium_memzero(value.data(),value.size());}};
            try{auto result=cxs::google_authorize(current,secrets,[](const std::string& url){bool opened=false;QMetaObject::invokeMethod(qApp,[&]{opened=QDesktopServices::openUrl(QUrl(q(url)));},Qt::BlockingQueuedConnection);if(!opened)throw std::runtime_error("cannot open the Google authorization browser");});clear();return q(Json{{"ok",true},{"result",result}}.dump());}
            catch(const std::exception& e){clear();return q(Json{{"ok",false},{"error",e.what()}}.dump());}
        }));emit stateChanged();
    }
    Q_INVOKABLE void run(const QString& operation){if(busy())return;Json request={{"op",s(operation)}};if(operation==QStringLiteral("sync")){if(QMessageBox::question(nullptr,QStringLiteral("确认离线同步"),QStringLiteral("请先关闭 Codex。冲突版本会保留，删除不会传播。\n\nCodex 已关闭，继续？"),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;request["offline"]=true;}execute(std::move(request));}
    Q_INVOKABLE void restore(const QString& snapshot){auto folder=QFileDialog::getExistingDirectory(nullptr,QStringLiteral("选择恢复目录的父目录"));if(folder.isEmpty())return;execute({{"op","restore"},{"snapshot",s(snapshot)},{"output",s(folder+QStringLiteral("/CodexSync-restore-")+QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")))}});}
    void execute(Json request){if(busy()||!save())return;try{request["config"]=s(filename);if(!key.isEmpty()){auto b=cxs::read_bytes(cxs::path(s(key)),256);request["key_hex"]=std::string(b.begin(),b.end());sodium_memzero(b.data(),b.size());}if(!username.isEmpty())request["dav_user"]=s(username);if(!password.isEmpty())request["dav_password"]=s(password);}catch(const std::exception& e){error(q(e.what()));return;}
        statusText=QStringLiteral("正在处理");watcher.setFuture(QtConcurrent::run([request=std::move(request)]()mutable{auto input=request.dump();char* result=nullptr;cxs_run(input.c_str(),&result);sodium_memzero(input.data(),input.size());if(!result)return QStringLiteral("{\"ok\":false,\"error\":\"allocation failure\"}");auto text=QString::fromUtf8(result);cxs_free(result);return text;}));emit stateChanged();}
    Q_INVOKABLE void startApi(int port){if(apiRunning()||!save())return;auto environment=QProcessEnvironment::systemEnvironment();secret=q(cxs::random_id()+cxs::random_id());environment.insert(QStringLiteral("CXS_API_TOKEN"),secret);environment.insert(QStringLiteral("CXS_KEY_FILE"),key);environment.insert(QStringLiteral("CXS_DAV_USER"),username);environment.insert(QStringLiteral("CXS_DAV_PASSWORD"),password);process.setProcessEnvironment(environment);auto executable=QCoreApplication::applicationDirPath()+QStringLiteral("/codex-sync");
#ifdef _WIN32
        executable+=QStringLiteral(".exe");
#endif
        process.start(executable,{QStringLiteral("serve"),QStringLiteral("--config"),filename,QStringLiteral("--port"),QString::number(port)});emit stateChanged();}
    Q_INVOKABLE void stopApi(){if(process.state()!=QProcess::NotRunning){process.terminate();if(!process.waitForFinished(1500)){process.kill();process.waitForFinished(1500);}}secret.clear();emit stateChanged();}
    Q_INVOKABLE void copyToken(){QApplication::clipboard()->setText(secret);}
    Q_INVOKABLE void copyTerminalCommand(){QApplication::clipboard()->setText(terminalCommand());}
    Q_INVOKABLE void setTheme(int mode){QGuiApplication::styleHints()->setColorScheme(mode==1?Qt::ColorScheme::Light:mode==2?Qt::ColorScheme::Dark:Qt::ColorScheme::Unknown);}
signals:void configurationChanged();void stateChanged();
};
int main(int argc,char** argv){QApplication app(argc,argv);app.setApplicationName(QStringLiteral("CodexSyncNative"));app.setOrganizationName(QStringLiteral("CodexSync"));
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
    QString config,smoke;int theme=0;auto args=app.arguments();for(int i=1;i<args.size();++i){if(args[i]==QStringLiteral("--config")&&i+1<args.size())config=args[++i];else if(args[i]==QStringLiteral("--ui-smoke")&&i+1<args.size())smoke=args[++i];else if(args[i]==QStringLiteral("--ui-light"))theme=1;}
    Bridge bridge(config);bridge.setTheme(theme);QQmlApplicationEngine engine;engine.rootContext()->setContextProperty(QStringLiteral("ui"),&bridge);engine.load(QUrl(QStringLiteral("qrc:/ui/Main.qml")));if(engine.rootObjects().isEmpty())return 1;auto window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if(!smoke.isEmpty())QTimer::singleShot(1000,&app,[&]{QDir().mkpath(smoke);for(int page=0;page<6;++page){QTimer::singleShot(page*450,&app,[&,page]{window->setProperty("currentPage",page);});QTimer::singleShot(page*450+300,&app,[&,page]{const QStringList names={QStringLiteral("overview"),QStringLiteral("roots"),QStringLiteral("connection"),QStringLiteral("history"),QStringLiteral("api"),QStringLiteral("settings")};window->grabWindow().save(smoke+"/"+names[page]+QStringLiteral(".png"));});}QTimer::singleShot(2800,&app,[&]{window->setProperty("currentPage",0);window->resize(780,680);QTimer::singleShot(350,&app,[&]{window->grabWindow().save(smoke+QStringLiteral("/compact.png"));window->resize(1120,790);QSaveFile ready(smoke+QStringLiteral("/gui-ready.json"));ready.open(QIODevice::WriteOnly);ready.write(QJsonDocument(QJsonObject{{"framework",QStringLiteral("Qt Quick")},{"style",QQuickStyle::name()},{"pages",6},{"screenshots",7},{"backend_operations",0}}).toJson(QJsonDocument::Compact));ready.commit();});});});
    return app.exec();}
#include "gui_quick.moc"
