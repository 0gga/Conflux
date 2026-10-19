pragma ComponentBehavior: Bound
import QtQuick
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
        color: control.primary ? "#10251b" : "#e2e9e6"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: 8
        color: control.primary ? (control.hovered ? "#94efc3" : "#72e4ad") : (control.hovered ? "#303a35" : (control.subtle ? "transparent" : "#252e29"))
        border.width: control.primary || control.subtle ? 0 : 1
        border.color: "#364039"
    }
}
