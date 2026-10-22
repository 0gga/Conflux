pragma ComponentBehavior: Bound
import QtQuick
ListView {
    id: root
    boundsBehavior: Flickable.StopAtBounds
    readonly property bool wheelScrolling: scroll.scrolling
    function stopScrolling() { scroll.stop() }
    onModelChanged: scroll.stop()
    signal wheelMovementEnded()
    SmoothScroll { id: scroll; viewport: root; onFinished: root.wheelMovementEnded() }
}
