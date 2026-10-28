pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "." as Components

FocusDialog {
    id: root
    objectName: "commandPaletteDialog"
    required property var commands
    property var previousFocus: null
    property bool activating: false
    width: Math.min(640, Math.max(280, parent.width - 32))
    height: Math.min(520, Math.max(200, parent.height - 32))
    anchors.centerIn: parent
    padding: 12
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { radius: ControlStyle.panelRadius; color: Theme.colors.card; border.color: Theme.colors.border }
    function syncVisible() {
        if (root.commands.visible && !root.visible) {
            root.previousFocus = root.parent.Window.window.activeFocusItem
            root.activating = false
            root.open()
        } else if (!root.commands.visible) root.close()
    }
    function activate(key) {
        root.activating = true
        root.commands.activate(key)
        if (root.commands.visible) root.activating = false
    }
    function activateCurrent() {
        if (root.commands.selectedKey) root.activate(root.commands.selectedKey)
    }
    function syncSelection() {
        const rows = root.commands.rows
        let position = -1
        for (let i = 0; i < rows.length; ++i) if (rows[i].key === root.commands.selectedKey) { position = i; break }
        results.currentIndex = position
        if (position >= 0) results.positionViewAtIndex(position, ListView.Contain)
    }
    Component.onCompleted: syncVisible()
    Connections { target: root.commands; function onChanged() { root.syncVisible(); root.syncSelection() } function onSelectionChanged() { root.syncSelection() } }
    onOpened: { search.text = root.commands.query; search.forceActiveFocus() }
    onRejected: root.commands.dismiss()
    onClosed: {
        if (root.commands.visible) root.commands.dismiss()
        if (!root.activating && root.previousFocus && root.previousFocus.visible && root.previousFocus.enabled) root.previousFocus.forceActiveFocus()
        root.previousFocus = null
    }
    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Components.SearchField {
                id: search; objectName: "commandPaletteSearch"
                Layout.fillWidth: true; Layout.minimumWidth: 0
                maximumLength: 256; leadingIcon: "search"; placeholderText: "Search Conflux"
                onTextEdited: root.commands.query = text
                onAccepted: root.activateCurrent()
                Keys.onDownPressed: root.commands.moveSelection(1)
                Keys.onUpPressed: root.commands.moveSelection(-1)
            }
            ActionButton { objectName: "closeCommandPalette"; iconName: "close"; hint: "Close"; compact: true; subtle: true; onClicked: root.commands.dismiss() }
        }
        ListView {
            id: results; objectName: "commandPaletteResults"
            Layout.fillWidth: true; Layout.fillHeight: true
            model: root.commands.model; clip: true; spacing: 2; reuseItems: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ContentScrollBar { }
            Keys.onReturnPressed: root.activateCurrent()
            Keys.onEnterPressed: root.activateCurrent()
            Keys.onDownPressed: root.commands.moveSelection(1)
            Keys.onUpPressed: root.commands.moveSelection(-1)
            delegate: MenuOption {
                id: entry
                required property var modelData
                required property int index
                objectName: "commandPaletteEntry"
                width: results.width
                implicitHeight: modelData.subtitle ? 58 : 42
                text: modelData.title
                selected: modelData.key === root.commands.selectedKey
                onHoveredChanged: if (hovered) root.commands.select(modelData.key)
                onClicked: root.activate(modelData.key)
                contentItem: RowLayout {
                    spacing: 12
                    Icon { name: entry.modelData.iconName; size: 19; color: entry.selected ? Theme.colors.accent : Theme.colors.muted }
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 2
                        Text { Layout.fillWidth: true; text: entry.modelData.title; color: Theme.colors.text; font.pixelSize: 13; elide: Text.ElideRight; textFormat: Text.PlainText }
                        Text { Layout.fillWidth: true; visible: !!entry.modelData.subtitle; text: entry.modelData.subtitle; color: Theme.colors.muted; font.pixelSize: 11; elide: Text.ElideMiddle; textFormat: Text.PlainText }
                    }
                }
            }
            Text { anchors.centerIn: parent; visible: results.count === 0; text: "No results"; color: Theme.colors.muted; font.pixelSize: 13 }
        }
    }
}
