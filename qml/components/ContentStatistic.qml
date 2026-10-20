pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
RowLayout {
    id: root
    property string iconName: ""
    property string brand: ""
    readonly property var brandInfo: brand.length ? Branding.info(brand) : ({})
    required property string text
    property string hint: ""
    property real statisticScale: 1
    implicitHeight: 20 * statisticScale
    spacing: 5 * statisticScale
    Item {
        objectName: "statisticIcon"
        Layout.preferredWidth: 16 * root.statisticScale; Layout.preferredHeight: 16 * root.statisticScale
        Icon { anchors.centerIn: parent; visible: !root.brandInfo.image; name: root.brand.length ? (root.brandInfo.glyph || "content") : root.iconName; color: root.brandInfo.color || Theme.colors.muted; size: 16 * root.statisticScale }
        Image { anchors.fill: parent; visible: !!root.brandInfo.image; source: root.brandInfo.image || ""; sourceSize: Qt.size(32,32); fillMode: Image.PreserveAspectFit }
    }
    Text { objectName: "statisticText"; text: root.text; color: Theme.colors.muted; font.pixelSize: 12 * root.statisticScale; textFormat: Text.PlainText }
    HoverHandler { id: hover }
    RoundedToolTip { visible: hover.hovered && root.hint.length>0; text: root.hint }
}
