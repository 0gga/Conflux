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
    required property var catalogue
    required property var browsePreferences
    property var installationPreferences: null
    property string tab: "appearance"
    signal tabRequested(string key)
    readonly property bool compact: width < 650
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 24; spacing: 16
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 24
            BoundedScrollView {
                id: categoryScroll
                Layout.preferredWidth: root.compact ? 48 : 160; Layout.minimumWidth: Layout.preferredWidth; Layout.maximumWidth: Layout.preferredWidth; Layout.fillWidth: false; Layout.fillHeight: true; clip: true
                ColumnLayout {
                width: categoryScroll.availableWidth; spacing: 6
                Repeater {
                    model: [{category:"GENERAL",key:"appearance",label:"Appearance",icon:"settings"},{category:"MODPACKS",key:"warnings",label:"Warnings",icon:"warning"},{category:"",key:"browse",label:"Browsing",icon:"compass"},{category:"",key:"installation",label:"Installation",icon:"download"},{category:"SERVICES",key:"connections",label:"Connections",icon:"compass"}]
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true; spacing: 8
                        Text { visible: !root.compact && parent.modelData.category.length > 0; Layout.topMargin: 14; Layout.leftMargin: 14; text: parent.modelData.category; color: Theme.colors.muted; font.pixelSize: 10; font.letterSpacing: 1.3 }
                        NavigationButton { Layout.fillWidth: true; text: root.compact ? "" : parent.modelData.label; hint: parent.modelData.label; iconName: parent.modelData.icon; selected: root.tab === parent.modelData.key; onClicked: root.tabRequested(parent.modelData.key) }
                    }
                }
                }
            }
            StackLayout {
                Layout.minimumWidth: 0; Layout.fillWidth: true; Layout.fillHeight: true
                currentIndex: root.tab === "appearance" ? 0 : 1
                AppearanceSettings { Layout.fillWidth: true; Layout.fillHeight: true }
                BoundedScrollView {
                    id: scroll; clip: true
                    ColumnLayout {
                        width: scroll.availableWidth; spacing: 16
                        BrowseSettings { visible: root.tab === "browse"; Layout.fillWidth: true; preferences: root.browsePreferences; catalogue: root.catalogue }
                        Loader { active: root.installationPreferences !== null && root.tab === "installation"; visible: active; Layout.fillWidth: true; sourceComponent: InstallationSettings { preferences: root.installationPreferences } }
                        WarningSettings { visible: root.tab === "warnings"; Layout.fillWidth: true; preferences: root.warnings }
                        ConnectionSettings { visible: root.tab === "connections"; Layout.fillWidth: true; app: root.app }
                    }
                }
            }
        }
    }
}
