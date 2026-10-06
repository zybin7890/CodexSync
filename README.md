# CodexSync

<img src="ui/codexsync-128.png" width="80" alt="CodexSync icon">

Codex 本地数据的加密备份与同步工具，使用 C23 / C++23、Qt 6，支持 WebDAV、Google Drive 与本地仓库。提供 Windows 11 风格 GUI、终端 CLI、HTTP API 与 C API。非 OpenAI 官方项目。

**开发预览，尚非生产版本。** Google Drive 提供浏览器 OAuth 授权、令牌续期与加密快照；协议测试使用隔离的本地模拟服务，不等于真实 Google 账号验收。真实 Google Drive / WebDAV 服务端互操作及跨设备迁移尚未验收。请先使用测试数据，不要直接覆盖唯一的数据副本。

## 下载与运行

[v0.2.0 预览版](https://github.com/zybin7890/CodexSync/releases/tag/v0.2.0) 提供 Windows x64 ZIP、Debian 13 amd64 DEB 与 `SHA256SUMS`。旧版仍可在 Releases 页面下载。

- Windows 11：解压全部文件，运行 `CodexSync.exe`；终端使用 `codex-sync.exe`。已包含 Qt 与应用本地 VC 运行库，无需安装 Qt。当前未签名，可能出现 SmartScreen 提示。
- Debian 13：运行 `sudo apt install ./CodexSync-v0.2.0-debian13-amd64.deb`，随后启动 `CodexSync` 或 `codex-sync --help`。依赖由 apt 安装；该包不面向 Debian 12。Windows 使用 FluentWinUI3 控件，Debian 使用 Fusion 控件，保留相同的设置页布局。
- HTTP API 由终端程序的 `serve` 命令提供；C API 使用附带的动态库与头文件。

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

GUI 需另外安装 Qt 6.8+ SDK/开发模块，改为 `-DCXS_GUI=ON`，必要时通过 `CMAKE_PREFIX_PATH` 指定 SDK。较旧发行版的 Qt 版本可能不满足要求。Debian 13 的构建、测试与 DEB 安装检查见 [GitHub Actions](https://github.com/zybin7890/CodexSync/actions/workflows/build-debian-release.yml)；本地生成 DEB 时还需 `dpkg-dev` 与 `file`，然后运行 `cpack --config build/CPackConfig.cmake -G DEB`。

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

设置至少 32 个字符的 `CXS_API_TOKEN`，使用 `Authorization: Bearer <token>` 请求 `GET /v1/health` 或 `POST /v1/run`。请求示例：`{"op":"scan"}`，结果包含活动与归档历史覆盖信息。公开 C 接口见 [`include/codex_sync.h`](include/codex_sync.h)，通过 `cxs_run()` 调用，返回值使用 `cxs_free()` 释放。

## Google Drive

### 授权与配置

1. 在自己的 Google Cloud 项目启用 Drive API，创建 **Desktop app** 类型的 OAuth 客户端；按项目要求配置授权用户。所有同步设备使用同一 OAuth 应用、Google 账号、仓库 ID 与主密钥。
2. GUI 的“连接与加密”选择 **Google Drive**，填写 Client ID（若该客户端需要，也填写 Client Secret），选择主密钥，点击“在浏览器中授权”。只申请 `https://www.googleapis.com/auth/drive.appdata`，采用回环回调、随机 state 与 PKCE S256；不要求整个网盘权限。
3. 每台设备分别授权。刷新令牌与 Client Secret 用主密钥加密保存在 `state/google-oauth.cxs`；状态目录必须与同步数据根目录分离，凭据不上传、不写进普通配置。密钥更换后需重新授权。

参阅 [Google 桌面 OAuth 文档](https://developers.google.com/identity/protocols/oauth2/native-app) 与 [应用专用数据空间](https://developers.google.com/workspace/drive/api/guides/appdata)。网盘网页不会直接显示应用专用空间中的文件。Google 项目测试模式或账号策略可能限制令牌有效期；失效时重新授权。

终端配置中的 `remote` 示例（其余 `format` / `device` / `state` / `roots` 字段保持原有结构）：

```json
{
  "provider": "google_drive",
  "client_id": "YOUR_DESKTOP_CLIENT_ID.apps.googleusercontent.com",
  "repository": "default"
}
```

`repository` 使用 1–64 个小写字母、数字或连字符，不同 ID 隔离不同同步空间。先设置 `CXS_KEY_FILE`，随后执行：

```sh
codex-sync google-login --config /absolute/path/sync.json
codex-sync backup --config /absolute/path/sync.json
codex-sync history --config /absolute/path/sync.json
```

`google-login` 打印授权链接，在运行该命令的机器上用浏览器打开；120 秒内完成回环授权，不使用已弃用的复制授权码流程。可通过 `CXS_GOOGLE_CLIENT_ID` 提供 Client ID、`CXS_GOOGLE_CLIENT_SECRET` 提供所需的客户端 Secret。无浏览器终端/API 服务可使用已授权的加密凭据，或由环境变量提供 `CXS_GOOGLE_REFRESH_TOKEN` 与 Client ID；短期测试也可提供 `CXS_GOOGLE_ACCESS_TOKEN`。不要在聊天、命令参数、Git 或日志里粘贴这些凭据。

### 同步语义

- 文件块和快照使用现有加密格式与内容摘要，Drive 上传采用 resumable 会话；失败后可重试完整备份，已提交的不可变对象复用。
- Drive 不模拟 WebDAV 的可变 head 原子写入，而以追加式不可变快照及祖先关系计算同步分支。同设备/多设备并发分支都保留，冲突继续保存在加密历史中，不覆盖远端历史或传播删除。
- Drive 允许同名文件；实现分页列举、同名去重和命名空间过滤，不以名称唯一性假装原子提交。
- 为避免无限历史遍历，Drive 同步最多加载 4096 个快照；超过时明确报错，不截断后继续同步。当前版本不提供历史清理。
- GUI、CLI、HTTP API 与 C API 共用后端。HTTP API 不执行交互式登录；先完成 GUI/终端授权，再启动 API 服务。

`google-drive-contracts` 覆盖模拟 OAuth/state/PKCE、拒绝授权、凭据加密、401/429、分页/重复文件、并发冲突、远端字节一致恢复、危险端点和错误密钥，以及 WebDAV HTTP 回归；测试不会联系真实 Google 或上传用户数据。

## 许可证

本项目原创代码与原创应用图标采用 **GNU Affero General Public License v3.0 only**，SPDX：`AGPL-3.0-only`，见 [LICENSE](LICENSE)。第三方文件保留各自许可证与版权，见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
