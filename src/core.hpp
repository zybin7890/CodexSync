#pragma once
#include "json.hpp"
#include <filesystem>
#include <string>
#include <vector>
#include <array>
#include <functional>
#include <memory>
#include <stdexcept>
namespace cxs {
namespace fs = std::filesystem;
using Json = nlohmann::json;
using Bytes = std::vector<unsigned char>;
using Key = std::array<unsigned char,32>;
fs::path path(const std::string& s);
std::string utf8(const fs::path& p);
std::string env(const char* name);
fs::path application_directory();
bool portable_mode();
fs::path application_data_directory();
fs::path default_configuration_path();
fs::path default_key_path();
void normalize_application_config(Json& config);
Json application_paths();
std::string random_id();
Bytes read_bytes(const fs::path&, size_t limit=128*1024*1024);
void write_atomic(const fs::path&, const Bytes&, bool private_file=true);
void private_directory(const fs::path&);
std::string digest(const Bytes&, const Key&);
Bytes seal(const Bytes&, const Key&, const std::string& domain);
Bytes open(const Bytes&, const Key&, const std::string& domain);
Json discover();
void resolve_history_roots(Json& config);
bool excluded_file(const Json& config, const std::string& relative, const std::string& root = {});
void validate_selection_rules(const Json& config);
bool selected_file(const Json& config, const std::string& root, const std::string& relative);
Json conversation_inventory(const Json& config);
std::string rollout_thread_id(const fs::path& file);
Json selection_catalog(const Json& config);
void compile_selection(Json& config);
Json execute(Json request);
void runtime_log(const fs::path& state, const std::string& event, const Json& details = Json::object()) noexcept;
void generate_key(const fs::path&,const std::string& password={});
struct Response { int status{}; Bytes body; std::string etag; std::string location; };
using Headers = std::vector<std::string>;
struct HttpFailure : std::runtime_error {
    bool retryable;
    HttpFailure(unsigned long code, bool retryable) : std::runtime_error("HTTP transport failed; error "+std::to_string(code)), retryable(retryable) {}
};
Response http_request(const std::string& url, const std::string& method, const Bytes& data={}, const Headers& headers={});
Response http_file_request(const std::string& url,const std::string& method,const fs::path& upload,const fs::path& download,const Headers& headers={});
void with_http_session(const std::function<void()>& work);
std::string url_encode(const std::string&);
Key credential_key(const Json& secrets);
Json google_authorize(const Json& config, const Json& secrets, const std::function<void(const std::string&)>& open_browser, const std::function<void(const std::string&)>& progress = {});
class GoogleDrive;
class Store {
public:
    explicit Store(const Json&, const Json&);
    Response request(const std::string& method, const std::string& relative,
                     const Bytes& data={}, const std::string& condition={});
    void ensure();
    std::vector<std::string> list(const std::string& directory);
    Bytes get(const std::string& relative);
    bool put_immutable(const std::string&, const Bytes&);
    bool append_only() const { return static_cast<bool>(google_); }
    std::string identity(const Key& key);
    Json location();
    bool original()const{return original_;}
    void original_directory(const std::string&);
    bool original_upload(const std::string&,const fs::path&,const std::string& hash);
    bool original_copy(const std::string& from,const std::string& to,const std::string& hash);
    void original_download(const std::string&,const fs::path&);
private:
    std::string url_, user_, password_;
    fs::path directory_;
    bool insecure_local_{};
    bool original_{};
    std::shared_ptr<GoogleDrive> google_;
};
void serve(const fs::path&, int port);
}
