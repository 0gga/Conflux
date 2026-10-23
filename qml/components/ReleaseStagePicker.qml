pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
PopupButton {
    id: root
    objectName: "packReleaseStages"
    property var policy: null
    property string packPath: ""
    readonly property var stages: { const revision=policy ? policy.revision : 0; return policy ? policy.channels(packPath) : [] }
    readonly property string summary: stages.length === 3 ? "All" : stages.map(function(stage) { return stage.charAt(0).toUpperCase()+stage.slice(1) }).join(" + ")
    popup: menu; compact: true; iconName: "versions"
    text: "Stages: " + summary
    hint: "Allowed stages for this modpack’s installs and updates"
    visible: !!policy && packPath.length > 0
    enabled: visible
    onPackPathChanged: menu.close()
    FocusPopup {
        id: menu; objectName: "releaseStageMenu"; parent: root; y: root.height+5; width: Math.max(220,root.width); padding: 6; margins: 8
        background: Rectangle { radius: ControlStyle.panelRadius; color: Theme.colors.card; border.color: Theme.colors.border }
        contentItem: ColumnLayout {
            spacing: 3
            Repeater {
                model: root.policy ? root.policy.choices : []
                delegate: MenuOption {
                    id: option; required property string modelData
                    objectName: "packReleaseStage-" + modelData
                    Layout.fillWidth: true; selected: root.stages.indexOf(modelData) !== -1
                    text: modelData.charAt(0).toUpperCase()+modelData.slice(1)
                    contentItem: RowLayout {
                        Text { Layout.fillWidth: true; text: option.text; color: option.foreground; font: option.font; textFormat: Text.PlainText }
                        Icon { name: "check"; visible: option.selected; size: 16; color: Theme.colors.accent }
                    }
                    onClicked: {
                        let next=root.stages.slice(); const index=next.indexOf(modelData)
                        if(index === -1) next.push(modelData)
                        else if(next.length > 1) next.splice(index,1)
                        root.policy.setChannels(root.packPath,next)
                    }
                }
            }
        }
    }
}
