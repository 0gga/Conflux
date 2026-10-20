pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "../components"
Item {
    id: root; required property var worlds; required property var launch; property bool busy: false
    signal openRequested(string path)
    ColumnLayout {
        anchors.fill: parent; spacing: 16
        RowLayout { Layout.fillWidth: true; Text { Layout.fillWidth: true; text: "Your saved worlds"; color: Theme.colors.text; font.pixelSize: 18; font.weight: Font.DemiBold } ActionButton { text: "Refresh"; iconName: "refresh"; subtle: true; enabled: !root.worlds.busy; onClicked: root.worlds.refresh() } }
        Text { Layout.fillWidth: true; visible: !root.launch.available; text: root.launch.unavailableReason; color: Theme.colors.muted; wrapMode: Text.WordWrap; font.pixelSize: 12 }
        Text { Layout.fillWidth: true; visible: root.launch.status.length > 0; text: root.launch.status; color: Theme.colors.accent; wrapMode: Text.WordWrap; font.pixelSize: 12 }
        ListView { boundsBehavior: Flickable.StopAtBounds;
            id: list; Layout.fillWidth: true; Layout.fillHeight: true; model: root.worlds.entries; spacing: 10; clip: true; ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle { id: world; required property var modelData; width: list.width; height: 84; radius: 16; color: Theme.colors.card
                RowLayout { anchors.fill: parent; anchors.margins: 16; spacing: 14
                    ContentIcon { source: world.modelData.icon || ""; kind: "world" }
                    ColumnLayout { Layout.fillWidth: true; spacing: 7; Text { Layout.fillWidth: true; text: world.modelData.title; color: Theme.colors.text; font.pixelSize: 14; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText } Text { text: "Folder modified " + world.modelData.modified; color: Theme.colors.muted; font.pixelSize: 11 } }
                    ActionButton { text: "Open folder"; iconName: "folder"; onClicked: root.openRequested(world.modelData.relativePath) }
                    ActionButton { text: "Play world"; iconName: "play"; primary: true; enabled: root.launch.available && !root.busy; hint: root.launch.unavailableReason; onClicked: root.launch.play(world.modelData.relativePath) }
                }
            }
        }
        Text { visible: root.worlds.entries.length === 0; text: root.worlds.busy ? "Reading worlds…" : "No saved worlds in this pack yet."; color: Theme.colors.muted; font.pixelSize: 13 }
    }
}
