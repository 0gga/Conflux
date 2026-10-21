pragma ComponentBehavior: Bound
import QtQuick
import Conflux.Style
Row {
    id: root
    required property Window targetWindow
    readonly property bool maximized: targetWindow.visibility === Window.Maximized
    function toggleMaximized() {
        if (maximized) targetWindow.showNormal()
        else targetWindow.showMaximized()
    }
    spacing: 4
    ActionButton {
        objectName: "windowMinimize"; iconName: "minimize"; hint: "Minimize"
        compact: true; subtle: true; implicitWidth: 38
        onClicked: root.targetWindow.showMinimized()
    }
    ActionButton {
        objectName: "windowMaximize"; iconName: root.maximized ? "window-restore" : "maximize"
        hint: root.maximized ? "Restore" : "Maximize"
        compact: true; subtle: true; implicitWidth: 38
        onClicked: root.toggleMaximized()
    }
    ActionButton {
        objectName: "windowClose"; iconName: "close"; hint: "Close"
        compact: true; subtle: true; implicitWidth: 38
        onClicked: root.targetWindow.close()
    }
}
