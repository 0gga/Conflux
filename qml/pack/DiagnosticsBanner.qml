pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "../components"
Rectangle {
    id: root
    required property var issues
    property bool expanded: false
    signal settingsRequested()
    visible: issues.length > 0
    implicitHeight: body.implicitHeight + 28; radius: 14; color: Theme.colors.warningSurface
    ColumnLayout {
        id: body; anchors.fill: parent; anchors.margins: 14; spacing: 10
        RowLayout {
            Layout.fillWidth: true; spacing: 10
            Icon { name: "warning"; color: Theme.colors.warning }
            Text { Layout.fillWidth: true; text: root.issues.length + " items to review"; color: Theme.colors.warning; font.pixelSize: 13; font.weight: Font.DemiBold }
            ActionButton { text: root.expanded ? "Collapse" : "Review"; subtle: true; onClicked: root.expanded = !root.expanded }
            ActionButton { iconName: "settings"; hint: "Warning settings"; subtle: true; onClicked: root.settingsRequested() }
        }
        ScrollView {
            id: scroll; visible: root.expanded; Layout.fillWidth: true; Layout.preferredHeight: Math.min(150, detail.implicitHeight); clip: true
            Text { id: detail; width: scroll.availableWidth; text: root.issues.map(function(issue) { return issue.message }).join("\n\n"); wrapMode: Text.Wrap; textFormat: Text.PlainText; color: Theme.colors.warning; font.pixelSize: 12 }
        }
    }
}
