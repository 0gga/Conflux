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
    implicitWidth: Math.min(root.Overlay.overlay ? root.Overlay.overlay.width-16 : 480, 480, Math.ceil(measure.implicitWidth) + leftPadding + rightPadding + 2)
    width: implicitWidth
    implicitHeight: label.implicitHeight + topPadding + bottomPadding
    Text { id:measure;visible:false;text:root.text;font:label.font;textFormat:Text.PlainText }
    background: Rectangle {
        radius: ControlStyle.controlRadius
        color: Theme.colors.selection
        border.color: Theme.colors.border
    }
    contentItem: Text {
        id: label
        text: root.text
        color: Theme.colors.text
        font.pixelSize: 13
        textFormat: Text.PlainText
        wrapMode: Text.Wrap

    }
}
