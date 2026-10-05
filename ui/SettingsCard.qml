import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Rectangle {
    id: card
    property string title: ""
    property string description: ""
    property string iconName: "settings"
    property string trailing: ""
    property bool darkMode: false
    property bool clickable: false
    property bool expanded: false
    property bool expandAvailable: false
    default property alias detail: details.data
    signal clicked()
    radius: 4
    color: mouse.containsMouse && clickable ? (darkMode ? "#323232" : "#fbfbfb") : (darkMode ? "#2b2b2b" : "#ffffff")
    border.color: activeFocus ? (darkMode ? "#60cdff" : "#0067c0") : (darkMode ? "#353535" : "#e4e4e4")
    border.width: activeFocus ? 2 : 1
    implicitHeight: head.height + (expanded ? details.implicitHeight + 24 : 0)
    activeFocusOnTab: clickable || expandAvailable
    Accessible.role: clickable ? Accessible.Button : Accessible.Pane
    Accessible.name: title
    Keys.onReturnPressed: activate()
    Keys.onSpacePressed: activate()
    function activate() { if (expandAvailable) expanded = !expanded; else if (clickable) clicked() }
    Column {
        width: parent.width
        Item {
            id: head
            width: parent.width
            height: Math.max(76, textColumn.implicitHeight + 28)
            RowLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 18
                Icon { name: card.iconName; darkMode: card.darkMode; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
                ColumnLayout {
                    id: textColumn
                    Layout.fillWidth: true
                    spacing: 3
                    Label { text: card.title; font.pixelSize: 14; font.weight: Font.DemiBold; color: darkMode ? "#f3f3f3" : "#1b1b1b"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Label { text: card.description; visible: text.length > 0; font.pixelSize: 13; font.weight: Font.Medium; color: darkMode ? "#c4c4c4" : "#5a5a5a"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                }
                Label { text: card.trailing; visible: text.length > 0 && card.width > 430; color: darkMode ? "#c4c4c4" : "#5a5a5a"; font.pixelSize: 12 }
                Icon { name: "chevron_right"; darkMode: card.darkMode; visible: card.clickable || card.expandAvailable; rotation: card.expanded ? 90 : 0; Layout.preferredWidth: 16; Layout.preferredHeight: 16; Behavior on rotation { NumberAnimation { duration: 120 } } }
            }
            MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; cursorShape: clickable || expandAvailable ? Qt.PointingHandCursor : Qt.ArrowCursor; onClicked: card.activate() }
        }
        ColumnLayout { id: details; x: 60; width: parent.width - 80; spacing: 12; visible: card.expanded }
    }
}
