import QtQuick
import QtQuick.Shapes
import Conflux.Style
Shape {
    id: root
    property real radius: ControlStyle.panelRadius
    property color outsideColor: Theme.colors.background
    readonly property real corner: Math.max(0, Math.min(radius, width / 2, height / 2))
    z: 100
    antialiasing: true
    // Cover the physical clip edge even when UI scale puts it between pixels.
    ShapePath {
        strokeWidth: 0; strokeColor: "transparent"; fillColor: root.outsideColor
        startX: -1; startY: -1
        PathLine { x: root.corner; y: 0 }
        PathQuad { x: 0; y: root.corner; controlX: 0; controlY: 0 }
        PathLine { x: -1; y: -1 }
    }
    ShapePath {
        strokeWidth: 0; strokeColor: "transparent"; fillColor: root.outsideColor
        startX: root.width + 1; startY: -1
        PathLine { x: root.width; y: root.corner }
        PathQuad { x: root.width - root.corner; y: 0; controlX: root.width; controlY: 0 }
        PathLine { x: root.width + 1; y: -1 }
    }
    ShapePath {
        strokeWidth: 0; strokeColor: "transparent"; fillColor: root.outsideColor
        startX: -1; startY: root.height + 1
        PathLine { x: 0; y: root.height - root.corner }
        PathQuad { x: root.corner; y: root.height; controlX: 0; controlY: root.height }
        PathLine { x: -1; y: root.height + 1 }
    }
    ShapePath {
        strokeWidth: 0; strokeColor: "transparent"; fillColor: root.outsideColor
        startX: root.width + 1; startY: root.height + 1
        PathLine { x: root.width - root.corner; y: root.height }
        PathQuad { x: root.width; y: root.height - root.corner; controlX: root.width; controlY: root.height }
        PathLine { x: root.width + 1; y: root.height + 1 }
    }
}
