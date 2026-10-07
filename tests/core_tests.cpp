#include "core.hpp"
#include <sqlite3.h>
#include <fstream>
#include <iostream>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#endif

using cxs::Json;
namespace fs = std::filesystem;
static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static void file(const fs::path& path, const cxs::Bytes& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    check(bool(out), "cannot create fixture");
}
static void text_file(const fs::path& path, const std::string& text) {
    file(path, cxs::Bytes(text.begin(), text.end()));
}
struct Db {
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> handle{nullptr, sqlite3_close};
    explicit Db(const fs::path& path) {
        sqlite3* raw = nullptr;
        const int rc = sqlite3_open(cxs::utf8(path).c_str(), &raw);
        handle.reset(raw);
        check(rc == SQLITE_OK, "cannot create fixture database");
    }
    void sql(const std::string& statement) {
        check(sqlite3_exec(handle.get(), statement.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK, "fixture SQL failed");
    }
    std::string value(const char* query) {
        sqlite3_stmt* raw = nullptr;
        check(sqlite3_prepare_v2(handle.get(), query, -1, &raw, nullptr) == SQLITE_OK, "query failed");
        auto stmt = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>(raw, sqlite3_finalize);
        check(sqlite3_step(stmt.get()) == SQLITE_ROW, "query has no result");
        return reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
    }
};
static void set_env(const char* name, const fs::path& path) {
#ifdef _WIN32
    check(SetEnvironmentVariableW(cxs::path(name).c_str(), path.c_str()), "set environment failed");
#else
    check(setenv(name, cxs::utf8(path).c_str(), 1) == 0, "set environment failed");
#endif
}

int main(int argc, char** argv) {
    try {
        const auto notice = cxs::execute({{"op", "disclaimer"}});
        check(notice.at("version") == "2026-10-07" && notice.at("text").get<std::string>().find("AGPL-3.0-only") != std::string::npos, "disclaimer API must work without a data configuration");
        const auto base = fs::absolute(argc > 1 ? cxs::path(argv[1]) : fs::current_path() / "history-contracts") / cxs::random_id();
        const auto home = base / "home";
        const auto user = base / "user";
        fs::create_directories(home);
        fs::create_directories(user);
        set_env("CODEX_HOME", home);
#ifdef _WIN32
        set_env("USERPROFILE", user);
        set_env("APPDATA", user / "roaming");
        set_env("LOCALAPPDATA", user / "local");
#else
        set_env("HOME", user);
        set_env("XDG_CONFIG_HOME", user / "config");
#endif
        const cxs::Bytes compressed = {0x28, 0xb5, 0x2f, 0xfd, 0x00, 0x00, 0x17, 0x33};
        const auto active = home / "sessions/2020/rollout-active-2020.jsonl";
        const auto packed_active = home / "sessions/2018/rollout-active-2018.jsonl.zst";
        const auto archived = home / "archived_sessions/rollout-archive-2017.jsonl";
        const auto packed_archive = home / "archived_sessions/rollout-archive-2016.jsonl.zst";
        text_file(active, "active old conversation\n");
        file(packed_active, compressed);
        text_file(archived, "archived old conversation\n");
        file(packed_archive, compressed);
        text_file(home / "archived_sessions/rollout-unindexed-2015.jsonl", "unindexed archive\n");
        file(home / "archived_sessions/rollout-unindexed-2014.jsonl.zst", compressed);
        text_file(home / "session_index.jsonl", "session index fixture\n");
        text_file(home / "history.jsonl", "legacy history fixture\n");

        Db state(home / "state_5.sqlite");
        state.sql("PRAGMA journal_mode=WAL; CREATE TABLE threads(id TEXT PRIMARY KEY, rollout_path TEXT, archived INTEGER);");
        const auto insert = [&](const char* id, fs::path p, bool archived_flag) {
            auto filename = cxs::utf8(p);
            size_t offset = 0;
            while ((offset = filename.find('\'', offset)) != std::string::npos) { filename.insert(offset, 1, '\''); offset += 2; }
            state.sql("INSERT INTO threads VALUES('" + std::string(id) + "','" + filename + "'," + (archived_flag ? "1" : "0") + ");");
        };
        insert("active", active, false);
        auto active_reference = packed_active; active_reference.replace_extension();
        insert("active-packed", active_reference, false);
        insert("archived", archived, true);
        auto archive_reference = packed_archive; archive_reference.replace_extension();
        insert("archived-packed", archive_reference, true);
        Db history(home / "thread_history_1.sqlite");
        history.sql("PRAGMA journal_mode=WAL; CREATE TABLE thread_items(body TEXT); INSERT INTO thread_items VALUES('archived WAL-only turns');");

        auto config = cxs::discover();
        config["state"] = cxs::utf8(base / "state");
        config["remote"] = {{"directory", cxs::utf8(base / "repository")}};
        const auto config_path = base / "sync.json";
        text_file(config_path, config.dump(2));
        const auto request = [&](const char* op) { return Json{{"op", op}, {"config", cxs::utf8(config_path)}, {"key_hex", std::string(64, 'a')}}; };
        const auto coverage = cxs::execute(request("conversations"));
        check(coverage.at("active").at("files") == 2, "old active/compressed records missing");
        check(coverage.at("archived").at("files") == 4, "old or unindexed archives missing");
        check(coverage.at("archived").at("compressed") == 2, "compressed archives missing");
        check(coverage.at("indexed_active") == 2 && coverage.at("indexed_archived") == 2, "archive flags not audited");
        check(coverage.at("coverage_complete") == true, "compressed rollout references falsely reported missing");
        check(cxs::execute(request("scan")).at("files") == 10, "scan did not retain all history metadata");

        const auto backup = cxs::execute(request("backup"));
        const auto progress_request=Json{{"op","progress"},{"config",cxs::utf8(config_path)}};
        const auto upload_progress=cxs::execute(progress_request);
        check(upload_progress.at("phase")=="completed"&&upload_progress.at("done")==upload_progress.at("total"),"backup progress did not complete");
        check(upload_progress.at("files_done")==backup.at("files")&&upload_progress.at("files_total")==backup.at("files"),"backup progress file counts differ");
        check(upload_progress.at("bytes_done")==upload_progress.at("bytes_total")&&upload_progress.at("transferred_bytes")==upload_progress.at("bytes_total"),"backup progress byte counts differ");
        auto repeat=request("backup");repeat["progress_id"]="deduplicated-task";cxs::execute(repeat);
        const auto repeated_progress=cxs::execute(progress_request);
        check(repeated_progress.at("operation_id")=="deduplicated-task"&&repeated_progress.at("transferred_bytes")==0&&repeated_progress.at("bytes_done")==repeated_progress.at("bytes_total"),"deduplication counted reused objects as transfers");
        check(backup.at("conversation_history").at("archived").at("files") == 4, "backup coverage missing");
        auto restore = request("restore");
        restore["snapshot"] = backup.at("snapshot");
        restore["output"] = cxs::utf8(base / "restored");
        cxs::execute(restore);
        const auto restore_progress=cxs::execute(progress_request);
        check(restore_progress.at("phase")=="completed"&&restore_progress.at("operation_id")!=repeated_progress.at("operation_id"),"restore reused stale task progress");
        check(restore_progress.at("files_done")==backup.at("files")&&restore_progress.at("bytes_done")==backup.at("bytes")&&restore_progress.at("transferred_bytes")==0,"cached restore progress counts differ");
        auto failed=request("restore");failed["snapshot"]=std::string(64,'f');failed["output"]=cxs::utf8(base/"failed-output");failed["progress_id"]="failed-task";
        bool progress_failed=false;try{cxs::execute(failed);}catch(...){progress_failed=true;}
        const auto failed_progress=cxs::execute(progress_request);
        check(progress_failed&&failed_progress.at("phase")=="failed"&&failed_progress.at("operation_id")=="failed-task","failed restore retained completed progress");
        const auto restored = base / "restored/codex_home";
        for (fs::recursive_directory_iterator it(home), end; it != end; ++it) {
            if (!it->is_regular_file()) continue;
            const auto name = cxs::utf8(it->path().filename());
            if (name.ends_with(".sqlite") || name.ends_with("-wal") || name.ends_with("-shm")) continue;
            check(cxs::read_bytes(it->path()) == cxs::read_bytes(restored / it->path().lexically_relative(home)), "restored archive bytes differ");
        }
        Db restored_state(restored / "state_5.sqlite");
        check(restored_state.value("SELECT count(*) FROM threads WHERE archived=1") == "2", "SQLite archive status lost");
        Db restored_history(restored / "thread_history_1.sqlite");
        check(restored_history.value("SELECT body FROM thread_items") == "archived WAL-only turns", "live history WAL data lost");
        check(cxs::read_bytes(active) == cxs::Bytes({'a','c','t','i','v','e',' ','o','l','d',' ','c','o','n','v','e','r','s','a','t','i','o','n','\n'}), "source changed");

        const auto moved_id = "01234567-89ab-4cde-8fab-0123456789ab";
        const auto moved_archive = home / "archived_sessions" / (std::string("rollout-new-") + moved_id + ".jsonl.zst");
        file(moved_archive, compressed);
        insert(moved_id, home / "archived_sessions" / (std::string("rollout-old-") + moved_id + "_11234567-89ab-4cde-8fab-0123456789ab.jsonl"), true);
        const auto relocated = cxs::execute(request("conversations"));
        check(relocated.at("coverage_complete") == true && relocated.at("resolved_rollouts").size() == 1, "changed rollout path falsely reported missing");
        check(relocated.at("resolved_rollouts")[0].at("path") == cxs::utf8(moved_archive), "wrong conversation used to resolve stale path");
        const auto relocated_catalog = cxs::execute(request("catalog"));
        const auto relocated_item = std::find_if(relocated_catalog.at("conversations").begin(), relocated_catalog.at("conversations").end(), [&](const Json& item) { return item.at("id") == moved_id; });
        check(relocated_item != relocated_catalog.at("conversations").end() && relocated_item->at("available") == true && relocated_item->at("title") == moved_id, "moved conversation lost indexed metadata or selection availability");
        const auto duplicate_archive = home / "archived_sessions" / (std::string("rollout-other-") + moved_id + ".jsonl");
        text_file(duplicate_archive, "ambiguous duplicate fixture");
        const auto ambiguous = cxs::execute(request("conversations"));
        check(ambiguous.at("coverage_complete") == false && ambiguous.at("missing_rollouts").size() == 1, "ambiguous rollout path silently accepted");
        fs::rename(duplicate_archive, duplicate_archive.parent_path() / "not-a-rollout.txt");
        insert("dangling-archive", home / "archived_sessions/rollout-missing.jsonl", true);
        const auto incomplete = cxs::execute(request("conversations"));
        check(incomplete.at("coverage_complete") == false && incomplete.at("missing_rollouts").size() == 1, "missing archives silently accepted");
        config["exclude"] = Json::array({"archived_sessions"});
        text_file(config_path, config.dump(2));
        const auto excluded = cxs::execute(request("conversations"));
        check(excluded.at("archived").at("files") == 0 && !excluded.at("warnings").empty(), "explicit archive exclusion hidden");
        struct SelectionCase { const char* category; const char* root; const char* relative; };
        const SelectionCase selections[] = {
            {"conversations", "codex_home", "sessions/2020/rollout-active.jsonl"},
            {"conversations", "codex_home", "archived_sessions/rollout-old.jsonl.zst"},
            {"conversations", "codex_home", "state_5.sqlite"},
            {"conversations", "codex_home", "thread_history_1.sqlite"},
            {"conversations", "codex_home", "session_index.jsonl"},
            {"conversations", "extra_sessions", "2020/entry.jsonl"},
            {"conversations", "extra_archived_sessions", "entry.jsonl"},
            {"settings", "codex_home", "config.toml"},
            {"settings", "codex_home", "rules/default.rules"},
            {"settings", "desktop_local", "preferences.json"},
            {"credentials", "codex_home", "AUTH.JSON"},
            {"credentials", "desktop", "Cookies"},
            {"credentials", "codex_home", ".sandbox-secrets/value"},
            {"credentials", "codex_home", "secrets/value"},
            {"credentials", "codex_home", "skills/example/private.key"},
            {"credentials", "codex_home", "plugins/google-oauth.cxs"},
            {"plugins", "codex_home", "plugins/example/plugin.json"},
            {"plugins", "codex_home", "packages/example/data.json"},
            {"plugins", "codex_home", ".skill-lock.json"},
            {"skills", "codex_home", "skills/example/SKILL.md"},
            {"other", "codex_home", "notes.txt"}
        };
        const char* categories[] = {"conversations", "settings", "credentials", "plugins", "skills", "other"};
        for (const auto& item : selections) check(cxs::selected_file(Json::object(), item.root, item.relative), "legacy configuration changed file selection");
        for (const auto* category : categories) {
            Json disabled{{"data_types", {{category, false}}}};
            Json exclusive{{"data_types", Json::object()}};
            for (const auto* name : categories) exclusive["data_types"][name] = std::string(name) == category;
            for (const auto& item : selections) {
                const bool matches = std::string(item.category) == category;
                check(cxs::selected_file(disabled, item.root, item.relative) == !matches, "disabled category leaked or excluded another category");
                check(cxs::selected_file(exclusive, item.root, item.relative) == matches, "exclusive category selected wrong files");
            }
        }
        Json disabled_all{{"data_types", Json::object()}};
        for (const auto* category : categories) disabled_all["data_types"][category] = false;
        for (const auto& item : selections) check(!cxs::selected_file(disabled_all, item.root, item.relative), "all-disabled selection retained a file");

        const auto selection_source = base / "selection-source";
        const char* retained[] = {"sessions/2020/rollout-fixture.jsonl", "config.toml", "plugins/example/plugin.json", "skills/example/SKILL.md", "notes.txt"};
        const char* credentials[] = {"auth.json", ".sandbox-secrets/value", "skills/example/private.key"};
        for (const auto* name : retained) text_file(selection_source / name, std::string("selected fixture: ") + name);
        for (const auto* name : credentials) text_file(selection_source / name, std::string("credential fixture: ") + name);
        auto selected_config = config;
        selected_config["roots"] = Json::array({{{"id", "codex_home"}, {"path", cxs::utf8(selection_source)}}});
        selected_config["state"] = cxs::utf8(base / "selection-state");
        selected_config["remote"] = {{"provider", "local"}, {"directory", cxs::utf8(base / "selection-repository")}};
        selected_config["exclude"] = Json::array();
        const auto selected_path = base / "selection.json";
        const auto selected_request = [&](const char* op) { return Json{{"op", op}, {"config", cxs::utf8(selected_path)}, {"key_hex", std::string(64, 'a')}}; };
        text_file(selected_path, selected_config.dump(2));
        const auto all_backup = cxs::execute(selected_request("backup"));
        check(all_backup.at("files") == 8, "unfiltered fixture backup omitted data");
        selected_config["data_types"] = {{"credentials", false}};
        text_file(selected_path, selected_config.dump(2));
        check(cxs::execute(selected_request("scan")).at("files") == 5, "credential selection did not filter scan");
        const auto filtered_backup = cxs::execute(selected_request("backup"));
        check(filtered_backup.at("files") == 5, "credential selection did not filter backup");
        for (const auto& snapshot : {all_backup.at("snapshot"), filtered_backup.at("snapshot")}) {
            const auto output = base / (snapshot == all_backup.at("snapshot") ? "selection-old-restored" : "selection-filtered-restored");
            auto filtered_restore = selected_request("restore");
            filtered_restore["snapshot"] = snapshot; filtered_restore["output"] = cxs::utf8(output);
            cxs::execute(filtered_restore);
            for (const auto* name : retained) check(cxs::read_bytes(output / "codex_home" / name) == cxs::read_bytes(selection_source / name), "selected restore bytes differ");
            for (const auto* name : credentials) check(!fs::exists(output / "codex_home" / name), "credential leaked from filtered restore");
        }
        for (const auto* name : credentials) {
            const auto expected = std::string("credential fixture: ") + name;
            check(cxs::read_bytes(selection_source / name) == cxs::Bytes(expected.begin(), expected.end()), "selection modified source credentials");
        }
        // Recognize real entities from metadata, including archived and missing indexed records.
        const auto active_record=selection_source/"sessions/2026/rollout-2026-01-01-aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa.jsonl";
        const auto archived_record=selection_source/"archived_sessions/rollout-2025-bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb.jsonl.zst";
        text_file(active_record,"selected raw conversation bytes");text_file(archived_record,"opaque archived bytes");
        const auto project_folder=base/"project-source";text_file(project_folder/"main.cpp","project fixture");
        const auto quote=[](std::string value){size_t at=0;while((at=value.find('\'',at))!=std::string::npos){value.insert(at,"'");at+=2;}return "'"+value+"'";};
        {
            Db catalog_db(selection_source/"state_9.sqlite");
            catalog_db.sql("CREATE TABLE threads(id TEXT,rollout_path TEXT,title TEXT,cwd TEXT,archived INTEGER,project_id TEXT);CREATE TABLE projects(id TEXT,name TEXT);CREATE TABLE project_roots(project_id TEXT,position INTEGER,path TEXT);");
            catalog_db.sql("INSERT INTO projects VALUES('project-a','Fixture project');INSERT INTO project_roots VALUES('project-a',0,"+quote(cxs::utf8(project_folder))+");");
            catalog_db.sql("INSERT INTO threads VALUES('thread-active',"+quote(cxs::utf8(active_record))+",'Active fixture',"+quote(cxs::utf8(project_folder))+",0,'project-a');");
            catalog_db.sql("INSERT INTO threads VALUES('thread-archive',"+quote(cxs::utf8(archived_record))+",'Archived fixture',"+quote(cxs::utf8(project_folder))+",1,'project-a');");
            catalog_db.sql("INSERT INTO threads VALUES('thread-missing','sessions/missing.jsonl','Missing record','',1,'');");
        }
        const auto catalog=cxs::selection_catalog(selected_config);
        check(catalog.at("conversations").size()==4&&catalog.at("projects").size()==1&&catalog.at("plugins").size()==1&&catalog.at("skills").size()==1,"entity catalog omitted available or indexed entities");
        bool found_archive=false,found_missing=false;
        for(const auto& item:catalog.at("conversations")){if(item.at("id")=="thread-archive")found_archive=item.at("archived")&&item.at("project_id")=="project-a";if(item.at("id")=="thread-missing")found_missing=!item.at("available").get<bool>();}
        check(found_archive&&found_missing,"archive/project association or missing record status was lost");
        selected_config["data_types"]={{"conversations",true},{"settings",false},{"credentials",false},{"plugins",false},{"skills",false},{"other",false}};
        selected_config["selection"]={{"conversations",{{"mode","selected"},{"ids",Json::array({"thread-active"})}}}};
        text_file(selected_path,selected_config.dump(2));
        check(cxs::execute(selected_request("scan")).at("files")==1,"individual conversation selection leaked other records");
        const auto partial_backup=cxs::execute(selected_request("backup"));check(partial_backup.at("files")==1,"partial conversation backup included full indexes or other data");
        auto partial_restore=selected_request("restore");partial_restore["snapshot"]=partial_backup.at("snapshot");partial_restore["output"]=cxs::utf8(base/"individual-restored");cxs::execute(partial_restore);
        check(cxs::read_bytes(base/"individual-restored/codex_home"/active_record.lexically_relative(selection_source))==cxs::read_bytes(active_record),"partial conversation restore changed raw bytes");
        check(!fs::exists(base/"individual-restored/codex_home/state_9.sqlite")&&!fs::exists(base/"individual-restored/codex_home/archived_sessions"),"partial restore leaked another history or index");
        text_file(selection_source/"plugins/second/plugin.json","unselected plugin");text_file(selection_source/"skills/second/SKILL.md","unselected skill");text_file(selection_source/"skills/second/private.key","unselected credential");
        selected_config["data_types"]=Json::object();
        selected_config["selection"]={{"plugins",{{"mode","selected"},{"ids",Json::array({"codex_home/plugins/example"})}}},{"skills",{{"mode","selected"},{"ids",Json::array({"codex_home/skills/example"})}}}};
        auto scoped_config=selected_config;cxs::compile_selection(scoped_config);
        check(cxs::selected_file(scoped_config,"codex_home","plugins/example/plugin.json")&&!cxs::selected_file(scoped_config,"codex_home","plugins/second/plugin.json"),"individual plugin selection leaked another plugin");
        check(cxs::selected_file(scoped_config,"codex_home","skills/example/private.key")&&!cxs::selected_file(scoped_config,"codex_home","skills/second/private.key"),"unselected skill leaked its credential");
        selected_config["selection"]["skills"]["ids"]=Json::array();cxs::compile_selection(selected_config);check(!cxs::selected_file(selected_config,"codex_home","skills/example/SKILL.md"),"empty individual selection retained a skill");
        selected_config.erase("selection");selected_config.erase("_selection_paths");selected_config["data_types"]=Json::object();selected_config["roots"][0]["include"]=Json::array({"skills/example/SKILL.md"});
        text_file(selected_path,selected_config.dump(2));check(cxs::execute(selected_request("scan")).at("files")==1,"literal include selection leaked siblings");
        selected_config["roots"][0]["exclude"]=Json::array({"skills/example/SKILL.md"});text_file(selected_path,selected_config.dump(2));check(cxs::execute(selected_request("scan")).at("files")==0,"exclude did not take precedence");
        selected_config["roots"][0]["include"]=Json::array({"../escape"});bool invalid=false;try{cxs::validate_selection_rules(selected_config);}catch(...){invalid=true;}check(invalid,"selection allowed traversal paths");
        selected_config["roots"][0].erase("include");selected_config["roots"][0].erase("exclude");selected_config["roots"][0]["enabled"]=false;text_file(selected_path,selected_config.dump(2));check(cxs::execute(selected_request("scan")).at("files")==0,"disabled root was scanned");
        const auto log_state=base/"diagnostic-state";
        cxs::runtime_log(log_state,"operation_failed",{{"operation","backup"},{"error","synthetic read failure"},{"password","do-not-persist"},{"key_hex","do-not-persist"}});
        bool log_found=false;
        for(const auto& entry:fs::directory_iterator(log_state/"logs")){auto contents=cxs::read_bytes(entry.path(),1024*1024);std::string text(contents.begin(),contents.end());check(text.find("operation_failed")!=std::string::npos&&text.find("synthetic read failure")!=std::string::npos,"runtime error not persisted");check(text.find("do-not-persist")==std::string::npos,"runtime log leaked secret fields");log_found=true;}
        check(log_found,"runtime log file missing");
        const auto english_notice=cxs::execute(Json{{"op","disclaimer"},{"language","en"}});check(english_notice.at("text").get<std::string>().find("Risks and disclaimer")!=std::string::npos,"English API notice was not returned");
        std::cout << "HISTORY_CONTRACTS_PASS: old + compressed + unindexed archives, SQLite WAL history, byte-identical restore, exclusions and missing-reference warnings\n"
                  << "Fixtures retained: " << cxs::utf8(base) << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << "HISTORY_CONTRACTS_FAIL: " << e.what() << '\n'; return 1; }
}
