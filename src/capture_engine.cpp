#include "capture_engine.hpp"
#include "operation_runtime.hpp"
#include "vss.hpp"
#include <sqlite3.h>
#include <fstream>
#include <thread>
#include <climits>
#include <atomic>
namespace cxs {
namespace {
bool database(const fs::path& p){auto native=p;native.make_preferred();std::ifstream in(native,std::ios::binary);char h[16]{};in.read(h,16);return std::memcmp(h,"SQLite format 3\0",16)==0;}
struct Scratch {fs::path p;~Scratch(){std::error_code e;fs::remove(p,e);}};
struct DatabaseStage {
    fs::path directory;
    ~DatabaseStage(){if(!directory.empty()){std::error_code error;fs::remove_all(directory,error);}}
};
fs::path recover_shadow_database(const fs::path& source,DatabaseStage& stage,const fs::path& scratch){
    stage.directory=scratch/random_id();private_directory(stage.directory);
    const auto target=stage.directory/"database.sqlite";
    fs::copy_file(source,target);
    // All sidecars come from the same immutable volume snapshot. SHM is rebuilt.
    for(const auto* suffix:{"-wal","-journal"}){auto from=source;from+=suffix;if(fs::exists(from)){
        if(std::strcmp(suffix,"-journal")==0&&fs::file_size(from)>=16){
            std::ifstream file(from,std::ios::binary);unsigned char trailer[16]{};file.seekg(-16,std::ios::end);file.read(reinterpret_cast<char*>(trailer),16);
            const unsigned char magic[]={0xd9,0xd5,0x05,0xf9,0x20,0xa1,0x63,0xd7};
            if(!file||(!std::memcmp(trailer+8,magic,8)&&(trailer[0]||trailer[1]||trailer[2]||trailer[3])))
                throw std::runtime_error("multi-database super-journal requires coordinated recovery; source left unchanged: "+utf8(source));
        }
        auto to=target;to+=suffix;fs::copy_file(from,to);
    }}
    sqlite3* raw=nullptr;int rc=sqlite3_open_v2(utf8(target).c_str(),&raw,SQLITE_OPEN_READWRITE,nullptr);
    auto db=std::unique_ptr<sqlite3,decltype(&sqlite3_close)>(raw,sqlite3_close);
    if(rc!=SQLITE_OK)throw std::runtime_error("cannot open staged VSS database: "+utf8(source)+"; SQLite "+std::to_string(rc));
    sqlite3_busy_timeout(db.get(),2000);
    // A writable private stage performs hot-journal recovery and commits WAL pages.
    sqlite3_stmt* statement=nullptr;rc=sqlite3_prepare_v2(db.get(),"PRAGMA wal_checkpoint(TRUNCATE)",-1,&statement,nullptr);
    auto checkpoint=std::unique_ptr<sqlite3_stmt,decltype(&sqlite3_finalize)>(statement,sqlite3_finalize);
    if(rc!=SQLITE_OK||sqlite3_step(checkpoint.get())!=SQLITE_ROW||sqlite3_column_int(checkpoint.get(),0)!=0||sqlite3_step(checkpoint.get())!=SQLITE_DONE)
        throw std::runtime_error("staged VSS database checkpoint incomplete: "+utf8(source));
    checkpoint.reset();statement=nullptr;rc=sqlite3_prepare_v2(db.get(),"PRAGMA journal_mode=DELETE",-1,&statement,nullptr);
    auto mode=std::unique_ptr<sqlite3_stmt,decltype(&sqlite3_finalize)>(statement,sqlite3_finalize);
    if(rc!=SQLITE_OK||sqlite3_step(mode.get())!=SQLITE_ROW||std::strcmp(reinterpret_cast<const char*>(sqlite3_column_text(mode.get(),0)),"delete")!=0||sqlite3_step(mode.get())!=SQLITE_DONE)
        throw std::runtime_error("staged VSS database journal mode did not switch to DELETE: "+utf8(source));
    mode.reset();
    auto journal=target;journal+="-journal";
    if(fs::exists(journal)&&fs::file_size(journal)){
        // Switching modes alone does not remove an old PERSIST journal. Let
        // SQLite retire it with a one-page, same-value transaction in this
        // private stage only; never delete or modify a source sidecar.
        statement=nullptr;rc=sqlite3_prepare_v2(db.get(),"PRAGMA user_version",-1,&statement,nullptr);
        auto version=std::unique_ptr<sqlite3_stmt,decltype(&sqlite3_finalize)>(statement,sqlite3_finalize);
        if(rc!=SQLITE_OK||sqlite3_step(version.get())!=SQLITE_ROW)throw std::runtime_error("cannot read staged database metadata: "+utf8(source));
        const int value=sqlite3_column_int(version.get(),0);
        if(sqlite3_step(version.get())!=SQLITE_DONE)throw std::runtime_error("incomplete staged database metadata: "+utf8(source));
        version.reset();
        const auto transaction="BEGIN IMMEDIATE; PRAGMA user_version="+std::to_string(value)+"; COMMIT;";
        rc=sqlite3_exec(db.get(),transaction.c_str(),nullptr,nullptr,nullptr);
        if(rc!=SQLITE_OK)throw std::runtime_error("staged database journal cleanup failed: "+utf8(source)+"; SQLite "+std::to_string(rc));
    }
    statement=nullptr;rc=sqlite3_prepare_v2(db.get(),"PRAGMA quick_check",-1,&statement,nullptr);
    auto check=std::unique_ptr<sqlite3_stmt,decltype(&sqlite3_finalize)>(statement,sqlite3_finalize);
    if(rc!=SQLITE_OK||sqlite3_step(check.get())!=SQLITE_ROW||std::strcmp(reinterpret_cast<const char*>(sqlite3_column_text(check.get(),0)),"ok")!=0||sqlite3_step(check.get())!=SQLITE_DONE)
        throw std::runtime_error("VSS database integrity check failed: "+utf8(source));
    check.reset();raw=db.release();rc=sqlite3_close(raw);if(rc!=SQLITE_OK){db.reset(raw);throw std::runtime_error("staged VSS database connection did not close: "+utf8(source));}
    for(const auto* suffix:{"-wal","-journal","-shm"}){auto side=target;side+=suffix;if(fs::exists(side)&&fs::file_size(side))throw std::runtime_error("staged VSS database did not become self-contained ("+std::string(suffix)+"): "+utf8(source));}
    return target;
}
void sqlite_backup(const fs::path& source,const fs::path& output){
    sqlite3* sraw=nullptr;sqlite3* draw=nullptr;auto s=std::unique_ptr<sqlite3,decltype(&sqlite3_close)>(nullptr,sqlite3_close),d=std::unique_ptr<sqlite3,decltype(&sqlite3_close)>(nullptr,sqlite3_close);
    int rc=sqlite3_open_v2(utf8(source).c_str(),&sraw,SQLITE_OPEN_READONLY,nullptr);s.reset(sraw);if(rc!=SQLITE_OK)throw std::runtime_error("cannot open SQLite source: "+utf8(source));
    sqlite3_busy_timeout(s.get(),2000);
    // Pin a read snapshot so concurrent WAL writers do not restart every chunk.
    rc=sqlite3_exec(s.get(),"BEGIN; SELECT rootpage FROM sqlite_schema LIMIT 1;",nullptr,nullptr,nullptr);
    if(rc!=SQLITE_OK)throw std::runtime_error("cannot start SQLite read snapshot: "+utf8(source)+"; SQLite "+std::to_string(rc)+" ("+sqlite3_errstr(rc)+")");
    rc=sqlite3_open_v2(utf8(output).c_str(),&draw,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,nullptr);d.reset(draw);if(rc!=SQLITE_OK)throw std::runtime_error("cannot create SQLite snapshot: "+utf8(source));
    auto backup=sqlite3_backup_init(d.get(),"main",s.get(),"main");if(!backup)throw std::runtime_error("cannot initialize SQLite snapshot: "+utf8(source)+"; SQLite "+std::to_string(sqlite3_errcode(d.get())));
    auto start=std::chrono::steady_clock::now(),last_progress=start;int previous_remaining=INT_MAX,remaining=0,total=0;
    do{rc=sqlite3_backup_step(backup,256);remaining=sqlite3_backup_remaining(backup);total=sqlite3_backup_pagecount(backup);auto now=std::chrono::steady_clock::now();if(rc==SQLITE_OK&&remaining<previous_remaining)last_progress=now;previous_remaining=remaining;if(rc==SQLITE_BUSY||rc==SQLITE_LOCKED)std::this_thread::sleep_for(std::chrono::milliseconds(50));if(now-last_progress>std::chrono::seconds(15)||now-start>std::chrono::minutes(5))break;}while(rc==SQLITE_OK||rc==SQLITE_BUSY||rc==SQLITE_LOCKED);
    int end=sqlite3_backup_finish(backup);sqlite3_exec(s.get(),"ROLLBACK",nullptr,nullptr,nullptr);
    if(rc!=SQLITE_DONE||end!=SQLITE_OK)throw std::runtime_error("SQLite snapshot failed: "+utf8(source)+"; SQLite "+std::to_string(rc==SQLITE_DONE?end:rc)+" ("+sqlite3_errstr(rc==SQLITE_DONE?end:rc)+"); remaining pages "+std::to_string(remaining)+"/"+std::to_string(total));
}

}
Json CaptureEngine::entry(const fs::path& p,const std::string& root,const std::string& rel,bool scanning,bool shadow){
    auto& state=runtime_.state;
    auto& key=runtime_.key;

        Json e={{"root",root},{"path",rel},{"kind","file"},{"chunks",Json::array()},{"bytes",0},{"mode",static_cast<unsigned>(fs::status(p).permissions())&0777}};
        Scratch scratch{state/"scratch"/(random_id()+".sqlite")};DatabaseStage stage;fs::path input=p;bool sql=runtime_.config.value("payload_mode",std::string("encrypted"))=="encrypted"&&database(p);auto before_time=fs::last_write_time(p);auto before_size=fs::file_size(p);
        if(sql){if(shadow)input=recover_shadow_database(p,stage,state/"scratch");else{sqlite_backup(p,scratch.p);input=scratch.p;}e["sqlite"]=true;}
        std::ifstream in(input,std::ios::binary);if(!in)throw std::runtime_error("unreadable source: "+utf8(p));crypto_generichash_state h;crypto_generichash_init(&h,key.value.data(),key.value.size(),32);Bytes b(chunk_size);uint64_t size=0;
        while(in){checkpoint();in.read(reinterpret_cast<char*>(b.data()),chunk_size);auto n=in.gcount();if(n<=0)break;b.resize(static_cast<size_t>(n));crypto_generichash_update(&h,b.data(),b.size());e["chunks"].push_back(object(b));size+=b.size();if(scanning)advance_progress(0,0,b.size());b.resize(chunk_size);}if(in.bad())throw std::runtime_error("source read failed");
        unsigned char hash[32];crypto_generichash_final(&h,hash,32);char encoded[65];sodium_bin2hex(encoded,65,hash,32);e["hash"]=std::string(encoded);e["bytes"]=size;
        if(!sql&&(fs::last_write_time(p)!=before_time||fs::file_size(p)!=before_size))throw std::runtime_error("source changed during snapshot; retry when Codex is idle: "+utf8(p));return e;
    }
Json CaptureEngine::scan(){
    auto& config=runtime_.config;
    auto& state=runtime_.state;
Json entries=Json::object(),skipped=Json::array();uint64_t size=0;std::unique_ptr<VssSession> vss;
        if(config.value("windows_vss",false)){begin_progress("snapshotting",0,0,0);vss=std::make_unique<VssSession>(config,state);}
        begin_progress("scanning",0,0,0);for(auto& r:config.at("roots")){if(!r.value("enabled",true))continue;auto root=r.at("id").get<std::string>();OperationRuntime::require_id_root(root);auto original=path(r.at("path").get<std::string>());auto directory=vss?vss->source(original):original;if(!fs::is_directory(directory))throw std::runtime_error("missing data root: "+utf8(directory));if(link(directory))throw std::runtime_error("data root is a symlink or junction");const bool contains_state=inside(state,original);
        for(fs::recursive_directory_iterator it(directory),end;it!=end;++it){auto p=it->path();auto rel=utf8(p.lexically_relative(directory));if(contains_state&&inside(original/path(rel),state)){if(it->is_directory())it.disable_recursion_pending();continue;}
            if(link(p)){skipped.push_back({{"root",root},{"path",rel},{"reason","symlink/junction, add its target as an explicit root"}});if(it->is_directory())it.disable_recursion_pending();continue;}
            if(excluded_file(config,rel,root)){skipped.push_back({{"root",root},{"path",rel},{"reason","explicit exclusion"}});if(it->is_directory())it.disable_recursion_pending();continue;}
            if(it->is_regular_file()){if(!selected_file(config,root,rel))continue;const bool journal=rel.ends_with("-journal")&&fs::exists(directory/path(rel.substr(0,rel.size()-8)))&&database(directory/path(rel.substr(0,rel.size()-8)));if(volatile_file(rel)||journal){if(rel.ends_with("-wal")||rel.ends_with("-shm")){auto base=rel.substr(0,rel.size()-4);if(!fs::exists(directory/path(base))||!database(directory/path(base)))throw std::runtime_error("orphan or non-SQLite sidecar: "+rel);}else if(!journal)skipped.push_back({{"root",root},{"path",rel},{"reason","volatile process lock"}});continue;}
                auto e=entry(p,root,rel,true,vss!=nullptr);size+=e["bytes"].get<uint64_t>();entries[root+"/"+rel]=e;advance_progress(1,1,0);
            }else if(it->is_directory())entries[root+"/"+rel]={{"root",root},{"path",rel},{"kind","directory"},{"hash","directory"}};
            else throw std::runtime_error("unsupported special file: "+rel);
        }}auto history=conversation_inventory(config);if(vss){history["observation"]="live catalog metadata; file payload captured from one VSS set";phase_progress("snapshot_released");vss->finish();}
        return {{"format",1},{"entries",entries},{"skipped",skipped},{"bytes",size},{"conflicts",Json::array()},{"parents",Json::array()},{"conversation_history",history},{"source_consistency",vss?"VSS same-set; SQLite recovered and checked":"live SQLite read transactions"}};
    }
Json CaptureEngine::incremental_capture(ChangeCatalog& catalog,Json& next_catalog){
    auto& config=runtime_.config;
    auto& state=runtime_.state;
    auto& metadata_checked=runtime_.metadata_checked;
    auto& body_files=runtime_.body_files;
    auto& body_bytes=runtime_.body_bytes;
    auto& reused_files=runtime_.reused_files;
    auto& source_probe_bytes=runtime_.source_probe_bytes;
    auto& progress_mutex=runtime_.progress_mutex;
    auto& bytes_total=runtime_.bytes_total;

        struct Candidate {fs::path source;std::string root,relative;Json identity;};
        std::map<std::string,Candidate> candidates;Json entries=Json::object(),skipped=Json::array(),records=Json::object(),live_files=Json::array();
        auto previous=catalog.data.value("files",Json::object());
        auto reliable=[](const Json& family){return std::all_of(family.begin(),family.end(),[](const Json& identity){return identity.value("reliable",false);});};
        auto reusable=[&](const std::string& name,const Json& identity){if(!previous.contains(name)||!reliable(identity)||previous.at(name).at("identity")!=identity)return false;for(const auto& id:previous.at(name).at("entry").at("chunks"))if(!fs::exists(cache(id.get<std::string>())))return false;return true;};
        begin_progress("checking",0,0,0);const auto started=unix_now();
        for(const auto& root:config.at("roots")){if(!root.value("enabled",true))continue;auto id=root.at("id").get<std::string>();auto directory=path(root.at("path").get<std::string>());if(!fs::is_directory(directory)||link(directory))throw std::runtime_error("missing or linked data root: "+utf8(directory));
            for(fs::recursive_directory_iterator it(directory),end;it!=end;++it){checkpoint();auto source=it->path();auto relative=utf8(source.lexically_relative(directory));auto name=id+"/"+relative;
                if(link(source)){skipped.push_back({{"root",id},{"path",relative},{"reason","symlink/junction, add its target as an explicit root"}});if(it->is_directory())it.disable_recursion_pending();continue;}
                if(excluded_file(config,relative,id)){skipped.push_back({{"root",id},{"path",relative},{"reason","explicit exclusion"}});if(it->is_directory())it.disable_recursion_pending();continue;}
                if(it->is_directory()){entries[name]={{"root",id},{"path",relative},{"kind","directory"},{"hash","directory"}};continue;}
                if(!it->is_regular_file())throw std::runtime_error("unsupported special file: "+relative);
                if(!selected_file(config,id,relative))continue;
                if(volatile_file(relative)&&!relative.ends_with("-wal")&&!relative.ends_with("-shm")){skipped.push_back({{"root",id},{"path",relative},{"reason","volatile process lock"}});continue;}
                ++metadata_checked;candidates.emplace(name,Candidate{source,id,relative,source_family_identity(source)});runtime_.write_progress();
            }
        }
        // Sidecars are part of their database's identity, never independent WAL
        // payloads. Cached classification avoids opening unchanged database headers.
        struct Sidecar {std::string name,base_name;Candidate value;fs::path base;int suffix;bool selected;};std::vector<Sidecar> deferred;bool side_capture=false;
        const bool original=config.value("payload_mode",std::string("encrypted"))!="encrypted";
        for(auto it=candidates.begin();it!=candidates.end();){auto& value=it->second;auto rel=value.relative;if(original){if(rel.ends_with("-shm"))it=candidates.erase(it);else ++it;continue;}auto suffix=rel.ends_with("-journal")?8:rel.ends_with("-wal")||rel.ends_with("-shm")?4:0;if(!suffix){++it;continue;}auto base=it->first.substr(0,it->first.size()-suffix);auto source=value.source;source=source.parent_path()/path(utf8(source.filename()).substr(0,utf8(source.filename()).size()-suffix));
            // Classify selected sidecars from the base's captured entry. Opening
            // a live header here would defeat a zero-read unchanged capture.
            if(candidates.contains(base)){if(suffix==8&&previous.contains(base)&&!previous.at(base).at("entry").value("sqlite",false)&&!reusable(it->first,value.identity))side_capture=true;deferred.push_back({it->first,base,value,source,suffix,true});it=candidates.erase(it);continue;}
            bool known=previous.contains(base)&&previous.at(base).at("identity").at("main")==source_identity(source);bool sqlite=known&&previous.at(base).at("entry").value("sqlite",false);
            if(!known&&fs::exists(source)&&config.value("windows_vss",false)){side_capture=true;deferred.push_back({it->first,base,value,source,suffix,false});it=candidates.erase(it);continue;}
            if(!known&&fs::exists(source)){source_probe_bytes+=std::min<uint64_t>(16,fs::file_size(source));sqlite=database(source);}
            if(!sqlite&&suffix==4)throw std::runtime_error("orphan or non-SQLite sidecar: "+rel);
            if(sqlite)it=candidates.erase(it);else ++it;
        }
        bool need_capture=side_capture;uint64_t total_bytes=0;for(const auto& [name,value]:candidates){total_bytes+=value.identity.at("main").value("size",uint64_t(0));if(!reusable(name,value.identity))need_capture=true;}
        std::unique_ptr<VssSession> vss;if(need_capture&&config.value("windows_vss",false)){begin_progress("snapshotting",0,candidates.size(),total_bytes);vss=std::make_unique<VssSession>(config,state);}
        for(auto& side:deferred){if(side.selected)continue;auto source=vss?vss->source(side.base):side.base;source_probe_bytes+=std::min<uint64_t>(16,fs::file_size(source));if(!database(source)){if(side.suffix==4)throw std::runtime_error("orphan or non-SQLite sidecar: "+side.value.relative);total_bytes+=side.value.identity.at("main").value("size",uint64_t(0));candidates.emplace(side.name,side.value);}}
        begin_progress("capturing",candidates.size(),candidates.size(),total_bytes);uint64_t logical_bytes=0;
        auto capture_one=[&](const std::string& name,const Candidate& value){checkpoint();auto source=vss?vss->source(value.source):value.source;auto before=vss?value.identity:source_family_identity(source);Json e;const bool reused=reusable(name,before);bool live_fallback=false;
            if(reused){e=previous.at(name).at("entry");++reused_files;}
            else{
                if(vss){std::error_code error;auto status=fs::status(source,error);
                    if(error==std::errc::no_such_file_or_directory||(!error&&status.type()==fs::file_type::not_found)){
                        before=source_family_identity(value.source);
                        if(!before.at("main").value("exists",false)||!reliable(before)||link(value.source))throw std::runtime_error("source unavailable in VSS and live capture: "+value.relative);
                        source_probe_bytes+=std::min<uint64_t>(16,before.at("main").value("size",uint64_t(0)));
                        if(database(value.source))throw std::runtime_error("SQLite source is missing from the VSS set; retry snapshot: "+value.relative);
                        source=value.source;live_fallback=true;live_files.push_back({{"root",value.root},{"path",value.relative}});
                    }else if(error)throw fs::filesystem_error("VSS source metadata",source,error);
                }
                if(!original)source_probe_bytes+=std::min<uint64_t>(16,fs::file_size(source));e=entry(source,value.root,value.relative,true,vss!=nullptr&&!live_fallback);++body_files;body_bytes+=e.at("bytes").get<uint64_t>();
            }
            // A live SQLite read transaction may race a later WAL commit. Such
            // captures are valid snapshots but must never become reusable baselines.
            auto after=source_family_identity(vss?value.source:source);if(after!=before){if(live_fallback||(!vss&&!e.value("sqlite",false)))throw std::runtime_error("source identity changed during capture: "+value.relative);before["main"]["reliable"]=false;}
            logical_bytes+=e.at("bytes").get<uint64_t>();entries[name]=e;records[name]={{"identity",before},{"entry",e}};advance_progress(1,1,reused?e.at("bytes").get<uint64_t>():0);
        };
        for(const auto& [name,value]:candidates)capture_one(name,value);
        if(original&&!vss)for(const auto& [name,value]:candidates)if(source_family_identity(value.source)!=records.at(name).at("identity"))throw std::runtime_error("source family changed during original-file capture; retry while idle or enable VSS");
        for(const auto& side:deferred){if(!side.selected)continue;if(entries.at(side.base_name).value("sqlite",false))continue;if(side.suffix==4)throw std::runtime_error("orphan or non-SQLite sidecar: "+side.value.relative);{std::lock_guard guard(progress_mutex);++runtime_.progress_total;++runtime_.files_total;bytes_total+=side.value.identity.at("main").value("size",uint64_t(0));}capture_one(side.name,side.value);}
        Json history;if(!need_capture&&!catalog.data.empty())history=catalog.data.value("conversation_history",Json::object());else history=conversation_inventory(config);
        if(vss){history["observation"]="live catalog metadata; file payload captured from one VSS set";vss->finish();}
        next_catalog={{"schema",1},{"files",records},{"entries",entries},{"conversation_history",history}};
        {std::lock_guard guard(progress_mutex);bytes_total=logical_bytes;runtime_.write_progress(true);}
        return {{"format",1},{"entries",entries},{"skipped",skipped},{"bytes",logical_bytes},{"conflicts",Json::array()},{"parents",Json::array()},{"conversation_history",history},{"capture_started_unix",started},{"capture_finished_unix",unix_now()},{"vss_live_files",live_files},{"source_consistency",!live_files.empty()?"VSS database families; unavailable non-database files captured with stable live identities":original?(vss?"VSS same-set; original database families":"stable original file-family identities"):(vss?"VSS same-set; SQLite recovered and checked":"live SQLite read transactions")}};
    }
void CaptureEngine::begin_progress(const std::string& phase,uint64_t units,uint64_t files,uint64_t bytes)noexcept{runtime_.begin_progress(phase,units,files,bytes);}
void CaptureEngine::advance_progress(uint64_t units,uint64_t files,uint64_t bytes,uint64_t transferred)noexcept{runtime_.advance_progress(units,files,bytes,transferred);}
void CaptureEngine::phase_progress(const std::string& phase)noexcept{runtime_.phase_progress(phase);}
void CaptureEngine::checkpoint(){runtime_.checkpoint();}
std::string CaptureEngine::object(const Bytes& value){return runtime_.object(value);}
Bytes CaptureEngine::fetch(const std::string& value){return runtime_.fetch(value);}
fs::path CaptureEngine::cache(const std::string& value){return runtime_.cache(value);}
}

