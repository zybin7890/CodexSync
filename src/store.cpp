#include "core.hpp"
#include "tinyxml2.h"
#include "google_drive.hpp"
#include <sodium.h>
#include <stdexcept>
#include <algorithm>
#include <thread>
namespace cxs {
namespace {
bool safe_relative(const std::string& s) {return !s.empty() && s.front()!='/' && s.find("..") == std::string::npos && std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='/'||c=='.'||c=='-';});}
std::string hex_etag(const Bytes& b) {unsigned char h[32];crypto_generichash(h,32,b.data(),b.size(),nullptr,0);char text[65];sodium_bin2hex(text,65,h,32);return '"'+std::string(text)+'"';}
std::string local_name(const char* n) {std::string s=n?n:"";auto i=s.find(':');return i==std::string::npos?s:s.substr(i+1);}
void collect_hrefs(tinyxml2::XMLElement* e,std::vector<std::string>& out) {for(;e;e=e->NextSiblingElement()){if(local_name(e->Name())=="href"&&e->GetText())out.emplace_back(e->GetText());collect_hrefs(e->FirstChildElement(),out);}}
Response http(const std::string& url,const std::string& method,const Bytes& data,const std::string& condition,const std::string& user,const std::string& pass){
    Headers headers={"Content-Type: application/octet-stream"};
    if(method=="PROPFIND")headers.push_back("Depth: 1");
    if(!condition.empty())headers.push_back(condition=="*"?"If-None-Match: *":"If-Match: "+condition);
    if(!user.empty()){
        auto credentials=user+":"+pass;
        std::string encoded(sodium_base64_ENCODED_LEN(credentials.size(),sodium_base64_VARIANT_ORIGINAL),'\0');
        sodium_bin2base64(encoded.data(),encoded.size(),reinterpret_cast<const unsigned char*>(credentials.data()),credentials.size(),sodium_base64_VARIANT_ORIGINAL);
        encoded.resize(encoded.find('\0'));headers.push_back("Authorization: Basic "+encoded);
        sodium_memzero(credentials.data(),credentials.size());sodium_memzero(encoded.data(),encoded.size());
    }
    return http_request(url,method,data,headers);
}
}
Store::Store(const Json& c,const Json& secrets){auto remote=c.at("remote");auto provider=remote.value("provider",std::string("webdav"));if(provider=="google_drive"){google_=std::make_shared<GoogleDrive>(c,secrets);return;}if(provider!="webdav"&&provider!="local")throw std::runtime_error("unknown storage provider");if(provider=="local"&&!remote.contains("directory"))throw std::runtime_error("local provider requires a directory");if(remote.contains("directory")){directory_=fs::weakly_canonical(path(remote.at("directory").get<std::string>()))/"codex-sync-v1";return;}
    url_=remote.at("url").get<std::string>();if(url_.find('@')!=std::string::npos||url_.find('?')!=std::string::npos||url_.find('#')!=std::string::npos||url_.find('\r')!=std::string::npos||url_.find('\n')!=std::string::npos)throw std::runtime_error("WebDAV URL must not contain credentials, query or fragment");
    insecure_local_=remote.value("allow_loopback_http",false);bool local=url_.starts_with("http://127.0.0.1:")||url_.starts_with("http://localhost:");if(!url_.starts_with("https://")&&!(insecure_local_&&local))throw std::runtime_error("WebDAV requires HTTPS (HTTP allowed only for explicit loopback tests)");while(url_.ends_with('/'))url_.pop_back();url_+="/codex-sync-v1";
    user_=secrets.value("dav_user",env("CXS_DAV_USER"));password_=secrets.value("dav_password",env("CXS_DAV_PASSWORD"));if(user_.find(':')!=std::string::npos)throw std::runtime_error("invalid WebDAV username");
}
Response Store::request(const std::string& method,const std::string& relative,const Bytes& data,const std::string& condition){
    if(!safe_relative(relative))throw std::runtime_error("invalid repository path");
    if(google_){
        if(method=="MKCOL")return {405,{},{}};
        if(method=="GET"||method=="HEAD")return google_->get(relative,method=="HEAD");
        if(method=="PUT"&&condition=="*")return {google_->put_immutable(relative,data)?201:412,{},{}};
        throw std::runtime_error("Google Drive does not support mutable repository writes");
    }
    if(!directory_.empty()){auto p=directory_/path(relative);Response r;
        if(method=="MKCOL"){if(fs::exists(p))return {405,{},{}};fs::create_directories(p);return {201,{},{}};}
        if(method=="GET"||method=="HEAD"){if(!fs::exists(p))return {404,{},{}};r.body=read_bytes(p);r.etag=hex_etag(r.body);r.status=200;if(method=="HEAD")r.body.clear();return r;}
        if(method=="PUT"){if(condition=="*"&&fs::exists(p))return {412,{},{}};if(!condition.empty()&&condition!="*"&&(!fs::exists(p)||hex_etag(read_bytes(p))!=condition))return {412,{},{}};write_atomic(p,data);return {201,{},hex_etag(data)};}
        throw std::runtime_error("unsupported directory-store operation");
    }
    for(int attempt=0;;++attempt){auto r=http(url_+"/"+relative,method,data,condition,user_,password_);if(attempt<2&&(r.status==429||r.status==502||r.status==503||r.status==504)&&condition!=""){std::this_thread::sleep_for(std::chrono::milliseconds(300*(attempt+1)));continue;}return r;}
}
void Store::ensure(){if(google_)return;if(!directory_.empty())fs::create_directories(directory_);else{auto r=http(url_,"MKCOL",{},"",user_,password_);if(r.status!=201&&r.status!=405)throw std::runtime_error("cannot create WebDAV repository: HTTP "+std::to_string(r.status));}
    for(auto d:{"objects","snapshots","heads"}){auto r=request("MKCOL",d);if(r.status!=201&&r.status!=405)throw std::runtime_error("cannot create WebDAV collection: HTTP "+std::to_string(r.status));}}
