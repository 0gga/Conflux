pragma ComponentBehavior: Bound
import QtQuick
Item {
    id: root
    required property Window targetWindow
    function toggleMaximized() {
        if (targetWindow.visibility === Window.Maximized) targetWindow.showNormal()
        else targetWindow.showMaximized()
    }
    DragHandler {
        target: null; acceptedButtons: Qt.LeftButton
        onActiveChanged: if (active) root.targetWindow.startSystemMove()
    }
    TapHandler { acceptedButtons: Qt.LeftButton; onDoubleTapped: root.toggleMaximized() }
}
