#include "core.hpp"
#include "codex_sync.h"
#include "httplib.h"
#include <sodium.h>
#include <iostream>
namespace cxs {
void serve(const fs::path& config,int port){
    if(port<1024||port>65535)throw std::runtime_error("API port must be 1024..65535");auto token=env("CXS_API_TOKEN");if(token.size()<32)throw std::runtime_error("set CXS_API_TOKEN to at least 32 random characters");
    httplib::Server server;server.set_payload_max_length(16*1024);server.set_read_timeout(5,0);server.set_write_timeout(120,0);server.new_task_queue=[](){return new httplib::ThreadPool(2);};
    server.set_pre_routing_handler([&](const httplib::Request& r,httplib::Response& out){auto auth=r.get_header_value("Authorization");auto expected="Bearer "+token;if(auth.size()!=expected.size()||sodium_memcmp(auth.data(),expected.data(),expected.size())){out.status=401;out.set_content("{\"ok\":false,\"error\":\"unauthorized\"}","application/json");return httplib::Server::HandlerResponse::Handled;}
        auto host=r.get_header_value("Host");if(!r.get_header_value("Origin").empty()||(host!="127.0.0.1:"+std::to_string(port)&&host!="localhost:"+std::to_string(port))){out.status=403;out.set_content("{\"ok\":false,\"error\":\"untrusted origin or host\"}","application/json");return httplib::Server::HandlerResponse::Handled;}return httplib::Server::HandlerResponse::Unhandled;});
    server.Get("/v1/health",[](const auto&,auto& out){out.set_content("{\"ok\":true,\"version\":\"0.3.0\"}","application/json");});
    server.Post("/v1/run",[&](const httplib::Request& r,httplib::Response& out){try{auto request=Json::parse(r.body);auto op=request.at("op").get<std::string>();if(op!="scan"&&op!="backup"&&op!="sync"&&op!="history"&&op!="restore"&&op!="rollback"&&op!="disclaimer")throw std::runtime_error("operation not exposed by API");request["config"]=utf8(config);request.erase("key_hex");request.erase("dav_user");request.erase("dav_password");auto input=request.dump();char* result=nullptr;int code=cxs_run(input.c_str(),&result);if(!result)throw std::runtime_error("result allocation failed");out.status=code?409:200;out.set_content(result,"application/json");cxs_free(result);}catch(...){out.status=400;out.set_content("{\"ok\":false,\"error\":\"invalid request\"}","application/json");}});
    std::cout<<"API listening on http://127.0.0.1:"<<port<<" (Bearer token required)\n"<<std::flush;
    if(!server.listen("127.0.0.1",port))throw std::runtime_error("cannot bind loopback API");
}
}
