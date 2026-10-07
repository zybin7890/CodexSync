// Standalone elevated VSS requester. No core DLL, credentials, or networking.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <vss.h>
#include <vswriter.h>
#include <vsbackup.h>
#include "json.hpp"
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;
constexpr ULONGLONG lifetime_ms = 15 * 60 * 1000;

struct Failure {
    const char* stage;
    HRESULT code;
};
void check(HRESULT code, const char* stage) {
    if (FAILED(code)) throw Failure{stage, code};
}
[[noreturn]] void invalid(const char* stage) {
    throw Failure{stage, HRESULT_FROM_WIN32(ERROR_INVALID_DATA)};
}
[[noreturn]] void windows_failure(const char* stage) {
    const auto code = GetLastError();
    throw Failure{stage, HRESULT_FROM_WIN32(code ? code : ERROR_GEN_FAILURE)};
}
std::string error_text(const Failure& failure) {
    char code[16]{};
    std::snprintf(code, sizeof code, "0x%08lX", static_cast<unsigned long>(failure.code));
    return std::string(failure.stage) + ": HRESULT " + code;
}
std::string utf8(const std::wstring& text) {
    if (text.size() > static_cast<size_t>(std::numeric_limits<int>::max())) invalid("encoding");
    const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!size) windows_failure("encoding");
    std::string out(size, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), out.data(), size, nullptr, nullptr)) windows_failure("encoding");
    return out;
}
std::wstring wide(const std::string& text) {
    if (text.empty() || text.size() > 32768 || text.find('\0') != std::string::npos) invalid("request");
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0);
    if (!size) windows_failure("encoding");
    std::wstring out(size, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), out.data(), size)) windows_failure("encoding");
    return out;
}
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(std::exchange(other.value, INVALID_HANDLE_VALUE)) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE && value) CloseHandle(value); }
};
template<class T> struct Release {
    void operator()(T* value) const noexcept { if (value) value->Release(); }
};
template<class T> using Com = std::unique_ptr<T, Release<T>>;
struct ComApartment {
    bool initialized = false;
    ~ComApartment() { if (initialized) CoUninitialize(); }
};

