import QtQuick
import QtQuick.Controls
import Conflux.Style
ToolTip {
    id: root
    objectName: "roundedToolTip"
    delay: 600
    padding: 10
    implicitWidth: Math.min(340, metrics.width + leftPadding + rightPadding)
    background: Rectangle {
        radius: 10
        color: Theme.colors.input
        border.color: Theme.colors.border
    }
    contentItem: Text {
        id: label
        text: root.text
        color: Theme.colors.text
        font.pixelSize: 12
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        TextMetrics { id: metrics; text: root.text; font: label.font }
    }
}
