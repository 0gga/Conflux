pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
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
    implicitHeight: 26 * badgeScale; radius: 12 * badgeScale
    color: framed ? Theme.colors.input : "transparent"
    RowLayout {
        id: body; anchors.centerIn: parent; spacing: 6 * root.badgeScale
        Image { visible: !!root.info.image; source: root.info.image || ""; Layout.preferredWidth: 17 * root.badgeScale; Layout.preferredHeight: 17 * root.badgeScale; sourceSize: Qt.size(34,34); fillMode: Image.PreserveAspectFit }
        Icon { visible: !root.info.image; name: root.info.glyph || "content"; color: root.info.color || Theme.colors.muted; size: 17 * root.badgeScale }
        Text { visible: !root.iconOnly; text: root.text; color: root.framed ? (root.info.color || Theme.colors.muted) : Theme.colors.muted; font.pixelSize: 12 * root.badgeScale; textFormat: Text.PlainText }
    }
    HoverHandler { id: hover }
    RoundedToolTip { showWhen: root.tooltipsEnabled && hover.hovered; text: root.info.label }
}
