pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
import "../components"
import "../settings"
Item {
    id: root
    required property var app
    required property var warnings
    required property var browsePreferences
    property string tab: "appearance"
    signal tabRequested(string key)
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 28; spacing: 22
        RowLayout { Layout.fillWidth: true; Icon { name: "settings"; size: 26 } Text { text: "Settings"; color: Theme.colors.text; font.pixelSize: 29; font.weight: Font.DemiBold } }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 24
            ColumnLayout {
                Layout.preferredWidth: 176; Layout.minimumWidth: 176; Layout.maximumWidth: 176; Layout.fillWidth: false; Layout.fillHeight: true; spacing: 6
                Repeater {
                    model: [{category:"GENERAL",key:"appearance",label:"Appearance",icon:"settings"},{category:"MODPACKS",key:"warnings",label:"Warnings",icon:"warning"},{category:"",key:"browse",label:"Browsing",icon:"compass"},{category:"SERVICES",key:"connections",label:"Connections",icon:"compass"}]
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true; spacing: 8
                        Text { visible: parent.modelData.category.length > 0; Layout.topMargin: 14; Layout.leftMargin: 14; text: parent.modelData.category; color: Theme.colors.muted; font.pixelSize: 10; font.letterSpacing: 1.3 }
                        NavigationButton { Layout.fillWidth: true; text: parent.modelData.label; iconName: parent.modelData.icon; selected: root.tab === parent.modelData.key; onClicked: root.tabRequested(parent.modelData.key) }
                    }
                }
                Item { Layout.fillHeight: true }
            }
            StackLayout {
                Layout.minimumWidth: 0; Layout.fillWidth: true; Layout.fillHeight: true
                currentIndex: root.tab === "appearance" ? 0 : 1
                AppearanceSettings { Layout.fillWidth: true; Layout.fillHeight: true }
                BoundedScrollView {
                    id: scroll; clip: true
                    ColumnLayout {
                        width: scroll.availableWidth; spacing: 16
                        BrowseSettings { visible: root.tab === "browse"; Layout.fillWidth: true; preferences: root.browsePreferences }
                        WarningSettings { visible: root.tab === "warnings"; Layout.fillWidth: true; preferences: root.warnings }
                        ConnectionSettings { visible: root.tab === "connections"; Layout.fillWidth: true; app: root.app }
                    }
                }
            }
        }
    }
}
