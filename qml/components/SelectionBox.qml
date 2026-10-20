import QtQuick
import QtQuick.Controls
import Conflux.Style
CheckBox {
    id: control
    property bool partial: false
    implicitWidth: 28; implicitHeight: 32
    opacity: enabled ? 1 : 0.4
    indicator: Rectangle {
        x: (control.width-width)/2; y: (control.height-height)/2
        width: 20; height: 20; radius: 6
        color: control.checked || control.partial ? Theme.colors.selection : "transparent"
        border.color: control.visualFocus ? Theme.colors.accent : control.hovered ? Theme.colors.muted : Theme.colors.border
        Icon { anchors.centerIn: parent; name: "check"; size: 14; color: Theme.colors.accent; visible: control.checked && !control.partial }
        Rectangle { anchors.centerIn: parent; width: 10; height: 2; radius: 1; color: Theme.colors.accent; visible: control.partial }
    }
}
