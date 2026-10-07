# CodexSync

<img src="ui/codexsync-128.png" width="80" alt="CodexSync icon">

Codex 本地数据的加密备份与同步工具，使用 C23 / C++23、Qt 6，支持 WebDAV、Google Drive 与本地仓库。提供 Windows 11 风格 GUI、终端 CLI、HTTP API 与 C API。非 OpenAI 官方项目。

**开发预览，尚非生产版本。** Windows 已验证真实 Google 隐藏库的加密备份、历史查询及空缓存抽样恢复；原样和部分加密模式通过隔离仓库与模拟 Drive 服务验证。Debian 真实 Google 账号授权、真实 WebDAV 服务端互操作及完整跨设备迁移尚未验收。请先使用测试数据验证恢复，不要直接覆盖唯一的数据副本。

**使用前请阅读 [使用风险与免责声明](DISCLAIMER.md)。** GUI 首次使用需明确勾选已阅读，声明版本更新后会再次提示；GUI 未确认前不会启动授权、同步、恢复或 API。阅读记录只保存在本机，不上传。应用设置可随时重新查看；终端运行 `codex-sync disclaimer`，HTTP/C API 可请求 `{"op":"disclaimer"}`，不增加交互提示以免破坏自动化。此提示不限制开源许可证权利，也不要求放弃法定权利。

## 下载与运行

