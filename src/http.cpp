#include "core.hpp"
#include <algorithm>
#include <cctype>
#include <memory>
#include <stdexcept>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

namespace cxs {
namespace {
constexpr size_t maximum_response = 128 * 1024 * 1024;
thread_local const fs::path* upload_file{};
thread_local const fs::path* download_file{};
#ifdef _WIN32
std::wstring wide_text(const std::string& value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!length) throw std::runtime_error("invalid UTF-8 HTTP text");
    std::wstring result(length, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), length)) throw std::runtime_error("invalid UTF-8 HTTP text");
    return result;
}
std::string narrow_text(const wchar_t* value) {
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    if (!length) throw std::runtime_error("invalid HTTP response text");
    std::string result(length, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, result.data(), length, nullptr, nullptr)) throw std::runtime_error("invalid HTTP response text");
    result.pop_back();
    return result;
}
struct Internet {
    HINTERNET handle{};
    ~Internet() { if (handle) WinHttpCloseHandle(handle); }
    operator HINTERNET() const { return handle; }
};
struct Transport {
    Internet session{WinHttpOpen(L"CodexSync/0.2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    Internet connection;
    std::wstring host;
    INTERNET_PORT port{};
    INTERNET_SCHEME scheme{};
    Transport() {
        if (!session.handle) throw std::runtime_error("HTTP session failed");
        if (!WinHttpSetTimeouts(session, 10000, 10000, 30000, 90000)) throw std::runtime_error("HTTP timeout configuration failed");
    }
    HINTERNET connect(const std::wstring& next_host, INTERNET_PORT next_port, INTERNET_SCHEME next_scheme) {
        if (!connection.handle || host != next_host || port != next_port || scheme != next_scheme) {
            if (connection.handle) WinHttpCloseHandle(connection.handle);
            connection.handle = WinHttpConnect(session, next_host.c_str(), next_port, 0);
            if (!connection.handle) throw std::runtime_error("HTTP connection creation failed");
            host = next_host; port = next_port; scheme = next_scheme;
        }
        return connection;
    }
};
thread_local Transport* active_transport{};
[[noreturn]] void transport_failure() {
    const auto error=GetLastError();
    throw HttpFailure(error,error==ERROR_WINHTTP_TIMEOUT||error==ERROR_WINHTTP_CANNOT_CONNECT||error==ERROR_WINHTTP_CONNECTION_ERROR||error==ERROR_WINHTTP_NAME_NOT_RESOLVED||error==ERROR_WINHTTP_RESEND_REQUEST);
}
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
size_t send_file(char* data,size_t size,size_t count,void* context){auto& file=*static_cast<std::ifstream*>(context);file.read(data,static_cast<std::streamsize>(size*count));return static_cast<size_t>(file.gcount());}
size_t receive_file(char* data,size_t size,size_t count,void* context){auto& file=*static_cast<std::ofstream*>(context);const auto n=size*count;file.write(data,static_cast<std::streamsize>(n));return file?n:0;}
#endif
}
void with_http_session(const std::function<void()>& work) {
#ifdef _WIN32
    Transport transport;
    struct Binding {
        Transport* previous=active_transport;
        explicit Binding(Transport& transport) { active_transport=&transport; }
        ~Binding() { active_transport=previous; }
    } binding(transport);
#endif
    work();
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
    std::ifstream input;std::ofstream output;uint64_t input_size=0;
    if(upload_file){input.open(*upload_file,std::ios::binary);if(!input)throw std::runtime_error("cannot open prepared upload file");input_size=fs::file_size(*upload_file);}
    if(download_file){output.open(*download_file,std::ios::binary|std::ios::trunc);if(!output)throw std::runtime_error("cannot open staged download file");}
#ifdef _WIN32
    auto wide = wide_text(url);
    URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts)) throw std::runtime_error("invalid HTTP URL");
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength), target(parts.lpszUrlPath, parts.dwUrlPathLength);
    target.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    // A worker-owned scope retains proxy/TLS state. Handles are destroyed
    // explicitly before worker exit, never in a DLL thread-detach callback.
    std::unique_ptr<Transport> temporary;
    if (!active_transport) temporary=std::make_unique<Transport>();
    auto& transport=active_transport?*active_transport:*temporary;
    const auto connection = transport.connect(host, parts.nPort, parts.nScheme);
    Internet request{WinHttpOpenRequest(connection, wide_text(method).c_str(), target.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)};
    if (!request.handle) throw std::runtime_error("HTTP request creation failed");
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof policy)) throw std::runtime_error("cannot disable HTTP redirects");
    std::wstring block;
    for (const auto& header : headers) block += wide_text(header) + L"\r\n";
    if(upload_file)block+=L"Content-Length: "+std::to_wstring(input_size)+L"\r\n";
    if (!WinHttpSendRequest(request, block.c_str(), static_cast<DWORD>(block.size()), data.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<unsigned char*>(data.data()), static_cast<DWORD>(data.size()), upload_file?WINHTTP_IGNORE_REQUEST_TOTAL_LENGTH:static_cast<DWORD>(data.size()), 0))transport_failure();
    if(upload_file){std::vector<char> buffer(1024*1024);uint64_t sent=0;while(input){input.read(buffer.data(),buffer.size());const auto n=input.gcount();if(!n)break;DWORD written=0;if(!WinHttpWriteData(request,buffer.data(),static_cast<DWORD>(n),&written)||written!=n)transport_failure();sent+=written;}if(input.bad()||sent!=input_size)throw std::runtime_error("prepared upload file changed or cannot be read");}
    if(!WinHttpReceiveResponse(request,nullptr))transport_failure();
    Response result; DWORD n = sizeof(DWORD), status{};
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &n, WINHTTP_NO_HEADER_INDEX)) throw std::runtime_error("missing HTTP status");
    result.status = static_cast<int>(status);
    auto field = [&](DWORD query) {
        wchar_t value[8192]{}; DWORD length = sizeof(value);
        return WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, value, &length, WINHTTP_NO_HEADER_INDEX) ? narrow_text(value) : std::string{};
    };
    result.etag = field(WINHTTP_QUERY_ETAG); result.location = field(WINHTTP_QUERY_LOCATION);
    if (method != "HEAD") for (;;) {
        DWORD available{};
        if (!WinHttpQueryDataAvailable(request, &available)) transport_failure();
        if (!available) break;
        if(download_file){std::vector<char> buffer(1024*1024);DWORD got=0;if(!WinHttpReadData(request,buffer.data(),std::min<DWORD>(available,static_cast<DWORD>(buffer.size())),&got))transport_failure();if(!got)throw std::runtime_error("incomplete HTTP response");output.write(buffer.data(),got);if(!output)throw std::runtime_error("cannot write staged download");continue;}
        if (available > maximum_response - result.body.size()) throw std::runtime_error("HTTP response too large");
        const auto offset = result.body.size(); result.body.resize(offset + available); DWORD got{};
        if (!WinHttpReadData(request, result.body.data() + offset, available, &got)) transport_failure();
        if (!got) throw std::runtime_error("incomplete HTTP response");
        result.body.resize(offset + got);
    }
    if(download_file){output.flush();if(!output)throw std::runtime_error("cannot flush staged download");}return result;
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
    if(upload_file){curl_easy_setopt(handle,CURLOPT_UPLOAD,1L);curl_easy_setopt(handle,CURLOPT_READFUNCTION,send_file);curl_easy_setopt(handle,CURLOPT_READDATA,&input);curl_easy_setopt(handle,CURLOPT_INFILESIZE_LARGE,static_cast<curl_off_t>(input_size));}
    else if (method == "PUT" || method == "POST" || method == "PATCH") {
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, data.empty() ? "" : reinterpret_cast<const char*>(data.data()));
        curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(data.size()));
    }
    if(download_file){curl_easy_setopt(handle,CURLOPT_WRITEFUNCTION,receive_file);curl_easy_setopt(handle,CURLOPT_WRITEDATA,&output);}else{curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, receive_body); curl_easy_setopt(handle, CURLOPT_WRITEDATA, &result);}
    curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, receive_header); curl_easy_setopt(handle, CURLOPT_HEADERDATA, &result);
    const auto code = curl_easy_perform(handle); long status{};
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(block); curl_easy_cleanup(handle);
    if (code != CURLE_OK) throw std::runtime_error("HTTP connection or TLS verification failed");
    if(download_file){output.flush();if(!output)throw std::runtime_error("cannot flush staged download");}result.status = static_cast<int>(status);
    return result;
#endif
}
Response http_file_request(const std::string& url,const std::string& method,const fs::path& upload,const fs::path& download,const Headers& headers){struct Binding{const fs::path* old_upload=upload_file;const fs::path* old_download=download_file;~Binding(){upload_file=old_upload;download_file=old_download;}} binding;upload_file=upload.empty()?nullptr:&upload;download_file=download.empty()?nullptr:&download;return http_request(url,method,{},headers);}
}
