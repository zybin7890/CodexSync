#include "google_drive.hpp"
#include "httplib.h"
#include <sodium.h>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace cxs {
namespace {
struct PrivateKey {
    Key value;
    ~PrivateKey() { sodium_memzero(value.data(), value.size()); }
};
std::string base64url(const unsigned char* data, size_t size) {
    std::string encoded(sodium_base64_ENCODED_LEN(size, sodium_base64_VARIANT_URLSAFE_NO_PADDING), '\0');
    sodium_bin2base64(encoded.data(), encoded.size(), data, size, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    encoded.resize(encoded.find('\0')); return encoded;
}
void wipe(std::string& value) { sodium_memzero(value.data(), value.size()); }
}
Json google_authorize(const Json& config, const Json& secrets, const std::function<void(const std::string&)>& open_browser) {
    if (sodium_init() < 0) throw std::runtime_error("crypto initialization failed");
    const auto& remote = config.at("remote");
    if (remote.value("provider", std::string{}) != "google_drive") throw std::runtime_error("select the Google Drive provider first");
    const auto client = google_client_id(remote), token_url = google_token_url(remote);
    const auto destination = google_credential_path(config);
    PrivateKey key{credential_key(secrets)};
    std::string verifier = random_id() + random_id(), state = random_id() + random_id();
    unsigned char hash[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(hash, reinterpret_cast<const unsigned char*>(verifier.data()), verifier.size());
    const auto challenge = base64url(hash, sizeof hash);
    httplib::Server server;
    server.set_payload_max_length(4096); server.set_read_timeout(5, 0); server.set_write_timeout(5, 0);
    server.new_task_queue = [] { return new httplib::ThreadPool(2); };
    const auto port = server.bind_to_any_port("127.0.0.1");
    if (port < 1) throw std::runtime_error("cannot bind Google OAuth loopback callback");
    const auto redirect = "http://127.0.0.1:" + std::to_string(port) + "/";
    std::mutex mutex; std::condition_variable ready; bool completed = false, denied = false; std::string code;
    server.Get("/", [&](const httplib::Request& request, httplib::Response& response) {
        response.set_header("Cache-Control", "no-store");
        response.set_header("X-Content-Type-Options", "nosniff");
        const auto received = request.get_param_value("state");
        if (request.get_header_value("Host") != "127.0.0.1:" + std::to_string(port) || !request.get_header_value("Origin").empty() || request.params.count("state") != 1 || received.size() != state.size() || sodium_memcmp(received.data(), state.data(), state.size())) {
            response.status = 400; response.set_content("Invalid OAuth state. Return to CodexSync.", "text/plain"); return;
        }
        {
            std::lock_guard lock(mutex);
            if (completed) { response.status = 409; response.set_content("Authorization already completed.", "text/plain"); return; }
            const auto received_code = request.get_param_value("code");
            denied = request.has_param("error") || request.params.count("code") != 1 || received_code.empty() || received_code.size() > 4096;
            if (!denied) code = received_code;
            completed = true;
        }
        response.set_content(denied ? "Authorization declined. Return to CodexSync." : "Authorization received. You may close this tab and return to CodexSync.", "text/plain");
        ready.notify_one();
    });
    std::thread listener([&] { server.listen_after_bind(); });
    auto stop = [&] { server.stop(); if (listener.joinable()) listener.join(); };
    try {
        const auto url = "https://accounts.google.com/o/oauth2/v2/auth?client_id=" + url_encode(client)
            + "&redirect_uri=" + url_encode(redirect) + "&response_type=code&scope=" + url_encode("https://www.googleapis.com/auth/drive.appdata")
            + "&access_type=offline&prompt=consent&state=" + url_encode(state) + "&code_challenge=" + url_encode(challenge) + "&code_challenge_method=S256";
        open_browser(url);
        {
            std::unique_lock lock(mutex);
            if (!ready.wait_for(lock, std::chrono::seconds(120), [&] { return completed; })) throw std::runtime_error("Google authorization timed out after 120 seconds");
        }
        stop();
        if (denied) throw std::runtime_error("Google authorization was declined");
        auto secret = secrets.value("google_client_secret", env("CXS_GOOGLE_CLIENT_SECRET"));
        auto form = "grant_type=authorization_code&client_id=" + url_encode(client) + "&code=" + url_encode(code) + "&code_verifier=" + url_encode(verifier) + "&redirect_uri=" + url_encode(redirect);
        if (!secret.empty()) form += "&client_secret=" + url_encode(secret);
        Bytes body(form.begin(), form.end()); wipe(form); wipe(code); wipe(verifier);
        Response response;
        try { response = http_request(token_url, "POST", body, {"Content-Type: application/x-www-form-urlencoded"}); }
        catch (...) { sodium_memzero(body.data(), body.size()); wipe(secret); throw; }
        sodium_memzero(body.data(), body.size());
        if (response.status != 200) { wipe(secret); sodium_memzero(response.body.data(), response.body.size()); throw std::runtime_error("Google authorization token exchange failed: HTTP " + std::to_string(response.status)); }
        Json token;
        try { token = Json::parse(response.body.begin(), response.body.end()); }
        catch (...) { wipe(secret); sodium_memzero(response.body.data(), response.body.size()); throw std::runtime_error("invalid Google authorization response"); }
        sodium_memzero(response.body.data(), response.body.size());
        if (!token.contains("refresh_token") || !token["refresh_token"].is_string() || token["refresh_token"].get<std::string>().empty() || token.value("token_type", std::string{}) != "Bearer") { wipe(secret); throw std::runtime_error("Google did not return offline access; authorize again with consent"); }
        if (token.contains("scope")) {
            auto scopes = " " + token.at("scope").get<std::string>() + " ";
            if (scopes.find(" https://www.googleapis.com/auth/drive.appdata ") == std::string::npos) { wipe(secret); throw std::runtime_error("Google application-data permission was not granted"); }
        }
        auto encoded = Json{{"format", 1}, {"client_id", client}, {"client_secret", secret}, {"refresh_token", token.at("refresh_token")}}.dump();
        wipe(secret);
        for (auto field : {"access_token", "refresh_token"}) if (token.contains(field) && token[field].is_string()) wipe(token[field].get_ref<std::string&>());
        Bytes plain(encoded.begin(), encoded.end()); wipe(encoded);
        auto encrypted = seal(plain, key.value, "google-oauth-v1"); sodium_memzero(plain.data(), plain.size());
        private_directory(destination.parent_path()); write_atomic(destination, encrypted);
        return {{"ok", true}, {"authorized", true}, {"provider", "google_drive"}, {"credential_storage", "encrypted local state; not uploaded"}};
    } catch (...) {
        stop(); wipe(code); wipe(verifier); throw;
    }
}
}
