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
    font.family: Qt.platform.os === "windows" ? (ui.englishLanguage ? "Segoe UI" : "Microsoft YaHei UI") : "sans-serif"
    font.pixelSize: 14
    font.weight: Font.DemiBold
    property int currentPage: 0
    property bool compact: width < 920
    readonly property bool noticeVisible: usageNotice.visible
    readonly property bool selectionVisible: contentSelector.visible
    readonly property int selectionCount: selectionList.count
    Component.onCompleted: { if (!ui.noticeAcknowledged) usageNotice.open() }
    Connections { target: ui; function onNoticeRequested() { usageNotice.open() } }
    function acknowledgeNoticeForSmoke() {
        noticeRead.checked = true
        if (noticeContinue.enabled && ui.acknowledgeNotice()) usageNotice.close()
    }
    readonly property bool dark: Application.styleHints.colorScheme === Qt.Dark
    readonly property color canvas: dark ? "#202020" : "#f3f3f3"
    readonly property color surface: dark ? "#2b2b2b" : "#ffffff"
    readonly property color ink: dark ? "#f3f3f3" : "#1b1b1b"
    readonly property color secondary: dark ? "#c4c4c4" : "#5a5a5a"
    readonly property color line: dark ? "#353535" : "#e4e4e4"
    readonly property color accent: dark ? "#60cdff" : "#0067c0"
    readonly property string provider: ui.configuration.remote.provider || (ui.configuration.remote.directory ? "local" : "webdav")
    readonly property string providerName: provider === "google_drive" ? "Google Drive" : provider === "local" ? qsTr("本地仓库") : "WebDAV"
    readonly property bool remoteConfigured: provider === "google_drive" ? ui.googleAuthorized : provider === "local" ? !!ui.configuration.remote.directory : (!!ui.configuration.remote.url && ui.configuration.remote.url.indexOf("your-server.example") < 0)
    readonly property var navigation: [
        { title: qsTr("同步"), icon: "home", page: 0 },
        { title: qsTr("数据目录"), icon: "folder", page: 1 },
        { title: qsTr("连接"), icon: "cloud", page: 2 },
        { title: qsTr("加密"), icon: "shield_lock", page: 6 },
        { title: qsTr("快照与恢复"), icon: "history", page: 3 },
        { title: qsTr("终端模式"), icon: "code", page: 4 }
    ]
    readonly property var titles: [qsTr("同步"), qsTr("数据目录"), qsTr("连接"), qsTr("快照与恢复"), qsTr("终端模式"), qsTr("应用设置"), qsTr("加密")]
    readonly property var descriptions: [
        qsTr("在设备之间，安全保存你的 Codex 工作空间。"),
        qsTr("选择要保存的本地数据，并为其他设备映射相同的目录 ID。"),
        qsTr("设置 WebDAV、Google Drive 或本地仓库。"),
        qsTr("浏览历史版本，将数据恢复到独立的新目录。"),
        qsTr("在终端中使用 CLI；HTTP API 与 C API 仍可调用同一同步核心。"),
        qsTr("外观、配置文件与本地同步状态。"),
        qsTr("选择加密范围，管理主密钥与密钥密码。")
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
    component ReadableScrollBar: ScrollBar {
        id: readableBar
        implicitWidth: 12
        implicitHeight: 12
        padding: 3
        minimumSize: 0.04
        opacity: 1
        hoverEnabled: true
        contentItem: Rectangle {
            implicitWidth: 6
            implicitHeight: 6
            radius: 3
            color: readableBar.pressed || readableBar.hovered ? window.accent : window.dark ? "#929ba5" : "#707b88"
        }
        background: Rectangle {
            radius: 6
            color: readableBar.pressed || readableBar.hovered ? window.dark ? "#14ffffff" : "#10000000" : "transparent"
        }
    }
    component ReadableScrollView: ScrollView {
        id: readableView
        ScrollBar.vertical: ReadableScrollBar {
            parent: readableView
            x: readableView.mirrored ? 0 : readableView.width - width
            height: readableView.height
            active: true
        }
        ScrollBar.horizontal: ReadableScrollBar {
            parent: readableView
            y: readableView.height - height
            width: readableView.width
            active: true
        }
    }

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
                        Label { text: qsTr("个人数据同步"); font.pixelSize: 12; color: window.secondary }
                    }
                }
                TextField {
                    id: search
                    visible: !window.compact
                    Layout.fillWidth: true
                    Layout.bottomMargin: 12
                    placeholderText: qsTr("查找设置")
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
                Rectangle {
                    objectName: "sidebarSyncProgress"
                    Layout.fillWidth: true
                    implicitHeight: sidebarProgressContent.implicitHeight + (window.compact ? 16 : 24)
                    radius: 6
                    color: window.surface
                    border.color: window.line
                    Accessible.name: qsTr("同步进度")
                    Accessible.description: ui.syncProgress.phase ? ui.progressLabel + " · " + ui.progressDetail : qsTr("尚未开始同步")
                    ColumnLayout {
                        id: sidebarProgressContent
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                        anchors.margins: window.compact ? 5 : 12
                        spacing: 7
                        RowLayout {
                            Layout.fillWidth: true
                            visible: !window.compact
                            Icon { name: "arrow_sync"; darkMode: window.dark; Layout.preferredWidth: 16; Layout.preferredHeight: 16 }
                            Label { text: qsTr("同步进度"); color: window.ink; font.pixelSize: 12; Layout.fillWidth: true }
                        }
                        Label {
                            objectName: "sidebarProgressLabel"
                            Layout.fillWidth: true
                            font.pixelSize: 12
                            color: window.ink
                            wrapMode: Text.WordWrap
                            horizontalAlignment: window.compact ? Text.AlignHCenter : Text.AlignLeft
                            text: window.compact ? (ui.syncProgress.phase === "failed" ? qsTr("失败") : ui.progressIndeterminate ? qsTr("处理中") : ui.syncProgress.phase ? Math.floor(ui.progressValue * 100) + "%" : qsTr("就绪")) : ui.syncProgress.phase ? ui.progressLabel : qsTr("尚未开始同步")
                        }
                        ProgressBar {
                            objectName: "sidebarProgressBar"
                            Layout.fillWidth: true
                            from: 0; to: 1
                            value: ui.progressValue
                            indeterminate: ui.progressIndeterminate
                            Accessible.name: qsTr("当前阶段进度")
                        }
                        Label {
                            visible: !window.compact
                            Layout.fillWidth: true
                            text: ui.syncProgress.phase === "failed" ? qsTr("未完成") : ui.progressIndeterminate ? qsTr("正在计算进度…") : ui.syncProgress.phase ? Math.floor(ui.progressValue * 100) + "%" : qsTr("等待同步任务")
                            color: window.secondary
                            font.pixelSize: 11
                        }
                    }
                    HoverHandler { id: sidebarProgressHover }
                    ToolTip.visible: sidebarProgressHover.hovered
                    ToolTip.text: ui.syncProgress.phase ? ui.progressLabel + "\n" + ui.progressDetail : qsTr("尚未开始同步")
                    Layout.bottomMargin: 8
                }
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
                        Label { text: qsTr("当前设备"); color: window.secondary; font.pixelSize: 11 }
                    }
                }
                ItemDelegate {
                    Layout.fillWidth: true
                    implicitHeight: 42
                    Accessible.name: qsTr("应用设置")
                    onClicked: window.currentPage = 5
                    background: Rectangle { radius: 4; color: window.currentPage === 5 ? (window.dark ? "#303030" : "#dadada") : parent.hovered ? (window.dark ? "#252525" : "#e0e0e0") : "transparent"; Rectangle { visible: window.currentPage === 5; width: 3; height: 18; radius: 2; color: window.accent; anchors.verticalCenter: parent.verticalCenter } }
                    contentItem: RowLayout { spacing: 14; Icon { name: "settings"; darkMode: window.dark; Layout.preferredWidth: 21; Layout.preferredHeight: 21; Layout.leftMargin: 3 } Label { text: qsTr("应用设置"); visible: !window.compact; color: window.ink; Layout.fillWidth: true } }
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
                Button {
                    objectName: "startSyncButton"
                    text: ui.busy ? qsTr("处理中…") : qsTr("开始同步")
                    highlighted: true
                    implicitHeight: 44
                    implicitWidth: 158
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    icon.source: "icons/arrow_sync.svg"
                    icon.width: 20; icon.height: 20
                    enabled: !ui.busy && !ui.apiRunning
                    Accessible.name: qsTr("开始同步")
                    ToolTip.visible: hovered
                    ToolTip.text: !window.remoteConfigured || ui.keyFile.length === 0 ? qsTr("先配置存储空间与加密密钥") : qsTr("按已选择的同步方向和数据类型执行")
                    onClicked: {
                        if (!window.remoteConfigured || ui.keyFile.length === 0) window.currentPage = 2
                        else ui.runSelectedSync()
                    }
                }
                Button { text: qsTr("打开配置"); enabled: !ui.busy; onClicked: ui.openConfig(); icon.source: "icons/folder.svg"; icon.width: 16; icon.height: 16 }
            }
            Caption { text: window.descriptions[window.currentPage]; Layout.bottomMargin: 8; font.pixelSize: 13 }
            Caption { text: qsTr("当前同步方向：") + " " + (ui.syncMode === "bidirectional" ? qsTr("双向同步（须关闭 Codex）") : ui.syncMode === "download" ? qsTr("仅下载到独立目录") : qsTr("仅上传 · 备份")) + ((!window.remoteConfigured || ui.keyFile.length === 0) ? qsTr(" · 尚需配置") : ""); Layout.bottomMargin: 12 }
            Rectangle {
                objectName: "syncProgressPanel"
                Layout.fillWidth: true
                implicitHeight: progressContent.implicitHeight + 24
                visible: !!ui.syncProgress.phase
                color: window.surface
                radius: 6
                border.color: window.line
                ColumnLayout {
                    id: progressContent
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: 12
                    spacing: 7
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: ui.progressLabel; color: window.ink; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Label { visible: !ui.progressIndeterminate; text: ui.syncProgress.phase === "failed" ? qsTr("未完成") : Math.floor(ui.progressValue * 100) + "%"; color: window.secondary }
                    }
                    ProgressBar {
                        objectName: "syncProgressBar"
                        Layout.fillWidth: true
                        from: 0; to: 1
                        value: ui.progressValue
                        indeterminate: ui.progressIndeterminate
                        Accessible.name: qsTr("当前阶段进度")
                    }
                    Label { text: ui.progressDetail; color: window.secondary; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    RowLayout {
                        visible: !!ui.syncProgress.job_id || ui.busy
                        Button { text: qsTr("暂停"); visible: ui.busy; onClicked: ui.controlTask("pause") }
                        Button { text: qsTr("取消任务"); visible: ui.busy; onClicked: ui.controlTask("cancel") }
                        Button { text: qsTr("继续上传"); visible: !ui.busy && (ui.syncProgress.phase === "failed" || ui.syncProgress.phase === "paused" || ui.syncProgress.phase === "cancelled"); enabled: !!ui.syncProgress.job_id; onClicked: ui.run("resume") }
                        Label { text: ui.syncProgress.job_id ? qsTr("任务：") + ui.syncProgress.job_id : ""; color: window.secondary; elide: Text.ElideMiddle; Layout.fillWidth: true; font.pixelSize: 11 }
                    }
                }
                Layout.bottomMargin: 8
            }
            ReadableScrollView {
                id: scroll
                objectName: window.currentPage === 5 ? "settingsScroll" : "pageScroll"
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                contentHeight: pageLoader.implicitHeight
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                Loader {
                    id: pageLoader
                    objectName: "pageLoader"
                    width: scroll.availableWidth
                    height: implicitHeight
                    sourceComponent: [overviewPage, rootsPage, connectionPage, historyPage, apiPage, settingsPage, encryptionPage][window.currentPage]
                    onLoaded: { if (scroll.contentItem && scroll.contentItem.contentY !== undefined) scroll.contentItem.contentY = 0 }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 8
                Rectangle { width: 6; height: 6; radius: 3; color: ui.busy ? window.accent : window.secondary }
                Label { text: ui.busy ? qsTr("正在处理，请稍候") : ui.autoSync ? qsTr("自动备份已开启") : qsTr("自动同步已关闭"); color: window.secondary; font.pixelSize: 11; Layout.fillWidth: true }
                BusyIndicator { visible: ui.busy; running: ui.busy; implicitWidth: 24; implicitHeight: 24 }
                Label { text: "v" + ui.applicationVersion + "  ·  C++ / Qt Quick"; color: window.secondary; font.pixelSize: 11 }
            }
        }
    }

    Component {
        id: overviewPage
        ColumnLayout {
            spacing: 4
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: syncPanel.implicitHeight + 40
                radius: 8
                color: window.surface
                border.color: window.line
                ColumnLayout {
                    id: syncPanel
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: 20
                    spacing: 16
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Image { source: "codexsync.png"; sourceSize.width: 80; sourceSize.height: 80; Layout.preferredWidth: 40; Layout.preferredHeight: 40 }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Label { text: window.remoteConfigured ? window.providerName : qsTr("尚未连接同步空间"); color: window.ink; font.pixelSize: 18; font.weight: Font.DemiBold; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            Caption { text: window.provider === "google_drive" ? ui.googleStorageLabel + " · " + (ui.configuration.remote.repository || "default") : ui.encryptionLabel }
                        }
                        Button { text: window.remoteConfigured ? qsTr("管理连接") : qsTr("配置连接"); flat: true; onClicked: window.currentPage = 2 }
                    }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: window.line }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Rectangle { width: 8; height: 8; radius: 4; color: window.provider === "google_drive" && ui.googleConnectionState === "connected" ? (window.dark ? "#6cdd87" : "#166a2b") : window.provider === "google_drive" && ui.googleConnectionState.indexOf("failed") >= 0 ? (window.dark ? "#ffb4ab" : "#b42318") : window.secondary }
                        Label { text: ui.busy ? ui.status : window.provider === "google_drive" ? ui.googleConnectionLabel : window.remoteConfigured ? qsTr("存储空间已配置") : qsTr("等待配置存储空间"); color: window.ink; font.pixelSize: 19; font.weight: Font.DemiBold; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    }
                    Caption { text: window.provider === "google_drive" ? ui.googleSnapshotStatus : qsTr("备份可以在线执行；双向同步需要先关闭 Codex。") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 14
                        Icon { name: "shield_lock"; darkMode: window.dark; Layout.preferredWidth: 18; Layout.preferredHeight: 18 }
                        Label { text: ui.keyFile.length > 0 ? qsTr("加密密钥已选择") : qsTr("尚未选择加密密钥"); color: window.secondary; font.pixelSize: 13; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Label { text: ui.configuration.roots.length + qsTr(" 个数据目录"); color: window.secondary; font.pixelSize: 13 }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Label { text: qsTr("同步方向"); color: window.ink; font.pixelSize: 13 }
                        ComboBox {
                            objectName: "overviewSyncMode"
                            Layout.fillWidth: true
                            model: [qsTr("仅上传 · 备份"), qsTr("双向同步 · 离线合并"), qsTr("仅下载 · 独立恢复目录")]
                            currentIndex: ui.syncMode === "bidirectional" ? 1 : ui.syncMode === "download" ? 2 : 0
                            enabled: !ui.busy && !ui.apiRunning
                            onActivated: ui.setSyncMode(["upload", "bidirectional", "download"][currentIndex])
                            Accessible.name: qsTr("同步方向")
                        }
                    }
                }
            }
            Section { text: qsTr("同步设置") }
            Setting { title: qsTr("同步内容"); description: qsTr("对话与历史、配置与凭据、skills 与插件"); iconName: "folder"; trailing: ui.configuration.roots.length + qsTr(" 个目录"); clickable: true; onClicked: window.currentPage = 1 }
            Setting { title: qsTr("同步存储空间"); description: window.provider === "google_drive" ? ui.googleConnectionLabel + "\n" + ui.googleSnapshotStatus : window.remoteConfigured ? window.providerName + qsTr(" 已配置，连接状态尚未验证") : qsTr("选择 WebDAV、Google Drive 或本地仓库"); iconName: "cloud"; trailing: window.providerName; clickable: true; onClicked: window.currentPage = 2 }
            Setting { title: qsTr("加密"); description: ui.encryptionLabel; iconName: "shield_lock"; trailing: ui.keyFile.length > 0 ? qsTr("密钥已选择") : qsTr("待设置"); clickable: true; onClicked: window.currentPage = 6 }
            Setting { title: qsTr("快照与恢复"); description: qsTr("保留历史版本，恢复默认导出到新目录"); iconName: "history"; clickable: true; onClicked: window.currentPage = 3 }
            Section { text: qsTr("最近活动") }
            Rectangle {
                Layout.fillWidth: true; implicitHeight: 66; radius: 4; color: window.surface; border.color: window.line
                RowLayout { anchors.fill: parent; anchors.margins: 18; spacing: 18; Icon { name: "arrow_sync"; darkMode: window.dark; opacity: 0.6; Layout.preferredWidth: 24; Layout.preferredHeight: 24 } Label { text: ui.lastActivity; color: window.secondary; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 13 } Button { text: qsTr("查看详情"); flat: true; onClicked: details.open() } }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: 14; spacing: 8
                Icon { name: "info"; darkMode: window.dark; Layout.preferredWidth: 16; Layout.preferredHeight: 16; opacity: 0.8 }
                Caption { text: qsTr("备份可在线进行；双向同步前需关闭 Codex。冲突保留版本，不传播删除。") }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: 16; Layout.bottomMargin: 16; spacing: 8
                Button { text: qsTr("检查数据范围"); enabled: !ui.busy; onClicked: ui.run("scan") }
                Button { text: qsTr("备份"); enabled: !ui.busy && window.remoteConfigured && ui.keyFile.length > 0; onClicked: ui.run("backup") }
                Item { Layout.fillWidth: true }
            }
        }
    }
    Component {
        id: rootsPage
        ColumnLayout {
            spacing: 4
            RowLayout { Layout.fillWidth: true; Layout.bottomMargin: 14; Label { text: qsTr("已发现 ") + ui.configuration.roots.length + qsTr(" 个数据目录"); color: window.ink; Layout.fillWidth: true } Button { text: qsTr("添加目录"); highlighted: true; onClicked: ui.addRoot() } Button { text: qsTr("重新发现"); onClicked: ui.discoverRoots() } }
            Setting {
                title: qsTr("选择具体内容"); description: qsTr("按实际对话、项目、插件与 skills 勾选，不只按类型过滤"); iconName: "folder"; expanded: true
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    Repeater {
                        model: [{key:"conversations",label:qsTr("选择对话")},{key:"projects",label:qsTr("选择项目")},{key:"plugins",label:qsTr("选择插件")},{key:"skills",label:qsTr("选择 skills")}]
                        delegate: Button {
                            required property var modelData
                            text: modelData.label + "（" + ((ui.itemCatalog[modelData.key] || []).length || qsTr("待识别")) + "）"
                            enabled: !ui.busy && !ui.apiRunning
                            onClicked: contentSelector.openFor(modelData.key)
                        }
                    }
                }
                Caption { text: qsTr("可搜索标题、项目与路径；项目勾选会添加项目文件目录，不自动上传。") }
                Caption { text: qsTr("部分对话只保存所选原始记录，不带全量历史数据库；恢复为独立文件，不自动写入 Codex 索引。其他类型的数据按各自勾选控制。") }
            }
            Setting {
                title: qsTr("同步数据范围"); description: qsTr("仅处理勾选类型；默认全部选择"); iconName: "folder"; expandAvailable: true; expanded: true
                GridLayout {
                    Layout.fillWidth: true
                    columns: width >= 600 ? 2 : 1
                    columnSpacing: 20
                    rowSpacing: 0
                    Repeater {
                        model: [
                            { key: "conversations", label: qsTr("对话（含归档）") },
                            { key: "settings", label: qsTr("配置与应用状态") },
                            { key: "credentials", label: qsTr("密码、密钥与凭据") },
                            { key: "plugins", label: qsTr("插件") },
                            { key: "skills", label: "skills" },
                            { key: "other", label: qsTr("其他本地数据") }
                        ]
                        delegate: CheckBox {
                            required property var modelData
                            objectName: "dataType_" + modelData.key
                            text: modelData.label
                            checked: ui.dataTypes[modelData.key] !== false
                            onToggled: ui.setDataType(modelData.key, checked)
                            Accessible.name: text
                        }
                    }
                }
                Caption { text: qsTr("敏感凭据仅加密传输；系统绑定凭据跨设备恢复后可能需要重新登录。变更后请保存配置。") }
            }
            Setting {
                title: qsTr("对话与归档"); description: qsTr("包含所有日期的原始记录、.jsonl.zst 压缩归档、线程索引与历史数据库")
                iconName: "history"; expandAvailable: true; expanded: true
                trailing: ui.conversationHistory.files === undefined ? qsTr("未核查") : ui.conversationHistory.files + qsTr(" 个记录文件")
                Caption { text: ui.conversationHistory.files === undefined ? qsTr("核查只读取文件元数据和索引引用，不读取对话正文或上传数据。") : qsTr("活动记录：") + ui.conversationHistory.active.files + qsTr("（压缩 ") + ui.conversationHistory.active.compressed + qsTr("） · 归档记录：") + ui.conversationHistory.archived.files + qsTr("（压缩 ") + ui.conversationHistory.archived.compressed + "）" }
                Caption { visible: ui.conversationHistory.index_checked === true; text: qsTr("线程索引：活动 ") + (ui.conversationHistory.indexed_active || 0) + qsTr(" · 归档 ") + (ui.conversationHistory.indexed_archived || 0) + qsTr("。文件数量不等于唯一对话数量。") }
                Caption { visible: ui.conversationHistory.coverage_complete === false; text: qsTr("核查发现索引引用缺失或被排除的记录，请查看操作详情。现存归档和历史数据库仍会保留。"); color: window.dark ? "#ffb4ab" : "#b10e1e" }
                Caption { visible: (ui.conversationHistory.resolved_rollouts || []).length > 0; text: qsTr("已按对话 ID 找到 %1 条路径已变化的记录；原文件与索引保持不变。").arg((ui.conversationHistory.resolved_rollouts || []).length) }
                Button { text: qsTr("核查历史对话"); enabled: !ui.busy; onClicked: ui.run("conversations") }
            }
            Repeater {
                model: ui.configuration.roots
                delegate: Setting {
                    required property var modelData
                    required property int index
                    title: ({"codex_home":qsTr("Codex 主数据（含所有对话与归档）"),"codex_home_default":qsTr("默认 Codex 目录（含旧历史）"),"agents":qsTr("共享 skills 与代理配置"),"desktop":qsTr("桌面应用数据"),"desktop_local":qsTr("桌面应用本地状态")})[modelData.id] || modelData.id
                    description: modelData.path
                    iconName: "folder"
                    trailing: modelData.id
                    expandAvailable: true
                    CheckBox { text: qsTr("同步此目录"); checked: modelData.enabled !== false; onToggled: ui.setRootEnabled(index, checked) }
                    Caption { visible: modelData.enabled === false; text: qsTr("已排除此目录，不读取或上传其中的文件。") }
                    FieldLabel { text: qsTr("目录 ID") }
                    TextField { id: rootId; Layout.fillWidth: true; text: modelData.id; onEditingFinished: ui.updateRoot(index, text, rootPath.text) }
                    FieldLabel { text: qsTr("本地绝对路径") }
                    TextField { id: rootPath; Layout.fillWidth: true; text: modelData.path; onEditingFinished: ui.updateRoot(index, rootId.text, text) }
                    FieldLabel { text: qsTr("只包含这些相对路径（留空表示全部，每行一项）") }
                    ReadableScrollView { Layout.fillWidth: true; Layout.preferredHeight: 86; TextArea { id: includePaths; text: (modelData.include || []).join("\n"); placeholderText: qsTr("例如：sessions/2026/10\nskills/my-skill"); wrapMode: TextEdit.NoWrap; Accessible.name: qsTr("目录包含路径") } }
                    FieldLabel { text: qsTr("排除这些相对路径（每行一项，排除优先）") }
                    ReadableScrollView { Layout.fillWidth: true; Layout.preferredHeight: 86; TextArea { id: excludePaths; text: (modelData.exclude || []).join("\n"); placeholderText: qsTr("例如：cache\nplugins/cache/vendor/plugin"); wrapMode: TextEdit.NoWrap; Accessible.name: qsTr("目录排除路径") } }
                    Button { text: qsTr("应用路径规则"); onClicked: ui.setRootRules(index, includePaths.text, excludePaths.text) }
                    Caption { text: qsTr("精确匹配文件或文件夹及其子项，不使用通配符。路径规则与类型、条目选择同时生效。") }
                    RowLayout { Button { text: qsTr("移除映射"); onClicked: ui.removeRoot(index) } Caption { text: qsTr("仅移除映射，不删除文件") } }
                }
            }
            Caption { text: qsTr("Conversations、外部项目和符号链接的真实目标，请显式添加。不同设备用相同 ID 对应各自路径。"); Layout.topMargin: 16 }
            Button { text: qsTr("保存配置"); Layout.topMargin: 14; onClicked: ui.saveConfig() }
        }
    }
    Component {
        id: connectionPage
        ColumnLayout {
            spacing: 4
            Setting {
                title: qsTr("存储服务"); description: qsTr("同一同步空间的设备使用相同服务与仓库 ID"); iconName: "cloud"; expandAvailable: true; expanded: true
                ComboBox {
                    Layout.fillWidth: true
                    model: ["WebDAV", "Google Drive", qsTr("本地仓库")]
                    currentIndex: window.provider === "google_drive" ? 1 : window.provider === "local" ? 2 : 0
                    onActivated: ui.setSetting("provider", ["webdav", "google_drive", "local"][currentIndex])
                    Accessible.name: qsTr("同步存储服务")
                }
            }
            Setting {
                visible: window.provider === "google_drive"
                title: "Google Drive"; description: ui.googleStorageLabel + " · " + ui.encryptionLabel; iconName: "cloud"; expandAvailable: true; expanded: true
                trailing: ui.googleConnectionLabel
                FieldLabel { text: qsTr("备份显示方式") }
                ComboBox {
                    objectName: "googleStorageMode"
                    Layout.fillWidth: true
                    model: [qsTr("可见文件夹（我的云端硬盘）"), qsTr("隐藏应用数据")]
                    currentIndex: ui.configuration.remote.storage_mode === "appdata" ? 1 : 0
                    onActivated: ui.setSetting("storage_mode", currentIndex === 0 ? "visible" : "appdata")
                    Accessible.name: qsTr("备份显示方式")
                }
                Caption { text: qsTr("切换会选择另一存储位置，不会自动迁移或删除原备份。旧快照仍可通过原模式恢复。") }
                Button { text: qsTr("在 Google Drive 中打开"); visible: ui.googleFolderUrl.length > 0; onClicked: Qt.openUrlExternally(ui.googleFolderUrl) }
                Caption { text: ui.googleClientReady ? qsTr("已发现本机应用配置，无需填写 Client ID。") : qsTr("本机尚无应用配置；可安全导入，或展开高级设置使用自己的桌面 OAuth 应用。") }
                CheckBox { id: googleAdvanced; text: qsTr("高级 OAuth 设置"); checked: !ui.googleClientReady }
                FieldLabel { visible: googleAdvanced.checked; text: qsTr("桌面应用 OAuth Client ID") }
                TextField { visible: googleAdvanced.checked; Layout.fillWidth: true; text: ui.configuration.remote.client_id || ""; placeholderText: qsTr("…apps.googleusercontent.com 或 CXS_GOOGLE_CLIENT_ID"); onEditingFinished: ui.setSetting("client_id", text); Accessible.name: "Google OAuth Client ID" }
                FieldLabel { visible: googleAdvanced.checked; text: qsTr("Client Secret（若该 OAuth 客户端需要）") }
                TextField { visible: googleAdvanced.checked; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: qsTr("不写入配置文件"); onEditingFinished: ui.setSetting("google_client_secret", text); Accessible.name: "Google OAuth Client Secret" }
                FieldLabel { text: qsTr("仓库 ID") }
                TextField { Layout.fillWidth: true; text: ui.configuration.remote.repository || "default"; placeholderText: "default"; onEditingFinished: ui.setSetting("repository", text); Accessible.name: qsTr("Google Drive 仓库 ID") }
                RowLayout { spacing: 10; Button { text: ui.googleAuthorized ? qsTr("重新登录 Google") : qsTr("登录 Google"); highlighted: enabled; enabled: !ui.busy && ui.keyFile.length > 0 && ui.googleClientReady; onClicked: ui.authorizeGoogle() } Button { text: qsTr("验证连接"); enabled: !ui.busy && ui.googleAuthorized && ui.keyFile.length > 0; onClicked: ui.verifyGoogleConnection() } }
                Label { text: ui.googleConnectionLabel; font.pixelSize: 15; font.weight: Font.DemiBold; color: ui.googleConnectionState === "connected" ? (window.dark ? "#6cdd87" : "#166a2b") : ui.googleConnectionState.indexOf("failed") >= 0 ? (window.dark ? "#ffb4ab" : "#b42318") : window.ink; Layout.fillWidth: true; wrapMode: Text.WordWrap; Accessible.name: qsTr("Google 登录状态：") + text }
                Caption { text: ui.googleLoginMessage; visible: text.length > 0 }
                Caption { text: ui.googleSnapshotStatus }
                Caption { text: qsTr("浏览器授权提示不代表数据已上传；上传结果请以应用的备份操作详情为准。") }
                Caption { text: ui.configuration.remote.storage_mode === "appdata" ? qsTr("隐藏模式申请 drive.appdata；可见模式申请最小 drive.file。令牌加密保存在本机，不上传。") : qsTr("可见模式只管理应用创建或授权的文件，申请最小 drive.file。令牌加密保存在本机，不上传。") }
                Button { text: qsTr("查看 OAuth 配置说明"); flat: true; onClicked: Qt.openUrlExternally("https://developers.google.com/identity/protocols/oauth2/native-app") }
            }
            Setting {
                visible: window.provider === "webdav"
                title: "WebDAV"; description: qsTr("支持标准 WebDAV 与 HTTPS 证书校验"); iconName: "cloud"; expandAvailable: true; expanded: true
                FieldLabel { text: qsTr("服务器地址") }
                TextField { Layout.fillWidth: true; text: ui.configuration.remote.url || ""; placeholderText: "https://server/remote.php/dav/files/user"; onEditingFinished: ui.setSetting("url", text) }
                RowLayout {
                    Layout.fillWidth: true; spacing: 12
                    ColumnLayout { Layout.fillWidth: true; FieldLabel { text: qsTr("用户名") } TextField { Layout.fillWidth: true; placeholderText: qsTr("WebDAV 用户名"); onEditingFinished: ui.setSetting("username", text) } }
                    ColumnLayout { Layout.fillWidth: true; FieldLabel { text: qsTr("密码 / 应用密码") } TextField { Layout.fillWidth: true; placeholderText: qsTr("仅保存在本次进程"); echoMode: TextInput.Password; onEditingFinished: ui.setSetting("password", text) } }
                }
                Caption { text: qsTr("用户名和密码不写入配置文件。建议使用网盘的独立应用密码。") }
            }
            Setting {
                visible: window.provider === "local"
                title: qsTr("本地仓库"); description: qsTr("不访问网络，可用于离线备份与测试"); iconName: "folder"; expandAvailable: true; expanded: true
                FieldLabel { text: qsTr("仓库绝对路径") }
                TextField { Layout.fillWidth: true; text: ui.configuration.remote.directory || ""; placeholderText: qsTr("选择独立的备份目录"); onEditingFinished: ui.setSetting("directory", text) }
            }
            Setting {
                title: qsTr("高级设置"); description: qsTr("设备标识与本地状态"); iconName: "settings"; expandAvailable: true
                Caption { text: (ui.portableEdition ? qsTr("便携版 · 数据保存在程序目录") : qsTr("安装版 · 数据保存在当前用户目录")) + "\n" + ui.dataDirectory }
                FieldLabel { text: qsTr("本地状态目录") } TextField { Layout.fillWidth: true; readOnly: ui.portableEdition; text: ui.configuration.state; onEditingFinished: ui.setSetting("state", text) }
                FieldLabel { text: qsTr("设备 ID") } TextField { Layout.fillWidth: true; readOnly: true; text: ui.configuration.device }
            }
            Button { text: qsTr("保存配置"); highlighted: true; Layout.topMargin: 18; Layout.bottomMargin: 18; onClicked: ui.saveConfig() }
            Caption { text: qsTr("Google Drive 使用追加式不可变快照保留并发分支；不覆盖远端历史，不传播删除。系统绑定凭据跨机器恢复后可能需要重新登录。") }
        }
    }
    Component {
        id: encryptionPage
        ColumnLayout {
            spacing: 4
            Setting {
                title: qsTr("保存方式"); description: ui.encryptionLabel; iconName: "shield_lock"; expandAvailable: true; expanded: true
                ComboBox {
                    objectName: "encryptionMode"
                    Layout.fillWidth: true
                    model: [qsTr("不加密 · 原样保存"), qsTr("全部加密"), qsTr("部分加密")]
                    currentIndex: ui.configuration.payload_mode === "original" ? 0 : ui.configuration.payload_mode === "selective" ? 2 : 1
                    onActivated: ui.setSetting("payload_mode", ["original", "encrypted", "selective"][currentIndex])
                    Accessible.name: qsTr("保存方式")
                }
                Caption { text: qsTr("未加密的文件保留目录、文件名和内容，可在存储端直接打开。加密文件恢复时需要原主密钥。") }
                Caption { text: qsTr("原样模式保留 SQLite 主库与 WAL／journal；不复制进程锁和共享内存。") }
                Caption { visible: ui.configuration.payload_mode === "selective"; text: qsTr("部分加密保留目录与文件名；选中的文件加密并添加 .cxs 后缀，其余原样保存。") }
            }
            Repeater {
                model: ui.configuration.payload_mode === "selective" ? ui.configuration.roots : []
                delegate: Setting {
                    required property var modelData
                    title: modelData.id; description: modelData.path; iconName: "folder"; expandAvailable: true; expanded: true
                    CheckBox { id: allEncrypted; text: qsTr("加密此目录的全部数据"); checked: ui.encryptionRootAll(modelData.id); onClicked: ui.setEncryptionRules(modelData.id, checked, encryptedPaths.text) }
                    FieldLabel { visible: !allEncrypted.checked; text: qsTr("只加密这些相对路径（每行一项）") }
                    ReadableScrollView { visible: !allEncrypted.checked; Layout.fillWidth: true; Layout.preferredHeight: 86; TextArea { id: encryptedPaths; text: ui.encryptionRuleText(modelData.id); placeholderText: "sessions\nskills/private-skill\nauth.json"; wrapMode: TextEdit.NoWrap; Accessible.name: qsTr("加密相对路径") } }
                    Button { visible: !allEncrypted.checked; text: qsTr("应用加密范围"); onClicked: ui.setEncryptionRules(modelData.id, false, encryptedPaths.text) }
                    Caption { text: qsTr("路径包含其子目录。SQLite 主库被选中时，其 WAL／journal 同时加密。") }
                }
            }
            Setting {
                title: qsTr("密钥密码"); description: qsTr("保护本机主密钥文件"); iconName: "shield_lock"; expandAvailable: true; expanded: true
                TextField { objectName: "keyPassword"; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: qsTr("仅保留在本次会话"); onTextEdited: ui.setKeyPassword(text); Accessible.name: qsTr("密钥密码") }
                Caption { text: qsTr("生成新密钥时，填写的密码用于保护密钥文件。使用已保护的密钥时，输入原密码解锁。留空生成普通密钥；不会改写已有密钥。") }
            }
            Setting {
                title: qsTr("加密主密钥"); description: qsTr("同一同步空间的所有设备共用一把主密钥"); iconName: "key"; expandAvailable: true; expanded: true
                TextField { Layout.fillWidth: true; readOnly: true; text: ui.keyFile; placeholderText: qsTr("尚未选择主密钥") }
                RowLayout { spacing: 8; Button { text: qsTr("选择密钥"); onClicked: ui.chooseKey() } Button { text: qsTr("生成新密钥"); onClicked: ui.createKey() } }
                Caption { text: qsTr("密钥丢失无法恢复数据。请另存安全副本，不要放进同步目录。") }
            }
            Caption { text: qsTr("Google 登录令牌始终加密保存在本机。未加密备份中的数据可被有存储访问权限的人读取。") }
            Button { text: qsTr("保存配置"); highlighted: true; Layout.topMargin: 18; onClicked: ui.saveConfig() }
        }
    }
    Component {
        id: historyPage
        ColumnLayout {
            spacing: 4
            Setting { title: qsTr("不可变快照"); description: qsTr("每次备份保留独立版本，相同内容增量复用"); iconName: "history" }
            Rectangle {
                visible: ui.snapshots.length === 0
                Layout.fillWidth: true; implicitHeight: 300; color: "transparent"
                ColumnLayout { anchors.centerIn: parent; spacing: 12; Icon { name: "history"; darkMode: window.dark; opacity: 0.45; Layout.preferredWidth: 48; Layout.preferredHeight: 48; Layout.alignment: Qt.AlignHCenter } Label { text: qsTr("还没有加载快照"); font.pixelSize: 18; color: window.ink; Layout.alignment: Qt.AlignHCenter } Label { text: qsTr("设置连接和密钥后，读取你的同步历史。"); color: window.secondary; font.pixelSize: 13 } Button { text: qsTr("读取快照"); enabled: ui.keyFile.length > 0 && !ui.busy; onClicked: ui.run("history"); Layout.alignment: Qt.AlignHCenter } }
            }
            Repeater { model: ui.snapshots; delegate: Setting { required property var modelData; title: modelData.time; description: modelData.entries + qsTr(" 个条目 · ") + modelData.conflicts + qsTr(" 个冲突"); iconName: "history"; expandAvailable: true; Caption { text: modelData.id } Button { text: qsTr("恢复到新目录"); onClicked: ui.restore(modelData.id) } } }
            Button { visible: ui.snapshots.length > 0; text: qsTr("刷新快照"); onClicked: ui.run("history"); Layout.topMargin: 16 }
        }
    }
    Component {
        id: apiPage
        ColumnLayout {
            spacing: 4
            Setting {
                title: qsTr("命令行 CLI"); description: qsTr("无需打开 GUI，可在 PowerShell 或 Linux 终端中运行"); iconName: "code"; expandAvailable: true; expanded: true
                Label { text: ui.terminalCommand; color: window.secondary; font.family: Qt.platform.os === "windows" ? "Consolas" : "monospace"; font.pixelSize: 13; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                Button { text: qsTr("复制帮助命令"); onClicked: ui.copyTerminalCommand() }
                Label { text: qsTr("codex-sync conversations --config <配置文件>\ncodex-sync scan --config <配置文件>\ncodex-sync backup --config <配置文件>\ncodex-sync sync --config <配置文件> --offline"); color: window.secondary; font.family: Qt.platform.os === "windows" ? "Consolas" : "monospace"; font.pixelSize: 13; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                Caption { text: qsTr("conversations 核查全部本地历史与归档；sync 应用改动前必须关闭 Codex。密钥与 WebDAV 凭据通过环境变量传入。") }
            }
            Section { text: qsTr("API 接口") }
            Setting {
                title: qsTr("本地 HTTP API"); description: qsTr("仅监听 127.0.0.1，拒绝未授权访问"); iconName: "code"; trailing: ui.apiRunning ? qsTr("运行中") : qsTr("未启动"); expandAvailable: true; expanded: true
                RowLayout { FieldLabel { text: qsTr("监听端口") } SpinBox { id: apiPort; objectName: "apiPort"; from: 1024; to: 65535; value: 12306; editable: true; textFromValue: function(value, locale) { return String(value) } } }
                RowLayout { spacing: 8; Button { text: ui.apiRunning ? qsTr("停止 API") : qsTr("启动 API"); highlighted: !ui.apiRunning; onClicked: ui.apiRunning ? ui.stopApi() : ui.startApi(apiPort.value) } Caption { text: qsTr("关闭应用时，将停止由界面启动的 API。") } }
            }
            Setting {
                title: qsTr("访问令牌"); description: qsTr("每次启动 API 时生成，仅在当前进程内有效"); iconName: "shield_lock"; expandAvailable: true; expanded: true
                RowLayout { Layout.fillWidth: true; TextField { Layout.fillWidth: true; readOnly: true; echoMode: TextInput.Password; text: ui.apiToken; placeholderText: qsTr("启动 API 后生成") } Button { text: qsTr("复制"); enabled: ui.apiToken.length > 0; onClicked: ui.copyToken() } }
            }
            Section { text: qsTr("开发者接口") }
            Setting { title: "HTTP / C API"; description: qsTr("GUI、终端模式与 API 共用同一个 C++ 核心"); iconName: "code"; expandAvailable: true; expanded: true; Label { text: "GET   /v1/health\nPOST  /v1/run\n\nAuthorization: Bearer <token>\n{\"op\": \"backup\"}\n\nC API: cxs_run() / cxs_free()"; color: window.secondary; font.family: "Consolas"; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere } }
        }
    }
    Component {
        id: settingsPage
        ColumnLayout {
            spacing: 4
            Section { text: qsTr("外观"); Layout.topMargin: 0 }
            Setting { title: qsTr("应用主题"); description: qsTr("使用 FluentWinUI3 控件，默认跟随系统外观"); iconName: "settings"; expandAvailable: true; expanded: true; ComboBox { model: [qsTr("跟随系统"), qsTr("浅色"), qsTr("深色")]; currentIndex: ui.themeMode; onActivated: ui.setTheme(currentIndex); Layout.preferredWidth: 180 } }
            Setting {
                title: qsTr("语言"); description: qsTr("立即切换界面语言，保存在本版数据目录"); iconName: "settings"; expanded: true
                ComboBox { objectName: "languageCombo"; model: ["简体中文", "English", qsTr("跟随系统")]; currentIndex: ui.language === "en" ? 1 : ui.language === "system" ? 2 : 0; onActivated: ui.setLanguage(["zh_CN", "en", "system"][currentIndex]); Layout.preferredWidth: 200; enabled: !ui.busy; Accessible.name: qsTr("语言") }
            }
            Section { text: qsTr("配置") }
            Setting { title: qsTr("同步配置文件"); description: ui.configFile.length > 0 ? ui.configFile : qsTr("尚未保存配置"); iconName: "folder"; expandAvailable: true; expanded: true; RowLayout { Button { text: qsTr("打开配置"); onClicked: ui.openConfig() } Button { text: qsTr("保存配置"); onClicked: ui.saveConfig() } } }
            Setting {
                title: qsTr("同步方向"); description: qsTr("选择上传、双向合并或下载恢复"); iconName: "arrow_sync"; expanded: true
                ComboBox {
                    objectName: "syncModeCombo"
                    Layout.fillWidth: true
                    model: [qsTr("仅上传备份"), qsTr("双向合并"), qsTr("仅下载至独立恢复目录")]
                    currentIndex: ui.syncMode === "bidirectional" ? 1 : ui.syncMode === "download" ? 2 : 0
                    onActivated: ui.setSyncMode(["upload", "bidirectional", "download"][currentIndex])
                    Accessible.name: qsTr("同步方向")
                }
                Caption { text: ui.syncMode === "bidirectional" ? qsTr("仅手动执行：双向合并前必须关闭 Codex，并确认操作；冲突保留版本，不传播删除。") : ui.syncMode === "download" ? qsTr("仅手动执行：下载至独立恢复目录，绝不覆盖本地源目录。") : qsTr("可手动上传或开启自动备份；不自动恢复或覆盖本地数据。") }
            }
            Setting {
                title: qsTr("后台自动同步"); description: qsTr("默认关闭 · 开启后关闭窗口仍在托盘运行"); iconName: "arrow_sync"
                objectName: "autoScheduleCard"
                enabled: true; expanded: true
                Switch {
                    objectName: "autoSyncSwitch"
                    text: checked ? qsTr("已开启") : qsTr("已关闭")
                    checked: ui.autoSync
                    enabled: ui.autoSync || (!ui.busy && ui.configurationSaved && ui.syncMode === "upload" && window.remoteConfigured && ui.keyFile.length > 0)
                    onToggled: ui.setAutoSync(checked)
                    Accessible.name: qsTr("自动同步")
                }
                ComboBox {
                    objectName: "autoTriggerCombo"; Layout.fillWidth: true
                    model: [qsTr("有变更即同步"), qsTr("按时间间隔"), qsTr("按变更数据量")]
                    currentIndex: ui.autoTrigger === "time" ? 1 : ui.autoTrigger === "amount" ? 2 : 0
                    enabled: !ui.busy
                    onActivated: ui.setAutoTrigger(["changes", "time", "amount"][currentIndex])
                    Accessible.name: qsTr("自动同步触发条件")
                }
                RowLayout {
                    visible: ui.autoTrigger === "time"; Layout.fillWidth: true
                    Label { text: qsTr("同步间隔"); Layout.fillWidth: true }
                    SpinBox { objectName: "autoIntervalMinutes"; from: 1; to: 1440; value: ui.autoIntervalMinutes; editable: true; enabled: !ui.busy; onValueModified: ui.setAutoIntervalMinutes(value); Accessible.name: qsTr("同步间隔（分钟）") }
                    Label { text: qsTr("分钟") }
                }
                RowLayout {
                    visible: ui.autoTrigger === "amount"; Layout.fillWidth: true
                    Label { text: qsTr("累计变更达到"); Layout.fillWidth: true }
                    SpinBox { objectName: "autoThresholdMB"; from: 1; to: 1048576; value: ui.autoThresholdMB; editable: true; enabled: !ui.busy; onValueModified: ui.setAutoThresholdMB(value); Accessible.name: qsTr("变更数据阈值（MiB）") }
                    Label { text: "MiB" }
                }
                Caption { visible: ui.autoTrigger === "amount"; text: ui.autoPending + "\n" + qsTr("按变更文件的大小估算，不是实际上传流量。连续变更合并核对；未达阈值不读取文件正文。") }
                Caption { text: qsTr("只在“仅上传备份”模式可开启；其他方向仅手动执行。开启前请保存配置、设置存储空间并选择主密钥；处理中仍可关闭。") }
            }
            Section { text: qsTr("启动与后台") }
            Setting {
                title: qsTr("开机自启"); description: qsTr("登录系统后启动 CodexSync，不等于自动上传"); iconName: "desktop"; expanded: true
                Switch { objectName: "startOnLoginSwitch"; text: checked ? qsTr("已开启") : qsTr("已关闭"); checked: ui.startOnLogin; onToggled: ui.setStartOnLogin(checked); Accessible.name: qsTr("开机自启") }
            }
            Setting {
                title: qsTr("关闭到托盘"); description: qsTr("关闭窗口后保留托盘图标；使用托盘菜单退出程序"); iconName: "desktop"; expanded: true
                Switch { objectName: "closeToTraySwitch"; text: checked ? qsTr("已开启") : qsTr("已关闭"); checked: ui.closeToTray; onToggled: ui.setCloseToTray(checked); Accessible.name: qsTr("关闭到托盘") }
                Caption { text: qsTr("托盘期间程序仍在运行；已开启的自动备份会继续，退出程序后停止。") }
            }
            Section { text: qsTr("关于") }
            Setting { title: "CodexSync"; description: "C++23 · Qt Quick · " + (Qt.platform.os === "windows" ? "FluentWinUI3" : "Fusion"); iconName: "arrow_sync"; trailing: "v" + ui.applicationVersion + qsTr(" 预览") }
            Setting { title: qsTr("使用风险与免责声明"); description: qsTr("非官方开发预览 · 无担保 · 请保留独立备份"); iconName: "shield_lock"; expandAvailable: true; expanded: true; Button { text: qsTr("查看声明"); onClicked: usageNotice.open() } }
            Caption { text: qsTr("支持本地仓库、WebDAV 与 Google Drive。请先用测试数据确认当前设备和存储配置可用，并保留独立备份。"); Layout.topMargin: 16 }
        }
    }
    function openSelectionForSmoke() { contentSelector.openFor("conversations") }
    Dialog {
        id: contentSelector
        objectName: "contentSelector"
        // Selection must remain visible even when the background window's animation clock is paused.
        enter: Transition { }
        exit: Transition { }
        property string category: "conversations"
        readonly property var titles: ({conversations:qsTr("选择对话"),projects:qsTr("选择项目"),plugins:qsTr("选择插件"),skills:qsTr("选择 skills")})
        property var records: (ui.itemCatalog[category] || []).filter(function(item) {
            const term = selectorSearch.text.toLowerCase()
            if (term.length > 0 && ((item.title || "") + " " + (item.detail || "")).toLowerCase().indexOf(term) < 0) return false
            if (category === "conversations") {
                if (archiveFilter.currentIndex === 1 && item.archived) return false
                if (archiveFilter.currentIndex === 2 && !item.archived) return false
                if (projectFilter.currentIndex > 0) {
                    const project = (ui.itemCatalog.projects || [])[projectFilter.currentIndex - 1]
                    if (project && item.project_id !== project.id) return false
                }
            }
            return true
        })
        function openFor(value) {
            category = value; selectorSearch.text = ""; archiveFilter.currentIndex = 0; projectFilter.currentIndex = 0
            open()
            if (ui.itemCatalog.conversations === undefined) ui.refreshCatalog()
        }
        title: titles[category]
        anchors.centerIn: parent
        width: Math.min(window.width - 64, 820)
        height: Math.min(window.height - 72, 660)
        modal: true
        footer: DialogButtonBox { Button { text: qsTr("关闭"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole } }
        contentItem: ColumnLayout {
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                TextField { id: selectorSearch; Layout.fillWidth: true; placeholderText: qsTr("搜索标题、项目、插件、skill 或路径"); Accessible.name: qsTr("搜索同步条目") }
                Button { text: qsTr("重新识别"); enabled: !ui.busy; onClicked: ui.refreshCatalog() }
            }
            RowLayout {
                visible: contentSelector.category === "conversations"
                Layout.fillWidth: true
                ComboBox { id: archiveFilter; model: [qsTr("全部对话"), qsTr("活动对话"), qsTr("归档对话")]; Layout.preferredWidth: 200; Accessible.name: qsTr("对话归档筛选") }
                ComboBox { id: projectFilter; model: [qsTr("全部项目")].concat((ui.itemCatalog.projects || []).map(function(item) { return item.title })); Layout.fillWidth: true; Accessible.name: qsTr("对话项目筛选") }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: ui.busy ? qsTr("正在识别…") : qsTr("显示 ") + contentSelector.records.length + qsTr(" 项"); color: window.secondary; Layout.fillWidth: true }
                Button { text: qsTr("全部同步"); visible: contentSelector.category !== "projects"; enabled: !ui.busy; onClicked: ui.setCatalogAll(contentSelector.category, true) }
                Button { text: qsTr("全部不选"); visible: contentSelector.category !== "projects"; enabled: !ui.busy; onClicked: ui.setCatalogAll(contentSelector.category, false) }
            }
            ListView {
                id: selectionList
                objectName: "selectionList"
                Layout.fillWidth: true; Layout.fillHeight: true
                model: contentSelector.records
                clip: true
                spacing: 2
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    implicitHeight: 76
                    enabled: !ui.busy && modelData.available !== false
                    contentItem: RowLayout {
                        spacing: 12
                        CheckBox {
                            checked: { const config = ui.configuration; return ui.catalogItemSelected(contentSelector.category, modelData.id) }
                            onToggled: ui.setCatalogItem(contentSelector.category, modelData.id, checked)
                            Accessible.name: modelData.title
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            Label { text: modelData.title; textFormat: Text.PlainText; color: window.ink; font.pixelSize: 14; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
                            Label { text: contentSelector.category === "conversations" ? (modelData.project_id ? modelData.project : qsTr("未分组")) + " · " + (modelData.archived ? qsTr("归档") : qsTr("活动")) + " · " + (modelData.available === false ? qsTr("原始记录缺失，无法同步") : (modelData.cwd || "")) : (modelData.detail || ""); textFormat: Text.PlainText; color: window.secondary; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                        }
                        Label { visible: contentSelector.category === "projects"; text: (modelData.chats || 0) + qsTr(" 个对话"); color: window.secondary; font.pixelSize: 12 }
                    }
                    onClicked: ui.setCatalogItem(contentSelector.category, modelData.id, !ui.catalogItemSelected(contentSelector.category, modelData.id))
                }
                Label { anchors.centerIn: parent; visible: !ui.busy && selectionList.count === 0; text: qsTr("未找到匹配条目"); color: window.secondary }
            }
            Caption { visible: (ui.itemCatalog.warnings || []).length > 0; text: qsTr("识别有 ") + (ui.itemCatalog.warnings || []).length + qsTr(" 条警告，请查看操作详情。"); color: window.dark ? "#ffb4ab" : "#b42318" }
            Caption { text: contentSelector.category === "projects" ? qsTr("勾选项目会加入项目源文件目录；不改动 Codex 的项目绑定，不自动上传。") : qsTr("勾选会实际影响同步范围，而不是仅过滤显示。搜索与归档/项目筛选只改变当前列表。") }
            Button { text: qsTr("保存选择"); highlighted: true; enabled: !ui.busy; onClicked: { ui.saveConfig(); if (ui.configurationSaved) contentSelector.close() } }
        }
    }
    Dialog {
        id: usageNotice
        objectName: "usageNotice"
        title: qsTr("使用风险与免责声明")
        anchors.centerIn: parent
        width: Math.min(window.width - 48, 760)
        height: Math.min(window.height - 48, 650)
        modal: true
        closePolicy: Popup.NoAutoClose
        contentItem: ColumnLayout {
            spacing: 14
            Label { text: qsTr("请先阅读：对话和密钥可能含敏感信息，同步不能代替独立备份。"); color: window.ink; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 14 }
            ReadableScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true
                ScrollBar.vertical.policy: ScrollBar.AlwaysOn
                TextArea { text: ui.disclaimerText; textFormat: TextEdit.MarkdownText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; color: window.ink; font.pixelSize: 14; font.weight: Font.Medium; background: null; Accessible.name: qsTr("使用风险与免责声明全文") }
            }
            CheckBox { id: noticeRead; text: qsTr("我已阅读并了解上述风险"); visible: !ui.noticeAcknowledged; Accessible.name: text }
            RowLayout {
                Layout.fillWidth: true; spacing: 12
                Caption { text: qsTr("可滚动阅读全文；阅读记录仅保存在本机。"); Layout.fillWidth: true }
                Button { text: ui.noticeAcknowledged ? qsTr("关闭") : qsTr("退出"); onClicked: ui.noticeAcknowledged ? usageNotice.close() : Qt.quit() }
                Button { id: noticeContinue; objectName: "noticeContinue"; text: qsTr("继续使用"); highlighted: enabled; visible: !ui.noticeAcknowledged; enabled: noticeRead.checked; onClicked: { if (ui.acknowledgeNotice()) usageNotice.close() } }
            }
        }
    }
    Dialog {
        id: details
        objectName: "operationDetails"
        title: qsTr("操作详情")
        anchors.centerIn: parent
        width: Math.min(window.width - 80, 720)
        height: Math.min(window.height - 100, 500)
        modal: true
        footer: DialogButtonBox { Button { objectName: "openRuntimeLogs"; text: qsTr("打开运行日志"); DialogButtonBox.buttonRole: DialogButtonBox.ActionRole; onClicked: ui.openLogs() } Button { text: qsTr("关闭"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole } }
        contentItem: ReadableScrollView { TextArea { text: ui.logText.length > 0 ? ui.logText : qsTr("尚未执行任何操作。\n\n这里显示真实的检查、备份和同步结果，不展示示例数据。"); readOnly: true; selectByMouse: true; wrapMode: TextArea.WrapAnywhere; font.pixelSize: 13 } }
    }
}
