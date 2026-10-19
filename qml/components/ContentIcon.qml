import QtQuick
import Conflux.Style
Rectangle {
    id: root
    property string source: ""
    property string kind: "file"
    property real size: 42
    implicitWidth: size; implicitHeight: size; radius: 12; color: Theme.colors.input
    Icon { anchors.centerIn: parent; name: root.kind; size: root.size * 0.5; visible: art.status !== Image.Ready }
    Image { id: art; anchors.fill: parent; anchors.margins: 6; source: root.source; sourceSize: Qt.size(64,64); fillMode: Image.PreserveAspectFit; asynchronous: true; visible: status === Image.Ready }
}
