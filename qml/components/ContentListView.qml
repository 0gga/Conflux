pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import Conflux.Style
Item {
    id: root
    property alias model: view.model
    property alias delegate: view.delegate
    property alias spacing: view.spacing
    property alias count: view.count
    property alias contentY: view.contentY
    property alias contentHeight: view.contentHeight
    property alias currentIndex: view.currentIndex
    readonly property bool moving: view.moving || view.wheelScrolling
    readonly property real viewportWidth: view.width
    readonly property ListView viewport: view
    property real bottomInset: 0
    property real cornerRadius: 14
    property color outsideColor: Theme.colors.background
    signal movementEnded()
    SmoothListView {
        id: view; objectName: "contentListViewport"
        anchors.fill: parent; anchors.rightMargin: 16
        footer: Item { width: 1; height: root.bottomInset }
        clip: true; boundsBehavior: Flickable.StopAtBounds
        onMovementEnded: root.movementEnded()
        onWheelMovementEnded: root.movementEnded()
        ScrollBar.vertical: ContentScrollBar { parent: root; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom }
    }
    ScrollViewportCorners { anchors.fill: view; radius: root.cornerRadius; outsideColor: root.outsideColor }
}
