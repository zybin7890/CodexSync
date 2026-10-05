#ifndef CODEX_SYNC_H
#define CODEX_SYNC_H
#ifdef _WIN32
# ifdef CODEX_SYNC_BUILD
#  define CXS_API __declspec(dllexport)
# else
#  define CXS_API __declspec(dllimport)
# endif
#else
# define CXS_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* UTF-8 JSON. Result is allocated by this library. Always call cxs_free().
   No secrets in result or error text. Operations are serialized per state directory.
   request: {"op":"discover|scan|backup|sync|history|restore", "config":"...", ...}
   Secret fields key_hex, dav_user, dav_password are optional transient overrides.
   Otherwise CXS_KEY_FILE, CXS_DAV_USER, CXS_DAV_PASSWORD are read from environment. */
CXS_API int cxs_run(const char* request_json, char** result_json);
CXS_API void cxs_free(char* result_json);
CXS_API const char* cxs_version(void);
#ifdef __cplusplus
}
#endif
#endif
