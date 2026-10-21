pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
Item {
    id: root
    property alias model: view.model
    property alias delegate: view.delegate
    property alias cellWidth: view.cellWidth
    property alias cellHeight: view.cellHeight
    readonly property int count: view.count
    readonly property real viewportWidth: view.width
    GridView {
        id: view; anchors.fill: parent; anchors.rightMargin: 16
        clip: true; boundsBehavior: Flickable.StopAtBounds
        SmoothScroll { viewport: view }
        ScrollBar.vertical: ContentScrollBar { parent: root; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom }
    }
    ScrollViewportCorners { anchors.fill: view }
}
