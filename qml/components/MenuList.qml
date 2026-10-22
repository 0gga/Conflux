pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import Conflux.Style
SmoothListView {
    id: root
    readonly property real optionWidth: Math.max(0,width-(track.visible ? track.width+4 : 0))
    clip: true; spacing: 2; boundsBehavior: Flickable.StopAtBounds
    implicitHeight: contentHeight
    ScrollBar.vertical: ContentScrollBar { id: track }
    ScrollViewportCorners { anchors.fill: parent; anchors.rightMargin: root.width-root.optionWidth; radius: ControlStyle.controlRadius; outsideColor: Theme.colors.card }
}
