import QtQuick
import Conflux.Style
import "IconPaths.js" as Icons
Item {
    id: root
    property string name: "file"
    property color color: Theme.colors.muted
    property real size: 20
    implicitWidth: size; implicitHeight: size
    Image {
        anchors.fill: parent; smooth: true
        sourceSize: Qt.size(Math.ceil(width * 2), Math.ceil(height * 2))
        fillMode: Image.PreserveAspectFit
        source: "data:image/svg+xml;utf8," + encodeURIComponent("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='" + root.color + "' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>" + (Icons.markup[root.name] || Icons.markup.file) + "</svg>")
    }
}
