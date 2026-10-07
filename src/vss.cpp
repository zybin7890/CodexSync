#include "vss.hpp"
#include <chrono>
#include <thread>
#include <map>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <sddl.h>
#include <aclapi.h>
#endif
namespace cxs {
#ifdef _WIN32
namespace {
void wincheck(bool ok, const char* what) {
    if (!ok) throw std::runtime_error(std::string(what)+"; Win32 "+std::to_string(GetLastError()));
}
fs::path volume(const fs::path& p) {
    wchar_t value[32768]{};
    wincheck(GetVolumePathNameW(p.c_str(),value,32768)!=0,"cannot find source volume");
    if(GetDriveTypeW(value)!=DRIVE_FIXED)throw std::runtime_error("VSS requires a local fixed volume");
    return fs::path(value);
}
void job_acl(const fs::path& job) {
    HANDLE token{};wincheck(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)!=0,"cannot identify VSS owner");
    DWORD size{};GetTokenInformation(token,TokenUser,nullptr,0,&size);Bytes data(size);
    bool ok=GetTokenInformation(token,TokenUser,data.data(),size,&size)!=0;CloseHandle(token);wincheck(ok,"cannot read VSS owner");
    LPWSTR sid{};wincheck(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&sid)!=0,"cannot encode VSS owner");
    std::wstring sddl=L"D:P(A;OICI;FA;;;"+std::wstring(sid)+L")(A;OICI;FA;;;BA)(A;OICI;FA;;;SY)";LocalFree(sid);
    PSECURITY_DESCRIPTOR sd{};wincheck(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&sd,nullptr)!=0,"cannot build VSS job ACL");
    PACL acl{};BOOL present{},defaults{};GetSecurityDescriptorDacl(sd,&present,&acl,&defaults);
    auto result=SetNamedSecurityInfoW(const_cast<wchar_t*>(job.c_str()),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,nullptr,nullptr,acl,nullptr);LocalFree(sd);
    if(result!=ERROR_SUCCESS)throw std::runtime_error("cannot protect VSS job; Win32 "+std::to_string(result));
}
Json read_json(const fs::path& p){auto b=read_bytes(p,65536);return Json::parse(b.begin(),b.end());}
std::string volume_key(const fs::path& p){auto s=utf8(p);std::replace(s.begin(),s.end(),'/','\\');return s;}
}
struct VssSession::Impl {
    fs::path job,state;HANDLE process{};std::map<fs::path,fs::path> roots;bool finished=false;
    ~Impl(){try{signal();}catch(...){}if(process)CloseHandle(process);}
    void signal(){if(!job.empty())write_atomic(job/"done",{},false);}
};
VssSession::VssSession(const Json& config,const fs::path& state):impl_(std::make_unique<Impl>()) {
    impl_->state=state;auto jobs=state/"vss-jobs";private_directory(jobs);
    impl_->job=fs::absolute(jobs/random_id()).make_preferred();fs::create_directory(impl_->job);job_acl(impl_->job);
    Json volumes=Json::array();for(const auto& root:config.at("roots"))if(root.value("enabled",true)){
        auto original=fs::absolute(path(root.at("path"))).lexically_normal();auto v=volume(original);
        if(!impl_->roots.contains(v)){impl_->roots[v]=fs::path{};volumes.push_back(volume_key(v));}
    }
    FILETIME created{},exited{},kernel{},user{};wincheck(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)!=0,"cannot identify VSS parent lifetime");
    ULARGE_INTEGER time{};time.LowPart=created.dwLowDateTime;time.HighPart=created.dwHighDateTime;
    auto request=Json{{"format",1},{"volumes",volumes},{"parent_creation_time",time.QuadPart}}.dump();
    write_atomic(impl_->job/"request.json",Bytes(request.begin(),request.end()),false);
    auto helper=application_directory()/"cxs-vss-helper.exe";if(!fs::is_regular_file(helper))throw std::runtime_error("VSS helper missing; reinstall the complete Windows package");
    std::wstring args=L"\""+impl_->job.wstring()+L"\" "+std::to_wstring(GetCurrentProcessId());
    SHELLEXECUTEINFOW launch{};launch.cbSize=sizeof launch;launch.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC|SEE_MASK_FLAG_NO_UI;launch.lpVerb=L"runas";launch.lpFile=helper.c_str();launch.lpParameters=args.c_str();launch.nShow=SW_HIDE;
    runtime_log(state,"vss_started",{{"phase","snapshotting"}});
    wincheck(ShellExecuteExW(&launch)!=0,"VSS elevation cancelled or failed");impl_->process=launch.hProcess;
    auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(3);
    while(!fs::exists(impl_->job/"response.json")){
        if(WaitForSingleObject(impl_->process,200)==WAIT_OBJECT_0)throw std::runtime_error("VSS helper exited without a response");
        if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("VSS creation timed out");
    }
    auto response=read_json(impl_->job/"response.json");if(!response.value("ok",false))throw std::runtime_error("VSS failed: "+response.value("error",std::string("unknown error")));
    for(auto& [v,shadow]:impl_->roots){auto text=response.at("roots").at(volume_key(v)).get<std::string>();
        auto normalized=path(text).wstring();const std::wstring prefix=L"\\\\?\\GLOBALROOT\\Device\\HarddiskVolumeShadowCopy";
        if(!normalized.starts_with(prefix)||normalized.back()!=L'\\')throw std::runtime_error("invalid VSS device response");
        auto number=normalized.substr(prefix.size(),normalized.size()-prefix.size()-1);
        if(number.empty()||!std::all_of(number.begin(),number.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';}))throw std::runtime_error("invalid VSS device number");shadow=path(text);
    }
}
fs::path VssSession::source(const fs::path& original)const {
    auto full=fs::absolute(original).lexically_normal();auto v=volume(full);auto found=impl_->roots.find(v);
    if(found==impl_->roots.end())throw std::runtime_error("source volume not in this VSS set");
    auto relative=full.lexically_relative(v);if(relative.empty()||relative.is_absolute())throw std::runtime_error("invalid VSS source mapping");
    for(const auto& part:relative)if(part==L"..")throw std::runtime_error("VSS source escapes volume");
    return (found->second/relative).make_preferred();
}
void VssSession::finish(){
    if(impl_->finished)return;impl_->signal();
    if(WaitForSingleObject(impl_->process,60000)!=WAIT_OBJECT_0)throw std::runtime_error("VSS cleanup timed out; helper still monitors parent lifetime");
    auto cleanup=read_json(impl_->job/"cleanup.json");if(!cleanup.value("ok",false))throw std::runtime_error("VSS cleanup failed: "+cleanup.value("error",std::string("unknown error")));
    impl_->finished=true;runtime_log(impl_->state,"vss_completed",{{"phase","snapshot_released"}});
}
#else
struct VssSession::Impl {};
VssSession::VssSession(const Json&,const fs::path&){throw std::runtime_error("VSS is only supported on Windows");}
fs::path VssSession::source(const fs::path&)const{throw std::runtime_error("VSS unavailable");}
void VssSession::finish(){}
#endif
VssSession::~VssSession()=default;
}
