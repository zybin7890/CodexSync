#include "core.hpp"
#include "codex_sync.h"
#include <QApplication>
#include <QMainWindow>
#include <QStackedWidget>
#include <QListWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSpinBox>
#include <QStyleFactory>
#include <QStandardPaths>
#include <QClipboard>
#include <QCloseEvent>
#include <QTimer>
#include <QSaveFile>
#include <QFileInfo>
#include <QDateTime>
#include <QDir>
#include <QStyle>
#include <sodium.h>
namespace {
using cxs::Json;
QString q(const std::string& s){return QString::fromUtf8(s.data(),static_cast<qsizetype>(s.size()));}
std::string s(const QString& v){return v.toUtf8().toStdString();}
class Window final:public QMainWindow {
    Json config=cxs::discover();QString filename;
    QListWidget* nav=new QListWidget;QStackedWidget* pages=new QStackedWidget;
    QTableWidget* roots=new QTableWidget(0,2);QTableWidget* history=new QTableWidget(0,4);
    QLineEdit *url=new QLineEdit,*user=new QLineEdit,*password=new QLineEdit,*keyfile=new QLineEdit,*state=new QLineEdit,*device=new QLineEdit;
    QLineEdit* localstore=new QLineEdit;QPlainTextEdit* log=new QPlainTextEdit;
    QLabel* summary=new QLabel;QLabel* status=new QLabel(QStringLiteral("就绪，尚未读取或同步任何数据"));
    QProgressBar* progress=new QProgressBar;QFutureWatcher<QString>* watcher=new QFutureWatcher<QString>(this);
    QList<QPushButton*> actions;QProcess* api=new QProcess(this);QSpinBox* port=new QSpinBox;QLineEdit* apiToken=new QLineEdit;
    QString apiSecret;QString smokeDir;
    QPushButton* button(const QString& text,QBoxLayout* layout,std::function<void()> callback,bool operation=true){auto b=new QPushButton(text);b->setMinimumHeight(32);layout->addWidget(b);connect(b,&QPushButton::clicked,this,std::move(callback));if(operation)actions<<b;return b;}
    QWidget* page(const QString& title,const QString& subtitle,QVBoxLayout*& layout){auto w=new QWidget;layout=new QVBoxLayout(w);layout->setContentsMargins(24,20,24,20);layout->setSpacing(16);auto heading=new QLabel(title);auto font=heading->font();font.setPointSize(20);font.setWeight(QFont::DemiBold);heading->setFont(font);layout->addWidget(heading);auto sub=new QLabel(subtitle);sub->setWordWrap(true);layout->addWidget(sub);pages->addWidget(w);return w;}
    void info(const QString& title,const QString& text){QMessageBox::information(this,title,text);}
    void refresh(){roots->setRowCount(0);for(auto& r:config["roots"]){int row=roots->rowCount();roots->insertRow(row);roots->setItem(row,0,new QTableWidgetItem(q(r["id"])));roots->setItem(row,1,new QTableWidgetItem(q(r["path"])));}url->setText(q(config["remote"].value("url",std::string{})));localstore->setText(q(config["remote"].value("directory",std::string{})));state->setText(q(config["state"]));device->setText(q(config["device"]));summary->setText(QStringLiteral("%1 个数据目录   ·   XChaCha20-Poly1305 加密   ·   不传播删除").arg(roots->rowCount()));setWindowTitle(QStringLiteral("CodexSync")+(filename.isEmpty()?QString():QStringLiteral(" · ")+QFileInfo(filename).fileName()));}
    void gather(){config["roots"]=Json::array();for(int row=0;row<roots->rowCount();++row){if(!roots->item(row,0)||!roots->item(row,1))throw std::runtime_error("目录 ID 和路径都必须填写");config["roots"].push_back({{"id",s(roots->item(row,0)->text())},{"path",s(roots->item(row,1)->text())}});}config["state"]=s(state->text());config["device"]=s(device->text());if(localstore->text().isEmpty())config["remote"]={{"url",s(url->text())}};else config["remote"]={{"directory",s(localstore->text())}};}
    bool save(){try{gather();if(filename.isEmpty())filename=QFileDialog::getSaveFileName(this,QStringLiteral("保存配置"),QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+QStringLiteral("/codex-sync.json"),QStringLiteral("JSON (*.json)"));if(filename.isEmpty())return false;auto text=config.dump(2);cxs::write_atomic(cxs::path(s(filename)),cxs::Bytes(text.begin(),text.end()));refresh();return true;}catch(const std::exception& e){QMessageBox::warning(this,QStringLiteral("配置错误"),q(e.what()));return false;}}
    void run(Json request){if(watcher->isRunning())return;if(!save())return;try{request["config"]=s(filename);if(!keyfile->text().isEmpty()){auto key=cxs::read_bytes(cxs::path(s(keyfile->text())),256);request["key_hex"]=std::string(key.begin(),key.end());sodium_memzero(key.data(),key.size());}if(!user->text().isEmpty())request["dav_user"]=s(user->text());if(!password->text().isEmpty())request["dav_password"]=s(password->text());}catch(const std::exception& e){QMessageBox::warning(this,QStringLiteral("密钥错误"),q(e.what()));return;}
        for(auto b:actions)b->setEnabled(false);roots->setEnabled(false);progress->setRange(0,0);progress->show();status->setText(QStringLiteral("正在处理：")+q(request["op"]));
        watcher->setFuture(QtConcurrent::run([request=std::move(request)]() mutable {auto text=request.dump();char* result=nullptr;cxs_run(text.c_str(),&result);sodium_memzero(text.data(),text.size());if(!result)return QStringLiteral("{\"ok\":false,\"error\":\"allocation failure\"}");QString output=QString::fromUtf8(result);cxs_free(result);return output;}));
    }
    void stopped(){if(api->state()!=QProcess::NotRunning){api->terminate();if(!api->waitForFinished(1500)){api->kill();api->waitForFinished(1500);}}apiToken->clear();apiSecret.clear();}
public:
    Window(const QString& configArg,const QString& smoke):smokeDir(smoke){resize(1060,760);setMinimumSize(760,580);auto central=new QWidget;auto outer=new QVBoxLayout(central);outer->setContentsMargins(0,0,0,0);outer->setSpacing(0);auto body=new QHBoxLayout;body->setSpacing(0);nav->addItems({QStringLiteral("概览"),QStringLiteral("数据目录"),QStringLiteral("连接与加密"),QStringLiteral("快照与恢复"),QStringLiteral("终端模式")});nav->setFixedWidth(170);nav->setSpacing(6);nav->setFrameShape(QFrame::NoFrame);nav->setContentsMargins(8,12,8,12);nav->setCurrentRow(0);body->addWidget(nav);body->addWidget(pages,1);outer->addLayout(body,1);
        auto footer=new QHBoxLayout;footer->setContentsMargins(16,8,16,8);footer->addWidget(status,1);progress->setFixedWidth(160);progress->hide();footer->addWidget(progress);outer->addLayout(footer);setCentralWidget(central);connect(nav,&QListWidget::currentRowChanged,pages,&QStackedWidget::setCurrentIndex);
        QVBoxLayout* layout;page(QStringLiteral("Codex 数据同步"),QStringLiteral("加密保存对话、状态数据库、配置、插件、skills 与凭据文件。原始内容不会明文上传。"),layout);layout->addWidget(summary);auto controls=new QHBoxLayout;
        button(QStringLiteral("打开配置"),controls,[this]{auto f=QFileDialog::getOpenFileName(this,QStringLiteral("打开配置"),{},QStringLiteral("JSON (*.json)"));if(f.isEmpty())return;try{config=Json::parse(cxs::read_bytes(cxs::path(s(f)),1024*1024));filename=f;refresh();}catch(...){info(QStringLiteral("读取失败"),QStringLiteral("不是有效配置文件。"));}});
        button(QStringLiteral("保存配置"),controls,[this]{save();});layout->addLayout(controls);
        auto group=new QGroupBox(QStringLiteral("同步操作"));auto gl=new QVBoxLayout(group);auto row=new QHBoxLayout;
        button(QStringLiteral("检查数据范围"),row,[this]{run({{"op","scan"}});});button(QStringLiteral("加密备份"),row,[this]{run({{"op","backup"}});});button(QStringLiteral("双向同步…"),row,[this]{if(QMessageBox::question(this,QStringLiteral("确认离线同步"),QStringLiteral("应用文件级合并前，必须关闭 Codex。冲突会保留历史版本，删除不会传播。\n\nCodex 已关闭，继续？"),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes)run({{"op","sync"},{"offline",true}});});gl->addLayout(row);auto safety=new QLabel(QStringLiteral("备份可以在 Codex 运行时进行，SQLite 使用在线备份 API。恢复默认导出到新目录，不覆盖原数据。系统绑定的登录凭据可能需要重新登录。"));safety->setWordWrap(true);gl->addWidget(safety);layout->addWidget(group);log->setReadOnly(true);log->setPlaceholderText(QStringLiteral("操作结果会显示在这里。没有启动后台扫描或同步。"));layout->addWidget(log,1);
        page(QStringLiteral("数据目录"),QStringLiteral("每台设备用相同的目录 ID 映射不同本地路径。可添加外部项目、Conversations、skills 链接目标或钥匙串导出目录。"),layout);roots->setHorizontalHeaderLabels({QStringLiteral("目录 ID"),QStringLiteral("本地绝对路径")});roots->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);roots->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);roots->setSelectionBehavior(QAbstractItemView::SelectRows);layout->addWidget(roots,1);auto rr=new QHBoxLayout;button(QStringLiteral("添加目录"),rr,[this]{auto f=QFileDialog::getExistingDirectory(this,QStringLiteral("添加数据目录"));if(f.isEmpty())return;int n=roots->rowCount();roots->insertRow(n);roots->setItem(n,0,new QTableWidgetItem(QStringLiteral("extra_%1").arg(n)));roots->setItem(n,1,new QTableWidgetItem(f));});button(QStringLiteral("移除映射"),rr,[this]{int n=roots->currentRow();if(n>=0)roots->removeRow(n);});button(QStringLiteral("重新发现"),rr,[this]{config["roots"]=cxs::discover()["roots"];refresh();});layout->addLayout(rr);
        page(QStringLiteral("连接与加密"),QStringLiteral("WebDAV 强制校验 HTTPS 证书。用户名、密码只保存在本次进程内，不写入配置。"),layout);auto form=new QFormLayout;form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);url->setPlaceholderText(QStringLiteral("https://server/remote.php/dav/files/user"));form->addRow(QStringLiteral("WebDAV 地址"),url);form->addRow(QStringLiteral("用户名"),user);password->setEchoMode(QLineEdit::Password);form->addRow(QStringLiteral("密码 / 应用密码"),password);
        auto kr=new QHBoxLayout;keyfile->setReadOnly(true);kr->addWidget(keyfile,1);button(QStringLiteral("选择密钥"),kr,[this]{auto f=QFileDialog::getOpenFileName(this,QStringLiteral("选择同步密钥"));if(!f.isEmpty())keyfile->setText(f);});button(QStringLiteral("生成密钥"),kr,[this]{auto f=QFileDialog::getSaveFileName(this,QStringLiteral("保存主密钥"),{},QStringLiteral("Key (*.key)"));if(f.isEmpty())return;try{cxs::generate_key(cxs::path(s(f)));keyfile->setText(f);info(QStringLiteral("密钥已生成"),QStringLiteral("其他设备需要同一密钥。请另存安全副本，密钥丢失无法恢复。密钥本身不得放入同步目录。"));}catch(const std::exception& e){info(QStringLiteral("生成失败"),q(e.what()));}});form->addRow(QStringLiteral("加密主密钥"),kr);form->addRow(QStringLiteral("本地同步状态目录"),state);device->setReadOnly(true);form->addRow(QStringLiteral("设备 ID"),device);localstore->setPlaceholderText(QStringLiteral("留空使用 WebDAV；填写绝对路径则使用本地仓库"));form->addRow(QStringLiteral("本地仓库（可选）"),localstore);layout->addLayout(form);auto hint=new QLabel(QStringLiteral("同一仓库必须使用同一主密钥；每台设备必须有独立设备 ID。不要把状态目录或主密钥纳入数据目录。不可移植的系统凭据只作为不透明文件保留。"));hint->setWordWrap(true);layout->addWidget(hint);layout->addStretch();
        page(QStringLiteral("快照与恢复"),QStringLiteral("快照不可变，冲突版本保留在历史中。默认恢复至新目录，便于检查后再替换。"),layout);history->setHorizontalHeaderLabels({QStringLiteral("时间"),QStringLiteral("条目"),QStringLiteral("冲突"),QStringLiteral("快照 ID")});history->setEditTriggers(QAbstractItemView::NoEditTriggers);history->setSelectionBehavior(QAbstractItemView::SelectRows);history->setSelectionMode(QAbstractItemView::SingleSelection);history->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch);layout->addWidget(history,1);auto hr=new QHBoxLayout;button(QStringLiteral("刷新快照"),hr,[this]{run({{"op","history"}});});button(QStringLiteral("导出所选快照…"),hr,[this]{int row=history->currentRow();if(row<0)return;auto folder=QFileDialog::getExistingDirectory(this,QStringLiteral("选择父目录，将创建全新恢复目录"));if(folder.isEmpty())return;auto out=folder+QStringLiteral("/CodexSync-restore-")+QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));run({{"op","restore"},{"snapshot",s(history->item(row,3)->text())},{"output",s(out)}});});layout->addLayout(hr);
        page(QStringLiteral("终端模式"),QStringLiteral("只监听 127.0.0.1，要求 Bearer Token。关闭本窗口会停止由窗口启动的 API。"),layout);auto af=new QFormLayout;port->setRange(1024,65535);port->setValue(17841);af->addRow(QStringLiteral("端口"),port);apiToken->setReadOnly(true);apiToken->setEchoMode(QLineEdit::Password);af->addRow(QStringLiteral("访问令牌（本次进程）"),apiToken);layout->addLayout(af);auto ar=new QHBoxLayout;button(QStringLiteral("启动 API"),ar,[this]{if(api->state()!=QProcess::NotRunning||!save())return;auto environment=QProcessEnvironment::systemEnvironment();apiSecret=q(cxs::random_id()+cxs::random_id());environment.insert(QStringLiteral("CXS_API_TOKEN"),apiSecret);environment.insert(QStringLiteral("CXS_KEY_FILE"),keyfile->text());environment.insert(QStringLiteral("CXS_DAV_USER"),user->text());environment.insert(QStringLiteral("CXS_DAV_PASSWORD"),password->text());api->setProcessEnvironment(environment);auto executable=QCoreApplication::applicationDirPath()+QStringLiteral("/codex-sync");
#ifdef _WIN32
            executable+=QStringLiteral(".exe");
#endif
            api->start(executable,{QStringLiteral("serve"),QStringLiteral("--config"),filename,QStringLiteral("--port"),QString::number(port->value())});apiToken->setText(apiSecret);});button(QStringLiteral("复制令牌"),ar,[this]{QApplication::clipboard()->setText(apiSecret);},false);button(QStringLiteral("停止 API"),ar,[this]{stopped();});layout->addLayout(ar);auto apiHelp=new QPlainTextEdit;apiHelp->setReadOnly(true);apiHelp->setPlainText(QStringLiteral("GET  /v1/health\nPOST /v1/run\n\nAuthorization: Bearer <token>\nContent-Type: application/json\n\n{\"op\":\"backup\"}\n\n共享 C API：cxs_run() / cxs_free()\nCLI：codex-sync --help"));layout->addWidget(apiHelp,1);connect(api,&QProcess::readyReadStandardOutput,this,[this]{log->appendPlainText(QString::fromUtf8(api->readAllStandardOutput()));});connect(api,&QProcess::errorOccurred,this,[this]{status->setText(QStringLiteral("API 启动失败"));});
        connect(watcher,&QFutureWatcher<QString>::finished,this,[this]{auto output=watcher->result();log->appendPlainText(output);for(auto b:actions)b->setEnabled(true);roots->setEnabled(true);progress->hide();try{auto value=Json::parse(s(output));status->setText(value.value("ok",false)?QStringLiteral("操作完成"):QStringLiteral("操作失败，请查看结果"));if(value.value("ok",false)&&value.at("result").contains("snapshots")){history->setRowCount(0);for(auto& snapshot:value["result"]["snapshots"]){int row=history->rowCount();history->insertRow(row);auto time=QDateTime::fromSecsSinceEpoch(snapshot["created_unix"].get<qint64>());history->setItem(row,0,new QTableWidgetItem(time.toString(QStringLiteral("yyyy-MM-dd HH:mm"))));history->setItem(row,1,new QTableWidgetItem(QString::number(snapshot["entries"].get<int>())));history->setItem(row,2,new QTableWidgetItem(QString::number(snapshot["conflicts"].size())));history->setItem(row,3,new QTableWidgetItem(q(snapshot["snapshot"])));}}if(!smokeDir.isEmpty()){QSaveFile ready(smokeDir+QStringLiteral("/gui-operation.json"));ready.open(QIODevice::WriteOnly);ready.write(output.toUtf8());ready.commit();}}catch(...){status->setText(QStringLiteral("无法解析结果"));}});
        if(!configArg.isEmpty()){try{config=Json::parse(cxs::read_bytes(cxs::path(s(configArg)),1024*1024));filename=configArg;}catch(const std::exception& e){log->appendPlainText(q(e.what()));}}refresh();
        if(!smokeDir.isEmpty())QTimer::singleShot(300,this,[this]{QDir().mkpath(smokeDir);QSaveFile ready(smokeDir+QStringLiteral("/gui-ready.json"));ready.open(QIODevice::WriteOnly);ready.write(QByteArray("{\"framework\":\"Qt6 Widgets\",\"visible\":")+(isVisible()?"true":"false")+",\"pages\":5,\"style\":\""+QApplication::style()->objectName().toUtf8()+"\"}");ready.commit();grab().save(smokeDir+QStringLiteral("/gui.png"));if(!filename.isEmpty())run({{"op","scan"}});});
    }
    void closeEvent(QCloseEvent* event)override{if(watcher->isRunning()){info(QStringLiteral("操作进行中"),QStringLiteral("请等待当前文件操作结束后关闭。"));event->ignore();return;}stopped();password->clear();event->accept();}
};
}
int main(int argc,char** argv){QApplication app(argc,argv);app.setApplicationName(QStringLiteral("CodexSyncNative"));app.setOrganizationName(QStringLiteral("CodexSync"));
#ifdef _WIN32
    if(auto style=QStyleFactory::create(QStringLiteral("windows11")))app.setStyle(style);else if(auto style=QStyleFactory::create(QStringLiteral("windowsvista")))app.setStyle(style);
#endif
    QString config,smoke;auto args=app.arguments();for(int i=1;i<args.size();++i){if(args[i]==QStringLiteral("--config")&&i+1<args.size())config=args[++i];else if(args[i]==QStringLiteral("--ui-smoke")&&i+1<args.size())smoke=args[++i];}
    Window window(config,smoke);window.show();return app.exec();}
