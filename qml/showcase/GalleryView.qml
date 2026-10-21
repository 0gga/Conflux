pragma ComponentBehavior: Bound
import QtQuick
import Conflux.Style
import "../components"
Item {
    id: root; objectName: "showcaseGallery"
    required property var images
    signal imageRequested(int index)
    ContentGridView {
        id: gallery; anchors.fill: parent; model: root.images
        cellWidth: viewportWidth/Math.max(1,Math.floor(viewportWidth/320)); cellHeight: 235
        delegate: Rectangle {
            required property string modelData
            required property int index
            objectName: "galleryThumbnail"
            width: gallery.cellWidth-10; height: gallery.cellHeight-10; radius: ControlStyle.panelRadius; color: hover.hovered ? Theme.colors.hover : Theme.colors.card
            ContentIcon { anchors.fill: parent; anchors.margins: 6; source: parent.modelData; preserveAspectFit: true; kind: "image"; radius: 10 }
            TapHandler { onTapped: root.imageRequested(parent.index) }
            HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        }
    }
}
