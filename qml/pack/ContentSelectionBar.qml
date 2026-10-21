pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
import "../components"
Rectangle {
    id: root
    required property var controller
    signal removeRequested()
    readonly property var items: controller.selection || []
    implicitWidth: Math.min(parent ? parent.width-24 : 760, body.implicitWidth+28)
    implicitHeight: body.implicitHeight+20
    radius: 18; color: Theme.colors.card; border.color: Theme.colors.border
    RowLayout {
        id: body; anchors.fill: parent; anchors.margins: 10; spacing: 6
        ContentIcon { source: root.items.length ? root.items[0].icon || "" : ""; size: 28; visible: root.width > 690 }
        Text { Layout.fillWidth: true; text: root.controller.selected.length + " selected"; color: Theme.colors.text; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight }
        ActionButton { objectName: "selectionClear"; text: root.width > 690 ? "Clear" : ""; hint: "Clear selection"; iconName: "close"; subtle: true; compact: true; onClicked: root.controller.clearSelection() }
        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: Theme.colors.border; Layout.leftMargin: 4; Layout.rightMargin: 4 }
        ActionButton { objectName: "selectionUpdate"; text: root.width > 690 ? "Update" : ""; iconName: "download"; hint: "Update selected content"; accented: true; subtle: true; compact: true; enabled: root.items.some(function(item) {return item.updateAvailable && !item.frozen && !item.locked && !item.pending}); onClicked: root.controller.updateSelected() }
        PopupButton { id: share; objectName: "selectionShare"; popup: shareMenu; text: root.width > 690 ? "Share" : ""; iconName: "copy"; hint: "Copy selected content"; subtle: true; compact: true }
        ActionButton { objectName: "selectionEnable"; text: root.width > 690 ? "Enable" : ""; iconName: "play"; hint: "Enable selected content"; subtle: true; compact: true; enabled: root.items.some(function(item) {return !item.locked && !item.enabled && item.sha1 && !item.directory}); onClicked: root.controller.setSelectedEnabled(true) }
        ActionButton { objectName: "selectionDisable"; text: root.width > 690 ? "Disable" : ""; iconName: "hide"; hint: "Disable selected content"; subtle: true; compact: true; enabled: root.items.some(function(item) {return !item.locked && item.enabled && item.sha1 && !item.directory}); onClicked: root.controller.setSelectedEnabled(false) }
        ActionButton { objectName: "selectionLock"; iconName: root.items.every(function(item) {return item.locked}) ? "unlock" : "lock"; hint: "Lock or unlock selected content"; compact: true; subtle: true; onClicked: {
            const locked = !root.items.every(function(item) {return item.locked})
            for (const item of root.items) root.controller.setLocked(item.relativePath,locked)
        } }
        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: Theme.colors.border; Layout.leftMargin: 4; Layout.rightMargin: 4 }
        ActionButton { objectName: "selectionRemove"; text: root.width > 690 ? "Delete" : ""; iconName: "trash"; hint: "Remove selected content"; accented: true; subtle: true; compact: true; enabled: root.items.some(function(item) {return !item.locked}); onClicked: root.removeRequested() }
    }
    FocusPopup {
        id: shareMenu; parent: share; y: -height-6; width: 215; padding: 6
        background: Rectangle { radius: 14; color: Theme.colors.card; border.color: Theme.colors.border }
        contentItem: ColumnLayout {
            spacing: 2
            Repeater {
                model: [{key:"names",label:"Project names",icon:"content"},{key:"files",label:"File names",icon:"file"},{key:"links",label:"Project links",icon:"external"},{key:"markdown",label:"Markdown links",icon:"copy"}]
                delegate: ActionButton { required property var modelData; Layout.fillWidth: true; text: modelData.label; iconName: modelData.icon; subtle: true; alignLeft: true; onClicked: { root.controller.copySelected(modelData.key); shareMenu.close() } }
            }
        }
    }
}
