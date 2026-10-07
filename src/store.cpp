#include "core.hpp"
#include "tinyxml2.h"
#include "google_drive.hpp"
#include <sodium.h>
#include <stdexcept>
#include <algorithm>
#include <thread>
#include <fstream>
namespace cxs {
namespace {
bool safe_relative(const std::string& s) {if(s.empty()||s.front()=='/'||s.find_first_of("\\:\r\n")!=std::string::npos||s.find('\0')!=std::string::npos)return false;size_t start=0;while(start<s.size()){const auto end=s.find('/',start);const auto part=s.substr(start,end==std::string::npos?s.size()-start:end-start);if(part.empty()||part=="."||part=="..")return false;if(end==std::string::npos)break;start=end+1;}return true;}
std::string encoded_path(const std::string& value){std::string result;size_t start=0;while(start<value.size()){auto end=value.find('/',start);result+=url_encode(value.substr(start,end==std::string::npos?value.size()-start:end-start));if(end==std::string::npos)break;result+='/';start=end+1;}return result;}
std::string hex_etag(const Bytes& b) {unsigned char h[32];crypto_generichash(h,32,b.data(),b.size(),nullptr,0);char text[65];sodium_bin2hex(text,65,h,32);return '"'+std::string(text)+'"';}
std::string local_name(const char* n) {std::string s=n?n:"";auto i=s.find(':');return i==std::string::npos?s:s.substr(i+1);}
void collect_hrefs(tinyxml2::XMLElement* e,std::vector<std::string>& out) {for(;e;e=e->NextSiblingElement()){if(local_name(e->Name())=="href"&&e->GetText())out.emplace_back(e->GetText());collect_hrefs(e->FirstChildElement(),out);}}
Response http(const std::string& url,const std::string& method,const Bytes& data,const std::string& condition,const std::string& user,const std::string& pass,const fs::path& upload={},const fs::path& download={},const Headers& extra={}){
    Headers headers={"Content-Type: application/octet-stream"};
    headers.insert(headers.end(),extra.begin(),extra.end());
    if(method=="PROPFIND")headers.push_back("Depth: 1");
    if(!condition.empty())headers.push_back(condition=="*"?"If-None-Match: *":"If-Match: "+condition);
    if(!user.empty()){
        auto credentials=user+":"+pass;
        std::string encoded(sodium_base64_ENCODED_LEN(credentials.size(),sodium_base64_VARIANT_ORIGINAL),'\0');
        sodium_bin2base64(encoded.data(),encoded.size(),reinterpret_cast<const unsigned char*>(credentials.data()),credentials.size(),sodium_base64_VARIANT_ORIGINAL);
        encoded.resize(encoded.find('\0'));headers.push_back("Authorization: Basic "+encoded);
        sodium_memzero(credentials.data(),credentials.size());sodium_memzero(encoded.data(),encoded.size());
    }
    return upload.empty()&&download.empty()?http_request(url,method,data,headers):http_file_request(url,method,upload,download,headers);
}
}
Store::Store(const Json& c,const Json& secrets){original_=c.value("payload_mode",std::string("encrypted"))!="encrypted";auto remote=c.at("remote");auto provider=remote.value("provider",std::string("webdav"));if(provider=="google_drive"){google_=std::make_shared<GoogleDrive>(c,secrets);return;}if(provider!="webdav"&&provider!="local")throw std::runtime_error("unknown storage provider");if(provider=="local"&&!remote.contains("directory"))throw std::runtime_error("local provider requires a directory");if(remote.contains("directory")){directory_=fs::weakly_canonical(path(remote.at("directory").get<std::string>()))/(original_?"CodexSync-original":"codex-sync-v1");return;}
    url_=remote.at("url").get<std::string>();if(url_.find('@')!=std::string::npos||url_.find('?')!=std::string::npos||url_.find('#')!=std::string::npos||url_.find('\r')!=std::string::npos||url_.find('\n')!=std::string::npos)throw std::runtime_error("WebDAV URL must not contain credentials, query or fragment");
    insecure_local_=remote.value("allow_loopback_http",false);bool local=url_.starts_with("http://127.0.0.1:")||url_.starts_with("http://localhost:");if(!url_.starts_with("https://")&&!(insecure_local_&&local))throw std::runtime_error("WebDAV requires HTTPS (HTTP allowed only for explicit loopback tests)");while(url_.ends_with('/'))url_.pop_back();url_+=original_?"/CodexSync-original":"/codex-sync-v1";
    user_=secrets.value("dav_user",env("CXS_DAV_USER"));password_=secrets.value("dav_password",env("CXS_DAV_PASSWORD"));if(user_.find(':')!=std::string::npos)throw std::runtime_error("invalid WebDAV username");
}
Response Store::request(const std::string& method,const std::string& relative,const Bytes& data,const std::string& condition){
    if(!safe_relative(relative))throw std::runtime_error("invalid repository path");
    if(google_){
        if(original_){if(method=="MKCOL"){google_->original_folder(relative,true);return {201,{},{}};}if(method=="GET"||method=="HEAD"){auto result=google_->original_get(relative);if(method=="HEAD")result.body.clear();return result;}if(method=="PUT"&&condition=="*")return {google_->original_metadata(relative,data)?201:412,{},{}};throw std::runtime_error("original snapshots only support immutable writes");}
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
    for(int attempt=0;;++attempt){auto r=http(url_+"/"+encoded_path(relative),method,data,condition,user_,password_);if(attempt<2&&(r.status==429||r.status==502||r.status==503||r.status==504)&&condition!=""){std::this_thread::sleep_for(std::chrono::milliseconds(300*(attempt+1)));continue;}return r;}
}
void Store::ensure(){if(google_){if(original_)google_->original_folder("",true);else google_->prepare_uploads();return;}if(!directory_.empty())fs::create_directories(directory_);else{auto r=http(url_,"MKCOL",{},"",user_,password_);if(r.status!=201&&r.status!=405)throw std::runtime_error("cannot create WebDAV repository: HTTP "+std::to_string(r.status));}
    for(const auto& d:original_?std::vector<std::string>{"files","snapshots"}:std::vector<std::string>{"objects","snapshots","heads"}){auto r=request("MKCOL",d);if(r.status!=201&&r.status!=405)throw std::runtime_error("cannot create WebDAV collection: HTTP "+std::to_string(r.status));}}
std::vector<std::string> Store::list(const std::string& directory){if(google_)return original_?google_->original_list(directory):google_->list(directory);std::vector<std::string> out;if(!directory_.empty()){if(!fs::exists(directory_/directory))return out;for(auto const& e:fs::directory_iterator(directory_/directory)){if(e.is_regular_file())out.push_back(utf8(e.path().filename()));}return out;}
    auto r=request("PROPFIND",directory+"/");if(r.status==404)return out;if(r.status!=207)throw std::runtime_error("WebDAV listing failed: HTTP "+std::to_string(r.status));tinyxml2::XMLDocument doc;if(doc.Parse(reinterpret_cast<const char*>(r.body.data()),r.body.size())!=tinyxml2::XML_SUCCESS)throw std::runtime_error("invalid WebDAV XML");std::vector<std::string> hrefs;collect_hrefs(doc.RootElement(),hrefs);for(auto h:hrefs){if(h.empty()||h.back()=='/')continue;auto i=h.find_last_of('/');auto name=h.substr(i==std::string::npos?0:i+1);if(safe_relative(name)&&name.find('/')==std::string::npos)out.push_back(name);}return out;
}
Bytes Store::get(const std::string& relative){auto r=request("GET",relative);if(r.status!=200)throw std::runtime_error("WebDAV download failed: HTTP "+std::to_string(r.status));return r.body;}
std::string Store::identity(const Key& key){auto value=(google_?google_->identity():!directory_.empty()?utf8(directory_):url_+"\n"+user_)+(original_?"\noriginal":"\nencrypted");return digest(Bytes(value.begin(),value.end()),key);}
Json Store::location(){return google_?google_->location():Json::object();}
bool Store::put_immutable(const std::string& relative,const Bytes& data){if(google_){if(!safe_relative(relative))throw std::runtime_error("invalid repository path");return original_?google_->original_metadata(relative,data):google_->put_immutable(relative,data);}auto h=request("HEAD",relative);if(h.status==200)return false;if(h.status!=404)throw std::runtime_error("WebDAV object check failed: HTTP "+std::to_string(h.status));auto r=request("PUT",relative,data,"*");if(r.status==412)return false;if(r.status!=200&&r.status!=201&&r.status!=204)throw std::runtime_error("WebDAV upload failed: HTTP "+std::to_string(r.status));return true;}
void Store::original_directory(const std::string& relative){if(!original_||!safe_relative(relative))throw std::runtime_error("invalid original-file directory");if(google_){google_->original_folder(relative,true);return;}size_t end=0;do{end=relative.find('/',end+1);auto r=request("MKCOL",relative.substr(0,end));if(r.status!=201&&r.status!=405)throw std::runtime_error("original directory creation failed: HTTP "+std::to_string(r.status));}while(end!=std::string::npos);}
void Store::original_download(const std::string& relative,const fs::path& output){if(!original_||!safe_relative(relative))throw std::runtime_error("invalid original-file path");if(google_){auto r=google_->original_get(relative,output);if(r.status!=200)throw std::runtime_error("original file download failed: HTTP "+std::to_string(r.status));return;}if(!directory_.empty()){fs::copy_file(directory_/path(relative),output,fs::copy_options::overwrite_existing);return;}auto r=http(url_+"/"+encoded_path(relative),"GET",{},"",user_,password_,{},output);if(r.status!=200)throw std::runtime_error("original file download failed: HTTP "+std::to_string(r.status));}
bool Store::original_upload(const std::string& relative,const fs::path& input,const std::string& hash){if(!original_||!safe_relative(relative))throw std::runtime_error("invalid original-file path");original_directory(utf8(path(relative).parent_path()));if(google_)return google_->original_upload(relative,input,hash);auto head=request("HEAD",relative);if(head.status==200){auto verify=input;verify+=".verify-"+random_id();try{original_download(relative,verify);std::ifstream a(input,std::ios::binary),b(verify,std::ios::binary);std::vector<char> aa(1024*1024),bb(1024*1024);bool equal=fs::file_size(input)==fs::file_size(verify);while(equal&&a){a.read(aa.data(),aa.size());b.read(bb.data(),bb.size());equal=a.gcount()==b.gcount()&&std::equal(aa.begin(),aa.begin()+a.gcount(),bb.begin());}fs::remove(verify);if(!equal||a.bad()||b.bad())throw std::runtime_error("original-file retry bytes mismatch");return false;}catch(...){std::error_code error;fs::remove(verify,error);throw;}}if(head.status!=404)throw std::runtime_error("original file check failed: HTTP "+std::to_string(head.status));if(!directory_.empty()){fs::copy_file(input,directory_/path(relative));return true;}auto r=http(url_+"/"+encoded_path(relative),"PUT",{},"*",user_,password_,input);if(r.status!=200&&r.status!=201&&r.status!=204)throw std::runtime_error("original file upload failed: HTTP "+std::to_string(r.status));return true;}
bool Store::original_copy(const std::string& from,const std::string& to,const std::string& hash){if(!original_||!safe_relative(from)||!safe_relative(to))throw std::runtime_error("invalid original-file copy path");original_directory(utf8(path(to).parent_path()));if(google_)return google_->original_copy(from,to,hash);if(!directory_.empty()){if(!fs::exists(directory_/path(from))||fs::exists(directory_/path(to)))return false;fs::copy_file(directory_/path(from),directory_/path(to));return true;}auto r=http(url_+"/"+encoded_path(from),"COPY",{},"",user_,password_,{},{},{"Destination: "+url_+"/"+encoded_path(to),"Overwrite: F"});return r.status==201||r.status==204;}
}
