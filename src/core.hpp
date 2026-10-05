#pragma once
#include "json.hpp"
#include <filesystem>
#include <string>
#include <vector>
#include <array>
#include <functional>
namespace cxs {
namespace fs = std::filesystem;
using Json = nlohmann::json;
using Bytes = std::vector<unsigned char>;
using Key = std::array<unsigned char,32>;
fs::path path(const std::string& s);
std::string utf8(const fs::path& p);
std::string env(const char* name);
std::string random_id();
Bytes read_bytes(const fs::path&, size_t limit=128*1024*1024);
void write_atomic(const fs::path&, const Bytes&, bool private_file=true);
void private_directory(const fs::path&);
std::string digest(const Bytes&, const Key&);
Bytes seal(const Bytes&, const Key&, const std::string& domain);
Bytes open(const Bytes&, const Key&, const std::string& domain);
Json discover();
void resolve_history_roots(Json& config);
bool excluded_file(const Json& config, const std::string& relative);
Json conversation_inventory(const Json& config);
Json execute(Json request);
void generate_key(const fs::path&);
struct Response { int status{}; Bytes body; std::string etag; };
class Store {
public:
    explicit Store(const Json&, const Json&);
    Response request(const std::string& method, const std::string& relative,
                     const Bytes& data={}, const std::string& condition={});
    void ensure();
    std::vector<std::string> list(const std::string& directory);
    Bytes get(const std::string& relative);
    bool put_immutable(const std::string&, const Bytes&);
private:
    std::string url_, user_, password_;
    fs::path directory_;
    bool insecure_local_{};
};
void serve(const fs::path&, int port);
}
