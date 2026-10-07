#include <memory>
#include <QDebug>

static void runProgressChecks(QApplication& app,Bridge& bridge,QQuickWindow* window,const QString& folder){
    struct State {int phase=0;bool upload=false,restore=false,stale=false,sidebarCompact=false;QString snapshot,uploadId;QElapsedTimer deadline;};
    auto state=std::make_shared<State>();state->deadline.start();auto poll=new QTimer(&app);poll->setInterval(50);
    const auto finish=[&app,&bridge,window,folder,state,poll](bool ok,const QString& message){poll->stop();QSaveFile report(folder+"/progress-check.json");if(report.open(QIODevice::WriteOnly)){report.write(QJsonDocument(QJsonObject{{"ok",ok},{"detail",message},{"upload_live",state->upload},{"restore_live",state->restore},{"stale_task_rejected",state->stale},{"idle_timer_stopped",!bridge.progressTimerRunning()},{"language",bridge.language()},{"phase",bridge.syncProgress().value("phase").toString()},{"backend_operations",bridge.backendOperationCount()}}).toJson());report.commit();}window->grabWindow().save(folder+"/progress-final.png");app.exit(ok?0:2);};
    QTimer::singleShot(400,&app,[&bridge,window,folder,poll,state,finish]{try{QMetaObject::invokeMethod(window,"acknowledgeNoticeForSmoke",Qt::DirectConnection);bridge.prepareProgressFixture(folder);if(!bridge.noticeAcknowledged()){finish(false,"notice gate failed");return;}bridge.execute({{"op","backup"}});state->uploadId=bridge.syncProgress().value("operation_id").toString();poll->start();}catch(const std::exception& e){finish(false,q(e.what()));}});
    QObject::connect(poll,&QTimer::timeout,&app,[&bridge,window,folder,state,finish]{
        if(state->deadline.elapsed()>30000){finish(false,"progress check timed out");return;}
        const auto data=bridge.syncProgress();const auto phase=data.value("phase").toString();
        auto panel=window->findChild<QQuickItem*>("syncProgressPanel");auto bar=window->findChild<QObject*>("syncProgressBar");
        if(!panel||!panel->isVisible()||!bar){finish(false,"visible progress controls missing");return;}
        auto sidebar=window->findChild<QQuickItem*>("sidebarSyncProgress");auto sidebarBar=window->findChild<QObject*>("sidebarProgressBar");
        if(!sidebar||!sidebar->isVisible()||!sidebarBar||sidebar->width()<20||sidebar->mapToScene(QPointF(0,sidebar->height())).y()>window->height()||std::abs(sidebarBar->property("value").toDouble()-bar->property("value").toDouble())>0.000001||sidebarBar->property("indeterminate").toBool()!=bar->property("indeterminate").toBool()){finish(false,"sidebar progress is missing, clipped or inconsistent");return;}
        if(bridge.busy()&&bridge.progressValue()>=1){finish(false,"unfinished operation displayed 100 percent");return;}
        switch(state->phase){
        case 0:
            if(bridge.busy()){
                if(phase=="uploading"&&data.value("done").toULongLong()>0&&data.value("done").toULongLong()<data.value("total").toULongLong()&&!state->upload){
                    if(bridge.progressIndeterminate()||bar->property("value").toDouble()<=0||bridge.progressDetail().isEmpty()){finish(false,"upload progress did not update");return;}
                    state->upload=true;window->grabWindow().save(folder+"/progress-upload.png");window->setProperty("currentPage",2);window->resize(760,580);
                }else if(state->upload&&window->width()==760&&phase=="uploading"&&!state->sidebarCompact){state->sidebarCompact=true;window->grabWindow().save(folder+"/progress-upload-compact.png");}return;
            }
            if(!state->upload||!state->sidebarCompact||phase!="completed"||bridge.progressValue()!=1||bridge.progressTimerRunning()){finish(false,"upload did not finish correctly");return;}
            state->stale=bridge.checkProgressIdentity();if(!state->stale){finish(false,"old operation progress replaced current task");return;}
            window->grabWindow().save(folder+"/progress-complete.png");window->setProperty("currentPage",0);window->resize(1120,790);
            {auto log=bridge.logText().trimmed().split('\n');auto result=Json::parse(s(log.last()));state->snapshot=q(result.at("result").at("snapshot").get<std::string>());}
            bridge.prepareRestoreProgressCheck();bridge.execute({{"op","restore"},{"snapshot",s(state->snapshot)},{"output",s(folder+"/restored")}});
            if(bridge.syncProgress().value("operation_id").toString()==state->uploadId||bridge.progressValue()!=0){finish(false,"next task reused old progress");return;}
            state->phase=1;break;
        case 1:
            if(bridge.busy()){
                if(phase=="downloading"&&data.value("files_done").toULongLong()>0&&!state->restore){state->restore=true;window->grabWindow().save(folder+"/progress-restore.png");}return;
            }
            if(!state->restore||phase!="completed"||bridge.progressValue()!=1||data.value("files_done").toULongLong()!=9||data.value("transferred_bytes").toULongLong()==0){finish(false,"restore did not update and complete");return;}
            bridge.updateRoot(0,"fixture",folder+"/missing-source");bridge.execute({{"op","backup"}});state->phase=2;break;
        case 2:
            if(bridge.busy())return;
            if(phase!="failed"||bridge.progressValue()>=1||bridge.progressTimerRunning()||bar->property("indeterminate").toBool()){finish(false,"failed task retained completion or active polling");return;}
            finish(true,"real isolated WebDAV upload/restore/failure and progress rendering passed");break;
        }
    });
}

