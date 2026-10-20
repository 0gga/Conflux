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
    signal createRequested()
    signal discoverRequested()
    signal sectionRequested(string key)
    signal configRequested(string path)
    signal folderRequested(string path)
    signal logRequested(string path)
    signal warningSettingsRequested()
    signal showcaseRequested(string path)
    EmptyState { visible: !root.workspace.pack.name; anchors.centerIn: parent; title: "Choose a modpack"; symbol: "content"; message: "Open a pack from your Library, or create something new." }
    ColumnLayout {
        visible: !!root.workspace.pack.name
        anchors.fill: parent; anchors.margins: 28; spacing: 16
        RowLayout {
            Layout.fillWidth: true; spacing: 14
            ContentIcon { source: root.workspace.pack.icon || ""; kind: "content"; size: 64 }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 7
                RowLayout {
                    Layout.fillWidth: true; spacing: 10
                    Text { Layout.maximumWidth: Math.max(80,parent.width-warningButton.width-14); text: root.workspace.pack.name || ""; color: Theme.colors.text; font.pixelSize: 27; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
                    ActionButton { id: warningButton; visible: root.visibleIssues.length > 0; text: String(root.visibleIssues.length); iconName: "warning"; compact: true; hint: root.visibleIssues.length + " items to review"; onClicked: warningsPopup.open() }
                    Item { Layout.fillWidth: true }
                }
                RowLayout { spacing: 8; SourceBadge { source: root.workspace.pack.provider || "conflux" } Text { text: (root.workspace.pack.minecraft || "Minecraft version unknown") + (root.workspace.pack.loader ? " · " + root.workspace.pack.loader : ""); color: Theme.colors.muted; font.pixelSize: 12 } }
            }
            ActionButton { text: "Play"; iconName: "play"; primary: true; enabled: root.session.launch.packAvailable && !root.app.repositoryBusy; hint: root.session.launch.packAvailable ? "Launch in Modrinth App" : "This pack has no supported launcher handoff yet"; onClicked: root.session.launch.playPack() }
            ActionButton { iconName: "settings"; hint: "Open modpack configs"; subtle: true; onClicked: root.sectionRequested("configs") }
            ActionButton { id: packMenuButton; iconName: "more"; hint: "Modpack actions"; subtle: true; onClicked: packMenu.open() }
        }
        SectionTabs {
            entries: [{key:"content",label:"Content",icon:"content"},{key:"configs",label:"Configs",icon:"config"},{key:"files",label:"Files",icon:"folder"},{key:"logs",label:"Logs",icon:"logs"},{key:"worlds",label:"Worlds",icon:"world"}]
            current: root.section; onSelected: function(key) { root.sectionRequested(key) }
        }
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
            ActionButton { visible: root.section !== "content"; iconName: "refresh"; hint: "Rescan pack content"; subtle: true; enabled: !root.workspace.busy && !root.session.editor.dirty; onClicked: root.workspace.refresh() }
        }
    }
    Component { id: contentView; ContentPanel { controller: root.session.content; onBrowseRequested: root.discoverRequested(); onShowcaseRequested: function(path) { root.showcaseRequested(path) } onShowFilesRequested: function(path) { root.app.openContentFolder(path) } } }
    Component { id: configView; ConfigPanel { library: root.session.configs; onEditRequested: function(path) { root.configRequested(path) } onShowFilesRequested: function(path) { root.app.openContentFolder(path) } } }
    Component { id: editorView; ConfigEditorPanel { editor: root.session.editor; onCloseRequested: root.sectionRequested("configs") } }
    Component { id: fileView; FilePanel { browser: root.session.files; onDirectoryRequested: function(path) { root.app.openFolder(root.workspace.pack.path + "/" + path) } onFolderRequested: function(path) { root.folderRequested(path) } onShowFilesRequested: function(path) { root.app.openContentFolder(path) } } }
    Component { id: logView; LogPanel { logs: root.session.logs; onSelected: function(path) { root.logRequested(path) } } }
    Component { id: worldView; WorldPanel { worlds: root.session.worlds; launch: root.session.launch; busy: root.app.repositoryBusy; onOpenRequested: function(path) { root.app.openFolder(root.workspace.pack.path + "/" + path) } } }
    Popup {
        id: packMenu; parent: packMenuButton; x: packMenuButton.width-width; y: packMenuButton.height+6; width: 220; padding: 6
        background: Rectangle { color: Theme.colors.card; border.color: Theme.colors.border; radius: 14 }
        contentItem: ColumnLayout {
            spacing: 2
            ActionButton { Layout.fillWidth: true; text: "Open pack folder"; iconName: "folder"; subtle: true; alignLeft: true; onClicked: { root.app.openFolder(root.workspace.pack.path); packMenu.close() } }
            ActionButton { Layout.fillWidth: true; text: "Refresh content"; iconName: "refresh"; subtle: true; alignLeft: true; enabled: !root.session.content.busy; onClicked: { root.workspace.refresh(); root.session.content.refresh(); packMenu.close() } }
        }
    }
    Popup {
        id: warningsPopup; objectName: "packWarnings"; parent: root; width: Math.min(620,root.width-56); padding: 0
        x: Math.max(28,Math.min(warningButton.mapToItem(root,0,0).x,root.width-width-28))
        onAboutToShow: { y = warningButton.mapToItem(root,0,warningButton.height).y+8 }
        background: Rectangle { color: Theme.colors.card; border.color: Theme.colors.border; radius: 16 }
        contentItem: DiagnosticsBanner { id: diagnostics; width: warningsPopup.width; issues: root.visibleIssues; expanded: true; onSettingsRequested: { warningsPopup.close(); root.warningSettingsRequested() } }
    }
}
