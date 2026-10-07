#include "core.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

namespace cxs {
namespace {
std::string identity(const fs::path& p) {
    auto value = utf8(fs::weakly_canonical(p));
#ifdef _WIN32
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c){return static_cast<char>(std::tolower(c));});
#endif
    return value;
}
bool beneath(const fs::path& file, const fs::path& parent) {
    const auto a=identity(file), b=identity(parent);
    return a==b || a.starts_with(b+"/");
}
bool linked(const fs::path& p) {
#ifdef _WIN32
    const auto attributes=GetFileAttributesW(p.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT);
#else
    return fs::is_symlink(fs::symlink_status(p));
#endif
}
std::string text(sqlite3_stmt* stmt,int column) {
    const auto value=sqlite3_column_text(stmt,column);
    return value?std::string(reinterpret_cast<const char*>(value)):std::string{};
}
struct Database {
    std::unique_ptr<sqlite3,decltype(&sqlite3_close)> db{nullptr,sqlite3_close};
    explicit Database(const fs::path& file) {
        sqlite3* raw{};const auto result=sqlite3_open_v2(utf8(file).c_str(),&raw,SQLITE_OPEN_READONLY,nullptr);db.reset(raw);
        if(result!=SQLITE_OK)throw std::runtime_error("cannot read catalog database");
        sqlite3_busy_timeout(raw,1000);
    }
    template<class F> void rows(const std::string& sql,F consume) {
        sqlite3_stmt* raw{};
        if(sqlite3_prepare_v2(db.get(),sql.c_str(),-1,&raw,nullptr)!=SQLITE_OK)throw std::runtime_error("unsupported catalog schema");
        std::unique_ptr<sqlite3_stmt,decltype(&sqlite3_finalize)> stmt(raw,sqlite3_finalize);
        int result{};while((result=sqlite3_step(raw))==SQLITE_ROW)consume(raw);
        if(result!=SQLITE_DONE)throw std::runtime_error("catalog read interrupted");
    }
    std::set<std::string> columns(const char* table) {
        std::set<std::string> result; rows(std::string("PRAGMA table_info(")+table+")",[&](auto s){result.insert(text(s,1));});return result;
    }
};
fs::path thread_database(const fs::path& folder) {
    int newest=-1;fs::path result;
    if(!fs::is_directory(folder))return result;
    for(const auto& entry:fs::directory_iterator(folder)) {
        const auto name=utf8(entry.path().filename());
        if(!entry.is_regular_file()||!name.starts_with("state_")||!name.ends_with(".sqlite"))continue;
        const auto number=name.substr(6,name.size()-13);
        if(number.empty()||number.size()>6||!std::all_of(number.begin(),number.end(),[](unsigned char c){return std::isdigit(c);}))continue;
        const auto version=std::stoi(number);if(version>newest){newest=version;result=entry.path();}
    }
    return result;
}
std::string rollout_id(const fs::path& file,const std::string& fallback) {
    const auto id=rollout_thread_id(file);
    return id.empty()?fallback:id;
}
bool rollout(const fs::path& file) {
    const auto name=utf8(file.filename());return name.starts_with("rollout-")&&(name.ends_with(".jsonl")||name.ends_with(".jsonl.zst"));
}
}
std::string rollout_thread_id(const fs::path& file) {
    auto name=utf8(file.filename());
    if(!name.starts_with("rollout-"))return {};
    if(name.ends_with(".zst"))name.resize(name.size()-4);
    if(name.ends_with(".jsonl"))name.resize(name.size()-6);
    if(name.size()>=36) {
        const auto id=name.substr(name.size()-36);
        if(id[8]=='-'&&id[13]=='-'&&id[18]=='-'&&id[23]=='-'&&std::all_of(id.begin(),id.end(),[](unsigned char c){return c=='-'||std::isxdigit(c);}))return id;
    }
    return {};
}
Json selection_catalog(const Json& config) {
    Json result={{"conversations",Json::array()},{"projects",Json::array()},{"plugins",Json::array()},{"skills",Json::array()},{"warnings",Json::array()}};
    std::map<std::string,Json> threads,threads_by_id,projects,conversations;
    std::map<std::string,std::string> assignments,hints;
    std::set<std::string> projectless,seen_files,seen_packages;
    const auto warn=[&](const fs::path& p,const std::string& reason){result["warnings"].push_back({{"path",utf8(p)},{"reason",reason}});};
    // Only metadata fields are consumed. Session bodies, credentials and skill instructions are never parsed.
    for(const auto& root:config.at("roots")) {
        const auto folder=path(root.at("path").get<std::string>());
        const auto global=folder/".codex-global-state.json";
        if(fs::is_regular_file(global))try {
            const auto state=Json::parse(read_bytes(global,16*1024*1024));
            if(state.contains("local-projects")&&state["local-projects"].is_object())for(const auto& [id,item]:state["local-projects"].items()) {
                if(!item.is_object())continue;
                Json paths=Json::array();for(const auto& p:item.value("rootPaths",Json::array()))if(p.is_string()&&path(p.get<std::string>()).is_absolute())paths.push_back(p);
                projects[id]={{"id",id},{"title",item.value("name",id)},{"paths",paths},{"chats",0}};
            }
            for(const auto& id:state.value("projectless-thread-ids",Json::array()))if(id.is_string())projectless.insert(id.get<std::string>());
            if(state.contains("thread-project-assignments")&&state["thread-project-assignments"].is_object())for(const auto& [id,item]:state["thread-project-assignments"].items())if(item.is_object()&&item.contains("projectId")&&item["projectId"].is_string())assignments[id]=item["projectId"];
            if(state.contains("thread-workspace-root-hints")&&state["thread-workspace-root-hints"].is_object())for(const auto& [id,item]:state["thread-workspace-root-hints"].items())if(item.is_string())hints[id]=item;
        }catch(const std::exception& e){warn(global,e.what());}
        const auto database=thread_database(folder);if(database.empty())continue;
        try {
            Database db(database);const auto cols=db.columns("threads");
            if(!cols.contains("id")||!cols.contains("rollout_path"))throw std::runtime_error("thread index lacks id or rollout_path");
            const auto field=[&](const char* name,const char* fallback){return cols.contains(name)?std::string(name):std::string(fallback);};
            db.rows("SELECT id,rollout_path,"+field("title","id")+","+field("cwd","''")+","+field("archived","0")+","+field("project_id","''")+" FROM threads",[&](auto s){
                auto file=path(text(s,1));if(!file.is_absolute())file=folder/file;
                if(!fs::is_regular_file(file)&&fs::is_regular_file(path(utf8(file)+".zst")))file=path(utf8(file)+".zst");
                const auto id=text(s,0);Json item={{"id",id},{"title",text(s,2)},{"cwd",text(s,3)},{"archived",sqlite3_column_int(s,4)!=0},{"project_id",text(s,5)}};
                if(assignments.contains(id))item["project_id"]=assignments[id];
                if(hints.contains(id))item["cwd"]=hints[id];
                if(projectless.contains(id))item["project_id"]="";
                threads_by_id[id]=item;threads[identity(file)]=std::move(item);
            });
            if(!db.columns("projects").empty()&&!db.columns("project_roots").empty()) {
                db.rows("SELECT id,name FROM projects",[&](auto s){const auto id=text(s,0);projects[id]={{"id",id},{"title",text(s,1)},{"paths",Json::array()},{"chats",0}};});
                db.rows("SELECT project_id,path FROM project_roots ORDER BY position",[&](auto s){const auto id=text(s,0),p=text(s,1);if(projects.contains(id)&&path(p).is_absolute())projects[id]["paths"].push_back(p);});
            }
        }catch(const std::exception& e){warn(database,e.what());}
    }
    const auto add_package=[&](const char* category,const Json& root,const fs::path& folder,const std::string& label) {
        if(!fs::is_directory(folder)||linked(folder))return;
        const auto key=std::string(category)+":"+identity(folder);if(!seen_packages.insert(key).second)return;
        const auto relative=utf8(folder.lexically_relative(path(root.at("path").get<std::string>())));
        result[category].push_back({{"id",root.at("id").get<std::string>()+"/"+relative},{"title",label},{"detail",utf8(folder)},
                                    {"paths",Json::array({{{"root",root.at("id")},{"path",relative}}})}});
    };
    for(const auto& root:config.at("roots")) {
        const auto folder=path(root.at("path").get<std::string>());if(!fs::is_directory(folder))continue;
        const auto walk=[&](const fs::path& base,bool archived){
            if(!fs::is_directory(base))return;
            for(fs::recursive_directory_iterator it(base,fs::directory_options::skip_permission_denied),end;it!=end;++it) {
                if(linked(it->path())){if(it->is_directory())it.disable_recursion_pending();continue;}
                if(!it->is_regular_file()||!rollout(it->path())||!seen_files.insert(identity(it->path())).second)continue;
                const auto relative=utf8(it->path().lexically_relative(folder)),key=identity(it->path());
                const auto file_id=rollout_id(it->path(),root.at("id").get<std::string>()+"/"+relative);
                Json item=threads.contains(key)?threads.at(key):threads_by_id.contains(file_id)?threads_by_id.at(file_id):Json{{"id",file_id},{"title",utf8(it->path().filename())},{"cwd",""},{"project_id",""},{"archived",archived}};
                const auto id=item.at("id").get<std::string>();
                if(!conversations.contains(id)){item["available"]=true;item["paths"]=Json::array();item["detail"]=item.value("cwd",std::string{});conversations[id]=std::move(item);}
                conversations[id]["paths"].push_back({{"root",root.at("id")},{"path",relative}});
            }
        };
        walk(folder/"sessions",false);walk(folder/"archived_sessions",true);
        if(folder.filename()=="sessions"||root.at("id").get<std::string>().ends_with("_sessions"))walk(folder,false);
        if(folder.filename()=="archived_sessions"||root.at("id").get<std::string>().ends_with("_archived_sessions"))walk(folder,true);
        const auto skills=folder.filename()=="skills"?folder:folder/"skills";
        if(fs::is_directory(skills))for(const auto& entry:fs::directory_iterator(skills)) {
            if(!entry.is_directory()||linked(entry.path()))continue;
            if(fs::is_regular_file(entry.path()/"SKILL.md"))add_package("skills",root,entry.path(),utf8(entry.path().filename()));
            else if(entry.path().filename()==".system")for(const auto& nested:fs::directory_iterator(entry.path()))if(fs::is_regular_file(nested.path()/"SKILL.md"))add_package("skills",root,nested.path(),utf8(nested.path().filename())+" (system)");
        }
        const auto plugins=folder/"plugins";
        if(fs::is_directory(plugins))for(const auto& entry:fs::directory_iterator(plugins)) {
            if(!entry.is_directory()||linked(entry.path()))continue;
            if(entry.path().filename()=="cache")for(const auto& vendor:fs::directory_iterator(entry.path())) {
                if(!vendor.is_directory()||linked(vendor.path()))continue;
                for(const auto& plugin:fs::directory_iterator(vendor.path()))add_package("plugins",root,plugin.path(),utf8(plugin.path().filename())+" · "+utf8(vendor.path().filename()));
            }else if(entry.path().filename()!="marketplaces")add_package("plugins",root,entry.path(),utf8(entry.path().filename()));
        }
    }
    for(const auto& [key,thread]:threads)if(!seen_files.contains(key)&&!conversations.contains(thread.at("id"))) {
        auto item=thread;const auto id=item.at("id").get<std::string>();item["paths"]=Json::array();item["available"]=false;conversations[id]=std::move(item);
    }
    for(auto& [id,item]:conversations) {
        auto project=item.value("project_id",std::string{});
        if(project.empty()&&!projectless.contains(id)&&!item.value("cwd",std::string{}).empty())for(const auto& [pid,p]:projects)for(const auto& root:p.at("paths"))if(beneath(path(item.at("cwd")),path(root.get<std::string>())))project=pid;
        item["project_id"]=project;item["project"]=projects.contains(project)?projects.at(project).at("title"):Json("未分组");
        if(projects.contains(project))projects[project]["chats"]=projects[project]["chats"].get<int>()+1;
        item["detail"]=item.at("project").get<std::string>()+" · "+(item.value("archived",false)?"归档":"活动")+" · "+(item.value("available",true)?item.value("cwd",std::string{}):"原始记录缺失，无法同步");
        result["conversations"].push_back(std::move(item));
    }
    for(auto& [id,item]:projects) {item["available"]=std::any_of(item.at("paths").begin(),item.at("paths").end(),[](const Json& p){return fs::is_directory(path(p.get<std::string>()));});item["detail"]="";for(const auto& p:item.at("paths")){if(!item["detail"].get<std::string>().empty())item["detail"]=item["detail"].get<std::string>()+"; ";item["detail"]=item["detail"].get<std::string>()+p.get<std::string>();}result["projects"].push_back(std::move(item));}
    result["note"]="Metadata only. Partial conversation selection omits full thread databases and indexes; restore exports selected raw records, not an automatic Codex database merge. Project selection adds source directories only after an explicit choice.";
    return result;
}
void compile_selection(Json& config) {
    config.erase("_selection_paths");
    const auto selected=config.value("selection",Json::object());bool partial=false;
    for(const auto* category:{"conversations","plugins","skills"}) {
        const auto rule=selected.value(category,Json::object());const auto mode=rule.value("mode",std::string("all"));
        if(mode!="all"&&mode!="selected")throw std::runtime_error("selection mode must be all or selected");
        if(mode=="selected")partial=true;
        if(rule.contains("ids")&&!rule.at("ids").is_array())throw std::runtime_error("selection ids must be an array");
    }
    if(!partial)return;
    const auto catalog=selection_catalog(config);
    for(const auto* category:{"conversations","plugins","skills"}) {
        const auto rule=selected.value(category,Json::object());if(rule.value("mode",std::string("all"))!="selected")continue;
        const auto ids=rule.value("ids",Json::array());Json rules=Json::object();
        for(const auto& item:catalog.at(category))if(std::find(ids.begin(),ids.end(),item.at("id"))!=ids.end())for(const auto& location:item.at("paths")) {
            const auto root=location.at("root").get<std::string>();if(!rules.contains(root))rules[root]=Json::array();rules[root].push_back(location.at("path"));
        }
        config["_selection_paths"][category]=std::move(rules);
    }
}
}
