#include "core.hpp"
#include "codex_sync.h"
#include <iostream>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
int main(int argc,char** argv){try{
    std::vector<std::string> args;
#ifdef _WIN32
    int count{};auto wide=CommandLineToArgvW(GetCommandLineW(),&count);for(int i=0;i<count;++i)args.push_back(cxs::utf8(std::filesystem::path(wide[i])));LocalFree(wide);SetConsoleOutputCP(CP_UTF8);
#else
    for(int i=0;i<argc;++i)args.push_back(argv[i]);
#endif
    if(args.size()<2||args[1]=="help"||args[1]=="--help"){std::cout<<"CodexSync 0.1.0 (C++23)\n"
        "  discover                     list known data roots (metadata only)\n"
        "  init --config FILE           write discovered roots; edit remote URL\n"
        "  keygen --output KEYFILE      create private master key; never overwrite\n"
        "  scan --config FILE           file and history-index inventory, no messages or secrets read\n"
        "  conversations --config FILE  audit active + archived rollouts, compressed archives and indexes\n"
        "  backup --config FILE         encrypted incremental snapshot, safe while live\n"
        "  sync --config FILE --offline [--dry-run]  merge without deletion; requires closed Codex\n"
        "  history --config FILE        list encrypted repository snapshots\n"
        "  restore --config FILE --snapshot ID --output NEW_DIRECTORY\n"
        "  restore --config FILE --snapshot ID --apply --offline\n"
        "  rollback --config FILE --snapshot ID --output NEW_DIRECTORY\n"
        "  serve --config FILE [--port 17841]  authenticated loopback HTTP API\n"
        "Secrets: CXS_KEY_FILE, CXS_DAV_USER, CXS_DAV_PASSWORD, CXS_API_TOKEN\n";return 0;}
    cxs::Json req={{"op",args[1]}};int port=17841;for(size_t i=2;i<args.size();++i){auto a=args[i];if(a=="--offline")req["offline"]=true;else if(a=="--apply")req["apply"]=true;else if(a=="--dry-run")req["dry_run"]=true;else if((a=="--config"||a=="--output"||a=="--snapshot"||a=="--port")&&i+1<args.size()){auto v=args[++i];if(a=="--port")port=std::stoi(v);else req[a.substr(2)]=v;}else throw std::runtime_error("unknown or incomplete argument: "+a);}
    if(args[1]=="keygen"){cxs::generate_key(cxs::path(req.at("output")));std::cout<<"{\"ok\":true,\"key_created\":true}\n";return 0;}
    if(args[1]=="init"){auto file=cxs::path(req.at("config"));if(std::filesystem::exists(file))throw std::runtime_error("configuration already exists");auto s=cxs::discover().dump(2);cxs::write_atomic(file,cxs::Bytes(s.begin(),s.end()));std::cout<<"{\"ok\":true,\"config_created\":true}\n";return 0;}
    if(args[1]=="serve"){cxs::serve(cxs::path(req.at("config")),port);return 0;}
    auto s=req.dump();char* result=nullptr;int rc=cxs_run(s.c_str(),&result);if(result){std::cout<<result<<'\n';cxs_free(result);}return rc;
}catch(const std::exception& e){std::cout<<cxs::Json{{"ok",false},{"error",e.what()}}.dump()<<'\n';return 1;}}
