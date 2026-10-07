#include "google_drive.hpp"
#include <sodium.h>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

namespace cxs {
namespace {
struct PrivateBytes {
    Bytes value;
    ~PrivateBytes() { sodium_memzero(value.data(), value.size()); }
};
struct PrivateKey {
    Key value;
    ~PrivateKey() { sodium_memzero(value.data(), value.size()); }
};
fs::path client_file(const Json& config) {
    return google_credential_path(config).parent_path() / "google-client.cxs";
}
fs::path system_client_file() {
#ifdef _WIN32
    const auto managed = application_data_directory() / "credentials/google-desktop-client.dpapi";
    if (portable_mode() || fs::is_regular_file(managed)) return managed;
    auto local = env("LOCALAPPDATA");
    if (!local.empty() && path(local).is_absolute())
        return path(local) / "CodexSync/credentials/google-desktop-client.dpapi";
#endif
    return {};
}
bool explicit_client(const Json& remote) {
    return !remote.value("client_id", std::string{}).empty() || !env("CXS_GOOGLE_CLIENT_ID").empty();
}
void clear_profile(Json& value) {
    if (!value.is_object()) return;
    for (const auto* field : {"client_secret", "refresh_token", "access_token"})
        if (value.contains(field) && value[field].is_string()) {
            auto& text = value[field].get_ref<std::string&>();
            sodium_memzero(text.data(), text.size());
        }
}
GoogleClient parse_profile(const Bytes& plain, bool imported = false) {
    Json value;
    try { value = Json::parse(plain.begin(), plain.end()); }
    catch (...) { throw std::runtime_error("invalid Google client profile JSON"); }
    if (value.contains("installed")) value = std::move(value.at("installed"));
    try {
        if (!value.is_object() || (!imported && value.value("format", 0) != 1))
            throw std::runtime_error("unsupported Google client profile");
        GoogleClient client(google_client_id({{"client_id", value.at("client_id")}}),
                            value.value("client_secret", std::string{}));
        if (client.secret.size() > 4096 || client.secret.find_first_of("\r\n") != std::string::npos)
            throw std::runtime_error("invalid Google desktop client secret");
        clear_profile(value);
        return client;
    } catch (...) { clear_profile(value); throw std::runtime_error("invalid Google desktop client profile"); }
}
PrivateBytes encrypted_profile(const fs::path& file, const Json& secrets, const char* domain) {
    PrivateKey key{credential_key(secrets)};
    return {open(read_bytes(file, 64 * 1024), key.value, domain)};
}
}
GoogleClient::GoogleClient(std::string identifier, std::string credential)
    : id(std::move(identifier)), secret(std::move(credential)) {}
GoogleClient::~GoogleClient() { sodium_memzero(secret.data(), secret.size()); }

bool google_client_ready(const Json& config) {
    try {
        const auto& remote = config.at("remote");
        if (explicit_client(remote)) { google_client_id(remote); return true; }
        if (remote.contains("test_token_origin") || remote.contains("test_api_origin")) return false;
        if (fs::exists(client_file(config)) || fs::exists(google_credential_path(config))) return true;
        const auto file = system_client_file();
        return !file.empty() && fs::is_regular_file(file);
    } catch (...) { return false; }
}
GoogleClient google_oauth_client(const Json& config, const Json& secrets) {
    const auto& remote = config.at("remote");
    GoogleClient overrides({}, secrets.value("google_client_secret", env("CXS_GOOGLE_CLIENT_SECRET")));
    if (explicit_client(remote)) return {google_client_id(remote), std::move(overrides.secret)};
    // Never send managed real application credentials to loopback protocol fixtures.
    if (remote.contains("test_token_origin") || remote.contains("test_api_origin"))
        throw std::runtime_error("Google test endpoints require an explicit fixture client ID");
    auto load = [&]() -> GoogleClient {
        const auto imported = client_file(config);
        if (fs::exists(imported)) {
            auto plain = encrypted_profile(imported, secrets, "google-client-v1");
            return parse_profile(plain.value);
        }
        const auto authorized = google_credential_path(config);
        if (fs::exists(authorized)) {
            auto plain = encrypted_profile(authorized, secrets, "google-oauth-v1");
            return parse_profile(plain.value);
        }
#ifdef _WIN32
        const auto file = system_client_file();
        if (!file.empty() && fs::is_regular_file(file)) {
            auto encrypted = read_bytes(file, 64 * 1024);
            const char entropy[] = "CodexSync-google-client-v1";
            DATA_BLOB input{static_cast<DWORD>(encrypted.size()), encrypted.data()};
            DATA_BLOB context{sizeof(entropy) - 1, reinterpret_cast<BYTE*>(const_cast<char*>(entropy))};
            DATA_BLOB output{};
            if (!CryptUnprotectData(&input, nullptr, &context, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
                throw std::runtime_error("cannot unlock Google client profile for this Windows user");
            PrivateBytes plain;
            try { plain.value.assign(output.pbData, output.pbData + output.cbData); }
            catch (...) { sodium_memzero(output.pbData, output.cbData); LocalFree(output.pbData); throw; }
            sodium_memzero(output.pbData, output.cbData); LocalFree(output.pbData);
            return parse_profile(plain.value);
        }
#endif
        throw std::runtime_error("Google client profile is missing; import it securely or use advanced OAuth settings");
    };
    auto client = load();
    if (!overrides.secret.empty()) {
        sodium_memzero(client.secret.data(), client.secret.size());
        client.secret = std::move(overrides.secret);
    }
    return client;
}
Json google_import_client(const Json& config, const Json& secrets, const Bytes& profile) {
    if (profile.size() > 64 * 1024) throw std::runtime_error("Google client profile is too large");
    const auto destination = client_file(config);
    if (fs::exists(destination)) throw std::runtime_error("refusing to overwrite an imported Google client profile");
    auto client = parse_profile(profile, true);
    PrivateKey key{credential_key(secrets)};
    auto text = Json{{"format", 1}, {"client_id", client.id}, {"client_secret", client.secret}}.dump();
    PrivateBytes plain{Bytes(text.begin(), text.end())};
    sodium_memzero(text.data(), text.size());
    auto encrypted = seal(plain.value, key.value, "google-client-v1");
    private_directory(destination.parent_path()); write_atomic(destination, encrypted);
    return {{"ok", true}, {"client_imported", true}, {"credential_storage", "encrypted local state; not uploaded"}};
}
}
