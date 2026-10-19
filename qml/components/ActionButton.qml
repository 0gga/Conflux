pragma ComponentBehavior: Bound
import QtQuick
import Conflux.Style
import QtQuick.Controls

Button {
    id: control
    property bool primary: false
    property bool subtle: false
    implicitHeight: 40
    implicitWidth: Math.max(88, contentItem.implicitWidth + 28)
    leftPadding: 14
    rightPadding: 14
    hoverEnabled: true
    opacity: enabled ? 1 : 0.42
    font.pixelSize: 13
    font.weight: Font.DemiBold
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.primary ? Theme.colors.accentText : Theme.colors.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: 8
        color: control.primary ? (control.hovered ? Qt.lighter(Theme.colors.accent, 1.08) : Theme.colors.accent) : (control.hovered ? Theme.colors.hover : (control.subtle ? "transparent" : Theme.colors.selection))
        border.width: control.primary || control.subtle ? 0 : 1
        border.color: Theme.colors.border
    }
}
