pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
RowLayout {
    id: root
    property string iconName: ""
    required property string text
    property string hint: ""
    property real statisticScale: 1
    spacing: 5 * statisticScale
    Icon { name: root.iconName; size: 16 * root.statisticScale }
    Text { text: root.text; color: Theme.colors.muted; font.pixelSize: 12 * root.statisticScale; textFormat: Text.PlainText }
    HoverHandler { id: hover }
    RoundedToolTip { visible: hover.hovered && root.hint.length>0; text: root.hint }
}
