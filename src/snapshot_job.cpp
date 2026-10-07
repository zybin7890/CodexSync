#include "snapshot_job.hpp"
#include <sodium.h>
#include <chrono>
#include <cwctype>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif
namespace cxs {
namespace {
Bytes encode(const Json& value) { const auto text=value.dump(); return {text.begin(),text.end()}; }
Json decode(const fs::path& file,const Key& key,const std::string& domain) {const auto plain=open(read_bytes(file),key,domain);return Json::parse(plain.begin(),plain.end());}
#ifdef _WIN32
Json directory_identity(const fs::path& source){
    // Query the containing directory when another application denies opening
    // the file. This still obtains file ID and change time without data access.
    auto handle=CreateFileW(source.parent_path().c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if(handle==INVALID_HANDLE_VALUE)return Json::object();FILE_ID_INFO volume{};
    if(!GetFileInformationByHandleEx(handle,FileIdInfo,&volume,sizeof volume)){CloseHandle(handle);return Json::object();}
    alignas(FILE_ID_EXTD_DIR_INFO) std::array<unsigned char,64*1024> buffer{};bool first=true;Json result=Json::object();
    while(GetFileInformationByHandleEx(handle,first?FileIdExtdDirectoryRestartInfo:FileIdExtdDirectoryInfo,buffer.data(),static_cast<DWORD>(buffer.size()))){first=false;size_t offset=0;
        for(;;){auto entry=reinterpret_cast<const FILE_ID_EXTD_DIR_INFO*>(buffer.data()+offset);auto name=std::wstring(entry->FileName,entry->FileNameLength/sizeof(wchar_t));
            if(!_wcsicmp(name.c_str(),source.filename().c_str())){char encoded[33];sodium_bin2hex(encoded,sizeof encoded,entry->FileId.Identifier,sizeof entry->FileId.Identifier);result={{"exists",true},{"reliable",true},{"volume",volume.VolumeSerialNumber},{"file",encoded},{"size",entry->EndOfFile.QuadPart},{"write",entry->LastWriteTime.QuadPart},{"change",entry->ChangeTime.QuadPart},{"created",entry->CreationTime.QuadPart},{"attributes",entry->FileAttributes}};break;}
            if(!entry->NextEntryOffset)break;offset+=entry->NextEntryOffset;
        }
        if(!result.empty())break;
    }
    CloseHandle(handle);return result;
}
#endif
}
int64_t unix_now() {return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
Json source_identity(const fs::path& source) {
    std::error_code error;const auto status=fs::symlink_status(source,error);
    if(error==std::errc::no_such_file_or_directory||(!error&&!fs::exists(status)))return {{"exists",false},{"reliable",true}};
    if(error)throw std::runtime_error("source metadata unavailable: "+utf8(source));
#ifdef _WIN32
    auto handle=CreateFileW(source.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(handle==INVALID_HANDLE_VALUE){auto code=GetLastError();if(code==ERROR_SHARING_VIOLATION){auto metadata=directory_identity(source);if(!metadata.empty())return metadata;return {{"exists",true},{"reliable",false},{"size",fs::file_size(source)},{"write",fs::last_write_time(source).time_since_epoch().count()}};}throw std::runtime_error("cannot inspect source identity: "+utf8(source)+"; Win32 "+std::to_string(code));}
    FILE_BASIC_INFO basic{};FILE_STANDARD_INFO standard{};FILE_ID_INFO id{};
    const bool reliable=GetFileInformationByHandleEx(handle,FileBasicInfo,&basic,sizeof basic)&&GetFileInformationByHandleEx(handle,FileStandardInfo,&standard,sizeof standard)&&GetFileInformationByHandleEx(handle,FileIdInfo,&id,sizeof id);
    CloseHandle(handle);
    if(reliable){char encoded[33];sodium_bin2hex(encoded,sizeof encoded,id.FileId.Identifier,sizeof id.FileId.Identifier);
        return {{"exists",true},{"reliable",true},{"volume",id.VolumeSerialNumber},{"file",encoded},{"size",standard.EndOfFile.QuadPart},{"write",basic.LastWriteTime.QuadPart},{"change",basic.ChangeTime.QuadPart},{"created",basic.CreationTime.QuadPart},{"attributes",basic.FileAttributes}};}
    return {{"exists",true},{"reliable",false},{"size",fs::file_size(source)},{"write",fs::last_write_time(source).time_since_epoch().count()}};
#else
    struct stat value{};if(lstat(source.c_str(),&value))throw std::runtime_error("cannot inspect source identity: "+utf8(source));
    return {{"exists",true},{"reliable",true},{"volume",value.st_dev},{"file",value.st_ino},{"size",value.st_size},{"write",{value.st_mtim.tv_sec,value.st_mtim.tv_nsec}},{"change",{value.st_ctim.tv_sec,value.st_ctim.tv_nsec}},{"mode",value.st_mode}};
#endif
}
Json source_family_identity(const fs::path& source) {Json family={{"main",source_identity(source)}};for(auto suffix:{"-wal","-journal"}){auto side=source;side+=suffix;family[suffix]=source_identity(side);}return family;}
std::string selection_identity(const Json& config,const Key& key) {
    Json identity={{"schema",1},{"device",config.at("device")},{"payload_mode",config.value("payload_mode",std::string("encrypted"))},{"capture_strategy",config.value("windows_vss",false)?"vss-sqlite-v2":"live-sqlite-v2"},{"format",config.at("format")},{"roots",config.at("roots")}};
    for(auto& root:identity["roots"]){auto canonical=fs::weakly_canonical(path(root.at("path")));auto text=utf8(canonical);
#ifdef _WIN32
        auto wide=canonical.wstring();std::transform(wide.begin(),wide.end(),wide.begin(),[](wchar_t ch){return std::towlower(ch);});text=utf8(fs::path(wide));
#endif
        root["path"]=text;}
    for(auto field:{"data_types","selection","exclude","encryption_rules"})identity[field]=config.value(field,Json::object());
    return digest(encode(identity),key);
}
ChangeCatalog::ChangeCatalog(const fs::path& state,const std::string& scope,const Key& key):filename_(state/"catalogs"/(scope+".cxs")),domain_("change-catalog-v1:"+scope),key_(key) {
    if(!fs::exists(filename_))return;
    try{data=decode(filename_,key_,domain_);if(data.at("schema")!=1||!data.at("files").is_object())throw std::runtime_error("unsupported catalog");}
    catch(...){data=Json::object();recovery="catalog unavailable; metadata baseline rebuilt";}
}
void ChangeCatalog::save(const Json& value) {write_atomic(filename_,seal(encode(value),key_,domain_));data=value;}
SnapshotJob::SnapshotJob(const fs::path& state,const Key& key):filename_(state/"jobs"/"active.cxs"),key_(key) {
    if(!fs::exists(filename_))return;data=decode(filename_,key_,"snapshot-job-v1");if(data.at("schema")!=1)throw std::runtime_error("unsupported prepared snapshot journal");for(const auto& object:data.at("confirmed"))confirmed_.insert(object.get<std::string>());
    receipts_=state/"jobs"/data.at("job_id").get<std::string>()/"receipts";
    if(pending()&&fs::exists(receipts_))for(const auto& item:fs::directory_iterator(receipts_)){auto object=utf8(item.path().stem());auto value=decode(item.path(),key_,"job-receipt:"+data.at("job_id").get<std::string>()+":"+object);if(value.at("target")!=data.at("target"))throw std::runtime_error("receipt target mismatch");confirmed_.insert(object);}
}
bool SnapshotJob::pending()const {return !data.empty()&&data.value("status",std::string())!="completed";}
void SnapshotJob::prepare(const Json& manifest,const Json& catalog,const std::string& scope,const std::string& target) {data={{"schema",1},{"job_id",random_id()},{"scope",scope},{"target",target},{"status","prepared"},{"manifest",manifest},{"catalog",catalog},{"confirmed",Json::array()},{"snapshot",random_id()+random_id()}};confirmed_.clear();receipts_=filename_.parent_path()/data.at("job_id").get<std::string>()/"receipts";save();}
void SnapshotJob::save() {write_atomic(filename_,seal(encode(data),key_,"snapshot-job-v1"));}
void SnapshotJob::check_target(const std::string& scope,const std::string& target)const {if(data.at("scope")!=scope||data.at("target")!=target)throw std::runtime_error("prepared snapshot belongs to a different selection, key, account or repository; resume its original configuration");}
void SnapshotJob::confirm(const std::string& object) {if(confirmed_.insert(object).second)write_atomic(receipts_/(object+".cxs"),seal(encode(Json{{"target",data.at("target")}}),key_,"job-receipt:"+data.at("job_id").get<std::string>()+":"+object));}
bool SnapshotJob::confirmed(const std::string& object)const {return confirmed_.contains(object);}
void SnapshotJob::inherit_confirmed(const Json& objects){for(const auto& object:objects)confirmed_.insert(object.get<std::string>());data["confirmed"]=confirmed_;save();}
void SnapshotJob::committed() {data["confirmed"]=confirmed_;data["status"]="completed";data["committed_unix"]=unix_now();save();}
}
