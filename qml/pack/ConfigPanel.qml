pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "../components"
Item {
    id: root
    required property var files
    property string query: ""
    readonly property var rows: files.filter(function(file) { return file.category === "configs" && (file.fileName + " " + (file.relatedModTitle || "") + " " + file.relativePath).toLowerCase().indexOf(root.query.toLowerCase()) !== -1 })
    signal editRequested(string path)
    signal showFilesRequested(string path)
    ColumnLayout {
        anchors.fill: parent; spacing: 16
        SearchField { Layout.fillWidth: true; leadingIcon: "search"; placeholderText: "Search configs or related mods…"; onTextChanged: root.query = text }
        Text { Layout.fillWidth: true; text: "Identified configs use their related mod's icon. Ownership matches are shown for review before editing."; color: Theme.colors.muted; font.pixelSize: 12; wrapMode: Text.WordWrap }
        ListView {
            id: list; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: root.rows; spacing: 8; ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: entry; required property var modelData; width: list.width; height: 88; radius: 14; color: hover.hovered ? Theme.colors.hover : Theme.colors.card
                HoverHandler { id: hover }
                RowLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 14
                    ContentIcon { source: entry.modelData.icon || ""; kind: entry.modelData.relatedModId ? "mod" : "config" }
                    ColumnLayout { Layout.fillWidth: true; spacing: 6
                        Text { Layout.fillWidth: true; text: entry.modelData.fileName; color: Theme.colors.text; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
                        Text { Layout.fillWidth: true; text: entry.modelData.relatedModTitle ? entry.modelData.relatedModTitle + " · " + entry.modelData.association : "Owner unidentified · " + entry.modelData.relativePath; color: Theme.colors.muted; font.pixelSize: 11; elide: Text.ElideRight; textFormat: Text.PlainText }
                    }
                    ActionButton { iconName: "folder"; subtle: true; hint: entry.modelData.relativePath; onClicked: root.showFilesRequested(entry.modelData.relativePath) }
                    ActionButton { text: "Edit config"; iconName: "config"; enabled: !!entry.modelData.editable; hint: entry.modelData.editable ? "Open visual editor" : "Visual editing requires an identified owner and JSON, TOML or properties format"; onClicked: root.editRequested(entry.modelData.relativePath) }
                }
            }
        }
        Text { visible: root.rows.length === 0; text: "No matching config files."; color: Theme.colors.muted; font.pixelSize: 13 }
    }
}
