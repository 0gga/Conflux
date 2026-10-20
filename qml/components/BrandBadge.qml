pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
Rectangle {
    id: root
    required property string brand
    readonly property var info: Branding.info(brand)
    property string text: info.label
    property bool framed: true
    property bool iconOnly: false
    Accessible.name: info.label
    implicitWidth: body.implicitWidth + (framed ? 16 : 0)
    implicitHeight: 26; radius: 12
    color: framed ? Theme.colors.input : "transparent"
    RowLayout {
        id: body; anchors.centerIn: parent; spacing: 6
        Image { visible: !!root.info.image; source: root.info.image || ""; Layout.preferredWidth: 17; Layout.preferredHeight: 17; sourceSize: Qt.size(34,34); fillMode: Image.PreserveAspectFit }
        Icon { visible: !root.info.image; name: root.info.glyph || "content"; color: root.info.color || Theme.colors.muted; size: 17 }
        Text { visible: !root.iconOnly; text: root.text; color: root.framed ? (root.info.color || Theme.colors.muted) : Theme.colors.muted; font.pixelSize: 12; textFormat: Text.PlainText }
    }
    HoverHandler { id: hover }
    RoundedToolTip { visible: hover.hovered; text: root.info.label }
}
