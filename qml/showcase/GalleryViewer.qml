pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "../components"
FocusPopup {
    id: root
    objectName: "galleryViewer"
    property var images: []
    property int currentIndex: 0
    property real zoom: 1
    signal externalRequested(string url)
    function show(items,index) { images=items;currentIndex=index;zoom=1;open() }
    function next(offset) { currentIndex=Math.max(0,Math.min(images.length-1,currentIndex+offset));zoom=1;pan.contentX=0;pan.contentY=0 }
    function magnify(factor) { zoom=Math.max(0.5,Math.min(4,zoom*factor)) }
    parent: Overlay.overlay
    width: parent ? parent.width / (Appearance.values.uiScale/100) : 1000
    height: parent ? parent.height / (Appearance.values.uiScale/100) : 700
    padding: 0; modal: true; dim: true
    closePolicy: Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: Theme.colors.background; opacity: 0.9 }
    background: Rectangle { radius: ControlStyle.panelRadius; color: "transparent" }
    contentItem: FocusScope {
        id: viewerContent
        focus: true
        Keys.onLeftPressed: root.next(-1)
        Keys.onRightPressed: root.next(1)
        Keys.onEscapePressed: root.close()
        MouseArea {
            id: backdrop; objectName: "galleryBackdrop"; anchors.fill: parent; z: 2
            acceptedButtons: Qt.LeftButton
            onPressed: function(mouse) {
                const point=Qt.point(mouse.x,mouse.y)
                const onImage=pan.contains(pan.mapFromItem(backdrop,point)) && picture.contains(picture.mapFromItem(backdrop,point))
                mouse.accepted=!onImage && !toolbar.contains(toolbar.mapFromItem(backdrop,point))
            }
            onClicked: root.close()
        }
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 18; spacing: 12
            Flickable {
                id: pan; objectName: "galleryEnlargedViewport"; Layout.fillWidth: true; Layout.fillHeight: true
                clip: true; boundsBehavior: Flickable.StopAtBounds
                contentWidth: Math.max(width,picture.width); contentHeight: Math.max(height,picture.height)
                ContentIcon {
                    id: picture; objectName: "galleryEnlargedImage"
                    readonly property real fit: naturalSize.width>0 ? Math.min(pan.width/naturalSize.width,pan.height/naturalSize.height) * Appearance.values.galleryFitPercent/100 : 1
                    width: naturalSize.width>0 ? naturalSize.width*fit*root.zoom : pan.width
                    height: naturalSize.height>0 ? naturalSize.height*fit*root.zoom : pan.height
                    x: Math.max(0,(pan.width-width)/2); y: Math.max(0,(pan.height-height)/2)
                    source: root.images[root.currentIndex] || ""; intrinsicSize: true; preserveAspectFit: true; radius: ControlStyle.panelRadius; kind: "image"
                }
                WheelHandler { onWheel: function(event) { root.magnify(event.angleDelta.y>0 ? 1.2 : 1/1.2);event.accepted=true } }
            }
            Rectangle {
                id: toolbar; objectName: "galleryToolbar"
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: controls.implicitWidth+16; implicitHeight: controls.implicitHeight+12; radius: ControlStyle.panelRadius
                color: Theme.colors.input
                RowLayout {
                    id: controls; anchors.centerIn: parent; spacing: 4
                    ActionButton { objectName: "galleryPrevious"; iconName: "back"; hint: "Previous image"; subtle: true; compact: true; enabled: root.currentIndex>0; onClicked: root.next(-1) }
                    Text { text: (root.currentIndex+1)+" / "+root.images.length; color: Theme.colors.muted; font.pixelSize: 13 }
                    ActionButton { objectName: "galleryNext"; iconName: "forward"; hint: "Next image"; subtle: true; compact: true; enabled: root.currentIndex<root.images.length-1; onClicked: root.next(1) }
                    Rectangle { implicitWidth: 1; implicitHeight: 22; color: Theme.colors.border }
                    ActionButton { objectName: "galleryZoomOut"; iconName: "zoomOut"; hint: "Zoom out"; subtle: true; compact: true; enabled: root.zoom>0.5; onClicked: root.magnify(1/1.2) }
                    ActionButton { text: Math.round(root.zoom*100)+"%"; hint: "Reset zoom"; subtle: true; compact: true; onClicked: root.zoom=1 }
                    ActionButton { objectName: "galleryZoomIn"; iconName: "zoomIn"; hint: "Zoom in"; subtle: true; compact: true; enabled: root.zoom<4; onClicked: root.magnify(1.2) }
                    Rectangle { implicitWidth: 1; implicitHeight: 22; color: Theme.colors.border }
                    ActionButton { iconName: "external"; hint: "Open original image"; subtle: true; compact: true; onClicked: root.externalRequested(root.images[root.currentIndex]) }
                    ActionButton { objectName: "galleryClose"; iconName: "close"; hint: "Close gallery"; subtle: true; compact: true; onClicked: root.close() }
                }
            }
        }
    }
}
