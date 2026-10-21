pragma ComponentBehavior: Bound
import QtQuick
import Conflux.Style
Rectangle {
    id: root
    required property string text
    property bool locked: false
    property bool removable: true
    signal removed()
    implicitWidth: label.implicitWidth + (locked || removable ? 39 : 18)
    implicitHeight: 28; radius: height/2; color: Theme.colors.input
    border.width: removeButton.visualFocus ? 1 : 0
    border.color: Theme.colors.accent
    Text { id: label; anchors.verticalCenter: parent.verticalCenter; x: root.locked ? 26 : 9; text: root.text; font.pixelSize: 11; color: Theme.colors.muted; textFormat: Text.PlainText }
    Icon { visible: root.locked; x: 8; anchors.verticalCenter: parent.verticalCenter; size: 13; name: "lock" }
    ActionButton { id: removeButton; objectName: "filterChipRemove"; pill: true; focusIndicator: false; visible: !root.locked && root.removable; anchors.right: parent.right; anchors.rightMargin: 2; anchors.verticalCenter: parent.verticalCenter; width: 24; implicitWidth: 24; implicitHeight: 24; iconName: "close"; subtle: true; compact: true; hint: "Remove " + root.text; onClicked: root.removed() }
    HoverHandler { id: hover }
    RoundedToolTip { showWhen: root.locked && hover.hovered; text: "From this instance" }
}
