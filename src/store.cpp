#include "core.hpp"
#include "tinyxml2.h"
#include <sodium.h>
#include <stdexcept>
#include <algorithm>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif
namespace cxs {
namespace {
constexpr size_t maximum_response=128*1024*1024;
bool safe_relative(const std::string& s) {return !s.empty() && s.front()!='/' && s.find("..") == std::string::npos && std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='/'||c=='.'||c=='-';});}
std::string hex_etag(const Bytes& b) {unsigned char h[32];crypto_generichash(h,32,b.data(),b.size(),nullptr,0);char text[65];sodium_bin2hex(text,65,h,32);return '"'+std::string(text)+'"';}
std::string local_name(const char* n) {std::string s=n?n:"";auto i=s.find(':');return i==std::string::npos?s:s.substr(i+1);}
void collect_hrefs(tinyxml2::XMLElement* e,std::vector<std::string>& out) {for(;e;e=e->NextSiblingElement()){if(local_name(e->Name())=="href"&&e->GetText())out.emplace_back(e->GetText());collect_hrefs(e->FirstChildElement(),out);}}
#ifdef _WIN32
struct Internet {HINTERNET handle{};~Internet(){if(handle)WinHttpCloseHandle(handle);}operator HINTERNET()const{return handle;}};
Response http(const std::string& url,const std::string& method,const Bytes& data,const std::string& condition,const std::string& user,const std::string& pass){
    auto wide=path(url).wstring();URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);parts.dwHostNameLength=static_cast<DWORD>(-1);parts.dwUrlPathLength=static_cast<DWORD>(-1);parts.dwExtraInfoLength=static_cast<DWORD>(-1);
    if(!WinHttpCrackUrl(wide.c_str(),static_cast<DWORD>(wide.size()),0,&parts))throw std::runtime_error("invalid WebDAV URL");
    std::wstring host(parts.lpszHostName,parts.dwHostNameLength),target(parts.lpszUrlPath,parts.dwUrlPathLength);target.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
    Internet session{WinHttpOpen(L"CodexSync/0.1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)};
    if(!session.handle)throw std::runtime_error("WebDAV session failed");WinHttpSetTimeouts(session,10000,10000,30000,60000);
    Internet connection{WinHttpConnect(session,host.c_str(),parts.nPort,0)};Internet request{WinHttpOpenRequest(connection,path(method).c_str(),target.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,parts.nScheme==INTERNET_SCHEME_HTTPS?WINHTTP_FLAG_SECURE:0)};
    if(!request.handle)throw std::runtime_error("WebDAV request creation failed");DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof policy);
    std::wstring headers=L"Content-Type: application/octet-stream\r\n";if(method=="PROPFIND")headers+=L"Depth: 1\r\n";
    if(!condition.empty())headers+=path(condition=="*"?"If-None-Match: *\r\n":"If-Match: "+condition+"\r\n").wstring();
    if(!user.empty()) {auto credentials=user+":"+pass;std::string base64(sodium_base64_ENCODED_LEN(credentials.size(),sodium_base64_VARIANT_ORIGINAL),'\0');sodium_bin2base64(base64.data(),base64.size(),reinterpret_cast<const unsigned char*>(credentials.data()),credentials.size(),sodium_base64_VARIANT_ORIGINAL);base64.resize(base64.find('\0'));headers+=path("Authorization: Basic "+base64+"\r\n").wstring();sodium_memzero(credentials.data(),credentials.size());}
    if(!WinHttpSendRequest(request,headers.c_str(),static_cast<DWORD>(headers.size()),data.empty()?WINHTTP_NO_REQUEST_DATA:const_cast<unsigned char*>(data.data()),static_cast<DWORD>(data.size()),static_cast<DWORD>(data.size()),0)||!WinHttpReceiveResponse(request,nullptr))throw std::runtime_error("WebDAV connection or TLS verification failed");
    Response result;DWORD n=sizeof(DWORD),code{};WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&code,&n,WINHTTP_NO_HEADER_INDEX);result.status=static_cast<int>(code);
    wchar_t etag[1024]{};n=sizeof etag;if(WinHttpQueryHeaders(request,WINHTTP_QUERY_ETAG,WINHTTP_HEADER_NAME_BY_INDEX,etag,&n,WINHTTP_NO_HEADER_INDEX))result.etag=utf8(fs::path(etag));
    if(method!="HEAD")for(;;){DWORD available{};if(!WinHttpQueryDataAvailable(request,&available))throw std::runtime_error("WebDAV read failed");if(!available)break;if(result.body.size()+available>maximum_response)throw std::runtime_error("WebDAV response too large");auto offset=result.body.size();result.body.resize(offset+available);DWORD got{};if(!WinHttpReadData(request,result.body.data()+offset,available,&got))throw std::runtime_error("WebDAV incomplete response");result.body.resize(offset+got);}
    return result;
}
#else
size_t body_callback(char* b,size_t size,size_t count,void* ctx){auto& body=*static_cast<Bytes*>(ctx);size_t n=size*count;if(body.size()+n>maximum_response)return 0;body.insert(body.end(),b,b+n);return n;}
size_t header_callback(char* b,size_t s,size_t c,void* ctx){size_t n=s*c;std::string line(b,n),lower=line;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char x){return std::tolower(x);});if(lower.starts_with("etag:")){auto value=line.substr(5);value.erase(0,value.find_first_not_of(" \t"));value.erase(value.find_last_not_of("\r\n \t")+1);*static_cast<std::string*>(ctx)=value;}return n;}
Response http(const std::string& url,const std::string& method,const Bytes& data,const std::string& condition,const std::string& user,const std::string& pass){
    static const auto initialized=curl_global_init(CURL_GLOBAL_DEFAULT);if(initialized!=CURLE_OK)throw std::runtime_error("curl initialization failed");
    auto handle=curl_easy_init();if(!handle)throw std::runtime_error("curl session failed");Response r;curl_slist* headers=nullptr;
    headers=curl_slist_append(headers,"Content-Type: application/octet-stream");if(method=="PROPFIND")headers=curl_slist_append(headers,"Depth: 1");if(!condition.empty())headers=curl_slist_append(headers,(condition=="*"?"If-None-Match: *":"If-Match: "+condition).c_str());
    curl_easy_setopt(handle,CURLOPT_URL,url.c_str());curl_easy_setopt(handle,CURLOPT_CUSTOMREQUEST,method.c_str());curl_easy_setopt(handle,CURLOPT_HTTPHEADER,headers);curl_easy_setopt(handle,CURLOPT_CONNECTTIMEOUT,10L);curl_easy_setopt(handle,CURLOPT_TIMEOUT,90L);curl_easy_setopt(handle,CURLOPT_NOSIGNAL,1L);curl_easy_setopt(handle,CURLOPT_FOLLOWLOCATION,0L);curl_easy_setopt(handle,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(handle,CURLOPT_SSL_VERIFYHOST,2L);
    if(method=="HEAD")curl_easy_setopt(handle,CURLOPT_NOBODY,1L);
    if(method=="PUT"){curl_easy_setopt(handle,CURLOPT_POSTFIELDS,data.empty()?"":reinterpret_cast<const char*>(data.data()));curl_easy_setopt(handle,CURLOPT_POSTFIELDSIZE_LARGE,static_cast<curl_off_t>(data.size()));}
    if(!user.empty()){curl_easy_setopt(handle,CURLOPT_USERNAME,user.c_str());curl_easy_setopt(handle,CURLOPT_PASSWORD,pass.c_str());curl_easy_setopt(handle,CURLOPT_HTTPAUTH,CURLAUTH_BASIC);}
    curl_easy_setopt(handle,CURLOPT_WRITEFUNCTION,body_callback);curl_easy_setopt(handle,CURLOPT_WRITEDATA,&r.body);curl_easy_setopt(handle,CURLOPT_HEADERFUNCTION,header_callback);curl_easy_setopt(handle,CURLOPT_HEADERDATA,&r.etag);
    auto result=curl_easy_perform(handle);long status{};curl_easy_getinfo(handle,CURLINFO_RESPONSE_CODE,&status);curl_slist_free_all(headers);curl_easy_cleanup(handle);if(result!=CURLE_OK)throw std::runtime_error("WebDAV connection or TLS verification failed");r.status=static_cast<int>(status);return r;
}
#endif
}
Store::Store(const Json& c,const Json& secrets){auto remote=c.at("remote");if(remote.contains("directory")){directory_=fs::weakly_canonical(path(remote.at("directory").get<std::string>()))/"codex-sync-v1";return;}
    url_=remote.at("url").get<std::string>();if(url_.find('@')!=std::string::npos||url_.find('?')!=std::string::npos||url_.find('#')!=std::string::npos||url_.find('\r')!=std::string::npos||url_.find('\n')!=std::string::npos)throw std::runtime_error("WebDAV URL must not contain credentials, query or fragment");
    insecure_local_=remote.value("allow_loopback_http",false);bool local=url_.starts_with("http://127.0.0.1:")||url_.starts_with("http://localhost:");if(!url_.starts_with("https://")&&!(insecure_local_&&local))throw std::runtime_error("WebDAV requires HTTPS (HTTP allowed only for explicit loopback tests)");while(url_.ends_with('/'))url_.pop_back();url_+="/codex-sync-v1";
    user_=secrets.value("dav_user",env("CXS_DAV_USER"));password_=secrets.value("dav_password",env("CXS_DAV_PASSWORD"));if(user_.find(':')!=std::string::npos)throw std::runtime_error("invalid WebDAV username");
}
Response Store::request(const std::string& method,const std::string& relative,const Bytes& data,const std::string& condition){
    if(!safe_relative(relative))throw std::runtime_error("invalid repository path");
    if(!directory_.empty()){auto p=directory_/path(relative);Response r;
        if(method=="MKCOL"){if(fs::exists(p))return {405,{},{}};fs::create_directories(p);return {201,{},{}};}
        if(method=="GET"||method=="HEAD"){if(!fs::exists(p))return {404,{},{}};r.body=read_bytes(p);r.etag=hex_etag(r.body);r.status=200;if(method=="HEAD")r.body.clear();return r;}
        if(method=="PUT"){if(condition=="*"&&fs::exists(p))return {412,{},{}};if(!condition.empty()&&condition!="*"&&(!fs::exists(p)||hex_etag(read_bytes(p))!=condition))return {412,{},{}};write_atomic(p,data);return {201,{},hex_etag(data)};}
        throw std::runtime_error("unsupported directory-store operation");
    }
    for(int attempt=0;;++attempt){auto r=http(url_+"/"+relative,method,data,condition,user_,password_);if(attempt<2&&(r.status==429||r.status==502||r.status==503||r.status==504)&&condition!=""){std::this_thread::sleep_for(std::chrono::milliseconds(300*(attempt+1)));continue;}return r;}
}
void Store::ensure(){if(!directory_.empty())fs::create_directories(directory_);else{auto r=http(url_,"MKCOL",{},"",user_,password_);if(r.status!=201&&r.status!=405)throw std::runtime_error("cannot create WebDAV repository: HTTP "+std::to_string(r.status));}
    for(auto d:{"objects","snapshots","heads"}){auto r=request("MKCOL",d);if(r.status!=201&&r.status!=405)throw std::runtime_error("cannot create WebDAV collection: HTTP "+std::to_string(r.status));}}
