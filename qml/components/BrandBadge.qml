pragma ComponentBehavior: Bound
import QtQuick
import Conflux.Style
Rectangle {
    id: root
    property bool tooltipsEnabled: true
    required property string brand
    readonly property var info: Branding.info(brand)
    property string text: info.label
    property bool framed: true
    property bool iconOnly: false
    property real badgeScale: 1
    Accessible.name: info.label
    implicitWidth: body.implicitWidth + (framed ? 16 * badgeScale : 0)
    implicitHeight: 26 * badgeScale; radius: height/2
    color: framed ? Theme.colors.input : "transparent"
    Item {
        id: body; anchors.centerIn: parent; anchors.alignWhenCentered: false
        implicitWidth: 17 * root.badgeScale + (root.iconOnly ? 0 : 6 * root.badgeScale + label.implicitWidth)
        implicitHeight: Math.max(17 * root.badgeScale, label.visible ? label.implicitHeight : 0)
        width: implicitWidth; height: implicitHeight
        Item {
            id: artwork; width: 17 * root.badgeScale; height: width
            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; anchors.alignWhenCentered: false
            Image {
                objectName: "brandBadgeImage"; visible: !!root.info.image; anchors.fill: parent
                source: root.info.image || ""; sourceSize: Qt.size(Math.ceil(width*2),Math.ceil(height*2))
                fillMode: Image.PreserveAspectFit
            }
            Icon {
                objectName: "brandBadgeGlyph"; visible: !root.info.image; anchors.fill: parent
                name: root.info.glyph || "content"; color: root.info.color || Theme.colors.muted; size: 17 * root.badgeScale
            }
        }
        Text {
            id: label; visible: !root.iconOnly
            anchors.left: artwork.right; anchors.leftMargin: 6 * root.badgeScale
            anchors.verticalCenter: parent.verticalCenter; anchors.alignWhenCentered: false
            text: root.text; color: root.framed ? (root.info.color || Theme.colors.muted) : Theme.colors.muted
            font.pixelSize: 12 * root.badgeScale; textFormat: Text.PlainText
        }
    }
    HoverHandler { id: hover }
    RoundedToolTip { showWhen: root.tooltipsEnabled && hover.hovered; text: root.info.label }
}
