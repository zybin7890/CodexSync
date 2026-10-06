#include "core.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

namespace cxs {
namespace {
constexpr size_t maximum_response = 128 * 1024 * 1024;
#ifdef _WIN32
struct Internet {
    HINTERNET handle{};
    ~Internet() { if (handle) WinHttpCloseHandle(handle); }
    operator HINTERNET() const { return handle; }
};
#else
size_t receive_body(char* data, size_t size, size_t count, void* context) {
    auto& body = static_cast<Response*>(context)->body;
    const size_t n = size * count;
    if (n > maximum_response - body.size()) return 0;
    body.insert(body.end(), data, data + n);
    return n;
}
size_t receive_header(char* data, size_t size, size_t count, void* context) {
    const size_t n = size * count;
    std::string line(data, n), lower = line;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto& response = *static_cast<Response*>(context);
    auto field = lower.starts_with("etag:") ? &response.etag : lower.starts_with("location:") ? &response.location : nullptr;
    if (field) {
        auto value = line.substr(line.find(':') + 1);
        const auto begin = value.find_first_not_of(" \t\r\n");
        *field = begin == std::string::npos ? "" : value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1);
    }
    return n;
}
#endif
}
std::string url_encode(const std::string& value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') result += static_cast<char>(c);
        else { result += '%'; result += hex[c >> 4]; result += hex[c & 15]; }
    }
    return result;
}
Response http_request(const std::string& url, const std::string& method, const Bytes& data, const Headers& headers) {
    if (url.find_first_of("\r\n") != std::string::npos || data.size() > maximum_response) throw std::runtime_error("invalid HTTP request");
    for (const auto& header : headers) if (header.find_first_of("\r\n") != std::string::npos) throw std::runtime_error("invalid HTTP header");
#ifdef _WIN32
    auto wide = path(url).wstring();
    URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts)) throw std::runtime_error("invalid HTTP URL");
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength), target(parts.lpszUrlPath, parts.dwUrlPathLength);
    target.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    Internet session{WinHttpOpen(L"CodexSync/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.handle) throw std::runtime_error("HTTP session failed");
    WinHttpSetTimeouts(session, 10000, 10000, 30000, 90000);
    Internet connection{WinHttpConnect(session, host.c_str(), parts.nPort, 0)};
    Internet request{WinHttpOpenRequest(connection, path(method).c_str(), target.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)};
    if (!request.handle) throw std::runtime_error("HTTP request creation failed");
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof policy)) throw std::runtime_error("cannot disable HTTP redirects");
    std::wstring block;
    for (const auto& header : headers) block += path(header).wstring() + L"\r\n";
    if (!WinHttpSendRequest(request, block.c_str(), static_cast<DWORD>(block.size()), data.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<unsigned char*>(data.data()), static_cast<DWORD>(data.size()), static_cast<DWORD>(data.size()), 0) || !WinHttpReceiveResponse(request, nullptr)) throw std::runtime_error("HTTP connection or TLS verification failed");
    Response result; DWORD n = sizeof(DWORD), status{};
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &n, WINHTTP_NO_HEADER_INDEX)) throw std::runtime_error("missing HTTP status");
    result.status = static_cast<int>(status);
    auto field = [&](DWORD query) {
        wchar_t value[8192]{}; DWORD length = sizeof(value);
        return WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, value, &length, WINHTTP_NO_HEADER_INDEX) ? utf8(fs::path(value)) : std::string{};
    };
    result.etag = field(WINHTTP_QUERY_ETAG); result.location = field(WINHTTP_QUERY_LOCATION);
    if (method != "HEAD") for (;;) {
        DWORD available{};
        if (!WinHttpQueryDataAvailable(request, &available)) throw std::runtime_error("HTTP read failed");
        if (!available) break;
        if (available > maximum_response - result.body.size()) throw std::runtime_error("HTTP response too large");
        const auto offset = result.body.size(); result.body.resize(offset + available); DWORD got{};
        if (!WinHttpReadData(request, result.body.data() + offset, available, &got) || !got) throw std::runtime_error("incomplete HTTP response");
        result.body.resize(offset + got);
    }
    return result;
#else
    static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (initialized != CURLE_OK) throw std::runtime_error("curl initialization failed");
    auto handle = curl_easy_init();
    if (!handle) throw std::runtime_error("HTTP session failed");
    Response result; curl_slist* block = nullptr;
    for (const auto& header : headers) block = curl_slist_append(block, header.c_str());
    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, block);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 10L); curl_easy_setopt(handle, CURLOPT_TIMEOUT, 90L);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L); curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L); curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
    if (method == "HEAD") curl_easy_setopt(handle, CURLOPT_NOBODY, 1L);
    if (method == "PUT" || method == "POST" || method == "PATCH") {
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, data.empty() ? "" : reinterpret_cast<const char*>(data.data()));
        curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(data.size()));
    }
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, receive_body); curl_easy_setopt(handle, CURLOPT_WRITEDATA, &result);
    curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, receive_header); curl_easy_setopt(handle, CURLOPT_HEADERDATA, &result);
    const auto code = curl_easy_perform(handle); long status{};
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(block); curl_easy_cleanup(handle);
    if (code != CURLE_OK) throw std::runtime_error("HTTP connection or TLS verification failed");
    result.status = static_cast<int>(status);
    return result;
#endif
}
}
