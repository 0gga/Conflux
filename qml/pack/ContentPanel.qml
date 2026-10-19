pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "../components"
Item {
    id: root
    required property var files
    required property bool busy
    property bool managed: false
    property string category: "all"
    property string query: ""
    readonly property var rows: files.filter(function(file) { return file.category !== "configs" && (root.category === "all" || file.category === root.category) && (file.title + " " + file.relativePath).toLowerCase().indexOf(root.query.toLowerCase()) !== -1 })
    signal removeRequested(string provider, string projectId, string title)
    signal showFilesRequested(string path)
    ColumnLayout {
        anchors.fill: parent; spacing: 14
        RowLayout {
            Layout.fillWidth: true
            SearchField { Layout.fillWidth: true; leadingIcon: "search"; placeholderText: "Search content…"; onTextChanged: root.query = text }
            Repeater { model: [{key:"all",label:"All"},{key:"mods",label:"Mods"},{key:"shaderpacks",label:"Shaders"},{key:"resourcepacks",label:"Resources"}]
                delegate: ActionButton { required property var modelData; text: modelData.label; subtle: root.category !== modelData.key; onClicked: root.category = modelData.key }
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; radius: 16; color: Theme.colors.panel; clip: true
            ColumnLayout {
                anchors.fill: parent; spacing: 0
                RowLayout {
                    Layout.fillWidth: true; Layout.margins: 18; spacing: 14
                    Text { Layout.fillWidth: true; text: root.rows.length + " projects"; color: Theme.colors.muted; font.pixelSize: 12 }
                    Text { Layout.preferredWidth: 160; Layout.minimumWidth: 160; Layout.maximumWidth: 160; text: "Version / status"; color: Theme.colors.muted; font.pixelSize: 12 }
                    Text { Layout.preferredWidth: 94; text: "Actions"; horizontalAlignment: Text.AlignRight; color: Theme.colors.muted; font.pixelSize: 12 }
                }
                Item {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    ListView {
                        id: list; anchors.fill: parent; clip: true; model: root.rows; ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            id: entry; required property var modelData
                            width: list.width; height: 80; color: hover.hovered ? Theme.colors.card : "transparent"; radius: 12
                            HoverHandler { id: hover }
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 16; spacing: 14
                                ContentIcon { source: entry.modelData.icon || ""; kind: entry.modelData.category === "mods" ? "mod" : entry.modelData.category === "shaderpacks" ? "shader" : "resource" }
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 7
                                    Text { Layout.fillWidth: true; text: entry.modelData.title; color: Theme.colors.text; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
                                    Text { Layout.fillWidth: true; text: entry.modelData.relativePath; color: Theme.colors.muted; font.pixelSize: 11; elide: Text.ElideMiddle; textFormat: Text.PlainText }
                                }
                                ColumnLayout {
                                    Layout.preferredWidth: 160; Layout.minimumWidth: 160; Layout.maximumWidth: 160; spacing: 7
                                    Text { Layout.fillWidth: true; text: entry.modelData.version || "—"; color: Theme.colors.text; font.pixelSize: 12; elide: Text.ElideRight; textFormat: Text.PlainText }
                                    Text { Layout.fillWidth: true; text: entry.modelData.status; color: entry.modelData.status === "Installed" || entry.modelData.status === "Disabled" ? Theme.colors.muted : Theme.colors.warning; font.pixelSize: 11; elide: Text.ElideRight }
                                }
                                ActionButton { Layout.preferredWidth: 94; iconName: root.managed && !!entry.modelData.projectId ? "trash" : "folder"; text: root.managed && !!entry.modelData.projectId ? "Remove" : "Files"; subtle: true
                                    onClicked: { if (root.managed && entry.modelData.projectId) root.removeRequested(entry.modelData.provider, entry.modelData.projectId, entry.modelData.title); else root.showFilesRequested(entry.modelData.relativePath) }
                                }
                            }
                        }
                    }
                    EmptyState { anchors.centerIn: parent; visible: root.rows.length === 0; title: root.busy ? "Reading your pack…" : "No matching content"; message: "Mods, shaders and resource packs live here."; symbol: "content" }
                }
            }
        }
    }
}
