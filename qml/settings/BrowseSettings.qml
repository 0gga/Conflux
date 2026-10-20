pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
import "../components"
ColumnLayout {
    id: root
    required property var preferences
    spacing: 18
    Text { text: "Browsing"; color: Theme.colors.text; font.pixelSize: 22; font.weight: Font.DemiBold }
    Rectangle {
        Layout.fillWidth: true; implicitHeight: body.implicitHeight + 32; radius: 16; color: Theme.colors.card
        RowLayout {
            id: body; anchors.fill: parent; anchors.margins: 16; spacing: 16
            ColumnLayout {
                Layout.fillWidth: true; spacing: 8
                Text { Layout.fillWidth: true; text: "Include Fabric with Connector"; color: Theme.colors.text; font.pixelSize: 14; font.weight: Font.DemiBold; wrapMode: Text.WordWrap }
                Text { Layout.fillWidth: true; text: "When an enabled Sinytra Connector is detected in a Forge or NeoForge instance, browse Fabric alongside its primary loader. Individual mod compatibility still depends on the bridge."; color: Theme.colors.muted; font.pixelSize: 12; wrapMode: Text.WordWrap }
            }
            ToggleSwitch { checked: root.preferences.connectorFabric; Accessible.name: "Include Fabric when Connector is present"; onToggled: root.preferences.connectorFabric=checked }
        }
    }
}
