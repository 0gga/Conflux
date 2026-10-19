pragma ComponentBehavior: Bound
import QtQuick
import Conflux.Style
import QtQuick.Controls
import QtQuick.Layouts
import "components"
import "pages"

ApplicationWindow {
    id: window
    required property var app
    required property var session
    property bool closeRequested: false
    onClosing: function(close) { if (session.editor.dirty) { close.accepted = false; closeRequested = true; unsaved.open() } }
    function finishLeaving() {
        unsaved.close()
        if (closeRequested) { closeRequested = false; window.close() }
        else session.navigation.resumePending()
    }
    readonly property string page: session.navigation.route.page || "library"
    function syncRoutePack() {
        const route = session.navigation.route
        if (route.packPath && route.packPath !== app.activePack.path && !app.repositoryBusy) app.openLibraryPack(route.packPath)
    }
    function navigate(page) {
        const target = {page: page}
        if ((page === "workspace" || page === "discover") && app.activePack.path) target.packPath = app.activePack.path
        return session.navigation.go(target)
    }
    function packRoute(section, extra) {
        const target = {page:"workspace",packPath:app.activePack.path,section:section}
        if (extra) for (const key in extra) target[key] = extra[key]
        return session.navigation.go(target)
    }
    width: 1360; height: 900
    minimumWidth: 1080; minimumHeight: 720
    visible: true
    title: "Conflux · Modpack Studio"
    color: Theme.colors.background
    palette.text: Theme.colors.text
    palette.buttonText: Theme.colors.text
    palette.button: Theme.colors.input
    palette.windowText: Theme.colors.text
    palette.base: Theme.colors.input
    palette.alternateBase: Theme.colors.input
    palette.window: Theme.colors.panel
    palette.highlight: Theme.colors.selection
    palette.highlightedText: Theme.colors.text
    palette.placeholderText: Theme.colors.muted
    palette.mid: Theme.colors.border
    palette.dark: Theme.colors.input

    Connections {
        target: window.app
        function onStateChanged() { window.syncRoutePack() }
        function onPackOpened() { newPack.close(); if (window.session.navigation.route.packPath !== window.app.activePack.path) window.session.navigation.go({page:"workspace",packPath:window.app.activePack.path}) }
        function onExploreRequested() { window.navigate("discover") }
    }
    Connections {
        target: window.session.navigation
        function onChanged() { window.syncRoutePack() }
        function onBlocked() { unsaved.open() }
    }
    Connections { target: window.session.editor; function onSaved() { if (window.closeRequested || window.session.navigation.hasPending) window.finishLeaving() } }
    Shortcut { sequence: "Alt+Left"; onActivated: window.session.navigation.back() }
    Shortcut { sequence: "Alt+Right"; onActivated: window.session.navigation.forward() }
    Shortcut { sequence: "Ctrl+K"; onActivated: { if (window.navigate("discover")) discovery.focusSearch() } }
    Shortcut { sequence: "Ctrl+N"; enabled: !window.session.editor.dirty && !window.session.editor.busy; onActivated: newPack.open() }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.preferredWidth: 200; Layout.fillHeight: true
            color: Theme.colors.panel
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 20; spacing: 8
                RowLayout {
                    Layout.topMargin: 16; Layout.bottomMargin: 36; spacing: 12
                    Item {
                        implicitWidth: 32; implicitHeight: 36
                        Rectangle { anchors.centerIn: parent; width: 23; height: 29; radius: 14; color: "transparent"; border.width: 3; border.color: Theme.colors.accent; rotation: -32 }
                        Rectangle { x: 20; y: 5; width: 9; height: 9; radius: 5; color: Theme.colors.panel; border.color: Theme.colors.accent; border.width: 2 }
                    }
                    Text { text: "conflux"; font.pixelSize: 25; font.weight: Font.DemiBold; color: Theme.colors.text; font.letterSpacing: -0.8 }
                }
                Text { text: "WORKSPACE"; color: Theme.colors.muted; font.pixelSize: 10; font.weight: Font.Bold; font.letterSpacing: 1.8; Layout.leftMargin: 12; Layout.bottomMargin: 10 }
                Repeater {
                    model: [{key:"library", label:"Library", icon:"library"}, {key:"discover", label:"Discover", icon:"compass"}, {key:"workspace", label:"Modpack", icon:"content"}]
                    delegate: Rectangle {
                        id: navItem
                        required property var modelData
                        Layout.fillWidth: true; height: 46; radius: 12
                        color: window.page === modelData.key ? Theme.colors.selection : (navMouse.containsMouse ? Theme.colors.input : "transparent")
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 13; anchors.rightMargin: 13; spacing: 12
                            Icon { name: navItem.modelData.icon; color: window.page === navItem.modelData.key ? Theme.colors.text : Theme.colors.muted; Layout.preferredWidth: 23 }
                            Text { text: navItem.modelData.label; font.pixelSize: 14; color: window.page === navItem.modelData.key ? Theme.colors.text : Theme.colors.muted; font.weight: window.page === navItem.modelData.key ? Font.DemiBold : Font.Normal; Layout.fillWidth: true }
                            Text { text: ""; color: Theme.colors.accent }
                        }
                        MouseArea { id: navMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: window.navigate(navItem.modelData.key) }
                    }
                }
                Item { Layout.fillHeight: true }
                ActionButton { Layout.fillWidth: true; Layout.topMargin: 12; text: "Settings"; iconName: "settings"; subtle: window.page !== "settings"; onClicked: window.navigate("settings") }
                Text { Layout.alignment: Qt.AlignHCenter; Layout.topMargin: 8; text: "Your mods. Connected."; color: Theme.colors.muted; font.pixelSize: 10 }
            }
        }
        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.colors.input }
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 72; color: Theme.colors.background
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 32; anchors.rightMargin: 32
                    ActionButton { iconName: "back"; hint: "Back · Alt+Left"; enabled: window.session.navigation.canBack; subtle: true; onClicked: window.session.navigation.back() }
                    ActionButton { iconName: "forward"; hint: "Forward · Alt+Right"; enabled: window.session.navigation.canForward; subtle: true; onClicked: window.session.navigation.forward() }
                    Text { text: "CONFLUX"; font.pixelSize: 10; font.letterSpacing: 2.4; font.weight: Font.DemiBold; color: Theme.colors.muted }
                    Item { Layout.fillWidth: true }
                    Text { text: "⌕  Search mods   Ctrl K"; font.pixelSize: 12; color: Theme.colors.muted; MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { if (window.navigate("discover")) discovery.focusSearch() } } }
                    Rectangle { Layout.leftMargin: 18; Layout.rightMargin: 18; implicitWidth: 1; implicitHeight: 23; color: Theme.colors.border }
                    ActionButton { text: "New modpack"; iconName: "plus"; primary: true; enabled: !window.session.editor.dirty && !window.session.editor.busy; onClicked: newPack.open() }
                }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.colors.input }
            StackLayout {
                Layout.fillWidth: true; Layout.fillHeight: true
                currentIndex: ["library", "discover", "workspace", "settings"].indexOf(window.page)
                LibraryPage { app: window.app; onCreateRequested: newPack.open(); onDiscoverRequested: window.navigate("discover"); onPackRequested: function(path, explore) { window.session.navigation.go({page:explore ? "discover" : "workspace",packPath:path}) } }
                DiscoverPage { id: discovery; app: window.app; onCreateRequested: newPack.open(); onWorkspaceRequested: window.navigate("workspace") }
                PackPage {
                    app: window.app; session: window.session; onCreateRequested: newPack.open(); onDiscoverRequested: window.navigate("discover")
                    onSectionRequested: function(key) { window.packRoute(key) }
                    onConfigRequested: function(path) { window.packRoute("configs",{configPath:path}) }
                    onFolderRequested: function(path) { window.packRoute("files",{folder:path}) }
                    onLogRequested: function(path) { window.packRoute("logs",{logPath:path}) }
                    onWarningSettingsRequested: window.session.navigation.go({page:"settings",settingsTab:"warnings"})
                }
                SettingsPage { app: window.app; warnings: window.session.warnings; tab: window.session.navigation.route.settingsTab || "appearance"; onTabRequested: function(key) { window.session.navigation.go({page:"settings",settingsTab:key}) } }
            }
        }
    }
    Rectangle {
        visible: window.app.notice.length > 0
        z: 1000; anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 24; width: Math.min(760, parent.width - 64)
        height: noticeLabel.implicitHeight + 36; color: Theme.colors.input; border.color: Theme.colors.muted; radius: 10
        Text { id: noticeLabel; anchors.left: parent.left; anchors.right: dismiss.left; anchors.margins: 18; anchors.verticalCenter: parent.verticalCenter; text: window.app.notice; color: Theme.colors.text; font.pixelSize: 13; wrapMode: Text.WordWrap }
        ActionButton { id: dismiss; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; text: "Dismiss"; subtle: true; onClicked: window.app.clearNotice() }
    }
    Dialog {
        id: unsaved; objectName: "unsavedConfigDialog"; modal: true; anchors.centerIn: parent; width: 480; padding: 24; title: "Unsaved config changes"
        closePolicy: Popup.NoAutoClose
        background: Rectangle { radius: 18; color: Theme.colors.card }
        ColumnLayout { width: parent.width; spacing: 16
            Text { Layout.fillWidth: true; text: "Save your config edits before leaving, or discard them."; color: Theme.colors.text; font.pixelSize: 14; wrapMode: Text.WordWrap }
            Text { visible: window.session.editor.error.length > 0; Layout.fillWidth: true; text: window.session.editor.error; color: Theme.colors.danger; wrapMode: Text.WordWrap }
            RowLayout {
                ActionButton { text: "Cancel"; subtle: true; onClicked: { window.closeRequested = false; window.session.navigation.cancelPending(); unsaved.close() } }
                ActionButton { text: "Discard"; enabled: !window.session.editor.busy; onClicked: { window.session.editor.discard(); window.finishLeaving() } }
                ActionButton { text: window.closeRequested ? "Save and close" : "Save and leave"; iconName: "save"; primary: true; enabled: !window.session.editor.busy; onClicked: window.session.editor.save() }
            }
        }
    }
    Dialog {
        id: newPack
        modal: true; anchors.centerIn: parent; width: 470; padding: 28
        title: "Create a modpack"
        closePolicy: window.app.repositoryBusy ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: Theme.colors.card; border.color: Theme.colors.muted; radius: 14 }
        header: Text { text: newPack.title; color: Theme.colors.text; font.pixelSize: 23; font.weight: Font.DemiBold; padding: 28; bottomPadding: 6 }
        ColumnLayout {
            width: parent.width; spacing: 16
            Text { Layout.fillWidth: true; text: "A new pack. A fresh Git history."; color: Theme.colors.muted; font.pixelSize: 13 }
            SearchField { id: packName; objectName: "packNameInput"; Layout.fillWidth: true; placeholderText: "Name your modpack"; font.pixelSize: 15; maximumLength: 100 }
            Text { text: "MINECRAFT VERSION"; color: Theme.colors.muted; font.pixelSize: 10; font.letterSpacing: 1.2 }
            SearchField { id: gameVersion; Layout.fillWidth: true; text: "1.21.1"; placeholderText: "e.g. 1.21.1"; maximumLength: 24 }
            Text { text: "MOD LOADER"; color: Theme.colors.muted; font.pixelSize: 10; font.letterSpacing: 1.2 }
            SelectBox { id: packLoader; Layout.fillWidth: true; model: ["fabric", "neoforge", "forge", "quilt", "vanilla"] }
            Text { Layout.fillWidth: true; text: "Exact versions and required dependencies are tracked in Git. Downloaded mod files stay local."; color: Theme.colors.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; lineHeight: 1.3 }
            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: 8
                ActionButton { text: "Cancel"; enabled: !window.app.repositoryBusy; subtle: true; onClicked: newPack.close() }
                Item { Layout.fillWidth: true }
                ActionButton { text: window.app.repositoryBusy ? "Creating…" : "Create modpack"; primary: true; enabled: packName.text.trim().length > 0 && !window.app.repositoryBusy; onClicked: window.app.createPack(packName.text, gameVersion.text, packLoader.currentText) }
            }
        }
        onOpened: packName.forceActiveFocus()
    }
}
