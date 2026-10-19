pragma ComponentBehavior: Bound
import QtQuick
import Conflux.Style
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
Item {
    id: root
    objectName: "packWorkspace"
    required property var app
    property string category: "all"
    property string query: ""
    property bool showIssues: false
    property bool showHistory: false
    readonly property bool managed: !!app.activePack.managed
    readonly property var files: app.contentFiles.filter(function(file) {
        return (root.category === "all" || file.category === root.category)
            && (file.title + " " + file.relativePath).toLowerCase().indexOf(root.query.toLowerCase()) !== -1
    })
    signal createRequested()
    signal discoverRequested()
    Connections { target: root.app; function onPackOpened() { root.category = "all"; root.query = ""; search.clear(); root.showIssues = false; root.showHistory = false } }
    EmptyState { visible: !root.app.activePack.name; anchors.centerIn: parent; title: "Choose a modpack"; symbol: "▦"; message: "Open Configure on a pack in your Library, or create a new one." }
    ActionButton { visible: !root.app.activePack.name; anchors.horizontalCenter: parent.horizontalCenter; anchors.top: parent.verticalCenter; anchors.topMargin: 110; text: "+ New modpack"; primary: true; onClicked: root.createRequested() }
    ColumnLayout {
        visible: !!root.app.activePack.name
        anchors.fill: parent; anchors.margins: 32; spacing: 18
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true; spacing: 9
                Text { Layout.fillWidth: true; text: root.app.activePack.name || ""; elide: Text.ElideRight; font.pixelSize: 29; color: Theme.colors.text; font.weight: Font.DemiBold; textFormat: Text.PlainText }
                RowLayout {
                    SourceBadge { source: root.app.activePack.provider || "conflux" }
                    Text { text: (root.app.activePack.minecraft || "Minecraft version unknown") + (root.app.activePack.loader ? "  ·  " + root.app.activePack.loader : ""); color: Theme.colors.muted; font.pixelSize: 12 }
                }
            }
            ActionButton { text: "Open folder"; subtle: true; onClicked: root.app.openFolder(root.app.activePack.path) }
            ActionButton { text: "Explore mods"; primary: true; onClicked: root.discoverRequested() }
        }
        Text { visible: !root.managed; Layout.fillWidth: true; text: "Connected launcher pack · read-only inspection. Manage its files in the original launcher."; color: Theme.colors.muted; font.pixelSize: 12 }
        Rectangle {
            visible: root.app.diagnostics.length > 0
            Layout.fillWidth: true; implicitHeight: issueContent.implicitHeight + 30; radius: 10
            color: Theme.colors.warningSurface; border.color: Theme.colors.warning
            ColumnLayout {
                id: issueContent; anchors.fill: parent; anchors.margins: 15; spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: root.app.diagnostics.length + (root.app.diagnostics.length === 1 ? " item needs review" : " items need review"); font.pixelSize: 14; font.weight: Font.DemiBold; color: Theme.colors.warning }
                    ActionButton { text: root.showIssues ? "Collapse" : "Review"; subtle: true; onClicked: root.showIssues = !root.showIssues }
                }
                ScrollView {
                    id: issueScroll; visible: root.showIssues; Layout.fillWidth: true; Layout.preferredHeight: Math.min(150, issues.implicitHeight); clip: true
                    Text { id: issues; width: issueScroll.availableWidth; text: root.app.diagnostics.join("\n\n"); wrapMode: Text.Wrap; textFormat: Text.PlainText; font.pixelSize: 12; color: Theme.colors.warning }
                }
            }
        }
        Rectangle {
            visible: root.managed; Layout.fillWidth: true; implicitHeight: gitContent.implicitHeight + 24; radius: 10; color: Theme.colors.card; border.color: Theme.colors.border
            ColumnLayout {
                id: gitContent; anchors.fill: parent; anchors.margins: 12; spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: "Git · main  /  " + (root.app.gitStatus.length ? "Unsaved changes" : "All changes saved"); color: Theme.colors.text; font.pixelSize: 13 }
                    ActionButton { text: root.showHistory ? "Close history" : "History"; subtle: true; onClicked: root.showHistory = !root.showHistory }
                    TextField { id: message; objectName: "commitMessageInput"; Layout.preferredWidth: 200; placeholderText: "Describe your changes…" }
                    ActionButton { text: root.app.repositoryBusy ? "Working…" : "Save to Git"; enabled: !root.app.repositoryBusy && message.text.trim().length > 0 && root.app.gitStatus.length > 0; onClicked: root.app.saveChanges(message.text) }
                }
                ScrollView {
                    id: historyScroll; visible: root.showHistory; Layout.fillWidth: true; Layout.preferredHeight: 110; clip: true
                    Text { width: historyScroll.availableWidth; text: root.app.history || "No commits yet. Save your first change."; color: Theme.colors.muted; font.pixelSize: 12; textFormat: Text.PlainText; wrapMode: Text.Wrap }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            TextField { id: search; Layout.fillWidth: true; placeholderText: "Search content and files…"; selectByMouse: true; onTextChanged: root.query = text }
            ActionButton { text: root.app.contentBusy ? "Scanning…" : "Refresh"; enabled: !root.app.contentBusy && !root.app.repositoryBusy; subtle: true; onClicked: root.app.refreshContent() }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 6
            Repeater {
                model: [{key:"all",label:"All"}, {key:"mods",label:"Mods"}, {key:"shaderpacks",label:"Shaders"}, {key:"resourcepacks",label:"Resource packs"}, {key:"configs",label:"Configs"}]
                delegate: ActionButton {
                    required property var modelData
                    text: modelData.label + "  " + (modelData.key === "all" ? root.app.contentFiles.length : root.app.contentFiles.filter(function(file) { return file.category === modelData.key }).length)
                    subtle: root.category !== modelData.key; onClicked: root.category = modelData.key
                }
            }
            Item { Layout.fillWidth: true }
        }
        RowLayout {
            visible: root.app.installationStatus.length > 0; Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: root.app.installationStatus; color: Theme.colors.accent; font.pixelSize: 12; elide: Text.ElideRight }
            ActionButton { visible: root.app.installBusy; text: "Cancel"; subtle: true; enabled: root.app.installCancellable; onClicked: root.app.cancelInstallation() }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; radius: 10; color: Theme.colors.panel; border.color: Theme.colors.border
            ColumnLayout {
                anchors.fill: parent; spacing: 0
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: 42; color: Theme.colors.card; radius: 10
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 18; anchors.rightMargin: 18; spacing: 14
                        Text { Layout.fillWidth: true; text: "Content"; color: Theme.colors.muted; font.pixelSize: 12 }
                        Text { Layout.preferredWidth: 170; Layout.minimumWidth: 170; Layout.maximumWidth: 170; text: "Version / status"; color: Theme.colors.muted; font.pixelSize: 12 }
                        Text { Layout.preferredWidth: 94; Layout.minimumWidth: 94; Layout.maximumWidth: 94; text: "Actions"; horizontalAlignment: Text.AlignRight; color: Theme.colors.muted; font.pixelSize: 12 }
                    }
                }
                Item {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    ListView {
                        id: content; anchors.fill: parent; clip: true; model: root.files
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            id: entry
                            required property var modelData
                            width: content.width; height: 76; color: entryHover.hovered ? Theme.colors.card : "transparent"
                            HoverHandler { id: entryHover }
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.colors.border; opacity: 0.45 }
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 18; spacing: 14
                                Rectangle {
                                    implicitWidth: 34; implicitHeight: 34; radius: 7; color: Theme.colors.input
                                    Text { anchors.centerIn: parent; text: entry.modelData.category === "configs" ? "≡" : entry.modelData.category === "mods" ? "◇" : "▧"; font.pixelSize: 22; color: Theme.colors.muted }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 6
                                    Text { Layout.fillWidth: true; text: entry.modelData.title; color: Theme.colors.text; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
                                    Text { Layout.fillWidth: true; text: entry.modelData.relativePath; color: Theme.colors.muted; font.pixelSize: 11; elide: Text.ElideMiddle; textFormat: Text.PlainText }
                                }
                                ColumnLayout {
                                    Layout.preferredWidth: 170; Layout.minimumWidth: 170; Layout.maximumWidth: 170; spacing: 6
                                    Text { Layout.fillWidth: true; text: entry.modelData.version || "—"; color: Theme.colors.text; font.pixelSize: 12; elide: Text.ElideRight; textFormat: Text.PlainText }
                                    Text { Layout.fillWidth: true; text: entry.modelData.status; color: entry.modelData.status === "Installed" || entry.modelData.status === "Local config" ? Theme.colors.muted : Theme.colors.warning; font.pixelSize: 11; elide: Text.ElideRight }
                                }
                                ActionButton {
                                    Layout.preferredWidth: 94; Layout.minimumWidth: 94; Layout.maximumWidth: 94; text: root.managed && !!entry.modelData.projectId ? "Remove" : "Show files"; subtle: true; enabled: !root.app.repositoryBusy
                                    onClicked: {
                                        if (root.managed && entry.modelData.projectId) { removeDialog.provider = entry.modelData.provider; removeDialog.projectId = entry.modelData.projectId; removeDialog.modTitle = entry.modelData.title; removeDialog.open() }
                                        else root.app.openContentFolder(entry.modelData.relativePath)
                                    }
                                }
                            }
                        }
                    }
                    EmptyState { visible: root.files.length === 0; anchors.centerIn: parent; title: root.app.contentBusy ? "Reading your pack…" : "No matching content"; message: root.app.contentBusy ? "Inspecting files and mod metadata." : "Explore mods to add content, or choose another tab."; symbol: "◇" }
                }
            }
        }
        Text { Layout.fillWidth: true; text: root.app.scanCoverage; color: Theme.colors.muted; font.pixelSize: 10; wrapMode: Text.WordWrap }
    }
    Dialog {
        id: removeDialog; property string provider; property string projectId; property string modTitle
        anchors.centerIn: parent; modal: true; title: "Remove mod?"; standardButtons: Dialog.Ok | Dialog.Cancel
        Label { text: "Remove " + removeDialog.modTitle + " from this pack?\nRequired dependencies are kept."; wrapMode: Text.WordWrap }
        onAccepted: root.app.removeMod(provider, projectId)
    }
}
