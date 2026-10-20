import QtQuick
import Conflux.Style
Rectangle {
    id: root
    property string source: ""
    property string kind: "file"
    property real size: 42
    implicitWidth: size; implicitHeight: size; radius: Math.min(12,size*0.2); color: art.status === Image.Ready ? "transparent" : Theme.colors.input
    Icon { anchors.centerIn: parent; name: root.kind; size: root.size * 0.5; visible: art.status !== Image.Ready }
    Image { id: art; objectName: "artworkSource"; source: root.source; sourceSize: Qt.size(Math.ceil(root.size*2),Math.ceil(root.size*2)); asynchronous: true; visible: false; onStatusChanged: { if (status === Image.Ready) crop.loadImage(root.source); crop.requestPaint() } }
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
            const r = root.radius
            ctx.beginPath()
            ctx.moveTo(r,0); ctx.lineTo(width-r,0); ctx.quadraticCurveTo(width,0,width,r)
            ctx.lineTo(width,height-r); ctx.quadraticCurveTo(width,height,width-r,height)
            ctx.lineTo(r,height); ctx.quadraticCurveTo(0,height,0,height-r)
            ctx.lineTo(0,r); ctx.quadraticCurveTo(0,0,r,0); ctx.closePath(); ctx.clip()
            const scale = Math.max(width/art.implicitWidth,height/art.implicitHeight)
            const w = art.implicitWidth*scale, h = art.implicitHeight*scale
            ctx.drawImage(root.source,(width-w)/2,(height-h)/2,w,h)
        }
    }
}
