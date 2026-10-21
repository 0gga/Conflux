pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
import "../components"
ColumnLayout {
    id: root; required property var preferences
    spacing: 16
    RowLayout {
        Layout.fillWidth: true
        ColumnLayout { Layout.fillWidth: true; spacing: 8; Text { text: "Warnings"; color: Theme.colors.text; font.pixelSize: 22; font.weight: Font.DemiBold } Text { Layout.fillWidth: true; text: "Choose what appears in pack review banners. Scans still run; these switches only change visibility."; color: Theme.colors.muted; font.pixelSize: 12; wrapMode: Text.WordWrap } }
        ActionButton { iconName: "refresh"; text: "Reset"; subtle: true; onClicked: root.preferences.reset() }
    }
    Repeater {
        model: root.preferences.options
        delegate: Rectangle {
            id: option; required property var modelData
            Layout.fillWidth: true; implicitHeight: body.implicitHeight + 32; radius: ControlStyle.panelRadius; color: Theme.colors.card
            RowLayout { id: body; anchors.fill: parent; anchors.margins: 16; spacing: 16
                Icon { name: "warning"; color: Theme.colors.warning }
                ColumnLayout { Layout.fillWidth: true; spacing: 7; Text { text: option.modelData.title; color: Theme.colors.text; font.pixelSize: 14; font.weight: Font.DemiBold } Text { Layout.fillWidth: true; text: option.modelData.description; color: Theme.colors.muted; font.pixelSize: 12; wrapMode: Text.WordWrap } }
                ToggleSwitch { checked: option.modelData.enabled; Accessible.name: option.modelData.title; onToggled: root.preferences.setEnabled(option.modelData.id,checked) }
            }
        }
    }
}
