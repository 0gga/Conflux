pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import Conflux.Style
Switch {
    id: control
    implicitWidth: 54; implicitHeight: 32
    indicator: Rectangle {
        x: (control.width - width) / 2; y: (control.height - height) / 2
        implicitWidth: 44; implicitHeight: 24; radius: 12
        color: control.checked ? Theme.colors.accent : Theme.colors.input
        border.color: control.activeFocus ? Theme.colors.text : "transparent"
        Rectangle { x: control.checked ? 23 : 3; y: 3; width: 18; height: 18; radius: 9; color: control.checked ? Theme.colors.accentText : Theme.colors.muted }
    }
}
