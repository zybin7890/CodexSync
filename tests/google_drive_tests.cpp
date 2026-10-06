#include "core.hpp"
#include "google_drive.hpp"
#include "httplib.h"
#include <sodium.h>
#include <atomic>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

using cxs::Json;
using cxs::Bytes;
namespace fs = std::filesystem;
static void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static void text_file(const fs::path& p, const std::string& value) { cxs::write_atomic(p, Bytes(value.begin(), value.end())); }
static std::string decode(const std::string& value) {
    std::string result;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) { result += static_cast<char>(std::stoi(value.substr(i + 1, 2), nullptr, 16)); i += 2; }
        else result += value[i] == '+' ? ' ' : value[i];
    }
    return result;
}
static std::map<std::string, std::string> parameters(const std::string& value) {
    std::map<std::string, std::string> result;
    for (size_t start = 0; start < value.size();) {
        auto end = value.find('&', start); if (end == std::string::npos) end = value.size();
        auto equals = value.find('=', start);
        if (equals < end) result[decode(value.substr(start, equals - start))] = decode(value.substr(equals + 1, end - equals - 1));
        start = end + 1;
    }
    return result;
}
static void unset(const char* name) {
#ifdef _WIN32
    check(SetEnvironmentVariableW(cxs::path(name).c_str(), nullptr), "cannot clear fixture environment");
#else
    check(unsetenv(name) == 0, "cannot clear fixture environment");
#endif
}
// cpp-httplib rejects WebDAV extension verbs before routing, so this small
// loopback fixture reads the actual MKCOL/PROPFIND/conditional HTTP requests.
class DavFixture {
#ifdef _WIN32
    using Socket = SOCKET;
    static constexpr Socket invalid = INVALID_SOCKET;
    static void close(Socket s) { closesocket(s); }
#else
    using Socket = int;
    static constexpr Socket invalid = -1;
    static void close(Socket s) { ::close(s); }
#endif
    Socket listener = invalid;
    std::thread worker;
    std::atomic<bool> stopped{false};
    std::map<std::string, std::string> files;
    void serve(Socket client) {
        std::string input; char buffer[4096]; size_t headers_end{};
        while ((headers_end = input.find("\r\n\r\n")) == std::string::npos) {
            auto n = recv(client, buffer, sizeof buffer, 0); if (n <= 0) return;
            input.append(buffer, static_cast<size_t>(n)); check(input.size() < 32768, "fixture HTTP headers too large");
        }
        std::string method, target, version; std::istringstream lines(input.substr(0, headers_end));
        lines >> method >> target >> version; std::string line; std::getline(lines, line);
        std::map<std::string, std::string> headers;
        while (std::getline(lines, line)) {
            auto colon = line.find(':'); if (colon == std::string::npos) continue;
            auto name = line.substr(0, colon); std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            auto value = line.substr(colon + 1); value.erase(0, value.find_first_not_of(" \t")); if (value.ends_with('\r')) value.pop_back(); headers[name] = value;
        }
        const auto length = headers.contains("content-length") ? std::stoul(headers.at("content-length")) : 0;
        check(length < 65536, "fixture HTTP body too large");
        while (input.size() - headers_end - 4 < length) { auto n = recv(client, buffer, sizeof buffer, 0); if (n <= 0) return; input.append(buffer, static_cast<size_t>(n)); }
        auto file = target.substr(5); while (file.ends_with('/')) file.pop_back();
        std::string body; int status = 200;
        check(headers.at("authorization") == "Basic Zml4dHVyZS11c2VyOmZpeHR1cmUtcGFzcw==", "WebDAV Basic authentication changed");
        if (method == "MKCOL") status = 201;
        else if (method == "PROPFIND") {
            check(headers.at("depth") == "1", "WebDAV listing depth changed"); body = "<d:multistatus xmlns:d=\"DAV:\">";
            for (const auto& [name, data] : files) if (name.starts_with(file + "/")) body += "<d:response><d:href>/dav/" + name + "</d:href></d:response>";
            body += "</d:multistatus>"; status = 207;
        } else if (method == "GET" || method == "HEAD") { if (!files.contains(file)) status = 404; else body = files.at(file); }
        else if (method == "PUT") {
            if (headers["if-none-match"] == "*" && files.contains(file)) status = 412;
            else if (headers.contains("if-match") && headers.at("if-match") != "\"fixture-etag\"") status = 412;
            else { files[file] = input.substr(headers_end + 4, length); status = 201; }
        } else status = 405;
        auto output = "HTTP/1.1 " + std::to_string(status) + " Fixture\r\nConnection: close\r\nETag: \"fixture-etag\"\r\nContent-Type: application/octet-stream\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n";
        if (method != "HEAD") output += body;
        for (size_t offset = 0; offset < output.size();) { auto n = send(client, output.data() + offset, static_cast<int>(output.size() - offset), 0); if (n <= 0) return; offset += static_cast<size_t>(n); }
    }
public:
    std::string origin;
    DavFixture() {
        listener = socket(AF_INET, SOCK_STREAM, 0); check(listener != invalid, "WebDAV fixture socket failed");
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        check(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0 && listen(listener, 4) == 0, "WebDAV fixture bind failed");
#ifdef _WIN32
        int size = sizeof address;
#else
        socklen_t size = sizeof address;
#endif
        check(getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) == 0, "WebDAV fixture port failed");
        origin = "http://127.0.0.1:" + std::to_string(ntohs(address.sin_port));
        worker = std::thread([&] {
            while (!stopped) {
                auto client = accept(listener, nullptr, nullptr); if (client == invalid) break;
                try { serve(client); } catch (...) { const char error[] = "HTTP/1.1 500 Fixture\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"; send(client, error, sizeof(error) - 1, 0); }
                close(client);
            }
        });
    }
    ~DavFixture() {
        stopped = true;
#ifdef _WIN32
        shutdown(listener, SD_BOTH);
#else
        shutdown(listener, SHUT_RDWR);
#endif
        close(listener); if (worker.joinable()) worker.join();
    }
};
class DriveFixture {
public:
    struct File { std::string name, data; };
    httplib::Server server;
    std::thread thread;
    std::mutex mutex;
    std::map<std::string, File> files, sessions;
    std::atomic<int> counter{0}, refreshes{0}, pages{0}, downloads{0}, unauthorized{0};
    std::atomic<bool> once_401{true}, once_429{true}, evil_location{false}, incomplete{false};
    std::string origin, expected_challenge;
    DriveFixture() {
        server.new_task_queue = [] { return new httplib::ThreadPool(2); };
        server.set_pre_routing_handler([&](const auto& request, auto& response) {
            if (request.path == "/token") return httplib::Server::HandlerResponse::Unhandled;
            if (request.get_header_value("Authorization") != "Bearer fixture-access" || once_401.exchange(false)) { ++unauthorized; response.status = 401; return httplib::Server::HandlerResponse::Handled; }
            return httplib::Server::HandlerResponse::Unhandled;
        });
        server.Post("/token", [&](const auto& request, auto& response) {
            auto form = parameters(request.body);
            check(form.at("client_id") == "fixture.apps.googleusercontent.com", "OAuth client ID not preserved");
            if (form.at("grant_type") == "authorization_code") {
                check(form.at("code") == "fixture-code", "OAuth code not preserved");
                const auto& verifier = form.at("code_verifier"); unsigned char hash[32];
                crypto_hash_sha256(hash, reinterpret_cast<const unsigned char*>(verifier.data()), verifier.size());
                char challenge[128]; sodium_bin2base64(challenge, sizeof challenge, hash, sizeof hash, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
                check(expected_challenge == challenge, "OAuth PKCE verifier mismatch");
            } else { check(form.at("grant_type") == "refresh_token" && form.at("refresh_token") == "fixture-refresh", "invalid token refresh"); ++refreshes; }
            response.set_content(Json{{"access_token", "fixture-access"}, {"refresh_token", "fixture-refresh"}, {"token_type", "Bearer"}, {"expires_in", 3600}, {"scope", "https://www.googleapis.com/auth/drive.appdata"}}.dump(), "application/json");
        });
        server.Get("/drive/v3/files/generateIds", [&](const auto& request, auto& response) {
            check(request.get_param_value("space") == "appDataFolder", "wrong Drive ID space");
            response.set_content(Json{{"ids", Json::array({"id" + std::to_string(++counter)})}}.dump(), "application/json");
        });
        server.Get("/drive/v3/files", [&](const auto& request, auto& response) {
            if (once_429.exchange(false)) { response.status = 429; return; }
            check(request.get_param_value("spaces") == "appDataFolder", "wrong Drive listing scope");
            const auto expression = request.get_param_value("q");
            check(expression.starts_with("trashed = false and "), "trash filter missing");
            auto begin = expression.find('\''), end = expression.find_last_of('\'');
            auto target = expression.substr(begin + 1, end - begin - 1);
            auto matching = Json::array(); std::lock_guard lock(mutex);
            for (const auto& [id, file] : files) if (expression.find("name =") != std::string::npos ? file.name == target : file.name.find(target) != std::string::npos) matching.push_back({{"id", id}, {"name", file.name}});
            const auto offset = request.has_param("pageToken") ? std::stoul(request.get_param_value("pageToken")) : 0;
            auto result = Json{{"files", Json::array()}, {"incompleteSearch", incomplete.load()}};
            for (size_t i = offset; i < matching.size() && i < offset + 2; ++i) result["files"].push_back(matching[i]);
            if (offset + 2 < matching.size()) { result["nextPageToken"] = std::to_string(offset + 2); ++pages; }
            response.set_content(result.dump(), "application/json");
        });
        server.Post("/upload/drive/v3/files", [&](const auto& request, auto& response) {
            check(request.get_param_value("uploadType") == "resumable", "resumable upload not used");
            auto metadata = Json::parse(request.body);
            check(metadata.at("parents") == Json::array({"appDataFolder"}), "upload escaped appDataFolder");
            check(metadata.at("name").get<std::string>().starts_with("codex-sync-v1.default."), "wrong repository namespace");
            auto id = metadata.at("id").get<std::string>();
            { std::lock_guard lock(mutex); sessions[id] = {metadata.at("name"), ""}; }
            response.set_header("Location", evil_location ? "https://attacker.invalid/upload" : origin + "/upload/drive/v3/files?uploadType=resumable&upload_id=" + id);
        });
        server.Put("/upload/drive/v3/files", [&](const auto& request, auto& response) {
            auto id = request.get_param_value("upload_id"); std::lock_guard lock(mutex);
            auto file = sessions.at(id); file.data = request.body;
            check(file.data.size() >= 44 && file.data.substr(0, 3) == "CXS", "plaintext uploaded to Drive");
            check(file.data.find("fixture-refresh") == std::string::npos && file.name.find("google-oauth") == std::string::npos, "credential uploaded to Drive");
            check(!files.contains(id), "remote file overwritten"); files[id] = file;
            response.status = 201; response.set_content(Json{{"id", id}}.dump(), "application/json");
        });
        server.Get(R"(/drive/v3/files/(id[0-9]+))", [&](const auto& request, auto& response) {
            check(request.get_param_value("alt") == "media", "download missing alt=media");
            std::lock_guard lock(mutex); auto item = files.find(request.matches[1]);
            if (item == files.end()) { response.status = 404; return; }
            ++downloads; response.set_content(item->second.data, "application/octet-stream");
        });
        auto port = server.bind_to_any_port("127.0.0.1"); check(port > 0, "cannot bind Drive fixture");
        origin = "http://127.0.0.1:" + std::to_string(port);
        thread = std::thread([&] { server.listen_after_bind(); });
    }
    ~DriveFixture() { server.stop(); if (thread.joinable()) thread.join(); }
    void callback(const std::string& authorization, bool deny = false) {
        auto query = parameters(authorization.substr(authorization.find('?') + 1));
        check(authorization.starts_with("https://accounts.google.com/o/oauth2/v2/auth?"), "untrusted authorization URL");
        check(query.at("scope") == "https://www.googleapis.com/auth/drive.appdata" && query.at("code_challenge_method") == "S256", "overbroad scope or missing PKCE");
        expected_challenge = query.at("code_challenge");
        auto redirect = query.at("redirect_uri"); check(redirect.starts_with("http://127.0.0.1:"), "OAuth callback not loopback");
        const auto port = std::stoi(redirect.substr(17)); httplib::Client client("127.0.0.1", port); client.set_connection_timeout(3); client.set_read_timeout(3);
        auto invalid = client.Get("/?state=invalid&code=fixture-code"); check(invalid && invalid->status == 400, "OAuth state not checked");
        auto result = client.Get("/?state=" + cxs::url_encode(query.at("state")) + (deny ? "&error=access_denied" : "&code=fixture-code"));
        check(result && result->status == 200, "OAuth callback failed");
    }
};
int main(int argc, char** argv) {
    try {
        check(sodium_init() >= 0, "sodium init failed");
        for (auto name : {"CXS_GOOGLE_ACCESS_TOKEN", "CXS_GOOGLE_REFRESH_TOKEN", "CXS_GOOGLE_CLIENT_ID", "CXS_GOOGLE_CLIENT_SECRET"}) unset(name);
        const auto base = fs::absolute(argc > 1 ? cxs::path(argv[1]) : fs::current_path() / "google-contracts") / cxs::random_id();
        auto home = base / "source"; fs::create_directories(home);
        const auto source = home / "archived_sessions/rollout-old.jsonl";
        text_file(source, "private archived conversation fixture\n");
        cxs::generate_key(base / "master.key"); auto key_data = cxs::read_bytes(base / "master.key");
        Json secrets{{"key_hex", std::string(key_data.begin(), key_data.end())}};
        DriveFixture fixture;
        Json config{{"format", 1}, {"device", cxs::random_id()}, {"state", cxs::utf8(base / "state")}, {"roots", Json::array({{{"id", "codex_home"}, {"path", cxs::utf8(home)}}})}, {"exclude", Json::array()},
            {"remote", {{"provider", "google_drive"}, {"client_id", "fixture.apps.googleusercontent.com"}, {"repository", "default"}, {"allow_loopback_http", true}, {"test_api_origin", fixture.origin}, {"test_token_origin", fixture.origin}}}};
        auto authorized = cxs::google_authorize(config, secrets, [&](const auto& url) { fixture.callback(url); });
        check(authorized.at("authorized") == true, "Google authorization failed");
        auto credential = cxs::read_bytes(cxs::google_credential_path(config));
        check(std::string(credential.begin(), credential.end()).find("fixture-refresh") == std::string::npos, "refresh token stored in plaintext");
        bool denied = false;
        try { cxs::google_authorize(config, secrets, [&](const auto& url) { fixture.callback(url, true); }); } catch (...) { denied = true; }
        check(denied && cxs::read_bytes(cxs::google_credential_path(config)) == credential, "declined consent damaged previous authorization");
        const auto file = base / "sync.json"; text_file(file, config.dump());
        auto request = [&](const char* op) { auto value = secrets; value["op"] = op; value["config"] = cxs::utf8(file); return value; };
        const auto first = cxs::execute(request("backup"));
        check(first.at("uploaded_objects") == 1 && fixture.refreshes >= 2 && fixture.unauthorized == 1, "refresh/401 retry or encrypted backup failed");
        auto second = cxs::execute(request("backup")); check(second.at("uploaded_objects") == 0, "immutable object uploaded twice");
        { std::lock_guard lock(fixture.mutex); auto duplicate = std::find_if(fixture.files.begin(), fixture.files.end(), [](const auto& item){return item.second.name.find(".snapshots.")!=std::string::npos;})->second; fixture.files["id" + std::to_string(++fixture.counter)] = duplicate; }
        check(cxs::execute(request("history")).at("snapshots").size() == 2 && fixture.pages > 0, "pagination or duplicate-name handling failed");
        const auto other = base / "other-source"; fs::create_directories(other);
        text_file(other / "archived_sessions/rollout-old.jsonl", "different device fixture\n");
        config["device"] = cxs::random_id(); config["state"] = cxs::utf8(base / "other-state"); config["roots"][0]["path"] = cxs::utf8(other);
        cxs::private_directory(base / "other-state"); cxs::write_atomic(cxs::google_credential_path(config), credential); text_file(file, config.dump());
        cxs::execute(request("backup"));
        auto dry = request("sync"); dry["offline"] = true; dry["dry_run"] = true;
        auto preview = cxs::execute(dry); check(preview.at("remote_heads") == 2 && !preview.at("conflicts").empty(), "concurrent Drive branches were lost");
        auto sync = request("sync"); sync["offline"] = true;
        auto merged = cxs::execute(sync); check(!merged.at("conflicts").empty() && merged.at("deletions") == 0, "Drive conflict history not retained");
        const auto output = base / "restored"; auto restore = request("restore"); restore["snapshot"] = first.at("snapshot"); restore["output"] = cxs::utf8(output);
        cxs::execute(restore); check(cxs::read_bytes(source) == cxs::read_bytes(output / "codex_home/archived_sessions/rollout-old.jsonl") && fixture.downloads > 0, "remote byte-identical staged restore failed");
        auto wrong = secrets; wrong["key_hex"] = std::string(64, '0'); bool rejected = false;
        try { cxs::GoogleDrive drive(config, wrong); } catch (...) { rejected = true; } check(rejected, "wrong credential key accepted");
        fixture.incomplete = true; rejected = false;
        try { cxs::GoogleDrive drive(config, secrets); drive.list("snapshots"); } catch (...) { rejected = true; } check(rejected, "incomplete Google listing accepted"); fixture.incomplete = false;
        fixture.evil_location = true; rejected = false;
        try { cxs::GoogleDrive drive(config, secrets); drive.put_immutable("objects/" + std::string(64, 'e') + ".cxs", credential); } catch (...) { rejected = true; } check(rejected, "untrusted upload endpoint accepted");
        auto unsafe = config; unsafe["remote"]["test_api_origin"] = "https://attacker.invalid"; rejected = false;
        try { cxs::GoogleDrive drive(unsafe, secrets); } catch (...) { rejected = true; } check(rejected, "remote endpoint override accepted");
        auto overlapping=config;overlapping["state"]=overlapping["roots"][0]["path"];rejected=false;
        try{cxs::google_credential_path(overlapping);}catch(...){rejected=true;}check(rejected,"Google credentials could enter synchronized roots");
        DavFixture dav_fixture;
        Json dav_config{{"remote",{{"url",dav_fixture.origin+"/dav"},{"allow_loopback_http",true}}}};
        cxs::Store dav(dav_config,{{"dav_user","fixture-user"},{"dav_password","fixture-pass"}});dav.ensure();
        check(dav.put_immutable("objects/test.cxs",credential)&&!dav.put_immutable("objects/test.cxs",credential),"WebDAV immutable writes regressed");
        check(dav.get("objects/test.cxs")==credential&&dav.list("objects")==std::vector<std::string>{"test.cxs"},"WebDAV download/listing regressed");
        check(dav.request("GET","objects/test.cxs").etag=="\"fixture-etag\""&&dav.request("PUT","objects/test.cxs",credential,"\"wrong-etag\"").status==412,"WebDAV ETag/CAS regressed");
        std::cout << "GOOGLE_DRIVE_CONTRACTS_PASS: PKCE/state, consent denial, encrypted credentials, refresh/401/429, pagination, immutable deduplication, concurrent branches/conflicts, byte-identical remote restore, wrong-key and endpoint rejection, WebDAV HTTP regression\n";
        std::cout << "Fixtures retained: " << cxs::utf8(base) << '\n'; return 0;
    } catch (const std::exception& error) { std::cerr << "GOOGLE_DRIVE_CONTRACTS_FAIL: " << error.what() << '\n'; return 1; }
}
