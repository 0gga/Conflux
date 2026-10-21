pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "../components"
Rectangle {
    id: root
    required property string status
    required property string history
    required property bool busy
    property bool expanded: false
    signal saveRequested(string message)
    implicitHeight: body.implicitHeight + 24; radius: ControlStyle.controlRadius; color: Theme.colors.card
    ColumnLayout {
        id: body; anchors.fill: parent; anchors.margins: 12; spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Icon { name: "git" }
            Text { Layout.fillWidth: true; text: "main  /  " + (root.status.length ? "Unsaved changes" : "All changes saved"); color: Theme.colors.text; font.pixelSize: 12 }
            ActionButton { text: "History"; subtle: true; onClicked: root.expanded = !root.expanded }
            SearchField { id: message; objectName: "commitMessageInput"; Layout.preferredWidth: 180; placeholderText: "Describe changes…" }
            ActionButton { iconName: "save"; text: "Save"; enabled: !root.busy && message.text.trim().length > 0 && root.status.length > 0; onClicked: root.saveRequested(message.text) }
        }
        BoundedScrollView { id: scroll; visible: root.expanded; Layout.fillWidth: true; Layout.preferredHeight: 100; clip: true
            Text { width: scroll.availableWidth; text: root.history || "No commits yet."; wrapMode: Text.Wrap; textFormat: Text.PlainText; color: Theme.colors.muted; font.pixelSize: 12 }
        }
    }
}
