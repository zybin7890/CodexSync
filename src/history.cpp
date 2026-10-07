#include "core.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <cctype>
#include <memory>
#include <set>
#ifdef _WIN32
#include <windows.h>
#endif

namespace cxs {
namespace {
fs::path physical(fs::path p) {
#ifdef _WIN32
    auto value = utf8(p);
    if (value.starts_with("//?/GLOBALROOT/")) return p.lexically_normal();
    if (value.starts_with("//?/UNC/")) p = path("//" + value.substr(8));
    else if (value.starts_with("//?/") && !value.starts_with("//?/GLOBALROOT/")) p = path(value.substr(4));
#endif
    return fs::weakly_canonical(p);
}
std::string identity(const fs::path& p) {
    auto result = utf8(physical(p));
#ifdef _WIN32
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return result;
}
bool beneath(const fs::path& file, const fs::path& root) {
    auto f = identity(file), r = identity(root);
    return f == r || (f.starts_with(r) && f.size() > r.size() && f[r.size()] == '/');
}
bool reparse(const fs::path& p) {
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(p.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    return fs::is_symlink(fs::symlink_status(p));
#endif
}
bool covered(const Json& config, const fs::path& p) {
    for (const auto& root : config.at("roots")) {
        const auto base = path(root.at("path").get<std::string>());
        if (!beneath(p, base)) continue;
        auto relative = utf8(physical(p).lexically_relative(physical(base)));
        if (!excluded_file(config, relative, root.at("id")) && (fs::is_directory(p) || selected_file(config, root.at("id"), relative))) return true;
    }
    return false;
}
bool rollout(const fs::path& p) {
    auto name = utf8(p.filename());
    return name.starts_with("rollout-") && (name.ends_with(".jsonl") || name.ends_with(".jsonl.zst"));
}
struct Database {
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> handle{nullptr, sqlite3_close};
    explicit Database(const fs::path& p) {
        sqlite3* raw = nullptr;
        const int rc = sqlite3_open_v2(utf8(p).c_str(), &raw, SQLITE_OPEN_READONLY, nullptr);
        handle.reset(raw);
        if (rc != SQLITE_OK) throw std::runtime_error("cannot read thread index: " + utf8(p));
        sqlite3_busy_timeout(handle.get(), 2000);
    }
};
int state_version(const std::string& name) {
    if (!name.starts_with("state_") || !name.ends_with(".sqlite")) return -1;
    const auto version = name.substr(6, name.size() - 13);
    if (version.empty() || version.size() > 6 || !std::all_of(version.begin(), version.end(),
        [](unsigned char c) { return std::isdigit(c); })) return -1;
    return std::stoi(version);
}
}

void validate_selection_rules(const Json& config) {
    for (const auto& root : config.at("roots")) {
        if (root.contains("enabled") && !root.at("enabled").is_boolean()) throw std::runtime_error("root enabled must be boolean");
        for (const auto* field : {"include", "exclude"}) {
            if (!root.contains(field)) continue;
            if (!root.at(field).is_array() || root.at(field).size() > 256) throw std::runtime_error("selection rules must be an array of at most 256 paths");
            for (const auto& item : root.at(field)) {
                const auto value = item.get<std::string>();
                if (value.empty() || value.size() > 4096 || value.find_first_of("\\:*?\r\n") != std::string::npos || path(value).is_absolute())
                    throw std::runtime_error("selection rules require relative literal paths, without wildcards");
                for (const auto& part : path(value)) if (part == ".." || part == ".") throw std::runtime_error("selection rules cannot contain . or .. segments");
            }
        }
    }
}
bool excluded_file(const Json& config, const std::string& relative, const std::string& root) {
    const auto normalize = [](std::string value) {
        std::replace(value.begin(), value.end(), '\\', '/');
        while (!value.empty() && value.back() == '/') value.pop_back();
#ifdef _WIN32
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
        return value;
    };
    const auto name = normalize(relative);
    const auto matches = [&](const Json& rules) {
        for (const auto& item : rules) {
            const auto pattern = normalize(item.get<std::string>());
            if (name == pattern || name.starts_with(pattern + "/")) return true;
        }
        return false;
    };
    if (matches(config.value("exclude", Json::array()))) return true;
    if (!root.empty() && config.contains("roots")) for (const auto& item : config.at("roots")) {
        if (item.at("id") != root) continue;
        if (!item.value("enabled", true) || matches(item.value("exclude", Json::array()))) return true;
        const auto& allow = item.value("include", Json::array());
        if (!allow.empty()) {
            bool retained = name == ".";
            for (const auto& rule : allow) {
                const auto pattern = normalize(rule.get<std::string>());
                if (name == pattern || name.starts_with(pattern + "/") || pattern.starts_with(name + "/")) retained = true;
            }
            if (!retained) return true;
        }
        break;
    }
    return false;
}

bool selected_file(const Json& config, const std::string& root, const std::string& relative) {
    if (excluded_file(config, relative, root)) return false;
    auto name = relative;
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto top = name.substr(0, name.find('/'));
    const auto file = utf8(path(name).filename());
    const auto scoped = [&](const char* category) {
        if (!config.contains("_selection_paths") || !config.at("_selection_paths").contains(category)) return true;
        const auto& rules=config.at("_selection_paths").at(category);
        if(!rules.contains(root))return false;
        for(const auto& item:rules.at(root)) {
            auto rule=item.get<std::string>();
#ifdef _WIN32
            std::transform(rule.begin(),rule.end(),rule.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
#endif
            const auto candidate=
#ifdef _WIN32
                name;
#else
                relative;
#endif
            if(candidate==rule||candidate.starts_with(rule+"/")||rule.starts_with(candidate+"/"))return true;
        }
        return false;
    };
    bool skill_root=false;
    for(const auto& item:config.value("roots",Json::array()))if(item.at("id")==root)skill_root=path(item.at("path").get<std::string>()).filename()=="skills";
    if ((top=="skills"||skill_root) && !scoped("skills")) return false;
    if ((top=="plugins"||top=="packages"||top=="code-review-plugin"||top=="vendor_imports"||file==".skill-lock.json") && !scoped("plugins")) return false;
    if (config.contains("_selection_paths") && config.at("_selection_paths").contains("conversations") && file.starts_with(".codex-global-state")) return false;
    std::string category = "other";
    if (top == "secrets" || top == ".sandbox-secrets" || file == "auth.json" || file == "cap_sid" ||
        file == "cookies" || file == "login data" || file.ends_with(".key") || file.ends_with(".pem") ||
        file.ends_with(".pfx") || file.ends_with(".p12") || file.find("oauth") != std::string::npos)
        category = "credentials";
    else if (top == "sessions" || top == "archived_sessions" || root.ends_with("_sessions") || root.ends_with("_archived_sessions") ||
             file.starts_with("rollout-") || file.starts_with("state_") || file.starts_with("thread_history_") ||
             file == "session_index.jsonl" || file == "history.jsonl" || file == "transcription-history.jsonl")
        category = "conversations";
    else if (top == "skills" || skill_root) category = "skills";
    else if (top == "plugins" || top == "packages" || top == "code-review-plugin" || top == "vendor_imports" || file == ".skill-lock.json")
        category = "plugins";
    else if (root.starts_with("desktop") || top == "rules" || top == "automations" || top == "state" ||
             file.starts_with("config.toml") || file.starts_with(".codex-global-state") || file == "agents.md" || file == "rtk.md")
        category = "settings";
    if (category=="conversations" && !scoped("conversations")) return false;
    return config.value("data_types",Json::object()).value(category, true);
}

void resolve_history_roots(Json& config) {
    auto& roots = config.at("roots");
    for (auto& root : roots) {
        auto p = path(root.at("path").get<std::string>());
        if (!p.is_absolute()) throw std::runtime_error("root paths must be absolute");
        root["path"] = utf8(physical(p));
    }
    const auto originals = roots;
    for (const auto& root : originals) {
        if (!root.value("enabled", true)) continue;
        for (const auto* folder : {"sessions", "archived_sessions"}) {
            if (excluded_file(config, folder, root.at("id"))) continue;
            const auto alias = path(root.at("path").get<std::string>()) / folder;
            if (!fs::is_directory(alias)) continue;
            const auto target = physical(alias);
            bool mapped = false;
            for (const auto& existing : roots)
                if (beneath(target, path(existing.at("path").get<std::string>()))) mapped = true;
            if (mapped) continue;
            const auto base = root.at("id").get<std::string>().substr(0, 35) + "_" + folder;
            std::string id = base;
            int suffix = 1;
            while (std::any_of(roots.begin(), roots.end(), [&](const Json& item) { return item.at("id") == id; }))
                id = base + "_" + std::to_string(suffix++);
            Json added = {{"id", id}, {"path", utf8(target)}};
            for (const auto* field : {"include", "exclude"}) if (root.contains(field)) {
                Json rules = Json::array();
                for (const auto& item : root.at(field)) {
                    const auto value = item.get<std::string>();
                    if (value == folder) { if (std::string(field) == "exclude") added["enabled"] = false; }
                    else if (value.starts_with(std::string(folder) + "/")) rules.push_back(value.substr(std::string(folder).size() + 1));
                }
                added[field] = rules;
            }
            roots.push_back(std::move(added));
        }
    }
}

Json conversation_inventory(const Json& config) {
    Json report = {{"active", {{"files", 0}, {"compressed", 0}}},
                   {"archived", {{"files", 0}, {"compressed", 0}}},
                   {"indexed_active", 0}, {"indexed_archived", 0}, {"index_checked", false},
                   {"metadata_files", Json::array()}, {"missing_rollouts", Json::array()}, {"resolved_rollouts", Json::array()},
                   {"warnings", Json::array()}};
    std::set<std::string> directories, files, indexes, metadata, thread_ids;
    std::map<std::string, std::vector<fs::path>> rollouts_by_id;
    Json references = Json::array();
    const auto warn = [&](const fs::path& p, const std::string& reason) {
        report["warnings"].push_back({{"path", utf8(p)}, {"reason", reason}});
    };
    const auto collect = [&](const fs::path& directory, bool archived) {
        if (!fs::is_directory(directory) || !directories.insert(identity(directory)).second) return;
        if (!covered(config, directory)) { warn(directory, "history directory excluded or not mapped"); return; }
        for (fs::recursive_directory_iterator it(physical(directory)), end; it != end; ++it) {
            const auto p = it->path();
            if (reparse(p)) {
                warn(p, "linked history entry needs an explicit root");
                if (it->is_directory()) it.disable_recursion_pending();
                continue;
            }
            if (!covered(config, p)) {
                warn(p, "history entry explicitly excluded");
                if (it->is_directory()) it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file() || !rollout(p) || !files.insert(identity(p)).second) continue;
            const auto thread = rollout_thread_id(p);
            if (!thread.empty()) rollouts_by_id[thread].push_back(p);
            auto& group = report[archived ? "archived" : "active"];
            group["files"] = group["files"].get<uint64_t>() + 1;
            if (utf8(p).ends_with(".zst")) group["compressed"] = group["compressed"].get<uint64_t>() + 1;
        }
    };
    for (const auto& root : config.at("roots")) {
        if (!root.value("enabled", true)) continue;
        const auto base = path(root.at("path").get<std::string>());
        if (!fs::is_directory(base)) { warn(base, "missing data root"); continue; }
        collect(base / "sessions", false);
        collect(base / "archived_sessions", true);
        const auto id = root.at("id").get<std::string>();
        if (base.filename() == "archived_sessions" || id.ends_with("_archived_sessions")) collect(base, true);
        else if (base.filename() == "sessions" || id.ends_with("_sessions")) collect(base, false);
        int newest = -1;
        fs::path state_index;
        for (const auto& item : fs::directory_iterator(base)) {
            if (!item.is_regular_file()) continue;
            const auto name = utf8(item.path().filename());
            if ((name.ends_with(".sqlite") || name == "session_index.jsonl" || name == "history.jsonl") &&
                covered(config, item.path()) && metadata.insert(identity(item.path())).second)
                report["metadata_files"].push_back(utf8(item.path()));
            const auto version = state_version(name);
            if (version > newest) { newest = version; state_index = item.path(); }
        }
        if (state_index.empty()) continue;
        if (!covered(config, state_index)) { warn(state_index, "thread index explicitly excluded"); continue; }
        if (!indexes.insert(identity(state_index)).second) continue;
        try {
            Database db(state_index);
            sqlite3_stmt* raw = nullptr;
            const int rc = sqlite3_prepare_v2(db.handle.get(), "SELECT id, rollout_path, archived FROM threads", -1, &raw, nullptr);
            auto stmt = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>(raw, sqlite3_finalize);
            if (rc != SQLITE_OK) throw std::runtime_error("unsupported thread index schema");
            report["index_checked"] = true;
            int step;
            while ((step = sqlite3_step(stmt.get())) == SQLITE_ROW) {
                const auto* identifier = sqlite3_column_text(stmt.get(), 0);
                const auto* filename = sqlite3_column_text(stmt.get(), 1);
                if (!identifier || !filename) throw std::runtime_error("thread index has a null rollout reference");
                const std::string thread(reinterpret_cast<const char*>(identifier));
                if (!thread_ids.insert(thread).second) continue;
                const bool archived = sqlite3_column_int(stmt.get(), 2) != 0;
                auto& count = report[archived ? "indexed_archived" : "indexed_active"];
                count = count.get<uint64_t>() + 1;
                auto p = path(reinterpret_cast<const char*>(filename));
                if (!p.is_absolute()) p = base / p;
                references.push_back({{"id", thread}, {"path", utf8(p)}, {"archived", archived}});
            }
            if (step != SQLITE_DONE) throw std::runtime_error("cannot finish reading thread index");
        } catch (const std::exception& e) { warn(state_index, e.what()); }
    }
    for (const auto& reference : references) {
        auto p = path(reference.at("path").get<std::string>());
        if (!fs::is_regular_file(p) && fs::is_regular_file(path(utf8(p) + ".zst"))) p = path(utf8(p) + ".zst");
        const auto thread = reference.at("id").get<std::string>();
        if (!fs::is_regular_file(p) && rollouts_by_id.contains(thread) && rollouts_by_id.at(thread).size() == 1) {
            p = rollouts_by_id.at(thread).front();
            report["resolved_rollouts"].push_back({{"id", thread}, {"indexed_path", reference.at("path")}, {"path", utf8(p)}});
        }
        if (!fs::is_regular_file(p)) report["missing_rollouts"].push_back(reference);
        else if (!covered(config, p)) warn(p, "indexed conversation is outside mapped roots or excluded");
    }
    report["files"] = report["active"]["files"].get<uint64_t>() + report["archived"]["files"].get<uint64_t>();
    report["coverage_complete"] = report["missing_rollouts"].empty() && report["warnings"].empty();
    report["selection_limited"] = config.contains("_selection_paths") && config.at("_selection_paths").contains("conversations");
    report["note"] = "All local rollout files, including unindexed and .jsonl.zst archives, are preserved without decompression or date limits. Counts are files, not unique chats. SQLite thread/history databases and indexes are included separately; cloud-only history is not available locally.";
    return report;
}
}
