pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "../components"
Item {
    id: root; required property var logs
    signal selected(string path)
    ColumnLayout {
        anchors.fill: parent; spacing: 16
        RowLayout {
            Layout.fillWidth: true
            SelectBox { Layout.fillWidth: true; model: root.logs.entries.map(function(row) { return row.title }); currentIndex: Math.max(0,root.logs.entries.findIndex(function(row) { return row.relativePath === root.logs.selected })); onActivated: { if (root.logs.entries[currentIndex]) root.selected(root.logs.entries[currentIndex].relativePath) } }
            ActionButton { iconName: "refresh"; text: "Refresh"; subtle: true; enabled: !root.logs.busy; onClicked: root.logs.refresh() }
        }
        Text { Layout.fillWidth: true; text: root.logs.error || (root.logs.selected ? root.logs.selected : "Choose a log to read. Logs are never executed or changed."); color: root.logs.error ? Theme.colors.warning : Theme.colors.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; radius: 16; color: Theme.colors.panel; clip: true
            ScrollView { id: scroll; anchors.fill: parent; anchors.margins: 16; clip: true
                TextEdit { width: Math.max(scroll.availableWidth, contentWidth); height: contentHeight; text: root.logs.text; font.family: "Consolas"; font.pixelSize: 12; color: Theme.colors.text; textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.NoWrap }
            }
        }
    }
}
