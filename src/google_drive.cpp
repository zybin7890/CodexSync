#include "google_drive.hpp"
#include <sodium.h>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <thread>

namespace cxs {
namespace {
void wipe(std::string& value) { sodium_memzero(value.data(), value.size()); value.clear(); }
bool identifier(const std::string& value) {
    return !value.empty() && value.size() <= 200 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}
std::string test_origin(const Json& remote, const char* setting, const std::string& normal) {
    if (!remote.contains(setting)) return normal;
    auto value = remote.at(setting).get<std::string>();
    constexpr auto start = "http://127.0.0.1:";
    if (!remote.value("allow_loopback_http", false) || !value.starts_with(start)) throw std::runtime_error("Google endpoint overrides are only allowed for explicit loopback tests");
    auto port = value.substr(std::char_traits<char>::length(start));
    if (port.empty() || port.size() > 5 || !std::all_of(port.begin(), port.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }) || std::stoi(port) < 1 || std::stoi(port) > 65535) throw std::runtime_error("invalid Google loopback test origin");
    return value;
}
Json parse(const Response& response) {
    try { return Json::parse(response.body.begin(), response.body.end()); }
    catch (...) { throw std::runtime_error("invalid Google response JSON"); }
}
void require_success(const Response& response, const char* operation) {
    if (response.status < 200 || response.status >= 300) throw std::runtime_error(std::string("Google Drive ") + operation + " failed: HTTP " + std::to_string(response.status) + "; check authorization and quota");
}
}
Key credential_key(const Json& secrets) {
    auto encoded = secrets.value("key_hex", std::string{});
    if (encoded.empty()) {
        const auto filename = env("CXS_KEY_FILE");
        if (filename.empty()) throw std::runtime_error("select the encryption key or set CXS_KEY_FILE before Google authorization");
        auto data = read_bytes(path(filename), 256);
        encoded.assign(data.begin(), data.end()); sodium_memzero(data.data(), data.size());
    }
    while (!encoded.empty() && (encoded.back() == '\n' || encoded.back() == '\r')) encoded.pop_back();
    Key key{};
    const bool valid = encoded.size() == 64 && sodium_hex2bin(key.data(), key.size(), encoded.data(), encoded.size(), nullptr, nullptr, nullptr) == 0;
    wipe(encoded);
    if (!valid) { sodium_memzero(key.data(), key.size()); throw std::runtime_error("invalid encryption key for Google credentials"); }
    return key;
}
std::string google_client_id(const Json& remote) {
    auto value = remote.value("client_id", std::string{});
    if(value.empty())value=env("CXS_GOOGLE_CLIENT_ID");
    if (value.size() > 256 || !value.ends_with(".apps.googleusercontent.com") || !std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
    })) throw std::runtime_error("set a Google Desktop OAuth client ID");
    return value;
}
std::string google_token_url(const Json& remote) {
    return test_origin(remote, "test_token_origin", "https://oauth2.googleapis.com") + "/token";
}
fs::path google_credential_path(const Json& config) {
    auto state = path(config.at("state").get<std::string>());
    if (!state.is_absolute()) throw std::runtime_error("Google credential state directory must be absolute");
    auto inside = [](const fs::path& child, const fs::path& parent) {
        auto a = fs::weakly_canonical(child), b = fs::weakly_canonical(parent); auto i = a.begin();
        for (auto j = b.begin(); j != b.end(); ++i, ++j) {
            if (i == a.end()) return false;
#ifdef _WIN32
            if (_wcsicmp(i->c_str(), j->c_str())) return false;
#else
            if (*i != *j) return false;
#endif
        }
        return true;
    };
    for (const auto& root : config.at("roots")) {
        auto folder = path(root.at("path").get<std::string>());
        if (!folder.is_absolute() || inside(state, folder) || inside(folder, state)) throw std::runtime_error("Google credential state must not overlap synchronized data roots");
    }
    return state / "google-oauth.cxs";
}
GoogleDrive::GoogleDrive(const Json& config, const Json& secrets) {
    const auto& remote = config.at("remote");
    origin_ = test_origin(remote, "test_api_origin", "https://www.googleapis.com");
    token_url_ = google_token_url(remote);
    auto repository = remote.value("repository", std::string("default"));
    if (repository.empty() || repository.size() > 64 || !std::all_of(repository.begin(), repository.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    })) throw std::runtime_error("Google repository ID must use lowercase letters, digits or hyphens (1..64)");
    prefix_ = "codex-sync-v1." + repository + ".";
    access_token_ = env("CXS_GOOGLE_ACCESS_TOKEN"); refresh_token_ = env("CXS_GOOGLE_REFRESH_TOKEN");
    client_secret_ = env("CXS_GOOGLE_CLIENT_SECRET");
    if (access_token_.empty() && refresh_token_.empty()) {
        const auto file = google_credential_path(config);
        if (!fs::exists(file)) throw std::runtime_error("Google Drive is not authorized; use google-login or the GUI authorization button");
        auto key = credential_key(secrets);
        Bytes plain;
        try { plain = open(read_bytes(file, 64 * 1024), key, "google-oauth-v1"); }
        catch (...) { sodium_memzero(key.data(), key.size()); throw; }
        sodium_memzero(key.data(), key.size());
        Json credentials;
        try { credentials = Json::parse(plain.begin(), plain.end()); }
        catch (...) { sodium_memzero(plain.data(), plain.size()); throw std::runtime_error("invalid encrypted Google credentials"); }
        sodium_memzero(plain.data(), plain.size());
        client_id_ = credentials.at("client_id").get<std::string>();
        refresh_token_ = credentials.at("refresh_token").get<std::string>();
        client_secret_ = credentials.value("client_secret", client_secret_);
        if ((!remote.value("client_id", std::string{}).empty() || !env("CXS_GOOGLE_CLIENT_ID").empty()) && google_client_id(remote) != client_id_) throw std::runtime_error("Google OAuth client changed; authorize again");
        for (auto field : {"refresh_token", "client_secret"}) if (credentials.contains(field)) wipe(credentials[field].get_ref<std::string&>());
    } else if (!refresh_token_.empty()) client_id_ = google_client_id(remote);
    if (!access_token_.empty()) expiry_ = std::chrono::steady_clock::now() + std::chrono::minutes(50);
}
GoogleDrive::~GoogleDrive() { wipe(access_token_); wipe(refresh_token_); wipe(client_secret_); }
void GoogleDrive::refresh() {
    if (refresh_token_.empty()) throw std::runtime_error("Google access token expired; authorize again or provide a refresh token");
    auto form = "grant_type=refresh_token&client_id=" + url_encode(client_id_) + "&refresh_token=" + url_encode(refresh_token_);
    if (!client_secret_.empty()) form += "&client_secret=" + url_encode(client_secret_);
    Bytes body(form.begin(), form.end()); wipe(form);
    Response response;
    try { response = http_request(token_url_, "POST", body, {"Content-Type: application/x-www-form-urlencoded"}); }
    catch (...) { sodium_memzero(body.data(), body.size()); throw; }
    sodium_memzero(body.data(), body.size());
    if (response.status != 200) throw std::runtime_error("Google token refresh failed: HTTP " + std::to_string(response.status) + "; authorize again");
    auto value = parse(response); sodium_memzero(response.body.data(), response.body.size());
    access_token_ = value.at("access_token").get<std::string>();
    if (access_token_.empty() || access_token_.size() > 16384 || value.value("token_type", std::string{}) != "Bearer") throw std::runtime_error("invalid Google access token response");
    expiry_ = std::chrono::steady_clock::now() + std::chrono::seconds(std::max(1, value.value("expires_in", 3600) - 60));
    wipe(value["access_token"].get_ref<std::string&>());
}
Response GoogleDrive::call(const std::string& url, const std::string& method, const Bytes& data, Headers headers) {
    if (!url.starts_with(origin_ + "/")) throw std::runtime_error("untrusted Google Drive request URL");
    if (access_token_.empty() || std::chrono::steady_clock::now() >= expiry_) refresh();
    bool refreshed = false;
    for (int attempt = 0; ; ++attempt) {
        auto authenticated = headers; authenticated.push_back("Authorization: Bearer " + access_token_);
        auto response = http_request(url, method, data, authenticated);
        if (response.status == 401 && !refreshed && !refresh_token_.empty()) { refresh(); refreshed = true; continue; }
        if (method == "GET" && attempt < 2 && (response.status == 429 || response.status == 502 || response.status == 503 || response.status == 504)) { std::this_thread::sleep_for(std::chrono::milliseconds(300 * (attempt + 1))); continue; }
        return response;
    }
}
std::string GoogleDrive::name(const std::string& relative) const {
    if (!(relative.starts_with("objects/") || relative.starts_with("snapshots/")) || !relative.ends_with(".cxs")) throw std::runtime_error("Google Drive only stores immutable objects and snapshots");
    auto id = relative.substr(relative.find('/') + 1); id.resize(id.size() - 4);
    if (id.size() != 64 || !std::all_of(id.begin(), id.end(), [](unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) throw std::runtime_error("invalid Google repository object ID");
    auto result = relative; std::replace(result.begin(), result.end(), '/', '.');
    return prefix_ + result;
}
std::vector<Json> GoogleDrive::query(const std::string& expression) {
    std::vector<Json> files; std::set<std::string> pages; std::string token;
    do {
        auto url = origin_ + "/drive/v3/files?spaces=appDataFolder&pageSize=1000&fields=" + url_encode("nextPageToken,incompleteSearch,files(id,name)") + "&q=" + url_encode("trashed = false and " + expression);
        if (!token.empty()) url += "&pageToken=" + url_encode(token);
        auto response = call(url, "GET"); require_success(response, "listing"); auto value = parse(response);
        if (value.value("incompleteSearch", false)) throw std::runtime_error("Google Drive returned an incomplete repository listing");
        for (auto& file : value.at("files")) {
            if (!identifier(file.at("id"))) throw std::runtime_error("invalid Google file ID");
            files.push_back(file);
            if (files.size() > 100000) throw std::runtime_error("Google repository listing exceeds safety limit");
        }
        token = value.value("nextPageToken", std::string{});
        if (!token.empty() && !pages.insert(token).second) throw std::runtime_error("Google pagination repeated a token");
    } while (!token.empty());
    return files;
}
std::string GoogleDrive::find(const std::string& relative) {
    if (auto found = files_.find(relative); found != files_.end()) return found->second;
    const auto expected = name(relative);
    auto candidates = query("name = '" + expected + "'");
    std::string id;
    for (const auto& file : candidates) if (file.at("name") == expected && (id.empty() || file.at("id").get<std::string>() < id)) id = file.at("id");
    if (!id.empty()) files_[relative] = id;
    return id;
}
std::vector<std::string> GoogleDrive::list(const std::string& directory) {
    if (directory != "objects" && directory != "snapshots") throw std::runtime_error("invalid Google repository collection");
    const auto prefix = prefix_ + directory + "."; std::set<std::string> names;
    for (const auto& file : query("name contains '" + prefix + "'")) {
        const auto full = file.at("name").get<std::string>();
        if (!full.starts_with(prefix)) continue;
        auto leaf = full.substr(prefix.size());
        if (leaf.size() != 68 || !leaf.ends_with(".cxs") || !std::all_of(leaf.begin(), leaf.begin() + 64, [](unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) continue;
        auto relative = directory + "/" + leaf, id = file.at("id").get<std::string>();
        if (!files_.contains(relative) || id < files_.at(relative)) files_[relative] = id;
        names.insert(leaf);
    }
    return {names.begin(), names.end()};
}
Response GoogleDrive::get(const std::string& relative, bool metadata_only) {
    const auto id = find(relative);
    if (id.empty()) return {404, {}, {}};
    if (metadata_only) return {200, {}, {}};
    auto response = call(origin_ + "/drive/v3/files/" + id + "?alt=media", "GET");
    require_success(response, "download");
    return response;
}
bool GoogleDrive::put_immutable(const std::string& relative, const Bytes& data) {
    if (!find(relative).empty()) return false;
    auto generated = call(origin_ + "/drive/v3/files/generateIds?space=appDataFolder&count=1", "GET"); require_success(generated, "ID allocation");
    auto id = parse(generated).at("ids").at(0).get<std::string>();
    if (!identifier(id)) throw std::runtime_error("invalid Google upload ID");
    auto metadata = Json{{"id", id}, {"name", name(relative)}, {"parents", Json::array({"appDataFolder"})}, {"mimeType", "application/octet-stream"}}.dump();
    auto session = call(origin_ + "/upload/drive/v3/files?uploadType=resumable&fields=id", "POST", Bytes(metadata.begin(), metadata.end()), {"Content-Type: application/json; charset=UTF-8", "X-Upload-Content-Type: application/octet-stream", "X-Upload-Content-Length: " + std::to_string(data.size())});
    require_success(session, "upload initiation");
    if (!session.location.starts_with(origin_ + "/upload/drive/v3/files?") || session.location.find_first_of("\r\n#") != std::string::npos) throw std::runtime_error("untrusted Google upload session URL");
    auto uploaded = call(session.location, "PUT", data, {"Content-Type: application/octet-stream"});
    require_success(uploaded, "upload");
    if (parse(uploaded).at("id") != id) throw std::runtime_error("Google upload returned a different file ID");
    files_[relative] = id;
    return true;
}
}
