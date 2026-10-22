pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
import "../components"
ColumnLayout {
    id: root; objectName: "browsingSettings"; required property var preferences; required property var catalogue
    property string section: "defaults"
    spacing: 14
    RowLayout {
        Layout.fillWidth: true
        Text { Layout.fillWidth: true; text: "Browsing"; color: Theme.colors.text; font.pixelSize: 22; font.weight: Font.DemiBold }
        ActionButton { text: "Reset defaults"; iconName: "restore"; compact: true; subtle: true; visible: root.section==="defaults"; onClicked: root.preferences.resetDefaults() }
    }
    SectionTabs { subtabs:true; objectName: "browseSettingsTabs"; current: root.section; entries: [{key:"defaults",label:"Defaults"},{key:"compatibility",label:"Compatibility"}]; onSelected: function(key) {root.section=key} }
    ColumnLayout {
        visible: root.section==="defaults"; Layout.fillWidth: true; spacing: 12
        Repeater {
            model: [{key:"sort",label:"Sort order",labels:["Relevance","Downloads","Followers","Date published","Date updated"],values:["relevance","downloads","follows","newest","updated"]},
                    {key:"size",label:"Content per page",labels:["10","20","50","100"],values:[10,20,50,100]},
                    {key:"source",label:"Content sources",labels:["All sources","Modrinth","CurseForge"],values:["merged","modrinth","curseforge"]}]
            delegate: Rectangle {
                id: choice; required property var modelData
                Layout.fillWidth: true; implicitHeight: choiceBody.implicitHeight+28; radius: ControlStyle.panelRadius; color: Theme.colors.card
                ColumnLayout {
                    id: choiceBody; anchors.fill: parent; anchors.margins: 14; spacing: 8
                    Text { text: choice.modelData.label; color: Theme.colors.text; font.pixelSize: 13 }
                    SelectBox { objectName: "browseDefault_"+choice.modelData.key; Layout.fillWidth: true; model: choice.modelData.labels; currentIndex: choice.modelData.values.indexOf(root.preferences.defaults[choice.modelData.key]); Accessible.name: choice.modelData.label; onActivated: root.preferences.setDefault(choice.modelData.key,choice.modelData.values[currentIndex]) }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: installedBody.implicitHeight+28; radius: ControlStyle.panelRadius; color: Theme.colors.card
            RowLayout {
                id: installedBody; anchors.fill: parent; anchors.margins: 14
                Text { Layout.fillWidth: true; text: "Hide installed content by default"; color: Theme.colors.text; font.pixelSize: 13; wrapMode: Text.WordWrap }
                ToggleSwitch { objectName: "browseDefault_hideInstalled"; checked: !!root.preferences.defaults.hideInstalled; Accessible.name: "Hide installed content by default"; onToggled: root.preferences.setDefault("hideInstalled",checked) }
            }
        }
        ActionButton { objectName: "applyBrowseDefaults"; text: "Apply to current view"; iconName: "check"; primary: true; onClicked: root.catalogue.configure(root.preferences.defaults) }
    }
    Rectangle {
        visible: root.section==="compatibility"; Layout.fillWidth: true; implicitHeight: bridgeBody.implicitHeight+28; radius: ControlStyle.panelRadius; color: Theme.colors.card
        RowLayout {
            id: bridgeBody; anchors.fill: parent; anchors.margins: 14; spacing: 14
            ColumnLayout {
                Layout.fillWidth: true; spacing: 8
                Text { Layout.fillWidth: true; text: "Include Fabric with Connector"; color: Theme.colors.text; font.pixelSize: 14; font.weight: Font.DemiBold; wrapMode: Text.WordWrap }
                Text { Layout.fillWidth: true; text: "Requires Connector in your modpack."; color: Theme.colors.muted; font.pixelSize: 12; wrapMode: Text.WordWrap }
            }
            ToggleSwitch { objectName: "browseConnectorFabric"; checked: root.preferences.connectorFabric; Accessible.name: "Include Fabric when Connector is present"; onToggled: root.preferences.connectorFabric=checked }
        }
    }
}
