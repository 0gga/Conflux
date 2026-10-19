import QtQuick
import QtQuick.Controls
import Conflux.Style
TextField {
    id: control
    property string leadingIcon: ""
    implicitHeight: 44; font.pixelSize: 13; selectByMouse: true
    leftPadding: leadingIcon.length ? 42 : 14; rightPadding: 14; color: Theme.colors.text
    placeholderTextColor: Theme.colors.muted
    background: Rectangle { radius: 12; color: Theme.colors.input; border.color: control.activeFocus ? Theme.colors.accent : "transparent"; border.width: 1 }
    Icon { anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter; visible: control.leadingIcon.length > 0; name: control.leadingIcon; size: 17 }
}
