# Third-party notices

The AGPL-3.0-only license applies to original CodexSync work, not to a relicensing of the following upstream files. Their original notices must be retained.

## Included source and assets

| Component | Version/source | License | Location |
| --- | --- | --- | --- |
| nlohmann JSON | [3.12.0](https://github.com/nlohmann/json/tree/v3.12.0) | MIT, copyright Niels Lohmann and contributors | `third_party/json.hpp`, `third_party/licenses/json-MIT.txt` |
| cpp-httplib | [0.28.0](https://github.com/yhirose/cpp-httplib/tree/v0.28.0) | MIT, copyright Yuji Hirose | `third_party/httplib.h`, `third_party/licenses/httplib-MIT.txt` |
| tinyxml2 | [11.0.0](https://github.com/leethomason/tinyxml2/tree/11.0.0) | zlib, original code by Lee Thomason | `third_party/tinyxml2.cpp` and `.h`, full notice retained in both files |
| SQLite amalgamation | 3.53.4, [SQLite copyright](https://sqlite.org/copyright.html) | Public domain, original dedication retained in source | `third_party/sqlite3.c` and `.h` |
| Microsoft Fluent UI System Icons | [upstream](https://github.com/microsoft/fluentui-system-icons) | MIT, copyright Microsoft Corporation | `ui/icons/`, `ui/icons/LICENSE.txt` |

The original CodexSync application mark in `ui/codexsync.svg` and its PNG/ICO renderings are not Microsoft assets.

## External build/runtime dependencies

- [libsodium](https://github.com/jedisct1/libsodium): ISC.
- [Qt 6](https://www.qt.io/licensing/): upstream module-specific LGPL/GPL/commercial terms. Qt binaries are not included in this source repository; consult the licenses of the installed SDK/modules when distributing a binary build.
- [libcurl](https://curl.se/docs/copyright.html), Linux WebDAV: curl license.
- Windows WinHTTP and system libraries: provided by Windows.

No compiled Qt/libsodium SDK, local credentials, Codex conversation data, or generated test repositories are distributed in this source repository.
