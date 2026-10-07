#include "core.hpp"
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

namespace cxs {
fs::path application_directory() {
#ifdef _WIN32
    // Locate the shared core, not the external program hosting the C API.
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&application_directory), &module))
        throw std::runtime_error("cannot locate application module");
    std::wstring buffer(32768, L'\0');
    const auto length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) throw std::runtime_error("cannot locate application directory");
    buffer.resize(length);
    return fs::path(buffer).parent_path();
#else
    return {};
#endif
}
bool portable_mode() {
#ifdef _WIN32
    return fs::is_regular_file(application_directory() / "portable.flag");
#else
    return false;
#endif
}
fs::path application_data_directory() {
    if (portable_mode()) return application_directory() / "data";
#ifdef _WIN32
    const auto local = path(env("LOCALAPPDATA"));
    if (!local.is_absolute()) throw std::runtime_error("LOCALAPPDATA must be an absolute directory");
    return local / "CodexSyncNative";
#else
    const auto state = env("XDG_STATE_HOME");
    return (state.empty() ? path(env("HOME")) / ".local/state" : path(state)) / "codex-sync";
#endif
}
fs::path default_configuration_path() { return application_data_directory() / "sync.json"; }
fs::path default_key_path() { return application_data_directory() / "secrets/master.key"; }
void normalize_application_config(Json& config) {
    const auto mode=config.value("payload_mode",std::string("encrypted"));if(mode!="encrypted"&&mode!="original"&&mode!="selective")throw std::runtime_error("payload_mode must be encrypted, original or selective");config["payload_mode"]=mode;
    for(const auto& rule:config.value("encryption_rules",Json::array())){auto relative=rule.at("path").get<std::string>();auto root=rule.at("root").get<std::string>();if(std::none_of(config.at("roots").begin(),config.at("roots").end(),[&](const Json& value){return value.at("id")==root;}))throw std::runtime_error("unknown encryption root");if(!relative.empty()){auto value=path(relative);if(value.is_absolute()||relative.find_first_of("\\:\r\n")!=std::string::npos)throw std::runtime_error("encryption paths must be relative and use forward slashes");for(const auto& part:value)if(part=="."||part=="..")throw std::runtime_error("invalid encryption path traversal");}}
    if (portable_mode()) config["state"] = utf8(application_data_directory() / "state");
    if(config.value("remote",Json::object()).value("provider",std::string())=="google_drive"&&!config["remote"].contains("storage_mode"))config["remote"]["storage_mode"]="appdata";
}
Json application_paths() {
    const auto data = application_data_directory();
    return {{"mode", portable_mode() ? "portable" : "installed"}, {"data", utf8(data)},
            {"config", utf8(default_configuration_path())}, {"key", utf8(default_key_path())},
            {"state", utf8(data / "state")}, {"preferences", utf8(data / "notices.ini")}};
}
}
