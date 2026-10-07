#pragma once
#include "core.hpp"
#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <array>

namespace cxs {
class GoogleDrive {
public:
    GoogleDrive(const Json& config, const Json& secrets);
    ~GoogleDrive();
    std::vector<std::string> list(const std::string& directory);
    Response get(const std::string& relative, bool metadata_only = false);
    bool put_immutable(const std::string& relative, const Bytes& data);
    void prepare_uploads();
    std::string identity();
    Json location();
    std::string original_folder(const std::string& relative,bool create);
    Response original_get(const std::string&,const fs::path& download={});
    bool original_upload(const std::string&,const fs::path&,const std::string& hash);
    bool original_copy(const std::string& from,const std::string& to,const std::string& hash);
    std::vector<std::string> original_list(const std::string&);
    bool original_metadata(const std::string&,const Bytes&);
private:
    fs::path state_;
    std::string origin_, token_url_, prefix_, client_id_, client_secret_, refresh_token_, access_token_;
    std::chrono::steady_clock::time_point expiry_{};
    std::map<std::string, std::string> files_;
    std::set<std::string> complete_directories_;
    std::vector<std::string> upload_ids_;
    std::mutex token_mutex_, refresh_mutex_, files_mutex_, ids_mutex_;
    std::array<std::mutex, 64> upload_mutexes_;
    size_t token_generation_ = 0;
    std::string storage_mode_, folder_id_, account_id_;
    Key folder_key_{};
    bool folder_checked_=false;
    bool original_=false;
    std::map<std::string,std::string> original_folders_;
    std::string original_find(const std::string&,const std::string& hash={});
    std::vector<Json> original_query(const std::string& parent,const std::string& expression);
    std::string account_identity();
    std::string visible_folder(bool create);
    void refresh(bool force = false, size_t rejected_generation = 0);
    std::string upload_id();
    Response call(const std::string& url, const std::string& method, const Bytes& data = {}, Headers headers = {},const fs::path& upload={},const fs::path& download={});
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
