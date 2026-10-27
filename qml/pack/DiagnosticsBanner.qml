pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
import "../components"
Rectangle {
    id: root
    required property var issues
    property bool expanded: false
    signal settingsRequested()
    signal fixRequested(var fix)
    visible: issues.length > 0
    implicitHeight: body.implicitHeight + 28; radius: ControlStyle.controlRadius; color: Theme.colors.warningSurface
    ColumnLayout {
        id: body; anchors.fill: parent; anchors.margins: 14; spacing: 10
        RowLayout {
            Layout.fillWidth: true; spacing: 10
            Icon { name: "warning"; color: Theme.colors.warning }
            Text { Layout.fillWidth: true; text: root.issues.length + " items to review"; color: Theme.colors.warning; font.pixelSize: 13; font.weight: Font.DemiBold }
            ActionButton { text: root.expanded ? "Collapse" : "Review"; subtle: true; onClicked: root.expanded = !root.expanded }
            ActionButton { iconName: "settings"; hint: "Warning settings"; subtle: true; onClicked: root.settingsRequested() }
        }
        BoundedScrollView {
            id: scroll; visible: root.expanded; Layout.fillWidth: true; Layout.preferredHeight: Math.min(150, detail.implicitHeight); clip: true
            Column {
                id: detail; width: scroll.availableWidth; spacing: 14
                Repeater {
                    model: root.issues
                    delegate: Column {
                        id: issueRow
                        required property var modelData
                        width: detail.width; spacing: 8
                        Text { width: parent.width; text: issueRow.modelData.message; wrapMode: Text.Wrap; textFormat: Text.PlainText; color: Theme.colors.warning; font.pixelSize: 12 }
                        Flow {
                            width: parent.width; spacing: 8
                            Repeater {
                                model: issueRow.modelData.fixes || []
                                delegate: ActionButton {
                                    required property var modelData
                                    objectName: "diagnosticAction-" + modelData.kind
                                    text: modelData.label
                                    iconName: modelData.kind === "remove" ? "trash" : modelData.kind === "enable" ? "check" : "close"
                                    onClicked: root.fixRequested(modelData)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
