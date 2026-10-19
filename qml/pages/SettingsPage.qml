pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "../components"
import "../settings"
Item {
    id: root
    required property var app
    required property var warnings
    property string tab: "appearance"
    signal tabRequested(string key)
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 28; spacing: 22
        RowLayout { Layout.fillWidth: true; Icon { name: "settings"; size: 26 } Text { text: "Settings"; color: Theme.colors.text; font.pixelSize: 29; font.weight: Font.DemiBold } }
        SectionTabs { entries: [{key:"appearance",label:"Appearance",icon:"settings"},{key:"warnings",label:"Warnings",icon:"warning"},{key:"connections",label:"Connections",icon:"compass"}]; current: root.tab; onSelected: function(key) { root.tabRequested(key) } }
        ScrollView {
            id: scroll; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            ColumnLayout {
                width: scroll.availableWidth; spacing: 16
                AppearanceSettings { visible: root.tab === "appearance"; Layout.fillWidth: true }
                WarningSettings { visible: root.tab === "warnings"; Layout.fillWidth: true; preferences: root.warnings }
                ConnectionSettings { visible: root.tab === "connections"; Layout.fillWidth: true; app: root.app }
            }
        }
    }
}
