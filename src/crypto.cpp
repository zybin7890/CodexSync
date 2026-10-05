#include "core.hpp"
#include <sodium.h>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <aclapi.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#endif
namespace cxs {
static void init() { static int ok=sodium_init(); if(ok<0) throw std::runtime_error("crypto initialization failed"); }
fs::path path(const std::string& s) { return fs::u8path(s); }
std::string utf8(const fs::path& p) { auto u=p.generic_u8string(); return {reinterpret_cast<const char*>(u.data()),u.size()}; }
std::string env(const char* n) {
#ifdef _WIN32
    auto name=path(n).wstring(); auto count=GetEnvironmentVariableW(name.c_str(),nullptr,0);
    if(!count) return {}; std::wstring value(count,L'\0'); GetEnvironmentVariableW(name.c_str(),value.data(),count); value.resize(count-1); return utf8(fs::path(value));
#else
    auto v=std::getenv(n); return v ? v : "";
#endif
}
std::string random_id() { init(); unsigned char b[16]; randombytes_buf(b,sizeof b); char out[33]; sodium_bin2hex(out,sizeof out,b,sizeof b); return out; }
static void secure(const fs::path& p) {
#ifdef _WIN32
    HANDLE token{}; if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) throw std::runtime_error("cannot identify local user");
    DWORD size{}; GetTokenInformation(token,TokenUser,nullptr,0,&size); Bytes data(size);
    if(!GetTokenInformation(token,TokenUser,data.data(),size,&size)){CloseHandle(token);throw std::runtime_error("cannot read local user identity");} CloseHandle(token);
    EXPLICIT_ACCESSW ea{}; ea.grfAccessPermissions=GENERIC_ALL; ea.grfAccessMode=SET_ACCESS; ea.grfInheritance=SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    ea.Trustee.TrusteeForm=TRUSTEE_IS_SID; ea.Trustee.TrusteeType=TRUSTEE_IS_USER; ea.Trustee.ptstrName=static_cast<LPWSTR>(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid);
    PACL acl{}; if(SetEntriesInAclW(1,&ea,nullptr,&acl)!=ERROR_SUCCESS) throw std::runtime_error("cannot build private ACL");
    auto result=SetNamedSecurityInfoW(const_cast<LPWSTR>(p.c_str()),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,nullptr,nullptr,acl,nullptr); LocalFree(acl);
    if(result!=ERROR_SUCCESS) throw std::runtime_error("cannot protect local file");
#else
    if(chmod(p.c_str(),fs::is_directory(p)?0700:0600)!=0) throw std::runtime_error("cannot protect local file");
#endif
}
void private_directory(const fs::path& p) { fs::create_directories(p); if(fs::is_symlink(fs::symlink_status(p)))throw std::runtime_error("private directory must not be a symlink"); secure(p); }
Bytes read_bytes(const fs::path& p,size_t limit) {
    std::ifstream in(p,std::ios::binary); if(!in)throw std::runtime_error("cannot open file: "+utf8(p));
    auto n=fs::file_size(p); if(n>limit)throw std::runtime_error("file exceeds supported size"); Bytes out(static_cast<size_t>(n));
    if(!out.empty()&&!in.read(reinterpret_cast<char*>(out.data()),static_cast<std::streamsize>(out.size())))throw std::runtime_error("incomplete file read"); return out;
}
void write_atomic(const fs::path& p,const Bytes& data,bool priv) {
    fs::create_directories(p.parent_path()); auto temp=p; temp += ".cxs-new-"+random_id();
    try {
        {std::ofstream out(temp,std::ios::binary|std::ios::trunc); if(!out)throw std::runtime_error("cannot create output"); if(priv)secure(temp); out.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));out.flush();if(!out)throw std::runtime_error("cannot flush output");}
#ifdef _WIN32
        HANDLE h=CreateFileW(temp.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr); if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("cannot sync output"); bool ok=FlushFileBuffers(h)!=0;CloseHandle(h);if(!ok)throw std::runtime_error("cannot sync output");
        if(!MoveFileExW(temp.c_str(),p.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("atomic replacement failed");
#else
        int fd=::open(temp.c_str(),O_RDONLY);if(fd<0||fsync(fd)!=0){if(fd>=0)::close(fd);throw std::runtime_error("cannot sync output");}::close(fd);fs::rename(temp,p);
        fd=::open(p.parent_path().c_str(),O_RDONLY|O_DIRECTORY);if(fd>=0){fsync(fd);::close(fd);}
#endif
    }catch(...){std::error_code ec;fs::remove(temp,ec);throw;}
}
std::string digest(const Bytes& b,const Key& k) { init(); unsigned char hash[32];crypto_generichash(hash,sizeof hash,b.data(),b.size(),k.data(),k.size());char text[65];sodium_bin2hex(text,sizeof text,hash,sizeof hash);return text; }
Bytes seal(const Bytes& b,const Key& k,const std::string& domain) {
    init();Bytes out(4+24+b.size()+crypto_aead_xchacha20poly1305_ietf_ABYTES);out[0]='C';out[1]='X';out[2]='S';out[3]=1;randombytes_buf(out.data()+4,24);unsigned long long n{};
    crypto_aead_xchacha20poly1305_ietf_encrypt(out.data()+28,&n,b.data(),b.size(),reinterpret_cast<const unsigned char*>(domain.data()),domain.size(),nullptr,out.data()+4,k.data());out.resize(28+static_cast<size_t>(n));return out;
}
Bytes open(const Bytes& b,const Key& k,const std::string& domain) {
    init();if(b.size()<44||b[0]!='C'||b[1]!='X'||b[2]!='S'||b[3]!=1)throw std::runtime_error("invalid encrypted object");Bytes out(b.size()-44);unsigned long long n{};
    if(crypto_aead_xchacha20poly1305_ietf_decrypt(out.data(),&n,nullptr,b.data()+28,b.size()-28,reinterpret_cast<const unsigned char*>(domain.data()),domain.size(),b.data()+4,k.data())!=0)throw std::runtime_error("authentication failed: wrong key or damaged object");out.resize(static_cast<size_t>(n));return out;
}
void generate_key(const fs::path& p) {init();if(fs::exists(p))throw std::runtime_error("refusing to overwrite an existing key");Key k;randombytes_buf(k.data(),k.size());char s[65];sodium_bin2hex(s,sizeof s,k.data(),k.size());Bytes b(s,s+64);b.push_back('\n');write_atomic(p,b);sodium_memzero(k.data(),k.size());sodium_memzero(s,sizeof s);}
}
