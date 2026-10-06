#pragma once
#include "core.hpp"
#include <chrono>
#include <map>

namespace cxs {
class GoogleDrive {
public:
    GoogleDrive(const Json& config, const Json& secrets);
    ~GoogleDrive();
    std::vector<std::string> list(const std::string& directory);
    Response get(const std::string& relative, bool metadata_only = false);
    bool put_immutable(const std::string& relative, const Bytes& data);
private:
    std::string origin_, token_url_, prefix_, client_id_, client_secret_, refresh_token_, access_token_;
    std::chrono::steady_clock::time_point expiry_{};
    std::map<std::string, std::string> files_;
    void refresh();
    Response call(const std::string& url, const std::string& method, const Bytes& data = {}, Headers headers = {});
    std::string find(const std::string& relative);
    std::vector<Json> query(const std::string& expression);
    std::string name(const std::string& relative) const;
};
std::string google_client_id(const Json& remote);
struct GoogleClient {
    std::string id, secret;
    GoogleClient(std::string id, std::string secret);
    ~GoogleClient();
    GoogleClient(const GoogleClient&) = delete;
    GoogleClient& operator=(const GoogleClient&) = delete;
    GoogleClient(GoogleClient&&) = default;
};
GoogleClient google_oauth_client(const Json& config, const Json& secrets);
bool google_client_ready(const Json& config);
Json google_import_client(const Json& config, const Json& secrets, const Bytes& profile);
std::string google_token_url(const Json& remote);
fs::path google_credential_path(const Json& config);
}
