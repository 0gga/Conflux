pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import Conflux.Style
Switch {
    id: control
    implicitWidth: 54; implicitHeight: 32
    hoverEnabled: true; opacity: enabled ? 1 : 0.38
    HoverHandler { cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
    indicator: Rectangle {
        x: (control.width - width) / 2; y: (control.height - height) / 2
        implicitWidth: 44; implicitHeight: 24; radius: 12
        color: control.checked ? Theme.colors.accent : Theme.colors.input
        border.color: control.visualFocus ? Theme.colors.text : control.checked ? "transparent" : Theme.colors.border
        Rectangle { x: control.checked ? 23 : 3; y: 3; width: 18; height: 18; radius: 9; color: control.checked ? Theme.colors.accentText : Theme.colors.muted; Behavior on x { NumberAnimation { duration: 100 } } }
    }
}
