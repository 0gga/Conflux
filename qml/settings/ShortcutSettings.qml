pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
import "../components"
ColumnLayout {
    id: root
    objectName: "shortcutSettings"
    required property var preferences
    spacing: 16
    RowLayout {
        Layout.fillWidth: true
        Text { Layout.fillWidth: true; text: "Shortcuts"; color: Theme.colors.text; font.pixelSize: 22; font.weight: Font.DemiBold }
        ActionButton { objectName: "resetAllShortcuts"; text: "Reset all"; iconName: "refresh"; compact: true; onClicked: root.preferences.resetAll() }
    }
    Text { Layout.fillWidth: true; visible: !!root.preferences.error && !root.preferences.capturing; text: root.preferences.error; color: Theme.colors.warning; wrapMode: Text.Wrap; textFormat: Text.PlainText }
    Repeater {
        model: root.preferences.model
        delegate: Rectangle {
            id: row
            required property var modelData
            Layout.fillWidth: true; implicitHeight: body.implicitHeight + 28
            color: Theme.colors.card; radius: ControlStyle.panelRadius
            RowLayout {
                id: body
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 14 }
                spacing: 12
                Icon { name: row.modelData.iconName; color: Theme.colors.muted; size: 20 }
                Text { Layout.fillWidth: true; Layout.minimumWidth: 0; text: row.modelData.title; color: Theme.colors.text; font.pixelSize: 13; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
                ActionButton { objectName: "recordShortcut-" + row.modelData.key; text: row.modelData.display; compact: true; onClicked: root.preferences.beginCapture(row.modelData.key) }
                ActionButton { objectName: "resetShortcut-" + row.modelData.key; iconName: "refresh"; hint: "Reset shortcut"; compact: true; subtle: true; enabled: !!row.modelData.custom; onClicked: root.preferences.reset(row.modelData.key) }
            }
        }
    }
}
