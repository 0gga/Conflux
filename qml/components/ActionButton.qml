pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import Conflux.Style
Button {
    id: control
    property bool primary: false
    property bool subtle: false
    property bool selected: false
    property bool compact: false
    property bool alignLeft: false
    property bool pill: false
    property bool accented: false
    property string iconName: ""
    property string hint: ""
    readonly property bool interactiveHover: enabled && hovered
    readonly property bool interactiveDown: enabled && down
    readonly property color foreground: primary ? Theme.colors.accentText
        : accented ? Theme.colors.accent : selected || !subtle || interactiveHover || interactiveDown ? Theme.colors.text : Theme.colors.muted
    implicitHeight: compact ? 36 : 40
    implicitWidth: Math.max(text.length ? 64 : implicitHeight, contentItem.implicitWidth + leftPadding + rightPadding)
    leftPadding: text.length ? (compact ? 12 : 16) : 10
    rightPadding: leftPadding
    hoverEnabled: enabled; opacity: enabled ? 1 : 0.38
    font.pixelSize: compact ? 12 : 13; font.weight: selected || primary ? Font.DemiBold : Font.Medium
    Accessible.name: text || hint
    Accessible.description: selected ? "Selected" : ""
    RoundedToolTip { visible: control.interactiveHover && control.hint.length > 0; text: control.hint }
    HoverHandler { enabled: control.enabled; cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
    contentItem: Item {
        implicitWidth: labelRow.implicitWidth; implicitHeight: labelRow.implicitHeight
        Row {
            id: labelRow
            x: control.alignLeft ? 0 : (parent.width - width) / 2
            anchors.verticalCenter: parent.verticalCenter
            spacing: control.iconName.length && control.text.length ? 8 : 0
            Icon {
                visible: control.iconName.length > 0; anchors.verticalCenter: parent.verticalCenter
                name: control.iconName; size: 17
                color: control.selected && !control.primary ? Theme.colors.accent : control.foreground
            }
            Text {
                visible: control.text.length > 0; text: control.text; font: control.font; color: control.foreground
                anchors.verticalCenter: parent.verticalCenter; textFormat: Text.PlainText
            }
        }
    }
    background: Rectangle {
        radius: control.pill ? height/2 : control.compact ? 9 : 11
        color: control.primary ? (control.interactiveDown ? Qt.darker(Theme.colors.accent, 1.15) : control.interactiveHover ? Qt.lighter(Theme.colors.accent, 1.07) : Theme.colors.accent)
            : control.interactiveDown ? Qt.darker(Theme.colors.selection, 1.2)
            : control.selected ? (control.interactiveHover ? Qt.lighter(Theme.colors.selection, 1.15) : Theme.colors.selection)
            : control.interactiveHover ? Theme.colors.hover : control.subtle ? "transparent" : Theme.colors.input
        border.width: 1
        border.color: control.primary ? "transparent" : control.selected ? Qt.lighter(Theme.colors.border, 1.5)
            : control.subtle ? "transparent" : Theme.colors.border
        Behavior on color { ColorAnimation { duration: 100 } }
        Rectangle {
            anchors.fill: parent; anchors.margins: -3; radius: parent.radius + 3
            color: "transparent"; border.width: 1; border.color: Theme.colors.accent
            visible: control.enabled && control.visualFocus
        }
    }
}
