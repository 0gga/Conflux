pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "../components"
Item {
    id: root; required property var browser
    signal folderRequested(string path)
    signal directoryRequested(string path)
    signal showFilesRequested(string path)
    ColumnLayout {
        anchors.fill: parent; spacing: 16
        RowLayout {
            Layout.fillWidth: true
            ActionButton { iconName: "up"; text: "Up"; enabled: root.browser.folder.length > 0; subtle: true; onClicked: root.folderRequested(root.browser.folder.indexOf("/") >= 0 ? root.browser.folder.substring(0,root.browser.folder.lastIndexOf("/")) : "") }
            Text { Layout.fillWidth: true; text: "/ " + root.browser.folder; color: Theme.colors.text; font.pixelSize: 13; elide: Text.ElideMiddle; textFormat: Text.PlainText }
            ActionButton { text: "Open folder"; iconName: "folder"; subtle: true; onClicked: root.directoryRequested(root.browser.folder) }
        }
        SmoothListView { boundsBehavior: Flickable.StopAtBounds;
            id: list; Layout.fillWidth: true; Layout.fillHeight: true; model: root.browser.entries; spacing: 6; clip: true; ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: row; required property var modelData; width: list.width; height: 60; radius: ControlStyle.panelRadius; color: hover.hovered ? Theme.colors.hover : Theme.colors.card
                HoverHandler { id: hover }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { if (row.modelData.directory) root.folderRequested(row.modelData.relativePath); else root.showFilesRequested(row.modelData.relativePath) } }
                RowLayout { anchors.fill: parent; anchors.margins: 14; spacing: 14
                    Icon { name: row.modelData.iconName; color: row.modelData.directory ? Theme.colors.accent : Theme.colors.muted }
                    Text { Layout.fillWidth: true; text: row.modelData.title; color: Theme.colors.text; font.pixelSize: 13; elide: Text.ElideRight; textFormat: Text.PlainText }
                    Text { text: row.modelData.modified; color: Theme.colors.muted; font.pixelSize: 11 }
                    Icon { visible: row.modelData.directory; name: "forward"; size: 16 }
                }
            }
        }
        Text { visible: root.browser.entries.length === 0; text: root.browser.busy ? "Reading files…" : "This folder is empty or unavailable."; color: Theme.colors.muted; font.pixelSize: 13 }
    }
}
