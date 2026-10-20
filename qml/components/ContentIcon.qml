import QtQuick
import Conflux.Style
Rectangle {
    id: root
    property string source: ""
    property string kind: "file"
    property real size: 42
    property bool preserveAspectFit: false
    property bool intrinsicSize: false
    readonly property int status: art.status
    readonly property size naturalSize: Qt.size(art.implicitWidth,art.implicitHeight)
    implicitWidth: size; implicitHeight: size; radius: Math.min(12,size*0.2); color: art.status === Image.Ready ? "transparent" : Theme.colors.input
    Icon { anchors.centerIn: parent; name: root.kind; size: root.size * 0.5; visible: art.status !== Image.Ready }
    Image { id: art; objectName: "artworkSource"; source: root.source; sourceSize: root.intrinsicSize ? Qt.size(0,0) : Qt.size(Math.min(2048,Math.ceil(root.width*2)),Math.min(2048,Math.ceil(root.height*2))); asynchronous: true; visible: false; onStatusChanged: { if (status === Image.Ready) crop.loadImage(root.source); crop.requestPaint() } }
    Canvas {
        id: crop; objectName: "artworkCrop"; anchors.fill: parent; visible: art.status === Image.Ready
        property string previousSource: ""
        function loadArtwork() {
            if (previousSource.length && previousSource !== root.source) unloadImage(previousSource)
            previousSource = root.source
            if (root.source.length) loadImage(root.source)
            requestPaint()
        }
        Connections { target: root; function onSourceChanged() { crop.loadArtwork() } }
        onAvailableChanged: if (available) loadArtwork()
        onImageLoaded: requestPaint()
        onVisibleChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            if (art.status !== Image.Ready || art.implicitWidth <= 0 || art.implicitHeight <= 0 || !isImageLoaded(root.source)) return
            const scale = root.preserveAspectFit ? Math.min(width/art.implicitWidth,height/art.implicitHeight) : Math.max(width/art.implicitWidth,height/art.implicitHeight)
            const w = art.implicitWidth*scale, h = art.implicitHeight*scale
            const x = root.preserveAspectFit ? (width-w)/2 : 0, y = root.preserveAspectFit ? (height-h)/2 : 0
            const rw = root.preserveAspectFit ? w : width, rh = root.preserveAspectFit ? h : height
            const r = Math.min(root.radius,rw/2,rh/2)
            ctx.beginPath()
            ctx.moveTo(x+r,y); ctx.lineTo(x+rw-r,y); ctx.quadraticCurveTo(x+rw,y,x+rw,y+r)
            ctx.lineTo(x+rw,y+rh-r); ctx.quadraticCurveTo(x+rw,y+rh,x+rw-r,y+rh)
            ctx.lineTo(x+r,y+rh); ctx.quadraticCurveTo(x,y+rh,x,y+rh-r)
            ctx.lineTo(x,y+r); ctx.quadraticCurveTo(x,y,x+r,y); ctx.closePath(); ctx.clip()
            ctx.drawImage(root.source,(width-w)/2,(height-h)/2,w,h)
        }
    }
}
