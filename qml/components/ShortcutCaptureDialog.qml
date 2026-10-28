pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
FocusDialog {
    id: root
    objectName: "shortcutCaptureDialog"
    required property var preferences
    width: Math.min(440, Math.max(280, parent.width - 32))
    anchors.centerIn: parent; padding: 20
    closePolicy: Popup.CloseOnEscape
    background: Rectangle { color: Theme.colors.card; border.color: Theme.colors.border; radius: ControlStyle.panelRadius }
    function syncVisible() { if (root.preferences.capturing && !root.visible) root.open(); else if (!root.preferences.capturing) root.close() }
    Component.onCompleted: syncVisible()
    Connections { target: root.preferences; function onCaptureChanged() { root.syncVisible() } }
    onOpened: capture.forceActiveFocus()
    onRejected: root.preferences.cancelCapture()
    onClosed: if (root.preferences.capturing) root.preferences.cancelCapture()
    contentItem: ColumnLayout {
        spacing: 16
        Text { Layout.fillWidth: true; text: root.preferences.recordingTitle; color: Theme.colors.text; font.pixelSize: 20; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
        FocusScope {
            id: capture; objectName: "shortcutCaptureKeys"
            Layout.fillWidth: true; implicitHeight: 84
            Rectangle { anchors.fill: parent; radius: ControlStyle.controlRadius; color: Theme.colors.input; border.color: capture.activeFocus ? Theme.colors.accent : Theme.colors.border }
            RowLayout {
                anchors.fill: parent; anchors.margins: 14; spacing: 12
                Icon { name: "keyboard"; size: 24; color: Theme.colors.muted }
                Text { Layout.fillWidth: true; text: root.preferences.candidate || "Press keys"; color: Theme.colors.text; font.pixelSize: 18; wrapMode: Text.Wrap; textFormat: Text.PlainText }
            }
            Keys.onPressed: function(event) { if ((event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) && (event.modifiers & ~Qt.ShiftModifier) === 0) { event.accepted = false; return } event.accepted = true; if (!event.isAutoRepeat) root.preferences.capturePress(event.key, event.modifiers) }
            Keys.onReleased: function(event) { event.accepted = true; if (!event.isAutoRepeat) root.preferences.captureRelease(event.key) }
        }
        Text { objectName: "shortcutCaptureError"; Layout.fillWidth: true; visible: !!root.preferences.error; text: root.preferences.error; color: Theme.colors.warning; wrapMode: Text.WordWrap; textFormat: Text.PlainText; font.pixelSize: 12 }
        RowLayout {
            Layout.fillWidth: true
            ActionButton { objectName: "cancelShortcutCapture"; text: "Cancel"; compact: true; onClicked: root.preferences.cancelCapture() }
            Item { Layout.fillWidth: true }
            ActionButton { objectName: "clearCapturedShortcut"; text: "Clear"; iconName: "close"; compact: true; onClicked: root.preferences.clearCaptured() }
        }
    }
}
