#pragma once
#include "core.hpp"
#include <algorithm>
#include <cctype>
#ifdef _WIN32
#include <windows.h>
#endif
namespace cxs {
constexpr size_t chunk_size=4*1024*1024;
inline Bytes bytes(const Json& j){auto s=j.dump();return Bytes(s.begin(),s.end());}
inline Json json(const Bytes& b){return Json::parse(b.begin(),b.end());}
inline bool id_ok(const std::string& s,size_t length=64){return s.size()==length&&std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
inline void require_id(const std::string& s,size_t n=64){if(!id_ok(s,n))throw std::runtime_error("invalid snapshot, object or device ID");}
inline bool inside(const fs::path& a,const fs::path& b){auto x=fs::weakly_canonical(a),y=fs::weakly_canonical(b);auto i=x.begin(),j=y.begin();for(;j!=y.end();++i,++j){if(i==x.end())return false;
#ifdef _WIN32
    auto left=i->wstring(),right=j->wstring();if(_wcsicmp(left.c_str(),right.c_str()))return false;
#else
    if(*i!=*j)return false;
#endif
}return true;}
inline bool link(const fs::path& p){
#ifdef _WIN32
    auto a=GetFileAttributesW(p.c_str());return a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_REPARSE_POINT);
#else
    return fs::is_symlink(fs::symlink_status(p));
#endif
}
inline fs::path safe_target(const fs::path& root,const std::string& relative){
    if(relative.empty()||relative.find('\\')!=std::string::npos||relative.find(':')!=std::string::npos||relative.find('\0')!=std::string::npos)throw std::runtime_error("unsafe manifest path");
    auto p=path(relative);if(p.is_absolute()||p.has_root_name())throw std::runtime_error("absolute manifest path rejected");
    for(auto& part:p){auto s=utf8(part);if(s=="."||s==".."||s.empty()||s.ends_with('.')||s.ends_with(' '))throw std::runtime_error("unsafe manifest component");
#ifdef _WIN32
        auto base=s.substr(0,s.find('.'));std::transform(base.begin(),base.end(),base.begin(),[](unsigned char c){return std::toupper(c);});if(base=="CON"||base=="PRN"||base=="AUX"||base=="NUL"||(base.size()==4&&(base.starts_with("COM")||base.starts_with("LPT"))&&base[3]>='1'&&base[3]<='9'))throw std::runtime_error("reserved Windows filename");
#endif
    }
    if(link(root))throw std::runtime_error("root is a symlink or junction");auto result=root/p,current=root;for(auto& part:p){current/=part;if(fs::exists(current)&&link(current))throw std::runtime_error("symlink or junction in target path");}if(!inside(result,root))throw std::runtime_error("manifest path escapes root");return result;
}
inline bool volatile_file(const std::string& p){auto name=utf8(path(p).filename());return name=="SingletonLock"||name=="SingletonSocket"||name=="SingletonCookie"||name=="LOCK"||name.ends_with(".lock")||name.ends_with("-wal")||name.ends_with("-shm");}

}
