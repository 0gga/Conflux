pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
ActionButton {
    id: root
    Layout.fillWidth: true
    compact: true; subtle: true; alignLeft: true; selectionOutlineOnly: true
    implicitHeight: 40
    background: Rectangle {
        radius: ControlStyle.controlRadius
        border.width: root.selected ? 1 : 0; border.color: Theme.colors.accent
        color: root.down ? Qt.darker(Theme.colors.selection,1.2)
            : root.selected || root.highlighted || root.hovered || root.visualFocus ? Theme.colors.selection : "transparent"
    }
}