std::vector<std::string> Store::list(const std::string& directory){std::vector<std::string> out;if(!directory_.empty()){if(!fs::exists(directory_/directory))return out;for(auto const& e:fs::directory_iterator(directory_/directory)){if(e.is_regular_file())out.push_back(utf8(e.path().filename()));}return out;}
    auto r=request("PROPFIND",directory+"/");if(r.status==404)return out;if(r.status!=207)throw std::runtime_error("WebDAV listing failed: HTTP "+std::to_string(r.status));tinyxml2::XMLDocument doc;if(doc.Parse(reinterpret_cast<const char*>(r.body.data()),r.body.size())!=tinyxml2::XML_SUCCESS)throw std::runtime_error("invalid WebDAV XML");std::vector<std::string> hrefs;collect_hrefs(doc.RootElement(),hrefs);for(auto h:hrefs){if(h.empty()||h.back()=='/')continue;auto i=h.find_last_of('/');auto name=h.substr(i==std::string::npos?0:i+1);if(safe_relative(name)&&name.find('/')==std::string::npos)out.push_back(name);}return out;
}
Bytes Store::get(const std::string& relative){auto r=request("GET",relative);if(r.status!=200)throw std::runtime_error("WebDAV download failed: HTTP "+std::to_string(r.status));return r.body;}
bool Store::put_immutable(const std::string& relative,const Bytes& data){auto h=request("HEAD",relative);if(h.status==200)return false;if(h.status!=404)throw std::runtime_error("WebDAV object check failed: HTTP "+std::to_string(h.status));auto r=request("PUT",relative,data,"*");if(r.status==412)return false;if(r.status!=200&&r.status!=201&&r.status!=204)throw std::runtime_error("WebDAV upload failed: HTTP "+std::to_string(r.status));return true;}
}
