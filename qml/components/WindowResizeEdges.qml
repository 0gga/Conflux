pragma ComponentBehavior: Bound
import QtQuick
Item {
    id: root
    required property Window targetWindow
    readonly property real grip: 6
    visible: targetWindow.visibility === Window.Windowed
    Repeater {
        model: [Qt.TopEdge, Qt.BottomEdge, Qt.LeftEdge, Qt.RightEdge,
            Qt.TopEdge|Qt.LeftEdge, Qt.TopEdge|Qt.RightEdge,
            Qt.BottomEdge|Qt.LeftEdge, Qt.BottomEdge|Qt.RightEdge]
        delegate: MouseArea {
            required property int modelData
            readonly property bool edgeLeft: (modelData & Qt.LeftEdge) !== 0
            readonly property bool edgeRight: (modelData & Qt.RightEdge) !== 0
            readonly property bool edgeTop: (modelData & Qt.TopEdge) !== 0
            readonly property bool edgeBottom: (modelData & Qt.BottomEdge) !== 0
            readonly property bool corner: (edgeLeft || edgeRight) && (edgeTop || edgeBottom)
            width: edgeLeft || edgeRight ? root.grip : root.width - root.grip*2
            height: edgeTop || edgeBottom ? root.grip : root.height - root.grip*2
            x: edgeRight ? root.width-width : edgeLeft ? 0 : root.grip
            y: edgeBottom ? root.height-height : edgeTop ? 0 : root.grip
            cursorShape: corner ? (edgeLeft === edgeTop ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor)
                : edgeLeft || edgeRight ? Qt.SizeHorCursor : Qt.SizeVerCursor
            acceptedButtons: Qt.LeftButton
            onPressed: root.targetWindow.startSystemResize(modelData)
        }
    }
}
