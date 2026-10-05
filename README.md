# CodexSync

<img src="ui/codexsync-128.png" width="80" alt="CodexSync icon">

Codex 本地数据的加密备份与同步工具，使用 C23 / C++23、Qt 6 和 WebDAV。提供 Windows 11 风格 GUI、终端 CLI、HTTP API 与 C API。非 OpenAI 官方项目。

**开发预览，尚非生产版本。** Windows 已编译运行，并通过本地仓库的历史记录备份/恢复测试；Debian GUI、DEB 包、真实 WebDAV 服务端互操作及跨设备迁移尚未验收。请先使用测试数据，不要直接覆盖唯一的数据副本。

## 数据范围

- Codex 主目录、桌面应用状态、配置、插件、skills、代理配置及其他显式添加的本地目录。
- `sessions` 与 `archived_sessions` 的全部日期记录，包括 `.jsonl.zst` 压缩文件和未进入索引的归档，压缩文件原样保存。
- 线程索引、`session_index.jsonl` 与 SQLite 历史数据库；运行中的 SQLite 使用在线备份，保留 WAL 中已提交的数据。
- 迁移目录的联接/符号链接会解析为真实根目录；外置的活动/归档目录单独映射。其他链接目标需显式添加。

`conversations` 命令核对文件和线程索引引用，报告缺失或被排除的数据。文件数量不等于唯一对话数量，远程云端独有的历史不在本地同步范围。

## 安全与限制

- 使用 libsodium XChaCha20-Poly1305 加密数据与清单，增量对象通过带密钥的摘要寻址；不同设备使用相同主密钥。
- 文件级同步，不传播删除；并发冲突保留历史版本，不进行 SQLite 记录级合并。
- 应用同步改动前须关闭 Codex，并明确使用 `--offline`。恢复默认写入独立目录，不覆盖源目录。
- 主密钥、WebDAV 密码和 API 令牌不得提交到 Git 或放入同步根目录。请单独安全备份密钥。
- 系统绑定的凭据、设备绝对路径和运行时数据库不能保证跨系统直接复用，迁移后可能需要重新登录或重新映射路径。
- HTTP API 仅监听 `127.0.0.1`，要求 Bearer Token。GUI 不会在启动时自动备份或同步。

## 构建

依赖：CMake 3.25+、支持 C++23 的编译器、libsodium；GUI 需要 Qt 6.8+ 的 Widgets、Quick、QuickControls2、Concurrent、Network、Svg。Windows WebDAV 使用 WinHTTP，Linux 使用 libcurl。SQLite、JSON、HTTP API 和 XML 的源码依赖已包含。

### Windows

在 x64 Visual Studio 开发者 PowerShell 中运行，替换 Qt 和 libsodium SDK 路径：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64" -DCXS_SODIUM_ROOT="C:/deps/libsodium" -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
windeployqt --qmldir ui build/CodexSync.exe
./build/CodexSync.exe
```

libsodium SDK 需提供 `include/sodium.h` 和 `x64/Release/v143/dynamic/libsodium.lib`、`libsodium.dll`。仅构建终端/API 时加 `-DCXS_GUI=OFF`。

### Debian / Linux

终端/API 模式不依赖 Qt：

```sh
sudo apt install cmake ninja-build g++ pkg-config libsodium-dev libcurl4-openssl-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCXS_GUI=OFF -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/codex-sync --help
```

GUI 需另外安装 Qt 6.8+ SDK/开发模块，改为 `-DCXS_GUI=ON`，必要时通过 `CMAKE_PREFIX_PATH` 指定 SDK。较旧发行版的 Qt 版本可能不满足要求。Linux 构建与打包目前未验证。

## 终端模式

```sh
codex-sync discover
codex-sync init --config /absolute/path/sync.json
codex-sync conversations --config /absolute/path/sync.json
codex-sync scan --config /absolute/path/sync.json
codex-sync keygen --output /absolute/private/path/master.key
codex-sync backup --config /absolute/path/sync.json
codex-sync history --config /absolute/path/sync.json
codex-sync restore --config /absolute/path/sync.json --snapshot SNAPSHOT_ID --output /absolute/new/restore-directory
codex-sync sync --config /absolute/path/sync.json --offline --dry-run
```

初始化后先编辑配置中的 WebDAV URL 与数据根目录。`conversations`/`scan` 不需要主密钥；加密操作通过 `CXS_KEY_FILE` 指定密钥文件，通过 `CXS_DAV_USER` 和 `CXS_DAV_PASSWORD` 提供认证。Windows 请将示例路径替换为本机绝对路径，并使用 PowerShell 的 `$env:变量名` 设置环境变量。

`history` 列出同步仓库快照；`conversations` 核查本地活动与归档对话范围。后者只读文件元数据和索引引用，不读取消息正文。

## API

```sh
codex-sync serve --config /absolute/path/sync.json --port 17841
```

设置至少 32 个字符的 `CXS_API_TOKEN`，使用 `Authorization: Bearer <token>` 请求 `GET /v1/health` 或 `POST /v1/run`。请求示例：`{"op":"conversations"}`。公开 C 接口见 [`include/codex_sync.h`](include/codex_sync.h)，通过 `cxs_run()` 调用，返回值使用 `cxs_free()` 释放。

## 许可证

本项目原创代码与原创应用图标采用 **GNU Affero General Public License v3.0 only**，SPDX：`AGPL-3.0-only`，见 [LICENSE](LICENSE)。第三方文件保留各自许可证与版权，见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
