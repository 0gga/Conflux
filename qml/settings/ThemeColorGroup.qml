pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
import "../components"
ColumnLayout {
    id: root
    required property var group
    signal colorRequested(string token)
    spacing: 8
    Text { text: root.group.key.charAt(0).toUpperCase()+root.group.key.slice(1); color: Theme.colors.text; font.pixelSize: 14; font.weight: Font.DemiBold }
    Repeater {
        model: root.group.tokens
        delegate: RowLayout {
            id: colorRow
            required property string modelData
            Layout.fillWidth: true; spacing: 10
            Rectangle {
                Layout.preferredWidth: 32; Layout.preferredHeight: 32; radius: 9; color: Theme.colors[parent.modelData]; border.color: Theme.colors.border
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.colorRequested(colorRow.modelData) }
            }
            Text { Layout.fillWidth: true; text: parent.modelData.replace(/([A-Z])/g," $1").replace(/^./,function(c){return c.toUpperCase()}); color: Theme.colors.muted; font.pixelSize: 12 }
            SearchField { Layout.preferredWidth: 130; text: Theme.colors[parent.modelData]; maximumLength: 32; selectByMouse: true; Accessible.name: parent.modelData + " color"; onEditingFinished: Theme.setColor(parent.modelData,text) }
        }
    }
}
