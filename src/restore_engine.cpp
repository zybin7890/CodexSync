#include "restore_engine.hpp"
#include "operation_runtime.hpp"
#include "vss.hpp"
#include "encryption.hpp"
#include <sqlite3.h>
#include <fstream>
#include <thread>
#include <climits>
#include <atomic>
namespace cxs {
fs::path RestoreEngine::root_path(const std::string& id){
    auto& config=runtime_.config;
for(auto& r:config.at("roots"))if(r.at("id")==id)return path(r.at("path").get<std::string>());throw std::runtime_error("snapshot root is not mapped in config: "+id);}
void RestoreEngine::materialize(const Json& e,const fs::path& dest){
    auto& key=runtime_.key;
if(e.at("kind")=="directory"){fs::create_directories(dest);return;}if(e.at("kind")!="file")throw std::runtime_error("unsupported manifest entry");fs::create_directories(dest.parent_path());crypto_generichash_state h;crypto_generichash_init(&h,key.value.data(),32,32);uint64_t size=0;
    if(e.contains("original_path")){auto encrypted=dest;encrypted+=".download-"+random_id();try{runtime_.store.original_download(e.at("original_path"),e.value("encrypted",false)?encrypted:dest);auto received=fs::file_size(e.value("encrypted",false)?encrypted:dest);advance_progress(0,0,0,received);if(e.value("encrypted",false)){unprotect_file(encrypted,dest,key.value);fs::remove(encrypted);}std::ifstream file(dest,std::ios::binary);Bytes block(chunk_size);while(file){checkpoint();file.read(reinterpret_cast<char*>(block.data()),block.size());auto n=file.gcount();if(!n)break;crypto_generichash_update(&h,block.data(),n);size+=n;advance_progress(0,0,n);}if(file.bad())throw std::runtime_error("cannot verify restored original file");}catch(...){std::error_code error;fs::remove(encrypted,error);throw;}}
    else{std::ofstream out(dest,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("cannot stage restored file");for(auto& x:e.at("chunks")){auto b=fetch(x.get<std::string>());if(b.size()>chunk_size)throw std::runtime_error("invalid chunk size");out.write(reinterpret_cast<const char*>(b.data()),b.size());crypto_generichash_update(&h,b.data(),b.size());size+=b.size();advance_progress(0,0,b.size());}out.flush();if(!out)throw std::runtime_error("cannot flush staged file");}
    unsigned char hash[32];crypto_generichash_final(&h,hash,32);char text[65];sodium_bin2hex(text,65,hash,32);if(size!=e.at("bytes").get<uint64_t>()||e.at("hash")!=text)throw std::runtime_error("restored file digest mismatch");
#ifndef _WIN32
        fs::permissions(dest,static_cast<fs::perms>(e.value("mode",0600u)&0777));
#endif
        advance_progress(1,1,0);
    }
void RestoreEngine::restore_progress(const Json& entries){
uint64_t files=0,size=0;for(const auto& e:entries)if(e.value("kind",std::string())=="file"){++files;size+=e.at("bytes").get<uint64_t>();}begin_progress("downloading",files,files,size);}
void RestoreEngine::begin_progress(const std::string& phase,uint64_t units,uint64_t files,uint64_t bytes)noexcept{runtime_.begin_progress(phase,units,files,bytes);}
void RestoreEngine::advance_progress(uint64_t units,uint64_t files,uint64_t bytes,uint64_t transferred)noexcept{runtime_.advance_progress(units,files,bytes,transferred);}
void RestoreEngine::phase_progress(const std::string& phase)noexcept{runtime_.phase_progress(phase);}
void RestoreEngine::checkpoint(){runtime_.checkpoint();}
std::string RestoreEngine::object(const Bytes& value){return runtime_.object(value);}
Bytes RestoreEngine::fetch(const std::string& value){return runtime_.fetch(value);}
fs::path RestoreEngine::cache(const std::string& value){return runtime_.cache(value);}
}
