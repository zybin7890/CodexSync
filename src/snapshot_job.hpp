#pragma once
#include "core.hpp"
#include <set>

namespace cxs {
Json source_identity(const fs::path& source);
Json source_family_identity(const fs::path& source);
std::string selection_identity(const Json& config, const Key& key);
int64_t unix_now();
class ChangeCatalog {
public:
    ChangeCatalog(const fs::path& state, const std::string& scope, const Key& key);
    Json data = Json::object();
    std::string recovery;
    void save(const Json& value);
private:
    fs::path filename_;
    std::string domain_;
    const Key& key_;
};
// Immutable capture and mutable receipts are durable before any network write.
class SnapshotJob {
public:
    SnapshotJob(const fs::path& state, const Key& key);
    Json data = Json::object();
    bool pending() const;
    void prepare(const Json& manifest, const Json& catalog, const std::string& scope, const std::string& target);
    void save();
    void check_target(const std::string& scope, const std::string& target) const;
    void confirm(const std::string& object);
    bool confirmed(const std::string& object) const;
    void inherit_confirmed(const Json& objects);
    void committed();
private:
    fs::path filename_;
    const Key& key_;
    std::set<std::string> confirmed_;
    fs::path receipts_;
};
}
