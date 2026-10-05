import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1120
    height: 790
    minimumWidth: 760
    minimumHeight: 580
    visible: true
    title: "CodexSync"
    font.family: Qt.platform.os === "windows" ? "Microsoft YaHei UI" : "sans-serif"
    font.pixelSize: 14
    font.weight: Font.DemiBold
    property int currentPage: 0
    property bool compact: width < 920
    readonly property bool dark: Application.styleHints.colorScheme === Qt.Dark
    readonly property color canvas: dark ? "#202020" : "#f3f3f3"
    readonly property color surface: dark ? "#2b2b2b" : "#ffffff"
    readonly property color ink: dark ? "#f3f3f3" : "#1b1b1b"
    readonly property color secondary: dark ? "#c4c4c4" : "#5a5a5a"
    readonly property color line: dark ? "#353535" : "#e4e4e4"
    readonly property color accent: dark ? "#60cdff" : "#0067c0"
    readonly property bool remoteConfigured: !!ui.configuration.remote.directory || (!!ui.configuration.remote.url && ui.configuration.remote.url.indexOf("your-server.example") < 0)
    readonly property var navigation: [
        { title: "同步", icon: "home", page: 0 },
        { title: "数据目录", icon: "folder", page: 1 },
        { title: "连接与加密", icon: "cloud", page: 2 },
        { title: "快照与恢复", icon: "history", page: 3 },
        { title: "终端模式", icon: "code", page: 4 }
    ]
    readonly property var titles: ["同步", "数据目录", "连接与加密", "快照与恢复", "终端模式", "应用设置"]
    readonly property var descriptions: [
        "在设备之间，安全保存你的 Codex 工作空间。",
        "选择要保存的本地数据，并为其他设备映射相同的目录 ID。",
        "设置 WebDAV 存储空间和设备共用的加密主密钥。",
        "浏览历史版本，将数据恢复到独立的新目录。",
        "在终端中使用 CLI；HTTP API 与 C API 仍可调用同一同步核心。",
        "外观、配置文件与本地同步状态。"
    ]
    palette.window: canvas
    palette.windowText: ink
    palette.text: ink
    palette.buttonText: ink
    palette.highlight: accent
    palette.accent: accent
    palette.placeholderText: secondary
    background: Rectangle { color: window.canvas }
    onClosing: function(close) { if (ui.busy) { close.accepted = false; details.open() } }

    component Section: Label {
        Layout.fillWidth: true
        Layout.topMargin: 24
        Layout.bottomMargin: 2
        font.pixelSize: 14
        font.weight: Font.DemiBold
        color: window.ink
    }
    component Setting: SettingsCard { Layout.fillWidth: true; darkMode: window.dark; enabled: !ui.busy }
    component Caption: Label { font.pixelSize: 13; font.weight: Font.Medium; color: window.secondary; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    component FieldLabel: Label { font.pixelSize: 13; color: window.ink; Layout.fillWidth: true }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.preferredWidth: window.compact ? 72 : 260
            Layout.fillHeight: true
            color: window.dark ? "#191919" : "#e9e9e9"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: window.compact ? 10 : 16
                spacing: 8
                RowLayout {
                    Layout.topMargin: 10
                    Layout.bottomMargin: 22
                    Layout.fillWidth: true
                    spacing: 12
                    Image {
                        Layout.preferredWidth: 46
                        Layout.preferredHeight: 46
                        source: "codexsync.png"
                        sourceSize.width: 92
                        sourceSize.height: 92
                        fillMode: Image.PreserveAspectFit
                    }
                    ColumnLayout {
                        visible: !window.compact
                        spacing: 2
                        Label { text: "CodexSync"; font.pixelSize: 16; font.weight: Font.DemiBold; color: window.ink }
                        Label { text: "个人数据同步"; font.pixelSize: 12; color: window.secondary }
                    }
                }
                TextField {
                    id: search
                    visible: !window.compact
                    Layout.fillWidth: true
                    Layout.bottomMargin: 12
                    placeholderText: "查找设置"
                    leftPadding: 34
                    Icon { anchors.verticalCenter: parent.verticalCenter; x: 10; width: 17; height: 17; name: "search"; darkMode: window.dark; opacity: 0.8 }
                }
                Repeater {
                    model: window.navigation.filter(function(item) { return window.compact || item.title.indexOf(search.text) >= 0 })
                    delegate: ItemDelegate {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 42
                        onClicked: window.currentPage = modelData.page
                        Accessible.name: modelData.title
                        ToolTip.text: modelData.title
                        ToolTip.visible: window.compact && hovered
                        background: Rectangle {
                            radius: 4
                            color: window.currentPage === modelData.page ? (window.dark ? "#303030" : "#dadada") : parent.hovered ? (window.dark ? "#252525" : "#e0e0e0") : "transparent"
                            Rectangle { visible: window.currentPage === modelData.page; width: 3; height: 18; radius: 2; color: window.accent; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter }
                            border.color: parent.activeFocus ? window.accent : "transparent"
                        }
                        contentItem: RowLayout {
                            spacing: 14
                            Icon { name: modelData.icon; darkMode: window.dark; Layout.preferredWidth: 21; Layout.preferredHeight: 21; Layout.leftMargin: 3 }
                            Label { visible: !window.compact; text: modelData.title; font.pixelSize: 14; color: window.ink; Layout.fillWidth: true }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                Rectangle { Layout.fillWidth: true; height: 1; color: window.line; Layout.bottomMargin: 6 }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 12
                    spacing: 12
                    Icon { name: "desktop"; darkMode: window.dark; opacity: 0.8; Layout.preferredWidth: 20; Layout.preferredHeight: 20; Layout.leftMargin: 12 }
                    ColumnLayout {
                        visible: !window.compact
                        Layout.fillWidth: true
                        spacing: 3
                        Label { text: ui.deviceName; color: window.ink; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                        Label { text: "当前设备"; color: window.secondary; font.pixelSize: 11 }
                    }
                }
                ItemDelegate {
                    Layout.fillWidth: true
                    implicitHeight: 42
                    Accessible.name: "应用设置"
                    onClicked: window.currentPage = 5
                    background: Rectangle { radius: 4; color: window.currentPage === 5 ? (window.dark ? "#303030" : "#dadada") : parent.hovered ? (window.dark ? "#252525" : "#e0e0e0") : "transparent"; Rectangle { visible: window.currentPage === 5; width: 3; height: 18; radius: 2; color: window.accent; anchors.verticalCenter: parent.verticalCenter } }
                    contentItem: RowLayout { spacing: 14; Icon { name: "settings"; darkMode: window.dark; Layout.preferredWidth: 21; Layout.preferredHeight: 21; Layout.leftMargin: 3 } Label { text: "应用设置"; visible: !window.compact; color: window.ink; Layout.fillWidth: true } }
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: window.compact ? 24 : 36
            Layout.rightMargin: window.compact ? 24 : 36
            Layout.topMargin: 26
            Layout.bottomMargin: 12
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                Layout.bottomMargin: 6
                Label { text: window.titles[window.currentPage]; color: window.ink; font.pixelSize: 28; font.weight: Font.DemiBold; Layout.fillWidth: true }
                Button { text: "打开配置"; enabled: !ui.busy; onClicked: ui.openConfig(); icon.source: "icons/folder.svg"; icon.width: 16; icon.height: 16 }
            }
            Caption { text: window.descriptions[window.currentPage]; Layout.bottomMargin: 20; font.pixelSize: 13 }
            ScrollView {
                id: scroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                Loader {
                    width: scroll.availableWidth
                    sourceComponent: [overviewPage, rootsPage, connectionPage, historyPage, apiPage, settingsPage][window.currentPage]
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 8
                Rectangle { width: 6; height: 6; radius: 3; color: ui.busy ? window.accent : window.secondary }
                Label { text: ui.busy ? "正在处理，请稍候" : "界面预览 · 未执行自动同步"; color: window.secondary; font.pixelSize: 11; Layout.fillWidth: true }
                BusyIndicator { visible: ui.busy; running: ui.busy; implicitWidth: 24; implicitHeight: 24 }
                Label { text: "C++  /  Qt Quick"; color: window.secondary; font.pixelSize: 11 }
            }
        }
    }

    Component {
        id: overviewPage
        ColumnLayout {
            spacing: 4
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: onboarding.implicitHeight + 40
                radius: 8
                color: window.surface
                border.color: window.line
                RowLayout {
                    id: onboarding
                    anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: 22
                    spacing: 20
                    Rectangle {
                        width: 64; height: 64; radius: 16
                        color: window.dark ? "#173e53" : "#e4f1fc"
                        Icon { anchors.centerIn: parent; width: 36; height: 36; name: "cloud"; accent: window.dark; darkMode: false }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 7
                        Label { text: window.remoteConfigured ? "你的同步空间" : "设置你的同步空间"; font.pixelSize: 19; font.weight: Font.DemiBold; color: window.ink; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Caption { text: "连接 WebDAV，然后选择加密密钥。\n你的对话与设置，始终由你掌控。"; font.pixelSize: 13 }
                        Button { text: window.remoteConfigured ? "管理连接" : "配置 WebDAV"; highlighted: true; onClicked: window.currentPage = 2; Layout.topMargin: 5 }
                    }
                    ColumnLayout {
                        visible: !window.compact
                        spacing: 6
                        Label { text: "当前状态"; color: window.secondary; font.pixelSize: 12 }
                        Label { text: window.remoteConfigured ? "地址已配置" : "尚未连接"; color: window.ink; font.pixelSize: 14 }
                        Label { text: ui.keyFile.length > 0 ? "已选择密钥" : "主密钥未选择"; color: window.secondary; font.pixelSize: 12 }
                    }
                }
            }
            Section { text: "同步设置" }
            Setting { title: "同步内容"; description: "对话与历史、配置与凭据、skills 与插件"; iconName: "folder"; trailing: ui.configuration.roots.length + " 个目录"; clickable: true; onClicked: window.currentPage = 1 }
            Setting { title: "WebDAV 存储空间"; description: window.remoteConfigured ? "地址已配置，连接状态尚未验证" : "连接你自己的服务器或网盘"; iconName: "cloud"; trailing: window.remoteConfigured ? "已配置" : "未配置"; clickable: true; onClicked: window.currentPage = 2 }
            Setting { title: "端到端加密"; description: "文件内容与快照目录均加密后上传"; iconName: "shield_lock"; trailing: ui.keyFile.length > 0 ? "密钥已选择" : "待设置"; clickable: true; onClicked: window.currentPage = 2 }
            Setting { title: "快照与恢复"; description: "保留历史版本，恢复默认导出到新目录"; iconName: "history"; clickable: true; onClicked: window.currentPage = 3 }
            Section { text: "最近活动" }
            Rectangle {
                Layout.fillWidth: true; implicitHeight: 66; radius: 4; color: window.surface; border.color: window.line
                RowLayout { anchors.fill: parent; anchors.margins: 18; spacing: 18; Icon { name: "arrow_sync"; darkMode: window.dark; opacity: 0.6; Layout.preferredWidth: 24; Layout.preferredHeight: 24 } Label { text: ui.lastActivity; color: window.secondary; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 13 } Button { text: "查看详情"; flat: true; onClicked: details.open() } }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: 14; spacing: 8
                Icon { name: "info"; darkMode: window.dark; Layout.preferredWidth: 16; Layout.preferredHeight: 16; opacity: 0.8 }
                Caption { text: "备份可在线进行；双向同步前需关闭 Codex。冲突保留版本，不传播删除。" }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: 16; Layout.bottomMargin: 16; spacing: 8
                Button { text: "检查数据范围"; enabled: !ui.busy; onClicked: ui.run("scan") }
                Button { text: "加密备份"; enabled: !ui.busy && window.remoteConfigured && ui.keyFile.length > 0; onClicked: ui.run("backup") }
                Button { text: "双向同步"; enabled: !ui.busy && window.remoteConfigured && ui.keyFile.length > 0; onClicked: ui.run("sync") }
                Item { Layout.fillWidth: true }
            }
        }
    }
    Component {
        id: rootsPage
        ColumnLayout {
            spacing: 4
            RowLayout { Layout.fillWidth: true; Layout.bottomMargin: 14; Label { text: "已发现 " + ui.configuration.roots.length + " 个数据目录"; color: window.ink; Layout.fillWidth: true } Button { text: "添加目录"; highlighted: true; onClicked: ui.addRoot() } Button { text: "重新发现"; onClicked: ui.discoverRoots() } }
            Setting {
                title: "对话与归档"; description: "包含所有日期的原始记录、.jsonl.zst 压缩归档、线程索引与历史数据库"
                iconName: "history"; expandAvailable: true; expanded: true
                trailing: ui.conversationHistory.files === undefined ? "未核查" : ui.conversationHistory.files + " 个记录文件"
                Caption { text: ui.conversationHistory.files === undefined ? "核查只读取文件元数据和索引引用，不读取对话正文或上传数据。" : "活动记录：" + ui.conversationHistory.active.files + "（压缩 " + ui.conversationHistory.active.compressed + "） · 归档记录：" + ui.conversationHistory.archived.files + "（压缩 " + ui.conversationHistory.archived.compressed + "）" }
                Caption { visible: ui.conversationHistory.index_checked === true; text: "线程索引：活动 " + (ui.conversationHistory.indexed_active || 0) + " · 归档 " + (ui.conversationHistory.indexed_archived || 0) + "。文件数量不等于唯一对话数量。" }
                Caption { visible: ui.conversationHistory.coverage_complete === false; text: "核查发现索引引用缺失或被排除的记录，请查看操作详情。现存归档和历史数据库仍会保留。"; color: window.dark ? "#ffb4ab" : "#b10e1e" }
                Button { text: "核查历史对话"; enabled: !ui.busy; onClicked: ui.run("conversations") }
            }
            Repeater {
                model: ui.configuration.roots
                delegate: Setting {
                    required property var modelData
                    required property int index
                    title: ({"codex_home":"Codex 主数据（含所有对话与归档）","codex_home_default":"默认 Codex 目录（含旧历史）","agents":"共享 skills 与代理配置","desktop":"桌面应用数据","desktop_local":"桌面应用本地状态"})[modelData.id] || modelData.id
                    description: modelData.path
                    iconName: "folder"
                    trailing: modelData.id
                    expandAvailable: true
                    FieldLabel { text: "目录 ID" }
                    TextField { id: rootId; Layout.fillWidth: true; text: modelData.id; onEditingFinished: ui.updateRoot(index, text, rootPath.text) }
                    FieldLabel { text: "本地绝对路径" }
                    TextField { id: rootPath; Layout.fillWidth: true; text: modelData.path; onEditingFinished: ui.updateRoot(index, rootId.text, text) }
                    RowLayout { Button { text: "移除映射"; onClicked: ui.removeRoot(index) } Caption { text: "仅移除映射，不删除文件" } }
                }
            }
            Caption { text: "Conversations、外部项目和符号链接的真实目标，请显式添加。不同设备用相同 ID 对应各自路径。"; Layout.topMargin: 16 }
            Button { text: "保存配置"; Layout.topMargin: 14; onClicked: ui.saveConfig() }
        }
    }
    Component {
        id: connectionPage
        ColumnLayout {
            spacing: 4
            Setting {
                title: "WebDAV"; description: "支持标准 WebDAV 与 HTTPS 证书校验"; iconName: "cloud"; expandAvailable: true; expanded: true
                FieldLabel { text: "服务器地址" }
                TextField { Layout.fillWidth: true; text: ui.configuration.remote.url || ""; placeholderText: "https://server/remote.php/dav/files/user"; onEditingFinished: ui.setSetting("url", text) }
                RowLayout {
                    Layout.fillWidth: true; spacing: 12
                    ColumnLayout { Layout.fillWidth: true; FieldLabel { text: "用户名" } TextField { Layout.fillWidth: true; placeholderText: "WebDAV 用户名"; onEditingFinished: ui.setSetting("username", text) } }
                    ColumnLayout { Layout.fillWidth: true; FieldLabel { text: "密码 / 应用密码" } TextField { Layout.fillWidth: true; placeholderText: "仅保存在本次进程"; echoMode: TextInput.Password; onEditingFinished: ui.setSetting("password", text) } }
                }
                Caption { text: "用户名和密码不写入配置文件。建议使用网盘的独立应用密码。" }
            }
            Setting {
                title: "加密主密钥"; description: "同一同步空间的所有设备共用一把主密钥"; iconName: "key"; expandAvailable: true; expanded: true
                TextField { Layout.fillWidth: true; readOnly: true; text: ui.keyFile; placeholderText: "尚未选择主密钥" }
                RowLayout { spacing: 8; Button { text: "选择密钥"; onClicked: ui.chooseKey() } Button { text: "生成新密钥"; onClicked: ui.createKey() } }
                Caption { text: "密钥丢失无法恢复数据。请另存安全副本，不要放进同步目录。" }
            }
            Setting {
                title: "高级设置"; description: "设备标识、本地状态与离线仓库"; iconName: "settings"; expandAvailable: true
                FieldLabel { text: "本地状态目录" } TextField { Layout.fillWidth: true; text: ui.configuration.state; onEditingFinished: ui.setSetting("state", text) }
                FieldLabel { text: "设备 ID" } TextField { Layout.fillWidth: true; readOnly: true; text: ui.configuration.device }
                FieldLabel { text: "本地仓库（可选）" } TextField { Layout.fillWidth: true; text: ui.configuration.remote.directory || ""; placeholderText: "留空使用 WebDAV"; onEditingFinished: ui.setSetting("directory", text) }
            }
            Button { text: "保存配置"; highlighted: true; Layout.topMargin: 18; Layout.bottomMargin: 18; onClicked: ui.saveConfig() }
            Caption { text: "系统钥匙串与机器绑定的登录凭据可作为不透明数据保留，跨机器恢复后可能需要重新登录。" }
        }
    }
    Component {
        id: historyPage
        ColumnLayout {
            spacing: 4
            Setting { title: "不可变快照"; description: "每次备份保留独立版本，相同内容增量复用"; iconName: "history" }
            Rectangle {
                visible: ui.snapshots.length === 0
                Layout.fillWidth: true; implicitHeight: 300; color: "transparent"
                ColumnLayout { anchors.centerIn: parent; spacing: 12; Icon { name: "history"; darkMode: window.dark; opacity: 0.45; Layout.preferredWidth: 48; Layout.preferredHeight: 48; Layout.alignment: Qt.AlignHCenter } Label { text: "还没有加载快照"; font.pixelSize: 18; color: window.ink; Layout.alignment: Qt.AlignHCenter } Label { text: "设置连接和密钥后，读取你的同步历史。"; color: window.secondary; font.pixelSize: 13 } Button { text: "读取快照"; enabled: ui.keyFile.length > 0 && !ui.busy; onClicked: ui.run("history"); Layout.alignment: Qt.AlignHCenter } }
            }
            Repeater { model: ui.snapshots; delegate: Setting { required property var modelData; title: modelData.time; description: modelData.entries + " 个条目 · " + modelData.conflicts + " 个冲突"; iconName: "history"; expandAvailable: true; Caption { text: modelData.id } Button { text: "恢复到新目录"; onClicked: ui.restore(modelData.id) } } }
            Button { visible: ui.snapshots.length > 0; text: "刷新快照"; onClicked: ui.run("history"); Layout.topMargin: 16 }
        }
    }
    Component {
        id: apiPage
        ColumnLayout {
            spacing: 4
            Setting {
                title: "命令行 CLI"; description: "无需打开 GUI，可在 PowerShell 或 Linux 终端中运行"; iconName: "code"; expandAvailable: true; expanded: true
                Label { text: ui.terminalCommand; color: window.secondary; font.family: Qt.platform.os === "windows" ? "Consolas" : "monospace"; font.pixelSize: 13; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                Button { text: "复制帮助命令"; onClicked: ui.copyTerminalCommand() }
                Label { text: "codex-sync conversations --config <配置文件>\ncodex-sync scan --config <配置文件>\ncodex-sync backup --config <配置文件>\ncodex-sync sync --config <配置文件> --offline"; color: window.secondary; font.family: Qt.platform.os === "windows" ? "Consolas" : "monospace"; font.pixelSize: 13; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                Caption { text: "conversations 核查全部本地历史与归档；sync 应用改动前必须关闭 Codex。密钥与 WebDAV 凭据通过环境变量传入。" }
            }
            Section { text: "API 接口" }
            Setting {
                title: "本地 HTTP API"; description: "仅监听 127.0.0.1，拒绝未授权访问"; iconName: "code"; trailing: ui.apiRunning ? "运行中" : "未启动"; expandAvailable: true; expanded: true
                RowLayout { FieldLabel { text: "监听端口" } SpinBox { id: apiPort; from: 1024; to: 65535; value: 17841; editable: true } }
                RowLayout { spacing: 8; Button { text: ui.apiRunning ? "停止 API" : "启动 API"; highlighted: !ui.apiRunning; onClicked: ui.apiRunning ? ui.stopApi() : ui.startApi(apiPort.value) } Caption { text: "关闭应用时，将停止由界面启动的 API。" } }
            }
            Setting {
                title: "访问令牌"; description: "每次启动 API 时生成，仅在当前进程内有效"; iconName: "shield_lock"; expandAvailable: true; expanded: true
                RowLayout { Layout.fillWidth: true; TextField { Layout.fillWidth: true; readOnly: true; echoMode: TextInput.Password; text: ui.apiToken; placeholderText: "启动 API 后生成" } Button { text: "复制"; enabled: ui.apiToken.length > 0; onClicked: ui.copyToken() } }
            }
            Section { text: "开发者接口" }
            Setting { title: "HTTP / C API"; description: "GUI、终端模式与 API 共用同一个 C++ 核心"; iconName: "code"; expandAvailable: true; expanded: true; Label { text: "GET   /v1/health\nPOST  /v1/run\n\nAuthorization: Bearer <token>\n{\"op\": \"backup\"}\n\nC API: cxs_run() / cxs_free()"; color: window.secondary; font.family: "Consolas"; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere } }
        }
    }
    Component {
        id: settingsPage
        ColumnLayout {
            spacing: 4
            Section { text: "外观"; Layout.topMargin: 0 }
            Setting { title: "应用主题"; description: "使用 FluentWinUI3 控件，默认跟随系统外观"; iconName: "settings"; expandAvailable: true; expanded: true; ComboBox { model: ["跟随系统", "浅色", "深色"]; currentIndex: 0; onActivated: ui.setTheme(currentIndex); Layout.preferredWidth: 180 } }
            Section { text: "配置" }
            Setting { title: "同步配置文件"; description: ui.configFile.length > 0 ? ui.configFile : "尚未保存配置"; iconName: "folder"; expandAvailable: true; expanded: true; RowLayout { Button { text: "打开配置"; onClicked: ui.openConfig() } Button { text: "保存配置"; onClicked: ui.saveConfig() } } }
            Section { text: "关于" }
            Setting { title: "CodexSync"; description: "C++23 · Qt Quick · FluentWinUI3"; iconName: "arrow_sync"; trailing: "UI 预览" }
            Caption { text: "本轮仅验证界面布局与交互。同步核心、WebDAV 和 Debian 构建尚未完成交付验收。"; Layout.topMargin: 16 }
        }
    }
    Dialog {
        id: details
        title: "操作详情"
        anchors.centerIn: parent
        width: Math.min(window.width - 80, 720)
        height: Math.min(window.height - 100, 500)
        modal: true
        standardButtons: Dialog.Close
        contentItem: ScrollView { TextArea { text: ui.logText.length > 0 ? ui.logText : "尚未执行任何操作。\n\n这里显示真实的检查、备份和同步结果，不展示示例数据。"; readOnly: true; selectByMouse: true; wrapMode: TextArea.WrapAnywhere; font.pixelSize: 13 } }
    }
}