std::vector<std::string> Store::list(const std::string& directory){if(google_)return google_->list(directory);std::vector<std::string> out;if(!directory_.empty()){if(!fs::exists(directory_/directory))return out;for(auto const& e:fs::directory_iterator(directory_/directory)){if(e.is_regular_file())out.push_back(utf8(e.path().filename()));}return out;}
    auto r=request("PROPFIND",directory+"/");if(r.status==404)return out;if(r.status!=207)throw std::runtime_error("WebDAV listing failed: HTTP "+std::to_string(r.status));tinyxml2::XMLDocument doc;if(doc.Parse(reinterpret_cast<const char*>(r.body.data()),r.body.size())!=tinyxml2::XML_SUCCESS)throw std::runtime_error("invalid WebDAV XML");std::vector<std::string> hrefs;collect_hrefs(doc.RootElement(),hrefs);for(auto h:hrefs){if(h.empty()||h.back()=='/')continue;auto i=h.find_last_of('/');auto name=h.substr(i==std::string::npos?0:i+1);if(safe_relative(name)&&name.find('/')==std::string::npos)out.push_back(name);}return out;
}
Bytes Store::get(const std::string& relative){auto r=request("GET",relative);if(r.status!=200)throw std::runtime_error("WebDAV download failed: HTTP "+std::to_string(r.status));return r.body;}
bool Store::put_immutable(const std::string& relative,const Bytes& data){auto h=request("HEAD",relative);if(h.status==200)return false;if(h.status!=404)throw std::runtime_error("WebDAV object check failed: HTTP "+std::to_string(h.status));auto r=request("PUT",relative,data,"*");if(r.status==412)return false;if(r.status!=200&&r.status!=201&&r.status!=204)throw std::runtime_error("WebDAV upload failed: HTTP "+std::to_string(r.status));return true;}
}
