#pragma once
#include "core.hpp"
#include <QObject>
#include <QFileSystemWatcher>
#include <functional>
#ifdef _WIN32
#include <windows.h>
#include <QWinEventNotifier>
#endif

// Native recursive directory notifications stay asleep while the source is idle.
// A low-frequency metadata reconciliation covers lost notifications.
class ChangeMonitor:public QObject {
public:
    explicit ChangeMonitor(QObject* parent=nullptr):QObject(parent){}
    std::function<void()> changed;
    bool configure(const cxs::Json& config){clear();config_=config;bool complete=true;
#ifdef _WIN32
        for(const auto& root:config.at("roots")){if(!root.value("enabled",true))continue;auto watch=std::make_unique<Watch>();watch->root=root.at("id");watch->directory=CreateFileW(cxs::path(root.at("path")).c_str(),FILE_LIST_DIRECTORY,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OVERLAPPED,nullptr);if(watch->directory==INVALID_HANDLE_VALUE){complete=false;continue;}
            watch->overlap.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!watch->overlap.hEvent){complete=false;continue;}watch->notifier=std::make_unique<QWinEventNotifier>(watch->overlap.hEvent,this);auto current=watch.get();
            QObject::connect(watch->notifier.get(),&QWinEventNotifier::activated,this,[this,current]{DWORD count=0;const bool ok=GetOverlappedResult(current->directory,&current->overlap,&count,FALSE)!=0;bool dirty=!ok||!count;if(!dirty){size_t offset=0;while(offset+sizeof(FILE_NOTIFY_INFORMATION)<=count){auto event=reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(current->buffer.data()+offset);if(event->FileNameLength>count-offset-FIELD_OFFSET(FILE_NOTIFY_INFORMATION,FileName)){dirty=true;break;}auto relative=cxs::utf8(cxs::fs::path(std::wstring(event->FileName,event->FileNameLength/sizeof(wchar_t))));auto leaf=cxs::utf8(cxs::path(relative).filename());if(!relative.ends_with("-shm")&&!leaf.ends_with(".lock")&&leaf!="LOCK"&&!leaf.starts_with("Singleton")&&!cxs::excluded_file(config_,relative,current->root))dirty=true;if(!event->NextEntryOffset)break;offset+=event->NextEntryOffset;}}ResetEvent(current->overlap.hEvent);if(!arm(*current)){current->notifier->setEnabled(false);dirty=true;}if(dirty&&changed)changed();if(!ok||!count)notifications_lost=true;});
            if(!arm(*watch)){complete=false;continue;}watches.push_back(std::move(watch));}
#else
        watcher=std::make_unique<QFileSystemWatcher>(this);QStringList folders;
        for(const auto& root:config.at("roots")){if(!root.value("enabled",true))continue;auto directory=cxs::path(root.at("path"));folders<<QString::fromUtf8(cxs::utf8(directory));for(cxs::fs::recursive_directory_iterator it(directory),end;it!=end;++it){if(it->is_symlink()){if(it->is_directory())it.disable_recursion_pending();continue;}if(it->is_directory())folders<<QString::fromUtf8(cxs::utf8(it->path()));}}
        complete=watcher->addPaths(folders).empty();QObject::connect(watcher.get(),&QFileSystemWatcher::directoryChanged,this,[this](const QString& folder){QStringList discovered;std::error_code error;for(cxs::fs::recursive_directory_iterator it(cxs::path(folder.toUtf8().toStdString()),error),end;!error&&it!=end;it.increment(error))if(it->is_directory()&&!it->is_symlink())discovered<<QString::fromUtf8(cxs::utf8(it->path()));watcher->addPaths(discovered);if(changed)changed();});
#endif
        return complete;}
    void clear(){
#ifdef _WIN32
        watches.clear();
#else
        watcher.reset();
#endif
        notifications_lost=false;}
    bool notifications_lost=false;
private:
    cxs::Json config_;
#ifdef _WIN32
    struct Watch {std::string root;HANDLE directory=INVALID_HANDLE_VALUE;OVERLAPPED overlap{};bool pending=false;alignas(FILE_NOTIFY_INFORMATION) std::array<unsigned char,64*1024> buffer{};std::unique_ptr<QWinEventNotifier> notifier;~Watch(){notifier.reset();if(directory!=INVALID_HANDLE_VALUE){if(pending){CancelIoEx(directory,&overlap);DWORD count=0;GetOverlappedResult(directory,&overlap,&count,TRUE);}CloseHandle(directory);}if(overlap.hEvent)CloseHandle(overlap.hEvent);}};
    static bool arm(Watch& watch){watch.pending=ReadDirectoryChangesW(watch.directory,watch.buffer.data(),static_cast<DWORD>(watch.buffer.size()),TRUE,FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_DIR_NAME|FILE_NOTIFY_CHANGE_SIZE|FILE_NOTIFY_CHANGE_LAST_WRITE|FILE_NOTIFY_CHANGE_ATTRIBUTES,nullptr,&watch.overlap,nullptr)!=0;return watch.pending;}
    std::vector<std::unique_ptr<Watch>> watches;
#else
    std::unique_ptr<QFileSystemWatcher> watcher;
#endif
};