static void runBackgroundChecks(QApplication& app,Bridge& bridge,const QString& folder){
    struct CheckState {
        int phase=0,baseline=0;
        qint64 started=QDateTime::currentMSecsSinceEpoch(),deadline=0,quietUntil=0;
        bool defaultOff=false,noticeGate=false,singleWorker=false,persisted=false,timerTriggered=false,eventTriggered=false,idleQuiet=false,disabledNoOperations=false,failurePaused=false,trayPersisted=false,timeChoice=false,amountChoice=false,thresholdHeld=false;
    };
    auto state=std::make_shared<CheckState>();
    auto poll=new QTimer(&app);poll->setInterval(25);
    const auto finish=[&app,&bridge,folder,state,poll](bool ok,const QString& detail){
        poll->stop();QObject::disconnect(poll,nullptr,&app,nullptr);
        bridge.setAutoSync(false);
        QDir().mkpath(folder);QSaveFile output(folder+QStringLiteral("/background-check.json"));
        const auto report=QJsonObject{{"ok",ok},{"detail",detail},{"backend_operations",bridge.backendOperationCount()},
            {"google_login_states",true},{"default_auto_off",state->defaultOff},{"notice_gate",state->noticeGate},{"busy_no_reentry",state->singleWorker},
            {"enabled_persisted",state->persisted},{"real_timer_trigger",state->timerTriggered},{"recursive_event_trigger",state->eventTriggered},{"idle_no_operations",state->idleQuiet},{"disabled_no_operations",state->disabledNoOperations},
            {"failure_paused_persisted",state->failurePaused},{"tray_preference_persisted",state->trayPersisted},{"time_schedule_verified",state->timeChoice},{"amount_schedule_verified",state->amountChoice},{"below_threshold_no_backup",state->thresholdHeld},{"provider",QStringLiteral("local")}};
        bool written=output.open(QIODevice::WriteOnly);
        if(written){const auto bytes=QJsonDocument(report).toJson(QJsonDocument::Indented);written=output.write(bytes)==bytes.size()&&output.commit();}
        if(ok&&written)qInfo("BACKGROUND_CHECKS_PASS: local timer, persistence, no reentry, failure pause, tray preference");
        else qCritical().noquote()<<"BACKGROUND_CHECKS_FAIL:"<<(written?detail:QStringLiteral("cannot write isolated check report"));
        app.exit(ok&&written?0:2);poll->deleteLater();
    };
    QObject::connect(poll,&QTimer::timeout,&app,[&bridge,folder,state,finish]{
        const auto now=QDateTime::currentMSecsSinceEpoch();
        if(now-state->started>20000||(state->deadline&&now>state->deadline)){finish(false,QStringLiteral("asynchronous check timed out"));return;}
        const auto require=[&](bool condition,const QString& message){if(!condition)finish(false,message);return condition;};
        try {
            switch(state->phase){
            case 0:{
                state->defaultOff=!bridge.autoSync();
                if(!require(state->defaultOff&&!bridge.noticeAcknowledged()&&bridge.backendOperationCount()==0,QStringLiteral("fixture preferences are not fresh")))return;
                bridge.setAutoSync(true);bridge.automaticTick();
                state->noticeGate=!bridge.autoSync()&&!bridge.busy()&&bridge.backendOperationCount()==0;
                if(!require(state->noticeGate,QStringLiteral("unacknowledged notice allowed automatic work")))return;
                bridge.prepareBackgroundFixture(folder);
                if(!require(bridge.checkGoogleStates(),QStringLiteral("Google status accepted unverified credentials or concealed failure")))return;
                bridge.load(bridge.configFile());
                if(!require(!bridge.configFile().isEmpty()&&!bridge.keyFile().isEmpty()&&bridge.acknowledgeNotice(),QStringLiteral("cannot prepare isolated configuration")))return;
                bridge.setCheckInterval(300000);bridge.setAutoSync(true);
                if(!require(bridge.autoSync(),QStringLiteral("valid isolated configuration could not enable auto backup")))return;
                bridge.automaticTick();bridge.automaticTick();
                state->singleWorker=bridge.backendOperationCount()==1;
                if(!require(state->singleWorker,QStringLiteral("automatic ticks started duplicate workers")))return;
                state->phase=1;state->deadline=now+10000;break;
            }
            case 1:{
                if(bridge.busy())return;
                if(!require(bridge.autoSync()&&bridge.backendOperationCount()==1,QStringLiteral("successful automatic backup paused unexpectedly")))return;
                Bridge replay(bridge.configFile(),folder);state->persisted=replay.autoSync();replay.setCheckInterval(300000);
                if(!require(state->persisted,QStringLiteral("enabled auto preference did not persist")))return;
                state->quietUntil=now+2200;state->phase=6;state->deadline=now+10000;break;
            }
            case 6:{
                if(!require(!bridge.busy()&&bridge.backendOperationCount()==1&&!bridge.progressTimerRunning(),QStringLiteral("idle source caused backend work or progress polling")))return;
                if(now<state->quietUntil)return;
                state->idleQuiet=true;QDir().mkpath(folder+"/source/nested");QSaveFile changed(folder+"/source/nested/event.txt");
                if(!require(changed.open(QIODevice::WriteOnly)&&changed.write("native directory event")>0&&changed.commit(),QStringLiteral("cannot create recursive event fixture")))return;
                state->phase=7;state->deadline=now+8000;break;
            }
            case 7:{
                if(bridge.backendOperationCount()==1)return;
                if(!require(bridge.backendOperationCount()==2,QStringLiteral("recursive event scheduled duplicate workers")))return;
                if(bridge.busy())return;
                state->eventTriggered=true;state->baseline=bridge.backendOperationCount();bridge.setCheckInterval(100);state->phase=2;state->deadline=now+10000;break;
            }
            case 2:{
                if(bridge.backendOperationCount()==state->baseline)return;
                state->timerTriggered=bridge.backendOperationCount()==state->baseline+1;
                if(!require(state->timerTriggered,QStringLiteral("real timer scheduled unexpected worker count")))return;
                bridge.setAutoSync(false);state->phase=3;state->deadline=now+10000;break;
            }
            case 3:{
                if(bridge.busy())return;
                state->baseline=bridge.backendOperationCount();state->quietUntil=now+250;state->phase=4;break;
            }
            case 4:{
                if(!require(!bridge.autoSync()&&bridge.backendOperationCount()==state->baseline,QStringLiteral("disabled automatic backup continued running")))return;
                if(now<state->quietUntil)return;
                state->disabledNoOperations=true;
                Bridge replay(bridge.configFile(),folder);
                if(!require(!replay.autoSync(),QStringLiteral("disabled auto preference did not persist")))return;
                bridge.setSetting(QStringLiteral("directory"),folder+QStringLiteral("/source/tiny.txt"));bridge.saveConfig();
                bridge.setCheckInterval(300000);bridge.setAutoSync(true);
                if(!require(bridge.autoSync(),QStringLiteral("failure fixture could not enable auto backup")))return;
                bridge.automaticTick();
                if(!require(bridge.backendOperationCount()==state->baseline+1,QStringLiteral("failure fixture did not reach backend")))return;
                state->phase=5;state->deadline=now+10000;break;
            }
            case 5:{
                if(bridge.busy())return;
                Bridge replay(bridge.configFile(),folder);
                state->failurePaused=!bridge.autoSync()&&!replay.autoSync();
                if(!require(state->failurePaused,QStringLiteral("failed automatic task was not paused persistently")))return;
                bridge.automaticTick();
                if(!require(bridge.backendOperationCount()==state->baseline+1,QStringLiteral("paused failure repeated backend work")))return;
                bridge.setCloseToTray(true);Bridge trayReplay(bridge.configFile(),folder);state->trayPersisted=trayReplay.closeToTray();
                if(!require(state->trayPersisted,QStringLiteral("close-to-tray preference did not persist")))return;
                bridge.prepareBackgroundFixture(folder+"/schedules");bridge.setAutoTrigger("time");bridge.setAutoIntervalMinutes(1);bridge.setAutoSync(true);bridge.setCheckInterval(350);
                Bridge scheduleReplay(bridge.configFile(),folder);state->timeChoice=scheduleReplay.autoTrigger()=="time"&&scheduleReplay.autoIntervalMinutes()==1&&bridge.closeToTray();
                if(!require(state->timeChoice,QStringLiteral("time schedule or background preference did not persist")))return;
                state->baseline=bridge.backendOperationCount();bridge.reconcileChangesForCheck();
                state->quietUntil=now+100;state->phase=8;state->deadline=now+8000;break;
            }
            case 8:{
                if(!require(bridge.backendOperationCount()==state->baseline,QStringLiteral("time schedule triggered before its timer")))return;
                if(now<state->quietUntil)return;state->phase=9;break;
            }
            case 9:{
                if(bridge.backendOperationCount()==state->baseline)return;
                if(bridge.busy())return;
                if(!require(bridge.backendOperationCount()==state->baseline+1,QStringLiteral("time schedule duplicated workers")))return;
                bridge.setAutoSync(false);bridge.setAutoTrigger("amount");bridge.setAutoThresholdMB(1);
                cxs::write_atomic(cxs::path(s(folder+"/schedules/source/below.bin")),cxs::Bytes(512*1024,0x41));
                bridge.setAutoSync(true);state->baseline=bridge.backendOperationCount();bridge.reconcileChangesForCheck();state->phase=10;state->deadline=now+8000;break;
            }
            case 10:{
                if(bridge.busy())return;
                state->thresholdHeld=bridge.backendOperationCount()==state->baseline+1&&bridge.autoPending().contains("0.50");
                if(!require(state->thresholdHeld,QStringLiteral("below-threshold changes started a backup")))return;
                cxs::write_atomic(cxs::path(s(folder+"/schedules/source/above.bin")),cxs::Bytes(512*1024,0x42));
                state->baseline=bridge.backendOperationCount();bridge.reconcileChangesForCheck();state->phase=11;state->deadline=now+8000;break;
            }
            case 11:{
                if(bridge.busy()||bridge.backendOperationCount()<state->baseline+2)return;
                Bridge scheduleReplay(bridge.configFile(),folder);state->amountChoice=scheduleReplay.autoTrigger()=="amount"&&scheduleReplay.autoThresholdMB()==1&&bridge.backendOperationCount()==state->baseline+2;
                if(!require(state->amountChoice,QStringLiteral("quantity threshold did not trigger exactly one backup")))return;
                finish(true,QStringLiteral("background event, interval and quantity schedules, metadata-only threshold and single worker passed"));break;
            }
            }
        }catch(const std::exception& error){finish(false,q(error.what()));}
    });
    poll->start();
}
