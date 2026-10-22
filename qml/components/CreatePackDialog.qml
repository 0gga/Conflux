pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "." as Components
FocusDialog {
    id: root; objectName: "createPackDialog"
    required property var app
    required property var catalogue
    property string version: "1.21.1"
    property string loader: "fabric"
    readonly property var versions: {
        const available = (catalogue.options.versions || []).filter(function(value) { return /^\d+\.\d+(?:\.\d+)?$/.test(value) })
        return available.indexOf(version) >= 0 ? available : [version].concat(available)
    }
    modal: true; anchors.centerIn: parent; width: Math.min(470,parent ? parent.width-24 : 470); padding: 24
    height: Math.min(implicitHeight,parent ? parent.height-24 : implicitHeight)
    title: "Create a modpack"
    closePolicy: app.repositoryBusy ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: Theme.colors.card; border.color: Theme.colors.border; radius: ControlStyle.panelRadius }
    header: Text { text: root.title; color: Theme.colors.text; font.pixelSize: 23; font.weight: Font.DemiBold; padding: 24; bottomPadding: 12 }
    contentItem: BoundedScrollView {
        id: form; implicitWidth: 422; implicitHeight: body.implicitHeight; rightPadding: 0
        ScrollBar.vertical: ContentScrollBar { parent: form; x: form.width+6; y: 0; height: form.height }
        ColumnLayout {
            id: body; width: form.availableWidth; spacing: 12
            Components.SearchField { id: packName; objectName: "packNameInput"; Layout.fillWidth: true; placeholderText: "Name your modpack"; font.pixelSize: 15; maximumLength: 100; enabled: !root.app.repositoryBusy }
            Text { text: "Minecraft version"; color: Theme.colors.muted; font.pixelSize: 12 }
            SearchableSelect { objectName: "createPackVersion"; Layout.fillWidth: true; value: root.version; choices: root.versions; label: "Minecraft version"; enabled: !root.app.repositoryBusy; onSelectionRequested: function(value) { root.version=value } }
            Text { text: "Mod loader"; color: Theme.colors.muted; font.pixelSize: 12 }
            SearchableSelect { objectName: "createPackLoader"; Layout.fillWidth: true; value: root.loader; choices: ["fabric","neoforge","forge","quilt","vanilla"]; label: "Mod loader"; brandNames: true; searchable: false; enabled: !root.app.repositoryBusy; onSelectionRequested: function(value) { root.loader=value } }
        }
    }
    footer: Item {
        implicitHeight: 68
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 24; anchors.rightMargin: 24; anchors.topMargin: 8; anchors.bottomMargin: 20
            ActionButton { text: "Cancel"; enabled: !root.app.repositoryBusy; subtle: true; onClicked: root.close() }
            Item { Layout.fillWidth: true }
            ActionButton { objectName: "createPackConfirm"; text: root.app.repositoryBusy ? "Creating…" : "Create modpack"; iconName: "plus"; primary: true; enabled: packName.text.trim().length>0 && !!root.version && !root.app.repositoryBusy; onClicked: root.app.createPack(packName.text,root.version,root.loader) }
        }
    }
    onOpened: { catalogue.loadOptions(); packName.forceActiveFocus() }
}
