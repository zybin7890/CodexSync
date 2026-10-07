#include "job_coordinator.hpp"
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#endif
namespace cxs {
struct JobCoordinator::Native {
#ifdef _WIN32
    HANDLE handle=INVALID_HANDLE_VALUE;
#else
    int fd=-1;
#endif
};
JobCoordinator::JobCoordinator(const fs::path& state):native_(std::make_unique<Native>()) {
    private_directory(state);
#ifdef _WIN32
    native_->handle=CreateFileW((state/"operation.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(native_->handle==INVALID_HANDLE_VALUE)throw std::runtime_error("another operation owns this state directory");
#else
    native_->fd=::open((state/"operation.lock").c_str(),O_CREAT|O_RDWR,0600);if(native_->fd<0||flock(native_->fd,LOCK_EX|LOCK_NB)){if(native_->fd>=0)::close(native_->fd);native_->fd=-1;throw std::runtime_error("another operation owns this state directory");}
#endif
}
JobCoordinator::~JobCoordinator(){
#ifdef _WIN32
    if(native_->handle!=INVALID_HANDLE_VALUE)CloseHandle(native_->handle);
#else
    if(native_->fd>=0)::close(native_->fd);
#endif
}
bool JobCoordinator::busy(const fs::path& state){auto file=state/"operation.lock";
#ifdef _WIN32
    auto handle=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(handle==INVALID_HANDLE_VALUE){auto error=GetLastError();if(error==ERROR_SHARING_VIOLATION)return true;if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)return false;throw std::runtime_error("cannot inspect task owner");}CloseHandle(handle);return false;
#else
    auto fd=::open(file.c_str(),O_RDONLY);if(fd<0){if(errno==ENOENT)return false;throw std::runtime_error("cannot inspect task owner");}const bool owned=flock(fd,LOCK_SH|LOCK_NB)!=0;::close(fd);return owned;
#endif
}
Json JobCoordinator::control(const fs::path& state,const std::string& task,const std::string& action){
    if(action!="pause"&&action!="cancel")throw std::runtime_error("invalid task control");
    auto progress=Json::parse(read_bytes(state/"progress.json",8192));auto id=progress.value("job_id",std::string());if(id.empty())id=progress.at("operation_id");
    if(task.empty()||task!=id)throw std::runtime_error("task identity changed; reload progress before controlling it");
    auto text=Json{{"task_id",task},{"action",action}}.dump();write_atomic(state/"control.json",Bytes(text.begin(),text.end()));
    return {{"job_id",id},{"requested",action},{"owner_running",busy(state)}};
}
void JobCoordinator::checkpoint(const fs::path& state,const std::string& job,const std::string& operation){auto file=state/"control.json";if(!fs::exists(file))return;auto value=Json::parse(read_bytes(file,1024));auto task=value.value("task_id",std::string());if(task.empty()||(task!=job&&task!=operation))return;auto action=value.value("action",std::string());if(action=="pause"||action=="cancel")throw JobInterrupted(action=="pause"?"paused":"cancelled");}
}