bool drive_root(const std::wstring& value) {
    return value.size() == 3 && ((value[0] >= L'A' && value[0] <= L'Z') ||
        (value[0] >= L'a' && value[0] <= L'z')) && value[1] == L':' && value[2] == L'\\';
}
void require_missing(const fs::path& path) {
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) invalid("output_exists");
    const auto code = GetLastError();
    if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND) windows_failure("output_attributes");
}
void regular_handle(HANDLE handle, const char* stage) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info)) windows_failure(stage);
    if (GetFileType(handle) != FILE_TYPE_DISK ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) invalid(stage);
}
// Pin every directory against rename/deletion. OPEN_REPARSE_POINT avoids resolving
// a junction before checking its attributes; no arbitrary privileged output paths.
std::vector<Handle> pin_job(const fs::path& job) {
    const auto value = job.native();
    if (value.size() <= 3 || value.size() >= 32700 || !drive_root(value.substr(0, 3)) ||
        value.find(L'/') != std::wstring::npos || value.find(L'\0') != std::wstring::npos) invalid("job_directory");
    std::vector<Handle> pins;
    auto current = job.root_path();
    for (const auto& component : job.relative_path()) {
        const auto part = component.native();
        if (part.empty()) continue; // A terminal separator is harmless.
        if (part == L"." || part == L".." || part.find(L':') != std::wstring::npos ||
            part.back() == L'.' || part.back() == L' ') invalid("job_directory");
        current /= component;
        Handle handle(CreateFileW(current.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (handle.value == INVALID_HANDLE_VALUE) windows_failure("job_directory");
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle.value, &info)) windows_failure("job_directory");
        if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) invalid("job_directory");
        pins.push_back(std::move(handle));
    }
    if (pins.empty()) invalid("job_directory");
    return pins;
}
Json read_request(const fs::path& job) {
    const auto path = job / L"request.json";
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (file.value == INVALID_HANDLE_VALUE) windows_failure("request_open");
    regular_handle(file.value, "request_file");
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.value, &size)) windows_failure("request_size");
    if (size.QuadPart <= 0 || size.QuadPart > 65536) invalid("request_size");
    std::string contents(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read{};
    if (!ReadFile(file.value, contents.data(), static_cast<DWORD>(contents.size()), &read, nullptr))
        windows_failure("request_read");
    if (read != contents.size()) invalid("request_read");
    auto request = Json::parse(contents, nullptr, false);
    if (!request.is_object() || request.size() != 3 || request.value("format", Json{}) != 1 ||
        !request.contains("volumes") || !request.at("volumes").is_array() ||
        !request.contains("parent_creation_time") || !request.at("parent_creation_time").is_number_unsigned())
        invalid("request");
    return request;
}
// Null security attributes intentionally inherit the job directory's parent-user /
// Administrators / SYSTEM ACL, including when UAC uses another administrator.
void write_json(const fs::path& job, const wchar_t* filename, const Json& document) {
    const auto destination = job / filename;
    auto temporary = destination;
    temporary += L".tmp";
    require_missing(destination);
    require_missing(temporary);
    const auto contents = document.dump();
    {
        Handle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (file.value == INVALID_HANDLE_VALUE) windows_failure("output_create");
        regular_handle(file.value, "output_file");
        DWORD written{};
        if (!WriteFile(file.value, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr))
            windows_failure("output_write");
        if (written != contents.size()) invalid("output_write");
        if (!FlushFileBuffers(file.value)) windows_failure("output_flush");
    }
    // No REPLACE_EXISTING: an unexpected target must never be overwritten.
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
        windows_failure("output_commit");
}
bool done(const fs::path& job) {
    const auto attributes = GetFileAttributesW((job / L"done").c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const auto code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return false;
        windows_failure("done_attributes");
    }
    if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) invalid("done_file");
    return true;
}
bool parent_exited(HANDLE parent) {
    const auto result = WaitForSingleObject(parent, 0);
    if (result == WAIT_FAILED) windows_failure("parent_wait");
    return result == WAIT_OBJECT_0;
}
DWORD parent_pid(const wchar_t* text) {
    uint64_t value = 0;
    if (!text || !*text) invalid("parent_pid");
    for (; *text; ++text) {
        if (*text < L'0' || *text > L'9') invalid("parent_pid");
        value = value * 10 + (*text - L'0');
        if (value > std::numeric_limits<DWORD>::max()) invalid("parent_pid");
    }
    if (!value || value == GetCurrentProcessId()) invalid("parent_pid");
    return static_cast<DWORD>(value);
}
struct Volume { std::wstring root, guid; VSS_ID snapshot = GUID_NULL; };
std::vector<Volume> volumes(const Json& request) {
    const auto& list = request.at("volumes");
    if (list.empty() || list.size() > 8) invalid("volumes");
    std::vector<Volume> result;
    for (const auto& value : list) {
        if (!value.is_string()) invalid("volumes");
        auto root = wide(value.get<std::string>());
        if (!drive_root(root) || GetDriveTypeW(root.c_str()) != DRIVE_FIXED) invalid("local_volume");
        root[0] = static_cast<wchar_t>(std::towupper(root[0]));
        wchar_t name[128]{};
        if (!GetVolumeNameForVolumeMountPointW(root.c_str(), name, static_cast<DWORD>(std::size(name))))
            windows_failure("volume_guid");
        for (const auto& previous : result)
            if (!_wcsicmp(previous.guid.c_str(), name)) invalid("duplicate_volume");
        result.push_back({std::move(root), name, GUID_NULL});
    }
    return result;
}
std::string guid_text(const GUID& value) {
    wchar_t text[40]{};
    if (!StringFromGUID2(value, text, static_cast<int>(std::size(text)))) invalid("snapshot_identifier");
    return utf8(text);
}
void wait_snapshot(IVssAsync* operation, HANDLE parent, const fs::path& job, ULONGLONG deadline) {
    for (;;) {
        if (parent_exited(parent) || done(job)) {
            operation->Cancel();
            throw Failure{"snapshot_cancelled", HRESULT_FROM_WIN32(ERROR_CANCELLED)};
        }
        if (GetTickCount64() >= deadline) {
            operation->Cancel();
            throw Failure{"snapshot_timeout", HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
        }
        check(operation->Wait(500), "snapshot_wait");
        HRESULT status{};
        check(operation->QueryStatus(&status, nullptr), "snapshot_status");
        if (status == VSS_S_ASYNC_FINISHED) return;
        if (status == VSS_S_ASYNC_PENDING) continue;
        if (status == VSS_S_ASYNC_CANCELLED)
            throw Failure{"snapshot_cancelled", HRESULT_FROM_WIN32(ERROR_CANCELLED)};
        check(status, "snapshot_create");
        throw Failure{"snapshot_status", E_UNEXPECTED};
    }
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    const auto deadline = GetTickCount64() + lifetime_ms;
    fs::path job;
    std::vector<Handle> directory_pins;
    Handle parent;
    ComApartment apartment;
    Com<IVssBackupComponents> backup;
    VSS_ID set = GUID_NULL;
    std::vector<Volume> requested_volumes;
    bool job_valid = false, response_written = false, snapshot_complete = false;
    int result = 1;
    try {
        job = fs::path(argv[1]);
        directory_pins = pin_job(job);
        require_missing(job / L"response.json");
        require_missing(job / L"response.json.tmp");
        require_missing(job / L"cleanup.json");
        require_missing(job / L"cleanup.json.tmp");
        job_valid = true;
        const auto request = read_request(job);
        const auto pid = parent_pid(argv[2]);
        parent.value = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!parent.value) windows_failure("parent_open");
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!GetProcessTimes(parent.value, &created, &exited, &kernel, &user)) windows_failure("parent_identity");
        const uint64_t created_at = (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        if (created_at != request.at("parent_creation_time").get<uint64_t>() || parent_exited(parent.value))
            invalid("parent_identity");
        if (done(job)) throw Failure{"job_cancelled", HRESULT_FROM_WIN32(ERROR_CANCELLED)};
        requested_volumes = volumes(request);

        check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "com_initialize");
        apartment.initialized = true;
        check(CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
            RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr), "com_security");
        IVssBackupComponents* raw = nullptr;
        const auto created_backup = CreateVssBackupComponents(&raw);
        backup.reset(raw);
        check(created_backup, "vss_initialize");
        check(backup->InitializeForBackup(), "vss_backup_initialize");
        // FILE_SHARE_BACKUP is NO_WRITERS and nonpersistent/auto-release.
        check(backup->SetContext(VSS_CTX_FILE_SHARE_BACKUP), "vss_context");
        check(backup->SetBackupState(false, false, VSS_BT_COPY, false), "vss_backup_state");
        check(backup->StartSnapshotSet(&set), "vss_start_set");
        for (auto& volume : requested_volumes) {
            BOOL supported = FALSE;
            check(backup->IsVolumeSupported(GUID_NULL, volume.guid.data(), &supported), "vss_volume_support");
            if (!supported) throw Failure{"vss_volume_support", VSS_E_VOLUME_NOT_SUPPORTED};
            check(backup->AddToSnapshotSet(volume.guid.data(), GUID_NULL, &volume.snapshot), "vss_add_volume");
        }
        IVssAsync* async_raw = nullptr;
        const auto started = backup->DoSnapshotSet(&async_raw);
        Com<IVssAsync> operation(async_raw);
        check(started, "vss_snapshot_start");
        if (!operation) throw Failure{"vss_snapshot_start", E_UNEXPECTED};
        wait_snapshot(operation.get(), parent.value, job, deadline);
        snapshot_complete = true;
        Json roots = Json::object();
        for (const auto& volume : requested_volumes) {
            VSS_SNAPSHOT_PROP property{};
            check(backup->GetSnapshotProperties(volume.snapshot, &property), "vss_snapshot_properties");
            struct PropertyGuard {
                VSS_SNAPSHOT_PROP& value;
                ~PropertyGuard() { VssFreeSnapshotProperties(&value); }
            } guard{property};
            if (!property.m_pwszSnapshotDeviceObject || !IsEqualGUID(property.m_SnapshotSetId, set))
                invalid("vss_snapshot_properties");
            std::wstring device(property.m_pwszSnapshotDeviceObject);
            if (!device.starts_with(L"\\\\?\\GLOBALROOT\\Device\\HarddiskVolumeShadowCopy"))
                invalid("vss_snapshot_path");
            if (device.back() != L'\\') device += L'\\';
            roots[utf8(volume.root)] = utf8(device);
        }
        if (parent_exited(parent.value) || done(job))
            throw Failure{"job_cancelled", HRESULT_FROM_WIN32(ERROR_CANCELLED)};
        write_json(job, L"response.json", {{"ok", true}, {"roots", roots}, {"set_id", guid_text(set)}});
        response_written = true;
        while (!parent_exited(parent.value) && !done(job)) {
            if (GetTickCount64() >= deadline)
                throw Failure{"job_timeout", HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
            const auto waited = WaitForSingleObject(parent.value, 500);
            if (waited == WAIT_FAILED) windows_failure("parent_wait");
        }
        result = 0;
    } catch (const Failure& failure) {
        if (job_valid && !response_written) {
            try { write_json(job, L"response.json", {{"ok", false}, {"error", error_text(failure)}}); }
            catch (...) {}
        }
    } catch (...) {
        if (job_valid && !response_written) {
            try { write_json(job, L"response.json", {{"ok", false}, {"error", error_text({"helper", E_UNEXPECTED})}}); }
            catch (...) {}
        }
    }

    LONG deleted = 0;
    bool cleanup_ok = true;
    std::string cleanup_error;
    if (backup && !IsEqualGUID(set, GUID_NULL)) {
        if (!snapshot_complete) backup->AbortBackup();
        VSS_ID nondeleted = GUID_NULL;
        const auto cleanup = backup->DeleteSnapshots(set, VSS_OBJECT_SNAPSHOT_SET, TRUE, &deleted, &nondeleted);
        if (cleanup != VSS_E_OBJECT_NOT_FOUND && (FAILED(cleanup) || !IsEqualGUID(nondeleted, GUID_NULL) ||
            (snapshot_complete && deleted != static_cast<LONG>(requested_volumes.size())))) {
            cleanup_ok = false;
            cleanup_error = error_text({"vss_cleanup", FAILED(cleanup) ? cleanup : E_UNEXPECTED});
        }
    }
    // Release is the automatic cleanup fallback even if explicit deletion fails.
    backup.reset();
    if (job_valid) {
        try { write_json(job, L"cleanup.json", {{"ok", cleanup_ok}, {"deleted", deleted}, {"error", cleanup_error}}); }
        catch (...) { cleanup_ok = false; }
    }
    return cleanup_ok ? result : 1;
}
