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
        check(backup.at("conversation_history").at("archived").at("files") == 4, "backup coverage missing");
        auto restore = request("restore");
        restore["snapshot"] = backup.at("snapshot");
        restore["output"] = cxs::utf8(base / "restored");
        cxs::execute(restore);
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

        insert("dangling-archive", home / "archived_sessions/rollout-missing.jsonl", true);
        const auto incomplete = cxs::execute(request("conversations"));
        check(incomplete.at("coverage_complete") == false && incomplete.at("missing_rollouts").size() == 1, "missing archives silently accepted");
        config["exclude"] = Json::array({"archived_sessions"});
        text_file(config_path, config.dump(2));
        const auto excluded = cxs::execute(request("conversations"));
        check(excluded.at("archived").at("files") == 0 && !excluded.at("warnings").empty(), "explicit archive exclusion hidden");
        std::cout << "HISTORY_CONTRACTS_PASS: old + compressed + unindexed archives, SQLite WAL history, byte-identical restore, exclusions and missing-reference warnings\n"
                  << "Fixtures retained: " << cxs::utf8(base) << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << "HISTORY_CONTRACTS_FAIL: " << e.what() << '\n'; return 1; }
}
