#include "core.hpp"
#include "disclaimer.hpp"
#include "codex_sync.h"
#include "vss.hpp"
#include "snapshot_job.hpp"
#include "job_coordinator.hpp"
#include "operation_runtime.hpp"
#include <sodium.h>
#include <sqlite3.h>
#include <fstream>
#include <set>
#include <map>
#include <thread>
#include <chrono>
#include <memory>
#include <mutex>
#include <cstring>
#include <atomic>
#include <climits>
#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#else
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#endif
namespace cxs {
namespace {

bool live_codex(){
#ifdef _WIN32
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)throw std::runtime_error("cannot check active Codex processes");PROCESSENTRY32W e{};e.dwSize=sizeof e;bool active=false;if(Process32FirstW(snapshot,&e))do{if(!_wcsicmp(e.szExeFile,L"Codex.exe")||!_wcsicmp(e.szExeFile,L"codex-cli.exe")){active=true;break;}}while(Process32NextW(snapshot,&e));CloseHandle(snapshot);return active;
#else
    for(auto& e:fs::directory_iterator("/proc")){auto name=e.path().filename().string();if(name.empty()||!std::isdigit(name[0]))continue;std::ifstream in(e.path()/"comm");std::string comm;std::getline(in,comm);std::transform(comm.begin(),comm.end(),comm.begin(),[](unsigned char c){return std::tolower(c);});if(comm=="codex"||comm=="codex-cli")return true;}return false;
#endif
}
void offline(const Json& c,const Json& request){if(!request.value("offline",false))throw std::runtime_error("applying changes requires --offline confirmation; close Codex first");auto real=discover();bool actual=false;for(auto& r:c.at("roots"))for(auto& d:real.at("roots"))if(inside(path(r.at("path")),path(d.at("path")))||inside(path(d.at("path")),path(r.at("path"))))actual=true;if(actual&&live_codex())throw std::runtime_error("Codex is running; use backup or staged restore instead of applying changes");}
Json apply(OperationRuntime& c,const Json& desired,const Json& current,const Json& request){
    offline(c.config,request);if(fs::exists(c.state/"pending.cxs"))throw std::runtime_error("an interrupted apply journal exists; inspect rollback before continuing");
    auto transaction=random_id();auto stage=c.state/"scratch"/transaction;private_directory(stage);Json changed=Json::array();
    Json changed_entries=Json::array();for(auto& [name,e]:desired.at("entries").items())if(selected_file(c.config,e.at("root"),e.at("path"))&&(!current.at("entries").contains(name)||current.at("entries").at(name).at("hash")!=e.at("hash")))changed_entries.push_back(e);
    c.restore_progress(changed_entries);
    for(auto& [name,e]:desired.at("entries").items()){if(!selected_file(c.config,e.at("root"),e.at("path")))continue;auto target=safe_target(c.root_path(e.at("root")),e.at("path"));if(current.at("entries").contains(name)&&current.at("entries").at(name).at("hash")==e.at("hash"))continue;auto staged=safe_target(stage/path(e.at("root")),e.at("path"));c.materialize(e,staged);changed.push_back({{"name",name},{"target",utf8(target)},{"staged",utf8(staged)}});}
    // Encrypted, complete rollback snapshot is durable before the first replacement.
    private_directory(c.state/"rollbacks");auto rollback_id=random_id()+random_id();write_atomic(c.state/"rollbacks"/(rollback_id+".cxs"),seal(bytes(current),c.key.value,"rollback:"+rollback_id));Json journal={{"rollback",rollback_id},{"changes",changed},{"applied",Json::array()}};write_atomic(c.state/"pending.cxs",seal(bytes(journal),c.key.value,"journal"));
    uint64_t apply_files=0,apply_bytes=0;for(auto& e:changed_entries)if(e.value("kind",std::string())=="file"){++apply_files;apply_bytes+=e.at("bytes").get<uint64_t>();}c.begin_progress("applying",changed.size(),apply_files,apply_bytes);
    for(auto& x:changed){auto name=x.at("name").get<std::string>();auto& e=desired.at("entries").at(name);auto target=safe_target(c.root_path(e.at("root")),e.at("path"));auto staged=path(x.at("staged"));
        if(e.at("kind")=="directory"){fs::create_directories(target);}else{if(fs::exists(target)&&fs::is_directory(target))throw std::runtime_error("file/directory conflict; rollback journal retained");
            if(fs::exists(target)){auto fresh=c.entry(target,e.at("root"),e.at("path"));if(!current.at("entries").contains(name)||fresh.at("hash")!=current.at("entries").at(name).at("hash"))throw std::runtime_error("destination changed after preflight; rollback journal retained");}
            fs::create_directories(target.parent_path());if(e.value("sqlite",false)){private_directory(stage/"sidecars");for(auto suffix:{"-wal","-shm","-journal"}){auto side=target;side+=suffix;if(fs::exists(side))fs::rename(side,stage/"sidecars"/(random_id()+suffix));}}
#ifdef _WIN32
            if(!MoveFileExW(staged.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_COPY_ALLOWED|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("replacement failed; encrypted rollback retained");
#else
            // Staging may be on another filesystem, copy to a private sibling first.
            auto sibling=target;sibling+=".cxs-new-"+transaction;fs::copy_file(staged,sibling);fs::rename(sibling,target);
#endif
        }c.advance_progress(1,e.at("kind")=="file"?1:0,e.value("bytes",uint64_t(0)));journal["applied"].push_back(name);write_atomic(c.state/"pending.cxs",seal(bytes(journal),c.key.value,"journal"));}
    // Archive completed journal; never delete user files or propagate deletions.
    fs::rename(c.state/"pending.cxs",c.state/"rollbacks"/(rollback_id+".journal.cxs"));return {{"applied",changed.size()},{"rollback",rollback_id},{"deletions",0}};
}
Json load_config(const fs::path& filename){auto c=json(read_bytes(filename,1024*1024));normalize_application_config(c);validate_selection_rules(c);resolve_history_roots(c);if(c.at("format")!=1)throw std::runtime_error("unsupported config format");std::set<std::string> ids;std::vector<fs::path> roots;auto state=path(c.at("state"));if(!state.is_absolute())throw std::runtime_error("state path must be absolute");for(auto& r:c.at("roots")){auto id=r.at("id").get<std::string>();OperationRuntime::require_id_root(id);if(!ids.insert(id).second)throw std::runtime_error("duplicate root id");auto p=path(r.at("path"));if(!p.is_absolute())throw std::runtime_error("root paths must be absolute");if(inside(state,p)||inside(p,state))throw std::runtime_error("state and data roots must not overlap");for(auto& other:roots)if(inside(p,other)||inside(other,p))throw std::runtime_error("data roots must not overlap");roots.push_back(p);}if(roots.empty())throw std::runtime_error("configure at least one data root");if(c.at("remote").contains("directory")){auto remote=path(c.at("remote").at("directory"));if(!remote.is_absolute())throw std::runtime_error("remote directory must be absolute");for(auto& p:roots)if(inside(remote,p)||inside(p,remote))throw std::runtime_error("remote repository and data roots must not overlap");}return c;}
}
Json discover(){auto home=env("CODEX_HOME");
#ifdef _WIN32
    auto user=env("USERPROFILE");auto state=application_data_directory()/"state";
#else
    auto user=env("HOME");auto xdg=env("XDG_STATE_HOME");auto state=(xdg.empty()?path(user)/".local/state":path(xdg))/"codex-sync";
#endif
    const auto default_home=path(user)/".codex";
    if(home.empty())home=utf8(default_home);
    Json roots=Json::array();
    auto add=[&](const std::string& id,fs::path p){
        if(!fs::is_directory(p))return;
        p=fs::weakly_canonical(p);
        for(auto& root:roots)if(inside(p,path(root.at("path"))))return;
        roots.push_back({{"id",id},{"path",utf8(p)}});
    };
    add("codex_home",path(home));add("codex_home_default",default_home);add("agents",path(user)/".agents");
#ifdef _WIN32
    add("desktop",path(env("APPDATA"))/"Codex");add("desktop_local",path(env("LOCALAPPDATA"))/"Codex");
#else
    auto config_home=env("XDG_CONFIG_HOME");add("desktop",(config_home.empty()?path(user)/".config":path(config_home))/"Codex");
#endif
    // Workspace data can be arbitrarily large; explicitly add it rather than guess external locations.
    Json config={{"format",1},{"device",random_id()},{"state",utf8(state)},{"roots",roots},{"remote",{{"url","https://your-server.example/dav"}}},{"exclude",Json::array()},{"note","Active and archived sessions, including .jsonl.zst and SQLite history, are included. Directory aliases are resolved; external linked history folders get named roots. Add custom workspaces, external skills and credential export directories explicitly. OS-bound credentials are opaque; login portability is not guaranteed."}};
    resolve_history_roots(config);
    return config;
}
Json execute_impl(Json request){if(sodium_init()<0)throw std::runtime_error("crypto initialization failed");auto op=request.at("op").get<std::string>();if(op=="disclaimer"){const auto language=request.value("language",std::string("zh_CN"));if(language!="en"&&language!="zh_CN")throw std::runtime_error("language must be en or zh_CN");return {{"version",std::string(disclaimer_version)},{"language",language},{"text",std::string(language=="en"?disclaimer_en_text:disclaimer_text)}};}if(op=="discover")return discover();if(op=="paths")return application_paths();auto config=load_config(path(request.value("config",utf8(default_configuration_path()))));if(op=="pause"||op=="cancel")return JobCoordinator::control(path(config.at("state")),request.value("job_id",std::string()),op);if(op=="progress"){auto file=path(config.at("state"))/"progress.json";if(!fs::exists(file))return {{"phase","idle"}};auto data=json(read_bytes(file,8192));Json result=Json::object();for(auto field:{"operation_id","job_id","phase","total","done","files_done","files_total","bytes_done","bytes_total","transferred_bytes","elapsed_ms","metadata_checked","body_files","body_bytes","source_probe_bytes","reused_files","resumed_objects","uploaded_objects","error_code","retryable"})if(data.contains(field))result[field]=data.at(field);result["owner_running"]=JobCoordinator::busy(path(config.at("state")));return result;}if(op=="catalog")return selection_catalog(config);if(op!="backup"&&op!="resume")compile_selection(config);if(op=="conversations")return conversation_inventory(config);if(op=="scan"){
        // Read file metadata and thread-index references only, not messages or credentials.
        Json result={{"roots",Json::array()},{"files",0},{"bytes",0},{"skipped_links",Json::array()}};uint64_t count=0,size=0;for(auto& r:config.at("roots")){if(!r.value("enabled",true)){auto v=r;v["files"]=0;v["bytes"]=0;result["roots"].push_back(v);continue;}auto p=path(r.at("path"));if(!fs::is_directory(p)||link(p))throw std::runtime_error("missing or linked data root");uint64_t n=0,b=0;for(fs::recursive_directory_iterator i(p),end;i!=end;++i){if(link(i->path())){result["skipped_links"].push_back(utf8(i->path()));if(i->is_directory())i.disable_recursion_pending();continue;}auto relative=utf8(i->path().lexically_relative(p));if(excluded_file(config,relative,r.at("id"))){if(i->is_directory())i.disable_recursion_pending();continue;}if(i->is_regular_file()&&!volatile_file(relative)&&selected_file(config,r.at("id"),relative)){++n;b+=i->file_size();}}count+=n;size+=b;auto v=r;v["files"]=n;v["bytes"]=b;result["roots"].push_back(v);}result["files"]=count;result["bytes"]=size;result["conversation_history"]=conversation_inventory(config);return result;
    }
    if(op=="changes"){
        SecretKey key(request);auto state=path(config.at("state"));auto marker=state/"key-check.cxs";
        if(fs::exists(marker))json(open(read_bytes(marker,1024),key.value,"state-key-v1"));
        ChangeCatalog catalog(state,selection_identity(config,key.value),key.value);SnapshotJob job(state,key.value);
        if(job.pending())return {{"pending_job",true},{"files",0},{"bytes",0},{"body_bytes",0}};
        std::map<std::string,const Json*> previous;const Json empty_records=Json::object();const auto& previous_records=catalog.data.contains("files")?catalog.data.at("files"):empty_records;
        for(const auto& [name,record]:previous_records.items()){
            previous[name]=&record.at("identity").at("main");
            if(record.at("entry").value("sqlite",false))for(auto suffix:{"-wal","-journal"}){
                const auto& side=record.at("identity").at(suffix);if(side.value("exists",false))previous[name+suffix]=&side;
            }
        }
        uint64_t files=0,bytes=0,checked=0;
        for(const auto& root:config.at("roots")){if(!root.value("enabled",true))continue;auto id=root.at("id").get<std::string>();auto directory=path(root.at("path"));
            if(!fs::is_directory(directory)||link(directory))throw std::runtime_error("missing or linked data root");
            for(fs::recursive_directory_iterator it(directory),end;it!=end;++it){auto relative=utf8(it->path().lexically_relative(directory));
                if(link(it->path())||excluded_file(config,relative,id)){if(it->is_directory())it.disable_recursion_pending();continue;}
                if(!it->is_regular_file()||!selected_file(config,id,relative))continue;
                if(volatile_file(relative)&&!relative.ends_with("-wal"))continue;
                auto identity=source_identity(it->path());auto found=previous.find(id+"/"+relative);++checked;
                if(found==previous.end()||!identity.value("reliable",false)||*found->second!=identity){++files;bytes+=identity.value("size",uint64_t(0));}
                if(found!=previous.end())previous.erase(found);
            }
        }
        const auto deleted=previous.size();files+=deleted;
        return {{"pending_job",false},{"files",files},{"bytes",bytes},{"deleted_files",deleted},{"metadata_checked",checked},{"body_bytes",0}};
    }
    if(std::none_of(config.at("roots").begin(),config.at("roots").end(),[](const Json& r){return r.value("enabled",true);}))throw std::runtime_error("select at least one enabled data root");
    if(config.value("payload_mode",std::string("encrypted"))!="encrypted"&&(op=="sync"||request.value("apply",false)))throw std::runtime_error("original and selective backups restore to a separate directory; direct application requires the encrypted SQLite recovery mode");
    OperationRuntime c(config,request);
    try {
    auto complete=[&](Json result){c.finish_progress(true);return result;};
    if(op=="backup"||op=="resume"){
        if(config.at("payload_mode")=="selective"&&config.value("encryption_rules",Json::array()).empty())throw std::runtime_error("select at least one encryption root or relative path");
        const auto marker=c.state/"key-check.cxs";
        if(fs::exists(marker)){if(json(open(read_bytes(marker,1024),c.key.value,"state-key-v1")).at("format")!=1)throw std::runtime_error("state key identity mismatch");}
        else write_atomic(marker,seal(bytes(Json{{"format",1}}),c.key.value,"state-key-v1"));
        auto scope=selection_identity(config,c.key.value);auto target=c.store.identity(c.key.value);
        ChangeCatalog catalog(c.state,scope,c.key.value);SnapshotJob job(c.state,c.key.value);
        if(!job.pending()&&!job.data.empty()&&job.data.at("scope")==scope&&job.data.at("target")==target&&catalog.data.value("snapshot",std::string())!=job.data.at("snapshot").get<std::string>()){
            auto recovered=job.data.at("catalog");recovered["target"]=target;recovered["snapshot"]=job.data.at("snapshot");recovered["remote_objects"]=job.data.at("confirmed");catalog.save(recovered);
        }
        const bool resumed=job.pending();Json m,next_catalog;
        if(resumed){job.check_target(scope,target);c.job_id=job.data.at("job_id");if(op=="resume"){write_atomic(c.state/"control.json",bytes(Json::object()));job.data["status"]="prepared";job.save();}else c.checkpoint();m=job.data.at("manifest");next_catalog=job.data.at("catalog");c.phase_progress("resuming");}
        else{
            if(op=="resume")throw std::runtime_error("no prepared snapshot to resume");
            compile_selection(c.config);m=c.incremental_capture(catalog,next_catalog);
            if(!catalog.data.empty()&&catalog.data.value("target",std::string())==target&&catalog.data.at("entries")==m.at("entries")){
                next_catalog["target"]=target;next_catalog["snapshot"]=catalog.data.at("snapshot");next_catalog["remote_objects"]=catalog.data.value("remote_objects",Json::array());catalog.save(next_catalog);
                c.finish_progress(true);c.phase_progress("no_changes");auto result=c.metrics();result.update(Json{{"no_changes",true},{"snapshot",catalog.data.at("snapshot")},{"files",c.files_total},{"bytes",m.at("bytes")},{"skipped",m.at("skipped")},{"conversation_history",m.at("conversation_history")}});return result;
            }
            const auto confirmed=catalog.data.value("target",std::string())==target?catalog.data.value("remote_objects",Json::array()):Json::array();
            job.prepare(m,next_catalog,scope,target);if(c.store.original()&&catalog.data.value("target",std::string())==target&&catalog.data.contains("snapshot")){job.data["previous_snapshot"]=catalog.data.at("snapshot");job.save();}job.inherit_confirmed(confirmed);c.job_id=job.data.at("job_id");c.phase_progress("prepared");
        }
        Json saved;try{saved=c.publish(m,Json::array(),&job);}catch(const JobInterrupted& signal){job.data["status"]=signal.phase;job.save();throw;}job.committed();next_catalog["target"]=target;next_catalog["snapshot"]=saved.at("snapshot");next_catalog["remote_objects"]=job.data.at("confirmed");catalog.save(next_catalog);
        auto result=c.metrics();result.update(Json{{"job_id",c.job_id},{"resumed",resumed},{"snapshot",saved.at("snapshot")},{"files",std::count_if(m["entries"].begin(),m["entries"].end(),[](const Json& e){return e.value("kind",std::string())=="file";})},{"bytes",m.at("bytes")},{"skipped",m.at("skipped")},{"conversation_history",m.at("conversation_history")},{"storage",c.store.location()},{"mode",config.at("payload_mode")},{"vss_live_files",m.value("vss_live_files",Json::array())},{"source_consistency",m.value("source_consistency",std::string())}});return complete(result);
    }
    if(op=="history"){Json out=Json::array();for(auto file:c.store.list("snapshots")){auto extension=c.store.original()?std::string(".json"):std::string(".cxs");if(!file.ends_with(extension))continue;auto id=file.substr(0,file.size()-extension.size());auto m=c.snapshot(id);out.push_back({{"snapshot",id},{"device",m["device"]},{"created_unix",m["created_unix"]},{"entries",m["entries"].size()},{"conflicts",m["conflicts"]}});}return {{"snapshots",out},{"storage",c.store.location()}};}
    if(op=="restore"){auto id=request.at("snapshot").get<std::string>();require_id(id);auto m=c.snapshot(id);auto& entries=m.at("entries");for(auto i=entries.begin();i!=entries.end();){const auto& e=i.value();if(!selected_file(config,e.at("root"),e.at("path")))i=entries.erase(i);else ++i;}if(request.value("apply",false)){offline(config,request);auto local=c.scan();return complete(apply(c,m,local,request));}auto output=path(request.at("output"));if(!output.is_absolute())throw std::runtime_error("restore output must be absolute");if(fs::exists(output)&&!fs::is_empty(output))throw std::runtime_error("staged restore output must be empty");for(auto& r:config.at("roots"))if(inside(output,path(r.at("path")))||inside(path(r.at("path")),output))throw std::runtime_error("staged restore must not overlap source roots");if(inside(output,c.state)||inside(c.state,output))throw std::runtime_error("restore output and state must not overlap");private_directory(output);c.restore_progress(m.at("entries"));for(auto& e:m.at("entries")){auto root=e.at("root").get<std::string>();OperationRuntime::require_id_root(root);c.materialize(e,safe_target(output/root,e.at("path")));}return complete(Json{{"snapshot",id},{"output",utf8(output)},{"entries",m["entries"].size()},{"source_unchanged",true}});}
    if(op=="rollback"){auto id=request.at("snapshot").get<std::string>();require_id(id);auto m=json(open(read_bytes(c.state/"rollbacks"/(id+".cxs")),c.key.value,"rollback:"+id));auto output=path(request.at("output"));if(!output.is_absolute()||fs::exists(output))throw std::runtime_error("rollback output must be a new absolute directory");for(auto& r:config.at("roots"))if(inside(output,path(r.at("path")))||inside(path(r.at("path")),output))throw std::runtime_error("rollback output overlaps source");private_directory(output);c.restore_progress(m.at("entries"));for(auto& e:m.at("entries")){auto root=e.at("root").get<std::string>();OperationRuntime::require_id_root(root);c.materialize(e,safe_target(output/root,e.at("path")));}return complete(Json{{"rollback",id},{"output",utf8(output)},{"note","Source was not modified; inspect the pending encrypted journal and restore while Codex is closed."}});}
    if(op=="sync"){
        offline(config,request);auto local=c.scan();c.phase_progress("merging");auto heads=c.frontier();Json baseline=Json::object();if(fs::exists(c.state/"baseline.cxs"))baseline=json(open(read_bytes(c.state/"baseline.cxs"),c.key.value,"baseline")).at("entries");auto desired=local;Json parents=Json::array(),conflicts=Json::array();std::map<std::string,std::map<std::string,Json>> candidates;
        for(auto& [id,m]:heads){parents.push_back(id);for(auto& inherited:m.value("conflicts",Json::array())){auto name=inherited.value("name",std::string{});auto split=name.find('/');if(split!=std::string::npos&&selected_file(config,name.substr(0,split),name.substr(split+1)))conflicts.push_back(inherited);}for(auto& [name,e]:m.at("entries").items()){if(!selected_file(config,e.at("root"),e.at("path")))continue;if(baseline.contains(name)&&baseline.at(name).at("hash")==e.at("hash"))continue;auto variant=e;variant["origin_snapshot"]=id;candidates[name][e.at("hash").get<std::string>()]=variant;}}
        for(auto& [name,versions]:candidates){bool present=local.at("entries").contains(name);std::string lh=present?local.at("entries").at(name).at("hash").get<std::string>():"";std::string bh=baseline.contains(name)?baseline.at(name).at("hash").get<std::string>():"";versions.erase(lh);if(versions.empty())continue;bool local_changed=lh!=bh;
            if(!local_changed&&versions.size()==1){auto e=versions.begin()->second;e.erase("origin_snapshot");desired["entries"][name]=e;}else for(auto& [hash,e]:versions)conflicts.push_back({{"name",name},{"snapshot",e.at("origin_snapshot")},{"remote_hash",hash},{"local_hash",lh},{"reason","concurrent file-level changes; both versions retained in encrypted history"}});
        }
        desired["conflicts"]=conflicts;
        if(request.value("dry_run",false))return complete(Json{{"local_entries",local["entries"].size()},{"remote_heads",heads.size()},{"conflicts",conflicts},{"dry_run",true}});
        auto applied=apply(c,desired,local,request);auto merged=c.scan();merged["conflicts"]=conflicts;auto saved=c.publish(merged,parents);write_atomic(c.state/"baseline.cxs",seal(bytes(saved),c.key.value,"baseline"));applied["snapshot"]=saved["snapshot"];applied["conflicts"]=conflicts;applied["uploaded_objects"]=c.uploaded;applied["skipped"]=merged["skipped"];return complete(std::move(applied));
    }
    throw std::runtime_error("unknown operation");
    }catch(const JobInterrupted&){throw;}
    catch(const HttpFailure& failure){c.fail_progress("http_transport",failure.retryable);throw;}
    catch(const std::exception& failure){auto message=std::string(failure.what());auto code=message.find("authentication")!=std::string::npos?"authentication_failed":message.find("different selection")!=std::string::npos?"job_identity_mismatch":message.find("cache")!=std::string::npos||message.find("cannot open file")!=std::string::npos?"local_cache_unavailable":message.find("HTTP")!=std::string::npos?"provider_http":message.find("source")!=std::string::npos?"capture_failed":"operation_failed";const bool retry=message.find("HTTP 429")!=std::string::npos||message.find("HTTP 50")!=std::string::npos;c.fail_progress(code,retry);throw;}
}
Json execute(Json request){
    auto op=request.value("op",std::string{});if(op=="progress")return execute_impl(std::move(request));
    fs::path state=application_data_directory()/"state";
    try{auto data=read_bytes(path(request.value("config",utf8(default_configuration_path()))),1024*1024);auto config=Json::parse(data.begin(),data.end());normalize_application_config(config);state=path(config.at("state"));}catch(...){}
    auto start=std::chrono::steady_clock::now();
    Json details{{"operation",op.size()<40?op:"unknown"}};runtime_log(state,"operation_started",details);
    try{auto result=execute_impl(request);details["elapsed_ms"]=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();for(auto field:{"job_id","snapshot","files","bytes","uploaded_objects","metadata_checked","body_files","body_bytes","reused_files","resumed_objects","no_changes"})if(result.contains(field))details[field]=result.at(field);if(result.contains("skipped")){details["skipped_count"]=result.at("skipped").size();details["skipped"]=result.at("skipped");}runtime_log(state,"operation_completed",details);return result;}
    catch(const std::exception& e){auto message=std::string(e.what());if(message.find("[json.exception")!=std::string::npos)message="JSON validation failed; sensitive configuration contents omitted";for(auto name:{"CXS_KEY_FILE","CXS_KEY_PASSWORD","CXS_DAV_PASSWORD","CXS_API_TOKEN","CXS_GOOGLE_ACCESS_TOKEN","CXS_GOOGLE_REFRESH_TOKEN","CXS_GOOGLE_CLIENT_SECRET"}){auto secret=env(name);if(std::string(name)=="CXS_KEY_FILE"||secret.empty())continue;size_t at=0;while((at=message.find(secret,at))!=std::string::npos){message.replace(at,secret.size(),"[redacted]");at+=10;}}for(auto field:{"key_hex","key_password","password","access_token","refresh_token","client_secret","api_token"})if(request.contains(field)&&request[field].is_string()){auto secret=request[field].get<std::string>();if(!secret.empty()){size_t at=0;while((at=message.find(secret,at))!=std::string::npos){message.replace(at,secret.size(),"[redacted]");at+=10;}}}details["error"]=message;details["elapsed_ms"]=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();runtime_log(state,"operation_failed",details);throw;}
}
}
extern "C" {
int cxs_run(const char* request,char** result){if(!result)return 1;*result=nullptr;int code=0;cxs::Json value;try{if(!request)throw std::runtime_error("null request");value={{"ok",true},{"result",cxs::execute(cxs::Json::parse(request))}};}catch(const std::exception& e){code=1;value={{"ok",false},{"error",e.what()}};}catch(...){code=1;value={{"ok",false},{"error","unexpected internal error"}};}auto s=value.dump();*result=static_cast<char*>(std::malloc(s.size()+1));if(!*result)return 2;std::memcpy(*result,s.c_str(),s.size()+1);return code;}
void cxs_free(char* p){if(p){sodium_memzero(p,std::strlen(p));std::free(p);}}
const char* cxs_version(){return "0.2.0";}
}

