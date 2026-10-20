import QtQuick
import QtQuick.Controls
import Conflux.Style
ScrollBar {
    id: root; objectName: "contentScrollBar"
    implicitWidth: 8; implicitHeight: 8
    padding: 1; policy: ScrollBar.AsNeeded
    contentItem: Rectangle {
        implicitWidth: 6; implicitHeight: 6; radius: 3
        color: root.pressed ? Theme.colors.accent : Theme.colors.muted
        opacity: root.active || root.hovered ? 0.75 : 0.35
    }
    background: Rectangle { radius: 4; color: "transparent" }
}
