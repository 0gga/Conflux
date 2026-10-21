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
    property bool closeAfterOperations: false
    onClosing: function(close) {
        if (session.editor.dirty) { close.accepted = false; closeRequested = true; unsaved.open() }
        else {
            session.browse.cancelPending()
            if (session.content.operationsBusy) { close.accepted=false; closeAfterOperations=true; window.hide() }
        }
    }
    Connections { target: window.session.content; function onOperationsChanged() {
        if (window.closeAfterOperations && !window.session.content.operationsBusy) window.close()
    } }
    function finishLeaving() {
        unsaved.close()
        if (closeRequested) { closeRequested = false; window.close() }
        else session.navigation.resumePending()
    }
    readonly property string page: session.navigation.route.page || "library"
    readonly property string browseInstanceName: {
        const path = session.navigation.route.packPath || ""
        const instance = app.libraryPacks.find(function(pack) { return pack.path === path })
        return instance ? instance.title : session.browse.pack.path === path ? (session.browse.pack.name || "selected instance") : "selected instance"
    }
    readonly property real interfaceScale: Appearance.values.uiScale / 100
    readonly property bool compactNavigation: Appearance.values.navigationMode === 1 || width / interfaceScale < (Appearance.values.navigationMode === 2 ? 820 : 1000)
    readonly property string pageTitle: page === "discover"
        ? (session.navigation.route.author ? session.navigation.route.author.name + " - Content" : session.navigation.route.packPath ? browseInstanceName + ' - Exploring Content' : "Explore content")
        : page === "showcase" ? (session.showcase.project.title || "Mod showcase")
        : page === "workspace" ? (app.activePack.name || "Modpack") : page === "settings" ? "Settings" : "Library"
    function syncRoutePack() {
        const route = session.navigation.route
        if (route.packPath && route.packPath !== app.activePack.path && !app.navigationBusy) app.openLibraryPack(route.packPath)
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
    minimumWidth: 1080; minimumHeight: 480
    visible: true
    title: "Conflux"
    // Native Windows placement must own maximize, Snap and restore geometry.
    flags: Qt.Window | (Qt.platform.os === "windows"
           ? Qt.CustomizeWindowHint | Qt.WindowTitleHint | Qt.WindowSystemMenuHint
           : Qt.FramelessWindowHint) | Qt.WindowMinimizeButtonHint | Qt.WindowMaximizeButtonHint | Qt.WindowCloseButtonHint
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

    WindowResizeEdges { anchors.fill: parent; targetWindow: window; z: 100 }
    Overlay.overlay.transform: Scale { xScale: window.interfaceScale; yScale: window.interfaceScale }
    Item {
        id: interfaceViewport
        width: window.width / window.interfaceScale; height: window.height / window.interfaceScale
        scale: window.interfaceScale; transformOrigin: Item.TopLeft
    RowLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.preferredWidth: window.compactNavigation ? 72 : 216; Layout.fillHeight: true
            color: Theme.colors.panel
            ColumnLayout {
                anchors.fill: parent; anchors.margins: window.compactNavigation ? 8 : 16; spacing: 6
                Item {
                    Layout.fillWidth: true; Layout.preferredHeight: 36; Layout.bottomMargin: 24
                    WindowDragArea { anchors.fill: parent; targetWindow: window }
                    RowLayout {
                    anchors.left: parent.left; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter; spacing: 12
                    Item {
                        implicitWidth: 32; implicitHeight: 36
                        Rectangle { anchors.centerIn: parent; width: 23; height: 29; radius: 14; color: "transparent"; border.width: 3; border.color: Theme.colors.accent; rotation: -32 }
                        Rectangle { x: 20; y: 5; width: 9; height: 9; radius: 5; color: Theme.colors.panel; border.color: Theme.colors.accent; border.width: 2 }
                    }
                    Text { visible: !window.compactNavigation; text: "conflux"; font.pixelSize: 25; font.weight: Font.DemiBold; color: Theme.colors.text; font.letterSpacing: -0.8 }
                }
                }
                Repeater {
                    model: [{key:"library", label:"Library", icon:"library"}, {key:"discover", label:"Explore content", icon:"compass"}, {key:"workspace", label:"Modpack", icon:"content"}]
                    delegate: NavigationButton {
                        required property var modelData
                        Layout.fillWidth: true
                        text: window.compactNavigation ? "" : modelData.label; hint: modelData.label; iconName: modelData.icon
                        selected: window.page === modelData.key || (window.page === "showcase" && modelData.key === (window.session.navigation.route.modPath ? "workspace" : "discover"))
                        onClicked: window.navigate(modelData.key)
                    }
                }
                NavigationButton { Layout.fillWidth: true; text: window.compactNavigation ? "" : "New modpack"; hint: "New modpack"; iconName: "plus"; enabled: !window.session.editor.dirty && !window.session.editor.busy; onClicked: newPack.open() }
                Item { Layout.fillHeight: true }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.colors.border; Layout.bottomMargin: 8 }
                NavigationButton { Layout.fillWidth: true; text: window.compactNavigation ? "" : "Settings"; hint: "Settings"; iconName: "settings"; selected: window.page === "settings"; onClicked: window.navigate("settings") }
            }
        }
        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.colors.border }
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 56; color: Theme.colors.background
                WindowDragArea { objectName: "windowDragArea"; anchors.fill: parent; targetWindow: window }
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 8
                    ActionButton { objectName: "navigationBackButton"; iconName: "back"; hint: "Back · Alt+Left"; enabled: window.session.navigation.canBack; subtle: true; onClicked: window.session.navigation.back() }
                    ActionButton { iconName: "forward"; hint: "Forward · Alt+Right"; enabled: window.session.navigation.canForward; subtle: true; onClicked: window.session.navigation.forward() }
                    Rectangle { Layout.leftMargin: 8; Layout.rightMargin: 12; implicitWidth: 1; implicitHeight: 18; color: Theme.colors.border }
                    Text {
                        id: titleLabel
                        objectName: "pageTitle"
                        Layout.fillWidth: true; Layout.minimumWidth: 0
                        elide: Text.ElideRight; text: window.pageTitle
                        font.pixelSize: 19; font.weight: Font.DemiBold; color: Theme.colors.text; textFormat: Text.PlainText
                        HoverHandler { id: titleHover }
                        RoundedToolTip { showWhen: titleHover.hovered && titleLabel.truncated; text: window.pageTitle }
                    }
                    WindowControls { Layout.alignment: Qt.AlignVCenter; targetWindow: window }
                }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.colors.border }
            StackLayout {
                Layout.fillWidth: true; Layout.fillHeight: true
                currentIndex: ["library", "discover", "workspace", "settings", "showcase"].indexOf(window.page)
                LibraryPage { app: window.app; onCreateRequested: newPack.open(); onDiscoverRequested: window.navigate("discover"); onPackRequested: function(path, explore) { window.session.navigation.go({page:explore ? "discover" : "workspace",packPath:path}) } }
                DiscoverPage { id: discovery; app: window.app; content: window.session.content; browse: window.session.browse; catalogue: window.session.catalogue; worlds: window.session.worlds; targetPath: window.session.navigation.route.packPath || ""; onInstanceRequested: function(path) { window.session.navigation.go({page:"discover",packPath:path}) } onCreateRequested: newPack.open(); onWorkspaceRequested: window.navigate("workspace"); onShowcaseRequested: function(project, tab) { window.session.navigation.go({page:"showcase",packPath:window.session.navigation.route.packPath || "",author:window.session.navigation.route.author,project:project,showcaseTab:tab || "description"}) } onAuthorRequested: function(author) { window.session.navigation.go({page:"discover",packPath:window.session.navigation.route.packPath || "",author:author}) } }
                PackPage {
                    app: window.app; session: window.session; onCreateRequested: newPack.open(); onDiscoverRequested: window.navigate("discover")
                    onSectionRequested: function(key) { window.packRoute(key) }
                    onConfigRequested: function(path) { window.packRoute("configs",{configPath:path}) }
                    onFolderRequested: function(path) { window.packRoute("files",{folder:path}) }
                    onLogRequested: function(path) { window.packRoute("logs",{logPath:path}) }
                    onWarningSettingsRequested: window.session.navigation.go({page:"settings",settingsTab:"warnings"})
                    onShowcaseRequested: function(path, tab) { window.session.navigation.go({page:"showcase",packPath:window.app.activePack.path,modPath:path,showcaseTab:tab || "description"}) }
                    onAuthorRequested: function(author) { window.session.navigation.go({page:"discover",packPath:window.app.activePack.path,author:author}) }
                }
                SettingsPage { app: window.app; catalogue: window.session.catalogue; warnings: window.session.warnings; browsePreferences: window.session.browsePreferences; tab: window.session.navigation.route.settingsTab || "appearance"; onTabRequested: function(key) { window.session.navigation.go({page:"settings",settingsTab:key}) } }
                ShowcasePage { app: window.app; browse: window.session.browse; showcase: window.session.showcase; releaseFilters: window.session.releaseFilters; contentController: window.session.content; tab: window.session.navigation.route.showcaseTab || "description"; onTabRequested: function(key) { const route = Object.assign({},window.session.navigation.route); route.showcaseTab = key; window.session.navigation.go(route) } }
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
    FocusDialog {
        id: unsaved; objectName: "unsavedConfigDialog"; modal: true; anchors.centerIn: parent; width: 480; padding: 24; title: "Unsaved config changes"
        closePolicy: Popup.NoAutoClose
        background: Rectangle { radius: 18; color: Theme.colors.card; border.color: Theme.colors.border }
        header: Text { text: unsaved.title; color: Theme.colors.text; font.pixelSize: 21; font.weight: Font.DemiBold; padding: 24; bottomPadding: 8 }
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
    FocusDialog {
        id: newPack
        modal: true; anchors.centerIn: parent; width: 470; padding: 28
        title: "Create a modpack"
        closePolicy: window.app.repositoryBusy ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: Theme.colors.card; border.color: Theme.colors.border; radius: 18 }
        header: Text { text: newPack.title; color: Theme.colors.text; font.pixelSize: 23; font.weight: Font.DemiBold; padding: 28; bottomPadding: 6 }
        ColumnLayout {
            width: parent.width; spacing: 16
            Text { Layout.fillWidth: true; text: "A new pack. A fresh Git history."; color: Theme.colors.muted; font.pixelSize: 13 }
            SearchField { id: packName; objectName: "packNameInput"; Layout.fillWidth: true; placeholderText: "Name your modpack"; font.pixelSize: 15; maximumLength: 100 }
            Text { text: "MINECRAFT VERSION"; color: Theme.colors.muted; font.pixelSize: 10; font.letterSpacing: 1.2 }
            SearchField { id: gameVersion; Layout.fillWidth: true; text: "1.21.1"; placeholderText: "e.g. 1.21.1"; maximumLength: 24 }
            Text { text: "MOD LOADER"; color: Theme.colors.muted; font.pixelSize: 10; font.letterSpacing: 1.2 }
            SelectBox { id: packLoader; Layout.fillWidth: true; model: ["Fabric", "NeoForge", "Forge", "Quilt", "Vanilla"] }
            Text { Layout.fillWidth: true; text: "Exact versions and required dependencies are tracked in Git. Downloaded mod files stay local."; color: Theme.colors.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; lineHeight: 1.3 }
            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: 8
                ActionButton { text: "Cancel"; enabled: !window.app.repositoryBusy; subtle: true; onClicked: newPack.close() }
                Item { Layout.fillWidth: true }
                ActionButton { text: window.app.repositoryBusy ? "Creating…" : "Create modpack"; primary: true; enabled: packName.text.trim().length > 0 && !window.app.repositoryBusy; onClicked: window.app.createPack(packName.text, gameVersion.text, ["fabric","neoforge","forge","quilt","vanilla"][packLoader.currentIndex]) }
            }
        }
        onOpened: packName.forceActiveFocus()
    }
    }
}
