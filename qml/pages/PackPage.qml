pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "../components"
import "../pack"
Item {
    id: root
    objectName: "packWorkspace"
    required property var app
    required property var session
    readonly property var workspace: session.workspace
    readonly property var route: session.navigation.route
    readonly property string section: route.section || "content"
    readonly property var visibleIssues: workspace.issues.filter(function(issue) { return root.session.warnings.enabledCategories.indexOf(issue.category) !== -1 })
    property alias showIssues: diagnostics.expanded
    signal createRequested()
    signal discoverRequested()
    signal sectionRequested(string key)
    signal configRequested(string path)
    signal folderRequested(string path)
    signal logRequested(string path)
    signal warningSettingsRequested()
    EmptyState { visible: !root.workspace.pack.name; anchors.centerIn: parent; title: "Choose a modpack"; symbol: "content"; message: "Open a pack from your Library, or create something new." }
    ColumnLayout {
        visible: !!root.workspace.pack.name
        anchors.fill: parent; anchors.margins: 28; spacing: 16
        RowLayout {
            Layout.fillWidth: true; spacing: 14
            ContentIcon { source: root.workspace.pack.icon || ""; kind: "content"; size: 56 }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 7
                Text { Layout.fillWidth: true; text: root.workspace.pack.name || ""; color: Theme.colors.text; font.pixelSize: 27; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
                RowLayout { spacing: 8; SourceBadge { source: root.workspace.pack.provider || "conflux" } Text { text: (root.workspace.pack.minecraft || "Minecraft version unknown") + (root.workspace.pack.loader ? " · " + root.workspace.pack.loader : ""); color: Theme.colors.muted; font.pixelSize: 12 } }
            }
            ActionButton { iconName: "folder"; hint: "Open pack folder"; subtle: true; onClicked: root.app.openFolder(root.workspace.pack.path) }
            ActionButton { text: "Explore mods"; iconName: "compass"; primary: true; onClicked: root.discoverRequested() }
        }
        SectionTabs {
            entries: [{key:"content",label:"Content",icon:"content"},{key:"configs",label:"Configs",icon:"config"},{key:"files",label:"Files",icon:"folder"},{key:"logs",label:"Logs",icon:"logs"},{key:"worlds",label:"Worlds",icon:"world"}]
            current: root.section; onSelected: function(key) { root.sectionRequested(key) }
        }
        DiagnosticsBanner { id: diagnostics; Layout.fillWidth: true; issues: root.visibleIssues; onSettingsRequested: root.warningSettingsRequested() }
        GitPanel { visible: !!root.workspace.pack.managed && root.section === "content"; Layout.fillWidth: true; status: root.app.gitStatus; history: root.app.history; busy: root.app.repositoryBusy; onSaveRequested: function(message) { root.app.saveChanges(message) } }
        RowLayout {
            visible: root.app.installationStatus.length > 0 && root.section === "content"; Layout.fillWidth: true
            Icon { name: "download" }
            Text { Layout.fillWidth: true; text: root.app.installationStatus; color: Theme.colors.accent; font.pixelSize: 12; elide: Text.ElideRight }
            ActionButton { visible: root.app.installBusy; text: "Cancel"; subtle: true; enabled: root.app.installCancellable; onClicked: root.app.cancelInstallation() }
        }
        Loader {
            Layout.fillWidth: true; Layout.fillHeight: true
            sourceComponent: root.section === "configs" ? (root.route.configPath ? editorView : configView) : root.section === "files" ? fileView : root.section === "logs" ? logView : root.section === "worlds" ? worldView : contentView
        }
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: root.section === "content" ? root.workspace.coverage : root.section === "configs" ? "Config changes are explicit, backed up, and checked against external changes." : "Files, logs and worlds are inspected without modification."; color: Theme.colors.muted; font.pixelSize: 10; wrapMode: Text.WordWrap }
            ActionButton { iconName: "refresh"; hint: "Rescan pack content"; subtle: true; enabled: !root.workspace.busy && !root.session.editor.dirty; onClicked: root.workspace.refresh() }
        }
    }
    Component { id: contentView; ContentPanel { files: root.workspace.files; busy: root.workspace.busy; managed: !!root.workspace.pack.managed; onRemoveRequested: function(provider,projectId,title) { removeDialog.provider = provider; removeDialog.projectId = projectId; removeDialog.modTitle = title; removeDialog.open() } onShowFilesRequested: function(path) { root.app.openContentFolder(path) } } }
    Component { id: configView; ConfigPanel { files: root.workspace.files; onEditRequested: function(path) { root.configRequested(path) } onShowFilesRequested: function(path) { root.app.openContentFolder(path) } } }
    Component { id: editorView; ConfigEditorPanel { editor: root.session.editor; onCloseRequested: root.sectionRequested("configs") } }
    Component { id: fileView; FilePanel { browser: root.session.files; onDirectoryRequested: function(path) { root.app.openFolder(root.workspace.pack.path + "/" + path) } onFolderRequested: function(path) { root.folderRequested(path) } onShowFilesRequested: function(path) { root.app.openContentFolder(path) } } }
    Component { id: logView; LogPanel { logs: root.session.logs; onSelected: function(path) { root.logRequested(path) } } }
    Component { id: worldView; WorldPanel { worlds: root.session.worlds; onOpenRequested: function(path) { root.app.openFolder(root.workspace.pack.path + "/" + path) } } }
    Dialog {
        id: removeDialog; property string provider; property string projectId; property string modTitle
        anchors.centerIn: parent; modal: true; title: "Remove mod?"; standardButtons: Dialog.Ok | Dialog.Cancel
        background: Rectangle { radius: 16; color: Theme.colors.card }
        Label { text: "Remove " + removeDialog.modTitle + " from this pack?\nRequired dependencies are kept."; wrapMode: Text.WordWrap }
        onAccepted: root.app.removeMod(provider,projectId)
    }
}
