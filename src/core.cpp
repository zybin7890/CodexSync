#include "core.hpp"
#include "codex_sync.h"
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
constexpr size_t chunk_size=4*1024*1024;
Bytes bytes(const Json& j){auto s=j.dump();return Bytes(s.begin(),s.end());}
Json json(const Bytes& b){return Json::parse(b.begin(),b.end());}
bool id_ok(const std::string& s,size_t length=64){return s.size()==length&&std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
void require_id(const std::string& s,size_t n=64){if(!id_ok(s,n))throw std::runtime_error("invalid snapshot, object or device ID");}
bool inside(const fs::path& a,const fs::path& b){auto x=fs::weakly_canonical(a),y=fs::weakly_canonical(b);auto i=x.begin(),j=y.begin();for(;j!=y.end();++i,++j){if(i==x.end())return false;
#ifdef _WIN32
    auto left=i->wstring(),right=j->wstring();if(_wcsicmp(left.c_str(),right.c_str()))return false;
#else
    if(*i!=*j)return false;
#endif
}return true;}
bool link(const fs::path& p){
#ifdef _WIN32
    auto a=GetFileAttributesW(p.c_str());return a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_REPARSE_POINT);
#else
    return fs::is_symlink(fs::symlink_status(p));
#endif
}
fs::path safe_target(const fs::path& root,const std::string& relative){
    if(relative.empty()||relative.find('\\')!=std::string::npos||relative.find(':')!=std::string::npos||relative.find('\0')!=std::string::npos)throw std::runtime_error("unsafe manifest path");
    auto p=path(relative);if(p.is_absolute()||p.has_root_name())throw std::runtime_error("absolute manifest path rejected");
    for(auto& part:p){auto s=utf8(part);if(s=="."||s==".."||s.empty()||s.ends_with('.')||s.ends_with(' '))throw std::runtime_error("unsafe manifest component");
#ifdef _WIN32
        auto base=s.substr(0,s.find('.'));std::transform(base.begin(),base.end(),base.begin(),[](unsigned char c){return std::toupper(c);});if(base=="CON"||base=="PRN"||base=="AUX"||base=="NUL"||(base.size()==4&&(base.starts_with("COM")||base.starts_with("LPT"))&&base[3]>='1'&&base[3]<='9'))throw std::runtime_error("reserved Windows filename");
#endif
    }
    if(link(root))throw std::runtime_error("root is a symlink or junction");auto result=root/p,current=root;for(auto& part:p){current/=part;if(fs::exists(current)&&link(current))throw std::runtime_error("symlink or junction in target path");}if(!inside(result,root))throw std::runtime_error("manifest path escapes root");return result;
}
class StateLock {
#ifdef _WIN32
    HANDLE handle=INVALID_HANDLE_VALUE;
#else
    int fd=-1;
#endif
public:
    explicit StateLock(const fs::path& state){private_directory(state);
#ifdef _WIN32
        handle=CreateFileW((state/"operation.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("another operation owns this state directory");
#else
        fd=::open((state/"operation.lock").c_str(),O_CREAT|O_RDWR,0600);if(fd<0||flock(fd,LOCK_EX|LOCK_NB)!=0){if(fd>=0)::close(fd);fd=-1;throw std::runtime_error("another operation owns this state directory");}
#endif
    }
    ~StateLock(){
#ifdef _WIN32
        if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);
#else
        if(fd>=0)::close(fd);
#endif
    }
};
class SecretKey {
public: Key value{};
    explicit SecretKey(const Json& req){auto s=req.value("key_hex",std::string{});if(s.empty()){auto filename=env("CXS_KEY_FILE");if(filename.empty())throw std::runtime_error("set CXS_KEY_FILE or select the encryption key in GUI");auto b=read_bytes(path(filename),256);s.assign(b.begin(),b.end());sodium_memzero(b.data(),b.size());}while(!s.empty()&&(s.back()=='\n'||s.back()=='\r'))s.pop_back();if(s.size()!=64||sodium_hex2bin(value.data(),32,s.c_str(),s.size(),nullptr,nullptr,nullptr)!=0)throw std::runtime_error("key must be a 64-character hex master key");sodium_memzero(s.data(),s.size());}
    ~SecretKey(){sodium_memzero(value.data(),value.size());}
};
bool volatile_file(const std::string& p){auto name=utf8(path(p).filename());return name=="SingletonLock"||name=="SingletonSocket"||name=="SingletonCookie"||name=="LOCK"||name.ends_with(".lock")||name.ends_with("-wal")||name.ends_with("-shm");}
bool database(const fs::path& p){std::ifstream in(p,std::ios::binary);char h[16]{};in.read(h,16);return std::memcmp(h,"SQLite format 3\0",16)==0;}
struct Scratch {fs::path p;~Scratch(){std::error_code e;fs::remove(p,e);}};
void sqlite_backup(const fs::path& source,const fs::path& output){
    sqlite3* sraw=nullptr;sqlite3* draw=nullptr;auto s=std::unique_ptr<sqlite3,decltype(&sqlite3_close)>(nullptr,sqlite3_close),d=std::unique_ptr<sqlite3,decltype(&sqlite3_close)>(nullptr,sqlite3_close);
    int rc=sqlite3_open_v2(utf8(source).c_str(),&sraw,SQLITE_OPEN_READONLY,nullptr);s.reset(sraw);if(rc!=SQLITE_OK)throw std::runtime_error("cannot open SQLite source: "+utf8(source));
    rc=sqlite3_open_v2(utf8(output).c_str(),&draw,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,nullptr);d.reset(draw);if(rc!=SQLITE_OK)throw std::runtime_error("cannot create SQLite snapshot");auto backup=sqlite3_backup_init(d.get(),"main",s.get(),"main");if(!backup)throw std::runtime_error("cannot initialize SQLite snapshot");auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    do{rc=sqlite3_backup_step(backup,256);if(rc==SQLITE_BUSY||rc==SQLITE_LOCKED)std::this_thread::sleep_for(std::chrono::milliseconds(50));}while((rc==SQLITE_OK||rc==SQLITE_BUSY||rc==SQLITE_LOCKED)&&std::chrono::steady_clock::now()<deadline);
    int end=sqlite3_backup_finish(backup);if(rc!=SQLITE_DONE||end!=SQLITE_OK)throw std::runtime_error("SQLite snapshot timed out or failed");
}
struct Context {
    Json config;fs::path state;SecretKey key;Store store;StateLock lock;size_t uploaded=0;
    Context(Json c,const Json& r):config(std::move(c)),state(path(config.at("state").get<std::string>())),key(r),store(config,r),lock(state){private_directory(state/"cache");private_directory(state/"scratch");}
    fs::path cache(const std::string& id){require_id(id);return state/"cache"/(id+".cxs");}
    std::string object(const Bytes& plain){auto id=digest(plain,key.value);auto p=cache(id);if(!fs::exists(p))write_atomic(p,seal(plain,key.value,"object:"+id));return id;}
    Bytes fetch(const std::string& id){require_id(id);auto p=cache(id);auto encrypted=fs::exists(p)?read_bytes(p,chunk_size+128):store.get("objects/"+id+".cxs");auto plain=open(encrypted,key.value,"object:"+id);if(digest(plain,key.value)!=id)throw std::runtime_error("object digest mismatch");if(!fs::exists(p))write_atomic(p,encrypted);return plain;}
    Json entry(const fs::path& p,const std::string& root,const std::string& rel){
        Json e={{"root",root},{"path",rel},{"kind","file"},{"chunks",Json::array()},{"bytes",0},{"mode",static_cast<unsigned>(fs::status(p).permissions())&0777}};
        Scratch scratch{state/"scratch"/(random_id()+".sqlite")};fs::path input=p;bool sql=database(p);auto before_time=fs::last_write_time(p);auto before_size=fs::file_size(p);
        if(sql){sqlite_backup(p,scratch.p);input=scratch.p;e["sqlite"]=true;}
        std::ifstream in(input,std::ios::binary);if(!in)throw std::runtime_error("unreadable source: "+utf8(p));crypto_generichash_state h;crypto_generichash_init(&h,key.value.data(),key.value.size(),32);Bytes b(chunk_size);uint64_t size=0;
        while(in){in.read(reinterpret_cast<char*>(b.data()),chunk_size);auto n=in.gcount();if(n<=0)break;b.resize(static_cast<size_t>(n));crypto_generichash_update(&h,b.data(),b.size());e["chunks"].push_back(object(b));size+=b.size();b.resize(chunk_size);}if(in.bad())throw std::runtime_error("source read failed");
        unsigned char hash[32];crypto_generichash_final(&h,hash,32);char encoded[65];sodium_bin2hex(encoded,65,hash,32);e["hash"]=std::string(encoded);e["bytes"]=size;
        if(!sql&&(fs::last_write_time(p)!=before_time||fs::file_size(p)!=before_size))throw std::runtime_error("source changed during snapshot; retry when Codex is idle: "+utf8(p));return e;
    }
    Json scan(){Json entries=Json::object(),skipped=Json::array();uint64_t size=0;for(auto& r:config.at("roots")){auto root=r.at("id").get<std::string>();require_id_root(root);auto directory=path(r.at("path").get<std::string>());if(!fs::is_directory(directory))throw std::runtime_error("missing data root: "+utf8(directory));if(link(directory))throw std::runtime_error("data root is a symlink or junction");
        for(fs::recursive_directory_iterator it(directory),end;it!=end;++it){auto p=it->path();auto rel=utf8(p.lexically_relative(directory));if(inside(p,state)){if(it->is_directory())it.disable_recursion_pending();continue;}
            if(link(p)){skipped.push_back({{"root",root},{"path",rel},{"reason","symlink/junction, add its target as an explicit root"}});if(it->is_directory())it.disable_recursion_pending();continue;}
            if(excluded_file(config,rel)){skipped.push_back({{"root",root},{"path",rel},{"reason","explicit exclusion"}});if(it->is_directory())it.disable_recursion_pending();continue;}
            if(it->is_regular_file()){if(volatile_file(rel)){if(rel.ends_with("-wal")||rel.ends_with("-shm")){auto base=rel.substr(0,rel.size()-4);if(!fs::exists(directory/path(base))||!database(directory/path(base)))throw std::runtime_error("orphan or non-SQLite sidecar: "+rel);}else skipped.push_back({{"root",root},{"path",rel},{"reason","volatile process lock"}});continue;}
                auto e=entry(p,root,rel);size+=e["bytes"].get<uint64_t>();entries[root+"/"+rel]=e;
            }else if(it->is_directory())entries[root+"/"+rel]={{"root",root},{"path",rel},{"kind","directory"},{"hash","directory"}};
            else throw std::runtime_error("unsupported special file: "+rel);
        }}return {{"format",1},{"entries",entries},{"skipped",skipped},{"bytes",size},{"conflicts",Json::array()},{"parents",Json::array()}};
    }
    static void require_id_root(const std::string& s){if(s.empty()||s.size()>64||!std::all_of(s.begin(),s.end(),[](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-';}))throw std::runtime_error("root id must use lowercase letters, digits, underscore or hyphen");}
    Json snapshot(const std::string& id){require_id(id);auto m=json(open(store.get("snapshots/"+id+".cxs"),key.value,"snapshot:"+id));if(m.at("format")!=1)throw std::runtime_error("unsupported repository format");return m;}
    Json publish(Json m,const Json& parents){store.ensure();auto device=config.at("device").get<std::string>();require_id(device,32);auto head=store.request("GET","heads/"+device+".cxs");if(head.status!=200&&head.status!=404)throw std::runtime_error("cannot read device head");if(head.status==200&&head.etag.empty())throw std::runtime_error("server must supply ETag for device-head compare-and-swap");
        for(auto& e:m.at("entries")){if(e.value("kind","")=="file")for(auto& x:e.at("chunks")){auto id=x.get<std::string>();if(store.put_immutable("objects/"+id+".cxs",read_bytes(cache(id),chunk_size+128)))++uploaded;}}
        m["parents"]=parents;if(head.status==200){auto previous=json(open(head.body,key.value,"head:"+device)).at("snapshot");if(std::find(m["parents"].begin(),m["parents"].end(),previous)==m["parents"].end())m["parents"].push_back(previous);}
        m["device"]=device;m["created_unix"]=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();auto id=random_id()+random_id();m["snapshot"]=id;store.put_immutable("snapshots/"+id+".cxs",seal(bytes(m),key.value,"snapshot:"+id));auto ref=seal(bytes(Json{{"snapshot",id}}),key.value,"head:"+device);auto r=store.request("PUT","heads/"+device+".cxs",ref,head.status==404?"*":head.etag);if(r.status==412)throw std::runtime_error("device head changed concurrently, retry with a fresh scan");if(r.status!=200&&r.status!=201&&r.status!=204)throw std::runtime_error("device head commit failed: HTTP "+std::to_string(r.status));return m;
    }
    std::map<std::string,Json> frontier(){std::map<std::string,Json> heads;for(auto file:store.list("heads")){if(!file.ends_with(".cxs"))continue;auto device=file.substr(0,file.size()-4);require_id(device,32);auto h=json(open(store.get("heads/"+file),key.value,"head:"+device));auto id=h.at("snapshot").get<std::string>();heads[id]=snapshot(id);}
        std::set<std::string> ancestors,visited;std::function<void(const Json&,int)> visit=[&](const Json& m,int depth){if(depth>256)throw std::runtime_error("history exceeds traversal depth limit");for(auto& p:m.at("parents")){auto id=p.get<std::string>();require_id(id);ancestors.insert(id);if(visited.insert(id).second){if(visited.size()>4096)throw std::runtime_error("history exceeds traversal limit");visit(heads.contains(id)?heads.at(id):snapshot(id),depth+1);}}};for(auto& [id,m]:heads)visit(m,0);for(auto& id:ancestors)heads.erase(id);return heads;
    }
    fs::path root_path(const std::string& id){for(auto& r:config.at("roots"))if(r.at("id")==id)return path(r.at("path").get<std::string>());throw std::runtime_error("snapshot root is not mapped in config: "+id);}
    void materialize(const Json& e,const fs::path& dest){if(e.at("kind")=="directory"){fs::create_directories(dest);return;}if(e.at("kind")!="file")throw std::runtime_error("unsupported manifest entry");fs::create_directories(dest.parent_path());std::ofstream out(dest,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("cannot stage restored file");crypto_generichash_state h;crypto_generichash_init(&h,key.value.data(),32,32);uint64_t size=0;for(auto& x:e.at("chunks")){auto b=fetch(x.get<std::string>());if(b.size()>chunk_size)throw std::runtime_error("invalid chunk size");out.write(reinterpret_cast<const char*>(b.data()),b.size());crypto_generichash_update(&h,b.data(),b.size());size+=b.size();}out.flush();if(!out)throw std::runtime_error("cannot flush staged file");unsigned char hash[32];crypto_generichash_final(&h,hash,32);char text[65];sodium_bin2hex(text,65,hash,32);if(size!=e.at("bytes").get<uint64_t>()||e.at("hash")!=text)throw std::runtime_error("restored file digest mismatch");
#ifndef _WIN32
        fs::permissions(dest,static_cast<fs::perms>(e.value("mode",0600u)&0777));
#endif
    }
};
bool live_codex(){
#ifdef _WIN32
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)throw std::runtime_error("cannot check active Codex processes");PROCESSENTRY32W e{};e.dwSize=sizeof e;bool active=false;if(Process32FirstW(snapshot,&e))do{if(!_wcsicmp(e.szExeFile,L"Codex.exe")||!_wcsicmp(e.szExeFile,L"codex-cli.exe")){active=true;break;}}while(Process32NextW(snapshot,&e));CloseHandle(snapshot);return active;
#else
    for(auto& e:fs::directory_iterator("/proc")){auto name=e.path().filename().string();if(name.empty()||!std::isdigit(name[0]))continue;std::ifstream in(e.path()/"comm");std::string comm;std::getline(in,comm);std::transform(comm.begin(),comm.end(),comm.begin(),[](unsigned char c){return std::tolower(c);});if(comm=="codex"||comm=="codex-cli")return true;}return false;
#endif
}
void offline(const Json& c,const Json& request){if(!request.value("offline",false))throw std::runtime_error("applying changes requires --offline confirmation; close Codex first");auto real=discover();bool actual=false;for(auto& r:c.at("roots"))for(auto& d:real.at("roots"))if(inside(path(r.at("path")),path(d.at("path")))||inside(path(d.at("path")),path(r.at("path"))))actual=true;if(actual&&live_codex())throw std::runtime_error("Codex is running; use backup or staged restore instead of applying changes");}
Json apply(Context& c,const Json& desired,const Json& current,const Json& request){
    offline(c.config,request);if(fs::exists(c.state/"pending.cxs"))throw std::runtime_error("an interrupted apply journal exists; inspect rollback before continuing");
    auto transaction=random_id();auto stage=c.state/"scratch"/transaction;private_directory(stage);Json changed=Json::array();
    for(auto& [name,e]:desired.at("entries").items()){auto target=safe_target(c.root_path(e.at("root")),e.at("path"));if(current.at("entries").contains(name)&&current.at("entries").at(name).at("hash")==e.at("hash"))continue;auto staged=safe_target(stage/path(e.at("root")),e.at("path"));c.materialize(e,staged);changed.push_back({{"name",name},{"target",utf8(target)},{"staged",utf8(staged)}});}
    // Encrypted, complete rollback snapshot is durable before the first replacement.
    private_directory(c.state/"rollbacks");auto rollback_id=random_id()+random_id();write_atomic(c.state/"rollbacks"/(rollback_id+".cxs"),seal(bytes(current),c.key.value,"rollback:"+rollback_id));Json journal={{"rollback",rollback_id},{"changes",changed},{"applied",Json::array()}};write_atomic(c.state/"pending.cxs",seal(bytes(journal),c.key.value,"journal"));
    for(auto& x:changed){auto name=x.at("name").get<std::string>();auto& e=desired.at("entries").at(name);auto target=safe_target(c.root_path(e.at("root")),e.at("path"));auto staged=path(x.at("staged"));
        if(e.at("kind")=="directory"){fs::create_directories(target);}else{if(fs::exists(target)&&fs::is_directory(target))throw std::runtime_error("file/directory conflict; rollback journal retained");
            if(fs::exists(target)){auto fresh=c.entry(target,e.at("root"),e.at("path"));if(!current.at("entries").contains(name)||fresh.at("hash")!=current.at("entries").at(name).at("hash"))throw std::runtime_error("destination changed after preflight; rollback journal retained");}
            fs::create_directories(target.parent_path());if(e.value("sqlite",false)){private_directory(stage/"sidecars");for(auto suffix:{"-wal","-shm"}){auto side=target;side+=suffix;if(fs::exists(side))fs::rename(side,stage/"sidecars"/(random_id()+suffix));}}
#ifdef _WIN32
            if(!MoveFileExW(staged.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_COPY_ALLOWED|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("replacement failed; encrypted rollback retained");
#else
            // Staging may be on another filesystem, copy to a private sibling first.
            auto sibling=target;sibling+=".cxs-new-"+transaction;fs::copy_file(staged,sibling);fs::rename(sibling,target);
#endif
        }journal["applied"].push_back(name);write_atomic(c.state/"pending.cxs",seal(bytes(journal),c.key.value,"journal"));}
    // Archive completed journal; never delete user files or propagate deletions.
    fs::rename(c.state/"pending.cxs",c.state/"rollbacks"/(rollback_id+".journal.cxs"));return {{"applied",changed.size()},{"rollback",rollback_id},{"deletions",0}};
}
Json load_config(const fs::path& filename){auto c=json(read_bytes(filename,1024*1024));resolve_history_roots(c);if(c.at("format")!=1)throw std::runtime_error("unsupported config format");std::set<std::string> ids;std::vector<fs::path> roots;auto state=path(c.at("state"));if(!state.is_absolute())throw std::runtime_error("state path must be absolute");for(auto& r:c.at("roots")){auto id=r.at("id").get<std::string>();Context::require_id_root(id);if(!ids.insert(id).second)throw std::runtime_error("duplicate root id");auto p=path(r.at("path"));if(!p.is_absolute())throw std::runtime_error("root paths must be absolute");if(inside(state,p)||inside(p,state))throw std::runtime_error("state and data roots must not overlap");for(auto& other:roots)if(inside(p,other)||inside(other,p))throw std::runtime_error("data roots must not overlap");roots.push_back(p);}if(roots.empty())throw std::runtime_error("configure at least one data root");if(c.at("remote").contains("directory")){auto remote=path(c.at("remote").at("directory"));if(!remote.is_absolute())throw std::runtime_error("remote directory must be absolute");for(auto& p:roots)if(inside(remote,p)||inside(p,remote))throw std::runtime_error("remote repository and data roots must not overlap");}return c;}
}
Json discover(){auto home=env("CODEX_HOME");
#ifdef _WIN32
    auto user=env("USERPROFILE");auto state=path(env("LOCALAPPDATA"))/"CodexSyncNative";
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
Json execute(Json request){if(sodium_init()<0)throw std::runtime_error("crypto initialization failed");auto op=request.at("op").get<std::string>();if(op=="discover")return discover();auto config=load_config(path(request.at("config")));if(op=="conversations")return conversation_inventory(config);if(op=="scan"){
        // Read file metadata and thread-index references only, not messages or credentials.
        Json result={{"roots",Json::array()},{"files",0},{"bytes",0},{"skipped_links",Json::array()}};uint64_t count=0,size=0;for(auto& r:config.at("roots")){auto p=path(r.at("path"));if(!fs::is_directory(p)||link(p))throw std::runtime_error("missing or linked data root");uint64_t n=0,b=0;for(fs::recursive_directory_iterator i(p),end;i!=end;++i){if(link(i->path())){result["skipped_links"].push_back(utf8(i->path()));if(i->is_directory())i.disable_recursion_pending();continue;}auto relative=utf8(i->path().lexically_relative(p));if(excluded_file(config,relative)){if(i->is_directory())i.disable_recursion_pending();continue;}if(i->is_regular_file()&&!volatile_file(relative)){++n;b+=i->file_size();}}count+=n;size+=b;auto v=r;v["files"]=n;v["bytes"]=b;result["roots"].push_back(v);}result["files"]=count;result["bytes"]=size;result["conversation_history"]=conversation_inventory(config);return result;
    }
    Context c(config,request);
    if(op=="backup"){auto m=c.scan();m["conversation_history"]=conversation_inventory(config);auto saved=c.publish(m,Json::array());return {{"snapshot",saved["snapshot"]},{"files",m["entries"].size()},{"bytes",m["bytes"]},{"uploaded_objects",c.uploaded},{"skipped",m["skipped"]},{"conversation_history",m["conversation_history"]},{"mode","encrypted snapshot; source unchanged"}};}
    if(op=="history"){Json out=Json::array();for(auto file:c.store.list("snapshots")){if(!file.ends_with(".cxs"))continue;auto id=file.substr(0,file.size()-4);auto m=c.snapshot(id);out.push_back({{"snapshot",id},{"device",m["device"]},{"created_unix",m["created_unix"]},{"entries",m["entries"].size()},{"conflicts",m["conflicts"]}});}return {{"snapshots",out}};}
    if(op=="restore"){auto id=request.at("snapshot").get<std::string>();require_id(id);auto m=c.snapshot(id);if(request.value("apply",false)){offline(config,request);auto local=c.scan();return apply(c,m,local,request);}auto output=path(request.at("output"));if(!output.is_absolute())throw std::runtime_error("restore output must be absolute");if(fs::exists(output)&&!fs::is_empty(output))throw std::runtime_error("staged restore output must be empty");for(auto& r:config.at("roots"))if(inside(output,path(r.at("path")))||inside(path(r.at("path")),output))throw std::runtime_error("staged restore must not overlap source roots");if(inside(output,c.state)||inside(c.state,output))throw std::runtime_error("restore output and state must not overlap");private_directory(output);for(auto& e:m.at("entries")){auto root=e.at("root").get<std::string>();Context::require_id_root(root);c.materialize(e,safe_target(output/root,e.at("path")));}return {{"snapshot",id},{"output",utf8(output)},{"entries",m["entries"].size()},{"source_unchanged",true}};}
    if(op=="rollback"){auto id=request.at("snapshot").get<std::string>();require_id(id);auto m=json(open(read_bytes(c.state/"rollbacks"/(id+".cxs")),c.key.value,"rollback:"+id));auto output=path(request.at("output"));if(!output.is_absolute()||fs::exists(output))throw std::runtime_error("rollback output must be a new absolute directory");for(auto& r:config.at("roots"))if(inside(output,path(r.at("path")))||inside(path(r.at("path")),output))throw std::runtime_error("rollback output overlaps source");private_directory(output);for(auto& e:m.at("entries")){auto root=e.at("root").get<std::string>();Context::require_id_root(root);c.materialize(e,safe_target(output/root,e.at("path")));}return {{"rollback",id},{"output",utf8(output)},{"note","Source was not modified; inspect the pending encrypted journal and restore while Codex is closed."}};}
    if(op=="sync"){
        offline(config,request);auto local=c.scan();auto heads=c.frontier();Json baseline=Json::object();if(fs::exists(c.state/"baseline.cxs"))baseline=json(open(read_bytes(c.state/"baseline.cxs"),c.key.value,"baseline")).at("entries");auto desired=local;Json parents=Json::array(),conflicts=Json::array();std::map<std::string,std::map<std::string,Json>> candidates;
        for(auto& [id,m]:heads){parents.push_back(id);for(auto& inherited:m.value("conflicts",Json::array()))conflicts.push_back(inherited);for(auto& [name,e]:m.at("entries").items()){if(baseline.contains(name)&&baseline.at(name).at("hash")==e.at("hash"))continue;auto variant=e;variant["origin_snapshot"]=id;candidates[name][e.at("hash").get<std::string>()]=variant;}}
        for(auto& [name,versions]:candidates){bool present=local.at("entries").contains(name);std::string lh=present?local.at("entries").at(name).at("hash").get<std::string>():"";std::string bh=baseline.contains(name)?baseline.at(name).at("hash").get<std::string>():"";versions.erase(lh);if(versions.empty())continue;bool local_changed=lh!=bh;
            if(!local_changed&&versions.size()==1){auto e=versions.begin()->second;e.erase("origin_snapshot");desired["entries"][name]=e;}else for(auto& [hash,e]:versions)conflicts.push_back({{"name",name},{"snapshot",e.at("origin_snapshot")},{"remote_hash",hash},{"local_hash",lh},{"reason","concurrent file-level changes; both versions retained in encrypted history"}});
        }
        desired["conflicts"]=conflicts;
        if(request.value("dry_run",false))return {{"local_entries",local["entries"].size()},{"remote_heads",heads.size()},{"conflicts",conflicts},{"dry_run",true}};
        auto applied=apply(c,desired,local,request);auto merged=c.scan();merged["conflicts"]=conflicts;auto saved=c.publish(merged,parents);write_atomic(c.state/"baseline.cxs",seal(bytes(saved),c.key.value,"baseline"));applied["snapshot"]=saved["snapshot"];applied["conflicts"]=conflicts;applied["uploaded_objects"]=c.uploaded;applied["skipped"]=merged["skipped"];return applied;
    }
    throw std::runtime_error("unknown operation");
}
}
extern "C" {
int cxs_run(const char* request,char** result){if(!result)return 1;*result=nullptr;int code=0;cxs::Json value;try{if(!request)throw std::runtime_error("null request");value={{"ok",true},{"result",cxs::execute(cxs::Json::parse(request))}};}catch(const std::exception& e){code=1;value={{"ok",false},{"error",e.what()}};}catch(...){code=1;value={{"ok",false},{"error","unexpected internal error"}};}auto s=value.dump();*result=static_cast<char*>(std::malloc(s.size()+1));if(!*result)return 2;std::memcpy(*result,s.c_str(),s.size()+1);return code;}
void cxs_free(char* p){if(p){sodium_memzero(p,std::strlen(p));std::free(p);}}
const char* cxs_version(){return "0.1.0";}
}
