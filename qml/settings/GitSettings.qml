pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
import "../components"
Rectangle {
    id: root; required property var app
    Layout.fillWidth: true; implicitHeight: body.implicitHeight+48
    color: Theme.colors.card; border.color: Theme.colors.border; radius: ControlStyle.panelRadius
    ColumnLayout {
        id: body; anchors.fill: parent; anchors.margins: 24; spacing: 12
        RowLayout {
            Layout.fillWidth: true; spacing: 10
            Icon { name: "git"; size: 22 }
            Text { Layout.fillWidth: true; text: "Git history"; color: Theme.colors.text; font.pixelSize: 19; font.weight: Font.DemiBold }
            ToggleSwitch { id: gitToggle; objectName: "gitEnabledDraft"; checked: root.app.gitEnabled === true; Accessible.name: "Enable Git history" }
        }
        ColumnLayout {
            visible: gitToggle.checked; Layout.fillWidth: true; spacing: 10
            SearchField { id: author; objectName: "gitAuthorName"; Layout.fillWidth: true; placeholderText: "Your name"; Accessible.name: "Git author name"; text: root.app.gitAuthorName || ""; maximumLength: 200 }
            SearchField { id: email; objectName: "gitAuthorEmail"; Layout.fillWidth: true; placeholderText: "Email address"; Accessible.name: "Git author email"; text: root.app.gitAuthorEmail || ""; maximumLength: 254 }
        }
        RowLayout {
            Layout.fillWidth: true
            ActionButton { objectName: "saveGitSettings"; text: "Save"; iconName: "save"; enabled: !gitToggle.checked || (author.text.trim().length>0 && email.text.indexOf("@")>0); onClicked: root.app.configureGit(gitToggle.checked,author.text,email.text) }
            ActionButton { objectName: "setupCurrentPackGit"; visible: root.app.gitEnabled === true && !!(root.app.activePack || {}).managed; text: (root.app.activePack || {}).gitEnabled ? "Update modpack identity" : "Set up this modpack"; iconName: "content"; enabled: !root.app.repositoryBusy; onClicked: root.app.enablePackGit() }
        }
    }
}
