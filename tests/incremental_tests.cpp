#include "core.hpp"
#include "snapshot_job.hpp"
#include <sqlite3.h>
#include <fstream>
#include <iostream>
using cxs::Json;
namespace fs=std::filesystem;
static void check(bool condition,const char* reason){if(!condition)throw std::runtime_error(reason);}
static void text(const fs::path& p,const std::string& value){fs::create_directories(p.parent_path());std::ofstream out(p,std::ios::binary|std::ios::trunc);out<<value;check(bool(out),"fixture write failed");}
int main(int argc,char** argv){try{
    const auto base=fs::absolute(cxs::path(argc>1?argv[1]:"incremental-contracts"))/cxs::random_id();
    const auto source=base/"source",state=base/"state",config_file=base/"config.json";
    text(source/"one.txt","alpha");text(source/"two.txt","bravo");
    Json config={{"format",1},{"device",cxs::random_id()},{"state",cxs::utf8(state)},{"roots",Json::array({{{"id","fixture"},{"path",cxs::utf8(source)}}})},{"remote",{{"provider","local"},{"directory",cxs::utf8(base/"remote")}}}};
    auto save=[&]{text(config_file,config.dump());};save();
    auto request=[&](const std::string& op){return Json{{"op",op},{"config",cxs::utf8(config_file)},{"key_hex",std::string(64,'a')}};};
    auto first=cxs::execute(request("backup"));check(first.at("body_files")==2&&first.at("uploaded_objects")==2,"first capture omitted files");
    auto second=cxs::execute(request("backup"));check(second.at("no_changes")==true&&second.at("body_bytes")==0&&second.at("source_probe_bytes")==0&&second.at("uploaded_objects")==0&&second.at("reused_files")==2,"unchanged backup read source bodies or submitted snapshot");
    check(cxs::execute(request("history")).at("snapshots").size()==1,"no-op created duplicate snapshot");
    auto catalog_file=fs::directory_iterator(state/"catalogs")->path();text(catalog_file,"damaged derived index");auto recovered=cxs::execute(request("backup"));check(recovered.at("no_changes")==true&&recovered.at("body_bytes")==0,"durable completed capture could not rebuild damaged index");
    auto cached_file=fs::directory_iterator(state/"cache")->path();fs::rename(cached_file,base/"retained-cache.cxs");auto repaired=cxs::execute(request("backup"));check(repaired.at("no_changes")==true&&repaired.at("body_files")==1,"missing cached block did not recapture its source file");
    const auto time=fs::last_write_time(source/"one.txt");text(source/"one.txt","ALPHA");fs::last_write_time(source/"one.txt",time);
    auto changed=cxs::execute(request("backup"));check(changed.at("body_files")==1&&changed.at("body_bytes")==5&&changed.at("reused_files")==1,"same-size rewrite with restored mtime was missed");
    auto empty=request("restore");empty["snapshot"]=changed.at("snapshot");empty["output"]=cxs::utf8(base/"restore");
    auto empty_config=config;empty_config["state"]=cxs::utf8(base/"empty-state");text(base/"empty.json",empty_config.dump());empty["config"]=cxs::utf8(base/"empty.json");cxs::execute(empty);check(cxs::read_bytes(base/"restore/fixture/one.txt")==cxs::Bytes({'A','L','P','H','A'}),"empty-cache restore differs");
    text(source/"replacement","OMEGA");fs::last_write_time(source/"replacement",time);fs::rename(source/"one.txt",source/"old.txt");fs::rename(source/"replacement",source/"one.txt");
    auto replaced=cxs::execute(request("backup"));check(replaced.at("body_files").get<size_t>()>=1,"same-name replacement was missed");
    fs::rename(source/"two.txt",source/"renamed.txt");auto renamed=cxs::execute(request("backup"));check(!renamed.value("no_changes",false),"rename was missed");
    fs::rename(source/"old.txt",base/"removed.txt");auto removed=cxs::execute(request("backup"));check(!removed.value("no_changes",false)&&removed.at("body_bytes")==0,"deletion required full content capture");
    auto wrong=request("backup");wrong["key_hex"]=std::string(64,'b');bool rejected=false;try{cxs::execute(wrong);}catch(...){rejected=true;}check(rejected,"wrong master key treated as an empty baseline");
    sqlite3* db=nullptr;check(sqlite3_open(cxs::utf8(source/"family.sqlite").c_str(),&db)==SQLITE_OK,"SQLite fixture open failed");
    check(sqlite3_exec(db,"PRAGMA journal_mode=WAL; PRAGMA user_version=29; CREATE TABLE sample(value); INSERT INTO sample VALUES(1);",nullptr,nullptr,nullptr)==SQLITE_OK,"SQLite fixture setup failed");
    auto sql_first=cxs::execute(request("backup"));auto sql_second=cxs::execute(request("backup"));check(sql_second.at("body_bytes")==0,"unchanged SQLite family was read");
    check(sqlite3_exec(db,"INSERT INTO sample VALUES(2)",nullptr,nullptr,nullptr)==SQLITE_OK,"WAL-only write failed");auto sql_changed=cxs::execute(request("backup"));check(sql_changed.at("body_files")==1,"WAL-only commit did not capture exactly one family");sqlite3_close(db);
    auto out=request("restore");out["snapshot"]=sql_changed.at("snapshot");out["output"]=cxs::utf8(base/"sqlite-restored");cxs::execute(out);
    check(sqlite3_open(cxs::utf8(base/"sqlite-restored/fixture/family.sqlite").c_str(),&db)==SQLITE_OK,"restored SQLite open failed");sqlite3_stmt* stmt=nullptr;
    sqlite3_prepare_v2(db,"SELECT count(*) FROM sample",-1,&stmt,nullptr);check(sqlite3_step(stmt)==SQLITE_ROW&&sqlite3_column_int(stmt,0)==2,"WAL data lost");sqlite3_finalize(stmt);
    sqlite3_prepare_v2(db,"PRAGMA integrity_check",-1,&stmt,nullptr);check(sqlite3_step(stmt)==SQLITE_ROW&&std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt,0)))=="ok","restored database corrupt");sqlite3_finalize(stmt);
    sqlite3_prepare_v2(db,"PRAGMA user_version",-1,&stmt,nullptr);check(sqlite3_step(stmt)==SQLITE_ROW&&sqlite3_column_int(stmt,0)==29,"user_version changed");sqlite3_finalize(stmt);sqlite3_close(db);
    // Store creation fails after capture; restart uses durable capture even when
    // the source no longer exists, proving no source scan or VSS is attempted.
    config["state"]=cxs::utf8(base/"resume-state");config["remote"]["directory"]=cxs::utf8(base/"blocked-remote");save();text(base/"blocked-remote/codex-sync-v1","blocker");
    rejected=false;try{cxs::execute(request("backup"));}catch(...){rejected=true;}check(rejected&&fs::exists(base/"resume-state/jobs/active.cxs"),"capture was not durable before network failure");
    auto original_remote=config["remote"];config["remote"]["directory"]=cxs::utf8(base/"wrong-target");save();rejected=false;try{cxs::execute(request("resume"));}catch(...){rejected=true;}check(rejected&&!fs::exists(base/"wrong-target/codex-sync-v1"),"prepared task was sent to another repository");config["remote"]=original_remote;save();
    fs::rename(base/"blocked-remote/codex-sync-v1",base/"blocker-retained");fs::rename(source,base/"source-away");
    auto resumed=cxs::execute(request("resume"));check(resumed.at("resumed")==true&&resumed.at("body_bytes")==0&&resumed.at("metadata_checked")==0,"resume scanned live source");
    fs::rename(base/"source-away",source);text(source/"one.txt","NEXT!");auto next=cxs::execute(request("backup"));check(next.at("snapshot")!=resumed.at("snapshot")&&next.at("body_files").get<size_t>()<=2,"next capture was mixed into prepared snapshot");
    Json report={{"first",first},{"unchanged",second},{"single_change",changed},{"wal_change",sql_changed},{"resumed",resumed},{"next",next},{"fixture",cxs::utf8(base)}};auto encoded=report.dump(2);cxs::write_atomic(base/"report.json",{encoded.begin(),encoded.end()});
    std::cout<<"INCREMENTAL_CONTRACTS_PASS: no-op, single change, identity traps, deletion, WAL family, isolated restore, durable resume\nEvidence: "<<cxs::utf8(base/"report.json")<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"INCREMENTAL_CONTRACTS_FAIL: "<<e.what()<<'\n';return 1;}}
