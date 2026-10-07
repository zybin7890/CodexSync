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
Json google_authorize(const Json& config, const Json& secrets, const std::function<void(const std::string&)>& open_browser, const std::function<void(const std::string&)>& progress) {
    const auto report = [&](const std::string& stage) { if (progress) { try { progress(stage); } catch (...) {} } };
    if (sodium_init() < 0) throw std::runtime_error("crypto initialization failed");
    const auto& remote = config.at("remote");
    const auto mode=remote.value("storage_mode",std::string("visible"));
    if(mode!="visible"&&mode!="appdata")throw std::runtime_error("invalid Google storage mode");
    const auto scope=std::string("https://www.googleapis.com/auth/")+(mode=="visible"?"drive.file":"drive.appdata");
    if (remote.value("provider", std::string{}) != "google_drive") throw std::runtime_error("select the Google Drive provider first");
    const auto application = google_oauth_client(config, secrets);
    const auto& client = application.id;
    const auto token_url = google_token_url(remote);
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
    std::mutex mutex; std::condition_variable ready, outcome;
    bool completed = false, denied = false, finalized = false, authorized = false; std::string code;
    const auto finish = [&](bool success) {
        { std::lock_guard lock(mutex); authorized = success; finalized = true; }
        outcome.notify_all();
    };
    server.Get("/", [&](const httplib::Request& request, httplib::Response& response) {
        response.set_header("Cache-Control", "no-store");
        response.set_header("X-Content-Type-Options", "nosniff");
        response.set_header("Connection", "close");
        const auto received = request.get_param_value("state");
        if (request.get_header_value("Host") != "127.0.0.1:" + std::to_string(port) || !request.get_header_value("Origin").empty() || request.params.count("state") != 1 || received.size() != state.size() || sodium_memcmp(received.data(), state.data(), state.size())) {
            response.status = 400; response.set_content("Invalid OAuth state. Return to CodexSync.", "text/plain"); return;
        }
        {
            std::lock_guard lock(mutex);
            if (finalized) { response.status = 410; response.set_content("授权已结束，请返回 CodexSync 查看结果。", "text/plain; charset=utf-8"); return; }
            if (completed) { response.status = 409; response.set_content("Authorization already completed.", "text/plain"); return; }
            const auto received_code = request.get_param_value("code");
            denied = request.has_param("error") || request.params.count("code") != 1 || received_code.empty() || received_code.size() > 4096;
            if (!denied) code = received_code;
            completed = true;
        }
        ready.notify_one();
        std::unique_lock lock(mutex);
        outcome.wait(lock, [&] { return finalized; });
        response.status = authorized ? 200 : 400;
        response.set_content(authorized ? "授权成功，凭据已加密保存在本机。可以关闭此页面并返回 CodexSync。" : "授权失败，凭据未更新。请返回 CodexSync 查看原因并重试。", "text/plain; charset=utf-8");
    });
    std::thread listener([&] { server.listen_after_bind(); });
    auto stop = [&] { server.stop(); if (listener.joinable()) listener.join(); };
    try {
        server.wait_until_ready();
        const auto url = "https://accounts.google.com/o/oauth2/v2/auth?client_id=" + url_encode(client)
            + "&redirect_uri=" + url_encode(redirect) + "&response_type=code&scope=" + url_encode(scope)
            + "&access_type=offline&prompt=consent&state=" + url_encode(state) + "&code_challenge=" + url_encode(challenge) + "&code_challenge_method=S256";
        report("browser_open"); open_browser(url);
        {
            std::unique_lock lock(mutex);
            if (!ready.wait_for(lock, std::chrono::seconds(120), [&] { return completed; })) throw std::runtime_error("Google authorization timed out after 120 seconds");
        }
        report("callback_received");
        if (denied) throw std::runtime_error("Google authorization was declined");
        auto secret = application.secret;
        auto form = "grant_type=authorization_code&client_id=" + url_encode(client) + "&code=" + url_encode(code) + "&code_verifier=" + url_encode(verifier) + "&redirect_uri=" + url_encode(redirect);
        if (!secret.empty()) form += "&client_secret=" + url_encode(secret);
        Bytes body(form.begin(), form.end()); wipe(form); wipe(code); wipe(verifier);
        Response response;
        report("token_exchange");
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
            if (scopes.find(" "+scope+" ") == std::string::npos) { wipe(secret); throw std::runtime_error("requested Google storage permission was not granted"); }
        }
        else {wipe(secret);throw std::runtime_error("Google authorization did not confirm granted scopes");}
        auto encoded = Json{{"format", 1}, {"client_id", client}, {"client_secret", secret}, {"refresh_token", token.at("refresh_token")},{"scope",token.at("scope")}}.dump();
        wipe(secret);
        for (auto field : {"access_token", "refresh_token"}) if (token.contains(field) && token[field].is_string()) wipe(token[field].get_ref<std::string&>());
        Bytes plain(encoded.begin(), encoded.end()); wipe(encoded);
        auto encrypted = seal(plain, key.value, "google-oauth-v1"); sodium_memzero(plain.data(), plain.size());
        private_directory(destination.parent_path()); write_atomic(destination, encrypted);
        finish(true); report("authorized"); stop();
        return {{"ok", true}, {"authorized", true}, {"provider", "google_drive"}, {"credential_storage", "encrypted local state; not uploaded"}};
    } catch (...) {
        finish(false); report("failed"); stop(); wipe(code); wipe(verifier); throw;
    }
}
}
