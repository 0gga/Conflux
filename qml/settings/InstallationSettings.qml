pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
import "../components"
ColumnLayout {
    id: root
    objectName: "installationSettings"
    required property var preferences
    spacing: 16
    Text { text: "Installation"; color: Theme.colors.text; font.pixelSize: 22; font.weight: Font.DemiBold }
    Rectangle {
        Layout.fillWidth: true; implicitHeight: choices.implicitHeight + 28
        radius: ControlStyle.panelRadius; color: Theme.colors.card
        RowLayout {
            id: choices
            anchors.fill: parent; anchors.margins: 14; spacing: 14
            Icon { name: "content"; size: 22; color: Theme.colors.muted }
            Text { Layout.fillWidth: true; text: "Offer optional content"; color: Theme.colors.text; font.pixelSize: 14; wrapMode: Text.WordWrap }
            ToggleSwitch { objectName: "promptOptionalDependencies"; checked: root.preferences.promptOptionalDependencies; Accessible.name: "Offer optional content after installs"; onToggled: root.preferences.promptOptionalDependencies = checked }
        }
    }
}
