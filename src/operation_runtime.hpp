#pragma once
#include "repository_paths.hpp"
#include "snapshot_job.hpp"
#include "job_coordinator.hpp"
#include <sodium.h>
#include <mutex>
#include <chrono>
namespace cxs {
class SecretKey {
public: Key value{};
    explicit SecretKey(const Json& req):value(credential_key(req)){}
    ~SecretKey(){sodium_memzero(value.data(),value.size());}
};
struct OperationRuntime {
    Json config;fs::path state;SecretKey key;Store store;std::unique_ptr<JobCoordinator> lock;size_t uploaded=0;
    std::string job_id;
    uint64_t metadata_checked=0,body_files=0,body_bytes=0,reused_files=0,resumed_objects=0,source_probe_bytes=0;
    std::mutex progress_mutex;
    std::chrono::steady_clock::time_point last_progress{},progress_started=std::chrono::steady_clock::now();
    std::string operation_id;
    std::string progress_phase;
    uint64_t progress_done=0,progress_total=0,files_done=0,files_total=0,bytes_done=0,bytes_total=0,transferred_bytes=0;
    bool track_progress=false,progress_finished=false;
    std::string error_code;
    bool retryable=false;
    OperationRuntime(Json c,const Json& r):config(std::move(c)),state(path(config.at("state").get<std::string>())),key(r),store(config,r){auto op=r.value("op",std::string{});track_progress=op=="backup"||op=="resume"||op=="sync"||op=="restore"||op=="rollback";if(track_progress){lock=std::make_unique<JobCoordinator>(state);private_directory(state/"cache");private_directory(state/"scratch");operation_id=r.value("progress_id",random_id());if(operation_id.empty()||operation_id.size()>128||!std::all_of(operation_id.begin(),operation_id.end(),[](unsigned char ch){return (ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='_'||ch=='-';}))throw std::runtime_error("invalid progress ID");begin_progress("checking",0,0,0);}}
    ~OperationRuntime(){if(track_progress&&!progress_finished)finish_progress(false);}
    void write_progress(bool force=false) noexcept {
        try {
            if(!track_progress)return;auto now=std::chrono::steady_clock::now();
            if(!force&&now-last_progress<std::chrono::milliseconds(250))return;
            auto data=metrics();data.update(Json{{"operation_id",operation_id},{"job_id",job_id},{"phase",progress_phase},{"total",progress_total},{"done",progress_done},{"files_done",files_done},{"files_total",files_total},{"bytes_done",bytes_done},{"bytes_total",bytes_total},{"transferred_bytes",transferred_bytes},{"elapsed_ms",std::chrono::duration_cast<std::chrono::milliseconds>(now-progress_started).count()}});write_atomic(state/"progress.json",bytes(data));
            last_progress=now;
        } catch (...) { /* Progress must not change operation correctness. */ }
    }
    void checkpoint(){try{JobCoordinator::checkpoint(state,job_id,operation_id);}catch(const JobInterrupted& signal){std::lock_guard guard(progress_mutex);progress_finished=true;progress_phase=signal.phase;write_progress(true);throw;}}
    Json metrics()const{return {{"metadata_checked",metadata_checked},{"body_files",body_files},{"body_bytes",body_bytes},{"source_probe_bytes",source_probe_bytes},{"reused_files",reused_files},{"resumed_objects",resumed_objects},{"uploaded_objects",uploaded},{"error_code",error_code},{"retryable",retryable}};}
    void fail_progress(const std::string& code,bool retry)noexcept{std::lock_guard guard(progress_mutex);error_code=code;retryable=retry;progress_phase="failed";progress_finished=true;write_progress(true);}
    void begin_progress(const std::string& phase,uint64_t total,uint64_t files,uint64_t size) noexcept {
        std::lock_guard guard(progress_mutex);runtime_log(state,"phase",{{"operation_id",operation_id},{"phase",phase}});progress_phase=phase;progress_done=0;progress_total=total;files_done=0;files_total=files;bytes_done=0;bytes_total=size;write_progress(true);
    }
    void advance_progress(uint64_t units,uint64_t files,uint64_t size,uint64_t transferred=0) noexcept {
        std::lock_guard guard(progress_mutex);progress_done+=units;files_done+=files;bytes_done+=size;transferred_bytes+=transferred;if(transferred&&progress_phase=="uploading")++uploaded;write_progress();
    }
    void phase_progress(const std::string& phase) noexcept {
        std::lock_guard guard(progress_mutex);runtime_log(state,"phase",{{"operation_id",operation_id},{"phase",phase}});progress_phase=phase;write_progress(true);
    }
    void finish_progress(bool success) noexcept {
        std::lock_guard guard(progress_mutex);progress_finished=true;if(success&&files_total)files_done=files_total;progress_phase=success?"completed":"failed";write_progress(true);
    }
    fs::path cache(const std::string& id){require_id(id);return state/"cache"/(id+".cxs");}
    std::string object(const Bytes& plain){auto id=digest(plain,key.value);auto p=cache(id);if(!fs::exists(p))write_atomic(p,seal(plain,key.value,"object:"+id));return id;}
    Bytes fetch(const std::string& id){require_id(id);auto p=cache(id);const bool cached=fs::exists(p);auto encrypted=cached?read_bytes(p,chunk_size+128):store.get("objects/"+id+".cxs");if(!cached)advance_progress(0,0,0,encrypted.size());auto plain=open(encrypted,key.value,"object:"+id);if(digest(plain,key.value)!=id)throw std::runtime_error("object digest mismatch");if(!cached)write_atomic(p,encrypted);return plain;}
    Json entry(const fs::path& p,const std::string& root,const std::string& rel,bool scanning=false,bool shadow=false);
    Json scan();
    static void require_id_root(const std::string& s){if(s.empty()||s.size()>64||!std::all_of(s.begin(),s.end(),[](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-';}))throw std::runtime_error("root id must use lowercase letters, digits, underscore or hyphen");}
    Json incremental_capture(ChangeCatalog& catalog,Json& next_catalog);
    Json snapshot(const std::string& id);
    Json publish(Json m,const Json& parents,SnapshotJob* job=nullptr);
    std::map<std::string,Json> frontier();
    fs::path root_path(const std::string& id);
    void materialize(const Json& e,const fs::path& dest);
    void restore_progress(const Json& entries);
};
}

