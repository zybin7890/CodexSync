#include "core.hpp"
#include "codex_sync.h"
#include "disclaimer.hpp"
#include "google_drive.hpp"
#include <sodium.h>
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
    if(args.size()<2||args[1]=="help"||args[1]=="--help"){std::cout<<"CodexSync 0.2.0 (C++23)\n"
        "  discover                     list known data roots (metadata only)\n"
        "  paths                        show installed/portable data locations\n"
        "  catalog                      identify chats, projects, plugins and skills (metadata)\n"
        "  init [--config FILE]         write discovered roots; edit remote URL\n"
        "  keygen --output KEYFILE      create private master key; never overwrite\n"
        "  scan --config FILE           file and history-index inventory, no messages or secrets read\n"
        "  conversations --config FILE  audit active + archived rollouts, compressed archives and indexes\n"
        "  progress --config FILE       show latest task progress as JSON\n"
        "  resume --config FILE         continue the durable prepared snapshot\n"
        "  pause|cancel --config FILE --job-id ID  control the current task\n"
        "  backup --config FILE         incremental snapshot using configured encryption mode\n"
        "  sync --config FILE --offline [--dry-run]  merge without deletion; requires closed Codex\n"
        "  history --config FILE        list repository snapshots\n"
        "  restore --config FILE --snapshot ID --output NEW_DIRECTORY\n"
        "  restore --config FILE --snapshot ID --apply --offline\n"
        "  rollback --config FILE --snapshot ID --output NEW_DIRECTORY\n"
        "  serve --config FILE [--port 12306]  authenticated loopback HTTP API\n"
        "  google-login --config FILE   authorize Google Drive in your browser (120s)\n"
        "  google-client-import --config FILE  encrypt desktop client JSON from stdin\n"
        "  disclaimer [--lang en|zh_CN]  read risks and warranty/liability notice\n"
        "Secrets: CXS_KEY_FILE, CXS_KEY_PASSWORD (optional), CXS_DAV_USER, CXS_DAV_PASSWORD, CXS_API_TOKEN\n"
        "Google: CXS_GOOGLE_CLIENT_ID, CXS_GOOGLE_CLIENT_SECRET (optional),\n"
        "        CXS_GOOGLE_ACCESS_TOKEN or CXS_GOOGLE_REFRESH_TOKEN (optional overrides)\n"
        "Preview software: no warranty to the extent permitted by law. Read DISCLAIMER.md.\n";return 0;}
    if(args[1]=="disclaimer"){auto english=args.size()==4&&args[2]=="--lang"&&args[3]=="en";if(args.size()!=2&&!english&&!(args.size()==4&&args[2]=="--lang"&&args[3]=="zh_CN"))throw std::runtime_error("disclaimer [--lang en|zh_CN]");std::cout<<(english?cxs::disclaimer_en_text:cxs::disclaimer_text);return 0;}
    cxs::Json req={{"op",args[1]}};int port=12306;for(size_t i=2;i<args.size();++i){auto a=args[i];if(a=="--offline")req["offline"]=true;else if(a=="--apply")req["apply"]=true;else if(a=="--dry-run")req["dry_run"]=true;else if((a=="--config"||a=="--output"||a=="--snapshot"||a=="--port"||a=="--job-id")&&i+1<args.size()){auto v=args[++i];if(a=="--port")port=std::stoi(v);else req[a=="--job-id"?"job_id":a.substr(2)]=v;}else throw std::runtime_error("unknown or incomplete argument: "+a);}
    if(!req.contains("config"))req["config"]=cxs::utf8(cxs::default_configuration_path());
    if(args[1]=="keygen"){cxs::generate_key(cxs::path(req.value("output",cxs::utf8(cxs::default_key_path()))),cxs::env("CXS_KEY_PASSWORD"));std::cout<<"{\"ok\":true,\"key_created\":true}\n";return 0;}
    if(args[1]=="init"){auto file=cxs::path(req.at("config"));if(std::filesystem::exists(file))throw std::runtime_error("configuration already exists");auto s=cxs::discover().dump(2);cxs::write_atomic(file,cxs::Bytes(s.begin(),s.end()));std::cout<<"{\"ok\":true,\"config_created\":true}\n";return 0;}
    if(args[1]=="serve"){cxs::serve(cxs::path(req.at("config")),port);return 0;}
    if(args[1]=="google-client-import"){
        auto config=cxs::Json::parse(cxs::read_bytes(cxs::path(req.at("config")),1024*1024));cxs::normalize_application_config(config);
        cxs::Bytes profile; char value;
        try{while(std::cin.get(value)){if(profile.size()>=64*1024)throw std::runtime_error("Google client profile is too large");profile.push_back(static_cast<unsigned char>(value));}
            auto result=cxs::google_import_client(config,req,profile);sodium_memzero(profile.data(),profile.size());std::cout<<result.dump()<<'\n';return 0;
        }catch(...){sodium_memzero(profile.data(),profile.size());throw;}
    }
    if(args[1]=="google-login"){
        auto data=cxs::read_bytes(cxs::path(req.at("config")),1024*1024);auto config=cxs::Json::parse(data.begin(),data.end());cxs::normalize_application_config(config);
        auto result=cxs::google_authorize(config,req,[](const std::string& url){std::cout<<"Open this authorization URL in your browser on this computer:\n"<<url<<'\n'<<std::flush;});
        std::cout<<result.dump()<<'\n';return 0;
    }
    auto s=req.dump();char* result=nullptr;int rc=cxs_run(s.c_str(),&result);if(result){std::cout<<result<<'\n';cxs_free(result);}return rc;
}catch(const std::exception& e){std::cout<<cxs::Json{{"ok",false},{"error",e.what()}}.dump()<<'\n';return 1;}}
