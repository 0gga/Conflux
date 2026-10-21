pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "../components"
Item {
    id: root
    required property var library
    property string category: "editable"
    readonly property var files: library.files
    readonly property int editableCount: files.filter(function(file) { return !!file.editable }).length
    property string query: ""
    readonly property var rows: files.filter(function(file) { return (root.category === "all" || (root.category === "editable" ? file.editable : !file.editable)) && (file.fileName + " " + (file.relatedModTitle || "") + " " + file.relativePath).toLowerCase().indexOf(root.query.toLowerCase()) !== -1 })
    signal editRequested(string path)
    signal showFilesRequested(string path)
    ColumnLayout {
        anchors.fill: parent; spacing: 16
        SearchField { Layout.fillWidth: true; leadingIcon: "search"; placeholderText: "Search configs or related mods…"; onTextChanged: root.query = text }
        RowLayout {
            ActionButton { text: "Editable (" + root.editableCount + ")"; selected: root.category === "editable"; compact: true; onClicked: root.category = "editable" }
            ActionButton { text: "Needs review"; selected: root.category === "review"; compact: true; onClicked: root.category = "review" }
            ActionButton { text: "All configs"; selected: root.category === "all"; compact: true; onClicked: root.category = "all" }
        }
        Text { Layout.fillWidth: true; text: "Edit settings using toggles, fields and lists. Needs review lets you identify a file's related mod and shows unsupported formats."; color: Theme.colors.muted; font.pixelSize: 12; wrapMode: Text.WordWrap }
        SmoothListView { boundsBehavior: Flickable.StopAtBounds;
            id: list; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: root.rows; spacing: 8; ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: entry; required property var modelData; width: list.width; height: 88; radius: 14; color: hover.hovered ? Theme.colors.hover : Theme.colors.card
                HoverHandler { id: hover }
                RowLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 14
                    ContentIcon { source: entry.modelData.icon || ""; kind: entry.modelData.relatedModId ? "mod" : "config" }
                    ColumnLayout { Layout.fillWidth: true; spacing: 6
                        Text { Layout.fillWidth: true; text: entry.modelData.relatedModTitle || entry.modelData.fileName; color: Theme.colors.text; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
                        Text { objectName: "configFilePath"; Layout.fillWidth: true; text: entry.modelData.relatedModTitle ? entry.modelData.relativePath + " · " + entry.modelData.association : "Owner unidentified · " + entry.modelData.relativePath; color: Theme.colors.muted; font.pixelSize: 11; elide: Text.ElideRight; textFormat: Text.PlainText }
                    }
                    ActionButton { iconName: "folder"; subtle: true; hint: entry.modelData.relativePath; onClicked: root.showFilesRequested(entry.modelData.relativePath) }
                    ActionButton { visible: !entry.modelData.relatedModId && !!entry.modelData.supported; text: "Identify mod"; iconName: "mod"; onClicked: { identify.path = entry.modelData.relativePath; identify.open() } }
                    Text { visible: !entry.modelData.supported; text: "Format not supported"; color: Theme.colors.muted; font.pixelSize: 11 }
                    ActionButton { visible: !!entry.modelData.editable; text: "Edit config"; iconName: "config"; enabled: !!entry.modelData.editable; hint: entry.modelData.editable ? "Open visual editor" : "Visual editing requires an identified owner and JSON, TOML or properties format"; onClicked: root.editRequested(entry.modelData.relativePath) }
                }
            }
        }
        Text { visible: root.rows.length === 0; text: root.category === "editable" ? "No matching editable configs. Use Needs review to identify supported files." : "No matching config files."; color: Theme.colors.muted; font.pixelSize: 13 }
    }
    FocusDialog {
        id: identify
        property string path: ""
        modal: true; anchors.centerIn: parent; width: 480; padding: 24
        background: Rectangle { color: Theme.colors.card; border.color: Theme.colors.border; radius: 16 }
        header: Text { text: "Choose the related mod"; padding: 24; color: Theme.colors.text; font.pixelSize: 20 }
        ColumnLayout {
            width: parent.width; spacing: 16
            Text { Layout.fillWidth: true; text: identify.path + "\nThis is your explicit association. Review the settings before saving."; textFormat: Text.PlainText; color: Theme.colors.muted; wrapMode: Text.WordWrap }
            SelectBox { id: owners; Layout.fillWidth: true; model: root.library.mods.map(function(mod) { return mod.title + " (" + mod.modId + ")" }) }
            RowLayout { Layout.alignment: Qt.AlignRight
                ActionButton { text: "Cancel"; subtle: true; onClicked: identify.close() }
                ActionButton { text: "Open visual editor"; primary: true; enabled: owners.currentIndex >= 0; onClicked: { const mod = root.library.mods[owners.currentIndex]; if (mod && root.library.identify(identify.path,mod.modId)) { root.editRequested(identify.path); identify.close() } } }
            }
        }
    }
}
