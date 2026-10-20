import QtQuick
import Conflux.Style
Canvas {
    id: root
    property real radius: 14
    property color outsideColor: Theme.colors.background
    z: 100
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onOutsideColorChanged: requestPaint()
    onRadiusChanged: requestPaint()
    onPaint: {
        const ctx=getContext("2d");ctx.reset();ctx.fillStyle=outsideColor
        const r=Math.min(radius,width/2,height/2)
        ctx.beginPath();ctx.moveTo(0,0);ctx.lineTo(r,0);ctx.quadraticCurveTo(0,0,0,r);ctx.closePath();ctx.fill()
        ctx.beginPath();ctx.moveTo(width,0);ctx.lineTo(width,r);ctx.quadraticCurveTo(width,0,width-r,0);ctx.closePath();ctx.fill()
        ctx.beginPath();ctx.moveTo(0,height);ctx.lineTo(0,height-r);ctx.quadraticCurveTo(0,height,r,height);ctx.closePath();ctx.fill()
        ctx.beginPath();ctx.moveTo(width,height);ctx.lineTo(width-r,height);ctx.quadraticCurveTo(width,height,width,height-r);ctx.closePath();ctx.fill()
    }
}
