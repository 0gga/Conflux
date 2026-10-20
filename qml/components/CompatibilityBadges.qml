pragma ComponentBehavior: Bound
import QtQuick
import Conflux.Style

Row {
    id: root
    property string minecraft: ""
    property string loader: ""
    property bool showUnknownVersion: false
    spacing: 6

    Rectangle {
        objectName: "compatibilityMinecraft"
        visible: root.minecraft.length > 0 || root.showUnknownVersion
        implicitWidth: label.implicitWidth + 16
        implicitHeight: 26
        radius: 12
        color: Theme.colors.input
        Text {
            id: label
            anchors.centerIn: parent
            text: root.minecraft || "Unknown"
            color: Theme.colors.muted
            font.pixelSize: 12
            textFormat: Text.PlainText
        }
        HoverHandler { id: hover }
        RoundedToolTip { visible: hover.hovered; text: "Minecraft " + (root.minecraft || "version unknown") }
    }
    BrandBadge {
        objectName: "compatibilityLoader"
        visible: root.loader.length > 0
        brand: root.loader
        iconOnly: true
    }
}