[0.2 Win11 预览版](https://github.com/zybin7890/CodexSync/releases/tag/v0.2.0-preview.20261008) 提供 Windows x64 安装版 Setup EXE、便携版 Portable ZIP，以及在 121 服务器编译的 Debian 13 amd64 DEB；同页附对应源码 ZIP 和 SHA-256 校验文件。Windows 包含 Qt 与应用本地 VC 运行库，无需另装 Qt；当前未签名，可能出现 SmartScreen 提示。HTTP API 使用 `serve`，C API 使用附带的动态库与头文件。

## Windows 两种发行方式

- 安装版：当前用户安装到 `%LOCALAPPDATA%\Programs\CodexSync`；配置、密钥、授权、偏好和状态保存在 `%LOCALAPPDATA%\CodexSyncNative`，卸载保留数据。
- 便携版：解压后保留程序旁的 `portable.flag`，所有应用运行数据保存在 `data` 子目录；移动整个文件夹即可带走配置与密钥。Codex 的源数据仍留在原来的位置，不会被迁移。
- GUI、CLI 和 C API 使用同一模式判定。`codex-sync paths` 可查看实际位置；`init` 和 `keygen` 不指定路径时写入本版默认目录。HTTP API 默认端口 **12306**，只监听本机并验证令牌。
- 请勿将含个人 `data` 的便携文件夹分享给其他人；发行 ZIP/安装程序不包含个人数据或应用凭据。跨 Windows 账号的 DPAPI 凭据可能需要重新授权。

## 语言与主题

GUI 应用设置支持简体中文、English、跟随系统；语言即时切换，主题支持跟随系统/浅色/深色并记忆选择，偏好保存在当前发行模式的数据目录。界面操作不自动开始同步。终端/API 字段保持稳定；英文风险声明使用 `codex-sync disclaimer --lang en` 或 `{"op":"disclaimer","language":"en"}`。

## 后台自动同步

应用设置可开启后台自动同步，关闭窗口后保留托盘运行；默认关闭。触发方式可选文件变更、时间间隔（1–1440 分钟）或累计变更量（MiB）。按量模式合并连续文件事件，只检查元数据估算变更文件大小，达到阈值后备份；这不是每日流量配额。后台操作串行执行，不同时启动多个备份；失败会暂停自动同步并显示原因。设置只保存在本机，开机启动需另行开启。

## 数据范围

- Codex 主目录、桌面应用状态、配置、插件、skills、代理配置及其他显式添加的本地目录。
- `sessions` 与 `archived_sessions` 的全部日期记录，包括 `.jsonl.zst` 压缩文件和未进入索引的归档，压缩文件原样保存。
- 线程索引、`session_index.jsonl` 与 SQLite 历史数据库；运行中的 SQLite 使用在线备份，保留 WAL 中已提交的数据。
- 迁移目录的联接/符号链接会解析为真实根目录；外置的活动/归档目录单独映射。其他链接目标需显式添加。

`conversations` 命令核对文件和线程索引引用，报告缺失或被排除的数据。文件数量不等于唯一对话数量，远程云端独有的历史不在本地同步范围。

### 精确选择

“数据目录 → 选择具体内容”识别本地对话（含归档）、项目、插件缓存与 skills，可搜索及按项目/归档状态筛选。勾选实际限制扫描、上传、同步与恢复；CLI/API 使用 `catalog` 获取同一清单。项目勾选会显式加入项目源目录，不改变 Codex 的项目绑定，不自动上传。

配置的 `selection.conversations/plugins/skills` 使用 `mode: "all"` 或 `mode: "selected"` 与 `ids` 数组；空 `ids` 表示该类全部不选。目录可单独启用，并设置相对路径 `include`/`exclude`（字面文件或子目录、不支持通配符，排除优先）。全量模式仍包含完整索引；部分对话模式不携带整库与全局索引，恢复仅导出所选原始记录到独立目录，**不会自动合并到 Codex 对话列表**。索引指向的缺失记录会显示为不可选；仅云端存在的数据不属于本地清单。

## 保存方式与密钥

左侧“加密”可选择 `payload_mode: "original"`（原样保存）、`"encrypted"`（全部加密）或 `"selective"`（部分加密）。旧配置保持全部加密。原样模式保留目录、文件名和可直接打开的内容，快照信息使用标准 JSON；部分模式仅将选中文件封装为 `.cxs`，其余文件原样保存，目录与文件名仍可见。`encryption_rules` 使用 `[{"root":"根目录ID","path":"相对文件或目录"}]`，空路径选择整个根；选中 SQLite 主库会同时加密其 WAL/journal。原样与部分模式保存同一捕获时间点的 SQLite 主库及日志文件，恢复到独立目录，不支持直接 `sync` 或 `restore --apply`。

“密钥密码”可保护新生成的本机主密钥文件，或解锁已有受保护密钥；留空生成普通密钥，不改写已有密钥。CLI/API 使用 `CXS_KEY_PASSWORD`，密码不保存到配置。主密钥仍用于本机缓存、任务状态与 Google 凭据加密，不加密云端文件时也需保管；各设备使用相同主密钥。

## 安全与限制

- 全部加密模式使用 libsodium XChaCha20-Poly1305 加密数据与清单，增量对象通过带密钥的摘要寻址；部分加密使用认证流式加密。不同设备使用相同主密钥。
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
codex-sync serve --config /absolute/path/sync.json --port 12306
```

设置至少 32 个字符的 `CXS_API_TOKEN`，使用 `Authorization: Bearer <token>` 请求 `GET /v1/health` 或 `POST /v1/run`。请求示例：`{"op":"scan"}`，结果包含活动与归档历史覆盖信息。公开 C 接口见 [`include/codex_sync.h`](include/codex_sync.h)，通过 `cxs_run()` 调用，返回值使用 `cxs_free()` 释放。

## Google Drive

### 授权与配置

1. 在自己的 Google Cloud 项目启用 Drive API，创建 **Desktop app** 类型的 OAuth 客户端；按项目要求配置授权用户。所有同步设备使用同一 OAuth 应用、Google 账号、仓库 ID 与主密钥。
2. GUI 的“连接”选择 **Google Drive**，在“加密”选择主密钥，然后点击“登录 Google”。可见文件夹模式申请 `drive.file`，旧隐藏应用区使用 `drive.appdata`；采用回环回调、随机 state 与 PKCE S256，不要求整个网盘权限。自己的 OAuth 应用可在“高级 OAuth 设置”填写。
3. 每台设备分别授权。刷新令牌与 Client Secret 用主密钥加密保存在 `state/google-oauth.cxs`；状态目录必须与同步数据根目录分离，凭据不上传、不写进普通配置。密钥更换后需重新授权。

应用配置不随公开源码或安装包分发。Windows 自动读取当前用户 `%LOCALAPPDATA%/CodexSync/credentials/google-desktop-client.dpapi` 的 DPAPI 加密配置。Windows / Debian 也支持一次性通过 `google-client-import --config FILE` 从标准输入导入 Google Desktop 客户端 JSON：立即用 `CXS_KEY_FILE` 加密到 `state/google-client.cxs`，不输出凭据、不覆盖已有配置。不要将真实凭据放入命令参数或示例脚本。导入后 GUI / 终端登录无需手动输入 Client ID；已有授权可复用加密文件中的应用配置。新下载的公开版仍需安全配置应用，不能宣称所有设备开箱即用。

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
