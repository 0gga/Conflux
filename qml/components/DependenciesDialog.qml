pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style

FocusDialog {
    id: root
    objectName: "optionalDependenciesDialog"
    required property var suggestions
    property bool removalMode: false
    property bool presentationEnabled: true
    readonly property bool compactLayout: height < 360
    onPresentationEnabledChanged: syncVisible()
    width: Math.min(680, Math.max(280, parent.width - 32))
    height: Math.min(560, Math.max(200, parent.height - 32))
    anchors.centerIn: parent
    padding: compactLayout ? 12 : 20
    closePolicy: Popup.CloseOnEscape
    background: Rectangle { color: Theme.colors.card; border.color: Theme.colors.border; radius: ControlStyle.panelRadius }
    function syncVisible() {
        if (root.suggestions.visible && root.presentationEnabled) root.open()
        else root.close()
    }
    Component.onCompleted: syncVisible()
    Connections { target: root.suggestions; function onChanged() { root.syncVisible() } }
    onRejected: root.suggestions.dismiss()
    header: Item {
        implicitHeight: heading.implicitHeight + (root.compactLayout ? 20 : 28)
        RowLayout {
            id: heading
            anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: 20; rightMargin: 20; topMargin: 16 }
            spacing: 12
            Icon { name: "content"; size: 23; color: Theme.colors.accent }
            ColumnLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 3
                Text { Layout.fillWidth: true; text: root.removalMode ? "Remove dependencies?" : "Optional content"; color: Theme.colors.text; font.pixelSize: 20; font.weight: Font.DemiBold; elide: Text.ElideRight }
                Text { Layout.fillWidth: true; text: root.suggestions.pack.name || root.suggestions.pack.title || ""; color: Theme.colors.muted; font.pixelSize: 12; elide: Text.ElideRight; textFormat: Text.PlainText }
            }
            ActionButton { objectName: "closeOptionalDependencies"; iconName: "close"; hint: "Close"; subtle: true; compact: true; onClicked: root.suggestions.dismiss() }
        }
    }
    contentItem: ColumnLayout {
        spacing: root.compactLayout ? 10 : 14
        ContentListView {
            id: list
            objectName: "optionalDependencyList"
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0
            outsideColor: Theme.colors.card; cornerRadius: ControlStyle.controlRadius
            spacing: 8; clip: true; model: root.suggestions.model
            delegate: Rectangle {
                id: entry
                required property var modelData
                width: list.viewportWidth
                height: Math.max(66, body.implicitHeight + 24)
                color: Theme.colors.input; radius: ControlStyle.controlRadius
                RowLayout {
                    id: body
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                    spacing: 12
                    ContentIcon { source: entry.modelData.icon || ""; kind: "mod"; size: 42; Layout.preferredWidth: 42; Layout.preferredHeight: 42; Layout.alignment: Qt.AlignTop }
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 5
                        Text { Layout.fillWidth: true; text: entry.modelData.title || ""; color: Theme.colors.text; font.pixelSize: 14; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            BrandBadge { visible: !!entry.modelData.provider; brand: entry.modelData.provider || "modrinth"; iconOnly: true }
                            Text { Layout.fillWidth: true; text: entry.modelData.version || ""; color: Theme.colors.muted; font.pixelSize: 12; elide: Text.ElideRight; textFormat: Text.PlainText }
                        }
                        Text { visible: root.removalMode && !!entry.modelData.reason; Layout.fillWidth: true; text: entry.modelData.reason || ""; color: Theme.colors.muted; font.pixelSize: 12; wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight; textFormat: Text.PlainText }
                        Text { visible: !!entry.modelData.error; Layout.fillWidth: true; text: entry.modelData.error || ""; color: Theme.colors.warning; font.pixelSize: 12; wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight; textFormat: Text.PlainText }
                    }
                    ActionButton {
                        objectName: root.removalMode ? "removeDependency" : "installOptionalDependency"
                        visible: entry.modelData.state === "ready" || entry.modelData.state === "error"
                        text: root.width < 420 ? "" : entry.modelData.state === "error" ? "Retry" : root.removalMode ? "Remove" : "Install"
                        hint: entry.modelData.state === "error" ? "Retry" : root.removalMode ? "Remove" : "Install"
                        iconName: entry.modelData.state === "error" ? "refresh" : root.removalMode ? "trash" : "plus"
                        primary: entry.modelData.state === "ready"; compact: true
                        Layout.alignment: Qt.AlignTop
                        onClicked: { if (entry.modelData.state === "error") root.suggestions.retry(entry.modelData.key); else if (root.removalMode) root.suggestions.remove(entry.modelData.key); else root.suggestions.install(entry.modelData.key) }
                    }
                    BusyIndicator { visible: entry.modelData.state === "waiting" || entry.modelData.state === "checking"; running: visible; implicitWidth: 28; implicitHeight: 28; Layout.alignment: Qt.AlignTop }
                    RowLayout {
                        visible: entry.modelData.state === "queued" || (entry.modelData.state === "installed" || entry.modelData.state === "removed"); spacing: 6; Layout.alignment: Qt.AlignTop
                        Icon { name: (entry.modelData.state === "installed" || entry.modelData.state === "removed") ? "check" : "history"; size: 17; color: (entry.modelData.state === "installed" || entry.modelData.state === "removed") ? Theme.colors.text : Theme.colors.muted }
                        Text { text: (entry.modelData.state === "installed" || entry.modelData.state === "removed") ? (root.removalMode ? "Removed" : "Installed") : "Queued"; color: (entry.modelData.state === "installed" || entry.modelData.state === "removed") ? Theme.colors.text : Theme.colors.muted; font.pixelSize: 12 }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            ActionButton { objectName: "removeUnusedDependencies"; visible: root.removalMode && root.suggestions.unusedCount > 0; text: root.width < 420 ? "" : "Remove unused (" + root.suggestions.unusedCount + ")"; hint: "Remove unused dependencies"; iconName: "trash"; compact: true; onClicked: root.suggestions.removeUnused() }
            Item { Layout.fillWidth: true }
            ActionButton { objectName: "finishOptionalDependencies"; text: "Done"; iconName: "check"; onClicked: root.suggestions.dismiss() }
        }
    }
}
