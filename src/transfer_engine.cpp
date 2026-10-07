#include "transfer_engine.hpp"
#include "operation_runtime.hpp"
#include "vss.hpp"
#include <sqlite3.h>
#include <fstream>
#include <thread>
#include <climits>
#include <atomic>
namespace cxs {
Json TransferEngine::snapshot(const std::string& id){
    auto& key=runtime_.key;
    auto& store=runtime_.store;
require_id(id);auto payload=store.get("snapshots/"+id+(store.original()?".json":".cxs"));auto m=store.original()?json(payload):json(open(payload,key.value,"snapshot:"+id));if(m.at("format")!=1)throw std::runtime_error("unsupported repository format");return m;}
Json TransferEngine::publish(Json m,const Json& parents,SnapshotJob* job){
    begin_progress("checking_remote",0,0,0);
    if(runtime_.store.original())return publish_original(std::move(m),parents,job);
    auto& config=runtime_.config;
    auto& state=runtime_.state;
    auto& key=runtime_.key;
    auto& store=runtime_.store;
    auto& resumed_objects=runtime_.resumed_objects;
    auto& progress_mutex=runtime_.progress_mutex;
store.ensure();auto device=config.at("device").get<std::string>();require_id(device,32);Response head{404,{},{}};
        if(!store.append_only()){head=store.request("GET","heads/"+device+".cxs");if(head.status!=200&&head.status!=404)throw std::runtime_error("cannot read device head");if(head.status==200&&head.etag.empty())throw std::runtime_error("server must supply ETag for device-head compare-and-swap");}
        m["parents"]=parents;
        // Drive has no WebDAV-style atomic head creation. Commit immutable snapshots only;
        // concurrent branches remain visible instead of emulating an unsafe compare-and-swap.
        if(store.append_only())for(auto& [id,previous]:frontier())if(previous.at("device")==device&&std::find(m["parents"].begin(),m["parents"].end(),id)==m["parents"].end())m["parents"].push_back(id);
        if(job&&job->data.contains("commit_manifest"))m=job->data.at("commit_manifest");
        if(job&&head.status==200&&json(open(head.body,key.value,"head:"+device)).at("snapshot")==job->data.at("snapshot")){auto saved=snapshot(job->data.at("snapshot"));if(saved!=m)throw std::runtime_error("committed prepared snapshot does not match journal");return saved;}
        std::set<std::string> unique;
        for(auto& e:m.at("entries"))if(e.value("kind","")=="file")for(auto& x:e.at("chunks")){auto id=x.get<std::string>();require_id(id);unique.insert(id);}
        std::vector<std::string> objects(unique.begin(),unique.end());
        const bool parallel=config.at("remote").value("provider",std::string("webdav"))=="google_drive";
        std::atomic<size_t> next{0},done{0},new_uploads{0};std::atomic<bool> failed{false};std::mutex error_mutex,journal_mutex;std::exception_ptr failure;
        uint64_t object_bytes=0,file_count=0;std::vector<uint64_t> object_sizes;object_sizes.reserve(objects.size());for(const auto& id:objects){auto size=fs::file_size(cache(id));object_sizes.push_back(size);object_bytes+=size;}for(const auto& e:m.at("entries"))if(e.value("kind",std::string())=="file")++file_count;
        begin_progress("uploading",objects.size(),file_count,object_bytes);
        auto worker=[&]{try{with_http_session([&]{while(!failed.load()){
            checkpoint();auto index=next.fetch_add(1);if(index>=objects.size())break;const auto& id=objects[index];auto relative="objects/"+id+".cxs";
            bool exists=false;if(job){std::lock_guard guard(journal_mutex);exists=job->confirmed(id);if(exists){std::lock_guard progress_guard(progress_mutex);++resumed_objects;}}
            if(!exists&&parallel){auto response=store.request("HEAD",relative);if(response.status!=200&&response.status!=404)throw std::runtime_error("Google object cache check failed");exists=response.status==200;}
            const auto size=object_sizes[index];bool transmitted=false;
            if(!exists){auto encrypted=read_bytes(cache(id),chunk_size+128);auto plain=open(encrypted,key.value,"object:"+id);if(digest(plain,key.value)!=id)throw std::runtime_error("prepared snapshot cache digest mismatch");if(store.put_immutable(relative,encrypted)){new_uploads.fetch_add(1);transmitted=true;}}
            if(job){std::lock_guard guard(journal_mutex);job->confirm(id);}
            done.fetch_add(1);advance_progress(1,0,size,transmitted?size:0);
        }});}catch(...){failed=true;std::lock_guard guard(error_mutex);if(!failure)failure=std::current_exception();}};
        if(parallel){std::vector<std::jthread> workers;for(size_t i=0;i<std::min<size_t>(8,objects.size());++i)workers.emplace_back(worker);for(auto& thread:workers)thread.join();}else worker();
        if(failure)std::rethrow_exception(failure);checkpoint();phase_progress("committing");
        if(!job||!job->data.contains("commit_manifest")){
            if(head.status==200){auto previous=json(open(head.body,key.value,"head:"+device)).at("snapshot");if(std::find(m["parents"].begin(),m["parents"].end(),previous)==m["parents"].end())m["parents"].push_back(previous);}
            m["device"]=device;m["created_unix"]=unix_now();m["committed_unix"]=m["created_unix"];m["snapshot"]=job?job->data.at("snapshot").get<std::string>():random_id()+random_id();
            if(job){job->data["commit_manifest"]=m;job->data["head_condition"]=head.status==404?"*":head.etag;job->data["status"]="committing";job->save();}
        }
        auto id=m.at("snapshot").get<std::string>();auto payload=seal(bytes(m),key.value,"snapshot:"+id);
        if(job){auto file=state/"jobs"/job->data.at("job_id").get<std::string>()/"manifest.cxs";if(fs::exists(file))payload=read_bytes(file);else write_atomic(file,payload);}
        if(!store.put_immutable("snapshots/"+id+".cxs",payload)&&snapshot(id)!=m)throw std::runtime_error("remote snapshot does not match prepared commit");
        if(store.append_only())return m;
        auto ref=seal(bytes(Json{{"snapshot",id}}),key.value,"head:"+device);auto condition=job?job->data.at("head_condition").get<std::string>():head.status==404?"*":head.etag;
        auto r=store.request("PUT","heads/"+device+".cxs",ref,condition);
        if(r.status==412){auto actual=store.request("GET","heads/"+device+".cxs");if(actual.status==200&&json(open(actual.body,key.value,"head:"+device)).at("snapshot")==id)return m;throw std::runtime_error("device head changed concurrently; prepared snapshot retained");}
        if(r.status!=200&&r.status!=201&&r.status!=204)throw std::runtime_error("device head commit failed: HTTP "+std::to_string(r.status));return m;
    }
std::map<std::string,Json> TransferEngine::frontier(){
    auto& key=runtime_.key;
    auto& store=runtime_.store;
std::map<std::string,Json> heads;
        if(store.append_only()){for(auto file:store.list("snapshots")){if(!file.ends_with(".cxs"))continue;if(heads.size()>=4096)throw std::runtime_error("Google snapshot history exceeds traversal safety limit");auto id=file.substr(0,file.size()-4);require_id(id);heads[id]=snapshot(id);}}
        else for(auto file:store.list("heads")){if(!file.ends_with(".cxs"))continue;auto device=file.substr(0,file.size()-4);require_id(device,32);auto h=json(open(store.get("heads/"+file),key.value,"head:"+device));auto id=h.at("snapshot").get<std::string>();heads[id]=snapshot(id);}
        std::set<std::string> ancestors,visited;std::function<void(const Json&,int)> visit=[&](const Json& m,int depth){if(depth>256)throw std::runtime_error("history exceeds traversal depth limit");for(auto& p:m.at("parents")){auto id=p.get<std::string>();require_id(id);ancestors.insert(id);if(visited.insert(id).second){if(visited.size()>4096)throw std::runtime_error("history exceeds traversal limit");visit(heads.contains(id)?heads.at(id):snapshot(id),depth+1);}}};for(auto& [id,m]:heads)visit(m,0);for(auto& id:ancestors)heads.erase(id);return heads;
    }
void TransferEngine::begin_progress(const std::string& phase,uint64_t units,uint64_t files,uint64_t bytes)noexcept{runtime_.begin_progress(phase,units,files,bytes);}
void TransferEngine::advance_progress(uint64_t units,uint64_t files,uint64_t bytes,uint64_t transferred)noexcept{runtime_.advance_progress(units,files,bytes,transferred);}
void TransferEngine::phase_progress(const std::string& phase)noexcept{runtime_.phase_progress(phase);}
void TransferEngine::checkpoint(){runtime_.checkpoint();}
std::string TransferEngine::object(const Bytes& value){return runtime_.object(value);}
Bytes TransferEngine::fetch(const std::string& value){return runtime_.fetch(value);}
fs::path TransferEngine::cache(const std::string& value){return runtime_.cache(value);}
}


