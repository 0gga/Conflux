import QtQuick
import QtQuick.Controls
import Conflux.Style
ScrollBar {
    id: root; objectName: "contentScrollBar"
    implicitWidth: Appearance.values.scrollbarWidth; implicitHeight: Appearance.values.scrollbarWidth
    padding: 1; policy: ScrollBar.AsNeeded
    visible: policy === ScrollBar.AlwaysOn || (policy === ScrollBar.AsNeeded && size < 1 - 0.000001)
    contentItem: Rectangle {
        implicitWidth: root.implicitWidth-2; implicitHeight: root.implicitHeight-2; radius: width/2
        color: root.pressed ? Theme.colors.accent : Theme.colors.muted
        opacity: root.active || root.hovered ? 0.75 : 0.35
    }
    background: Rectangle { radius: 4; color: "transparent" }
}
