import QtQuick
import QtQuick.Controls
import Conflux.Style
ToolTip {
    id: root
    objectName: "roundedToolTip"
    property bool showWhen: false
    visible: !!Appearance.values.tooltips && showWhen
    delay: Appearance.values.tooltipDelay
    padding: 10
    implicitWidth: Math.min(340, metrics.width + leftPadding + rightPadding)
    background: Rectangle {
        radius: 12
        color: Theme.colors.selection
        border.color: Theme.colors.border
    }
    contentItem: Text {
        id: label
        text: root.text
        color: Theme.colors.text
        font.pixelSize: 13
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        TextMetrics { id: metrics; text: root.text; font: label.font }
    }
}
