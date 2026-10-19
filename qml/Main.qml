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
    property string page: "library"
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
        function onPackOpened() { newPack.close(); window.page = "workspace" }
        function onExploreRequested() { window.page = "discover" }
    }
    Shortcut { sequence: "Ctrl+K"; onActivated: { window.page = "discover"; discovery.focusSearch() } }
    Shortcut { sequence: "Ctrl+N"; onActivated: newPack.open() }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.preferredWidth: 218; Layout.fillHeight: true
            color: Theme.colors.panel
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 20; spacing: 8
                RowLayout {
                    Layout.topMargin: 16; Layout.bottomMargin: 36; spacing: 12
                    Item {
                        implicitWidth: 32; implicitHeight: 36
                        Rectangle { anchors.centerIn: parent; width: 23; height: 29; radius: 11; color: "transparent"; border.width: 3; border.color: Theme.colors.accent; rotation: -32 }
                        Rectangle { x: 20; y: 5; width: 9; height: 9; radius: 5; color: Theme.colors.panel; border.color: Theme.colors.accent; border.width: 2 }
                    }
                    Text { text: "conflux"; font.pixelSize: 25; font.weight: Font.DemiBold; color: Theme.colors.text; font.letterSpacing: -0.8 }
                }
                Text { text: "WORKSPACE"; color: Theme.colors.muted; font.pixelSize: 10; font.weight: Font.Bold; font.letterSpacing: 1.8; Layout.leftMargin: 12; Layout.bottomMargin: 10 }
                Repeater {
                    model: [{key:"library", label:"Library", icon:"▦"}, {key:"discover", label:"Discover", icon:"⌕"}, {key:"workspace", label:"Modpack", icon:"⑂"}]
                    delegate: Rectangle {
                        id: navItem
                        required property var modelData
                        Layout.fillWidth: true; height: 46; radius: 8
                        color: window.page === modelData.key ? Theme.colors.selection : (navMouse.containsMouse ? Theme.colors.input : "transparent")
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 13; anchors.rightMargin: 13; spacing: 12
                            Text { text: navItem.modelData.icon; font.pixelSize: 21; color: window.page === navItem.modelData.key ? Theme.colors.accent : Theme.colors.muted; Layout.preferredWidth: 23 }
                            Text { text: navItem.modelData.label; font.pixelSize: 14; color: window.page === navItem.modelData.key ? Theme.colors.text : Theme.colors.muted; font.weight: window.page === navItem.modelData.key ? Font.DemiBold : Font.Normal; Layout.fillWidth: true }
                            Text { text: ""; color: Theme.colors.accent }
                        }
                        MouseArea { id: navMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: window.page = navItem.modelData.key }
                    }
                }
                Item { Layout.fillHeight: true }
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: 112; radius: 10; color: Theme.colors.card; border.color: Theme.colors.border
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 14; spacing: 7
                        Text { text: "Two sources. One home."; color: Theme.colors.text; font.pixelSize: 12; font.weight: Font.DemiBold }
                        Text { text: "Your packs, your history.\nBuilt on Git."; color: Theme.colors.muted; font.pixelSize: 12; lineHeight: 1.4 }
                        Text { text: "FOUNDATION  /  0.1"; color: Theme.colors.accent; font.pixelSize: 9; font.letterSpacing: 1.4 }
                    }
                }
                ActionButton { Layout.fillWidth: true; Layout.topMargin: 12; text: "Settings"; subtle: true; onClicked: window.page = "settings" }
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
                    Text { text: "MODPACK STUDIO"; font.pixelSize: 10; font.letterSpacing: 2.4; font.weight: Font.DemiBold; color: Theme.colors.muted }
                    Item { Layout.fillWidth: true }
                    Text { text: "⌕  Search mods   Ctrl K"; font.pixelSize: 12; color: Theme.colors.muted; MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { window.page = "discover"; discovery.focusSearch() } } }
                    Rectangle { Layout.leftMargin: 18; Layout.rightMargin: 18; implicitWidth: 1; implicitHeight: 23; color: Theme.colors.border }
                    ActionButton { text: "+  New modpack"; primary: true; onClicked: newPack.open() }
                }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.colors.input }
            StackLayout {
                Layout.fillWidth: true; Layout.fillHeight: true
                currentIndex: ["library", "discover", "workspace", "settings"].indexOf(window.page)
                LibraryPage { app: window.app; onCreateRequested: newPack.open(); onDiscoverRequested: window.page = "discover" }
                DiscoverPage { id: discovery; app: window.app; onCreateRequested: newPack.open(); onWorkspaceRequested: window.page = "workspace" }
                PackPage { app: window.app; onCreateRequested: newPack.open(); onDiscoverRequested: window.page = "discover" }
                SettingsPage { app: window.app }
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
        id: newPack
        modal: true; anchors.centerIn: parent; width: 470; padding: 28
        title: "Create a modpack"
        closePolicy: window.app.repositoryBusy ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: Theme.colors.card; border.color: Theme.colors.muted; radius: 14 }
        header: Text { text: newPack.title; color: Theme.colors.text; font.pixelSize: 23; font.weight: Font.DemiBold; padding: 28; bottomPadding: 6 }
        ColumnLayout {
            width: parent.width; spacing: 16
            Text { Layout.fillWidth: true; text: "A new pack. A fresh Git history."; color: Theme.colors.muted; font.pixelSize: 13 }
            TextField { id: packName; objectName: "packNameInput"; Layout.fillWidth: true; placeholderText: "Name your modpack"; font.pixelSize: 15; maximumLength: 100 }
            Text { text: "MINECRAFT VERSION"; color: Theme.colors.muted; font.pixelSize: 10; font.letterSpacing: 1.2 }
            TextField { id: gameVersion; Layout.fillWidth: true; text: "1.21.1"; placeholderText: "e.g. 1.21.1"; maximumLength: 24 }
            Text { text: "MOD LOADER"; color: Theme.colors.muted; font.pixelSize: 10; font.letterSpacing: 1.2 }
            ComboBox { id: packLoader; Layout.fillWidth: true; model: ["fabric", "neoforge", "forge", "quilt", "vanilla"] }
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
