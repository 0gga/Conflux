import QtQuick
import QtQuick.Controls
import Conflux.Style
TextField {
    id: control
    property string leadingIcon: ""
    implicitHeight: 42; font.pixelSize: 13; selectByMouse: true; hoverEnabled: true
    opacity: enabled ? 1 : 0.38
    leftPadding: leadingIcon.length ? 42 : 14; rightPadding: text.length ? 42 : 14; color: Theme.colors.text
    placeholderTextColor: Theme.colors.muted
    selectionColor: Theme.colors.selection; selectedTextColor: Theme.colors.text
    background: Rectangle {
        radius: 11; color: Theme.colors.input
        border.color: control.activeFocus ? Theme.colors.accent : control.hovered ? Qt.lighter(Theme.colors.border, 1.5) : Theme.colors.border
        border.width: 1
        Behavior on border.color { ColorAnimation { duration: Appearance.values.reduceMotion ? 0 : 100 } }
    }
    ActionButton {
        objectName: "clearSearchButton"; anchors.right: parent.right; anchors.rightMargin: 5; anchors.verticalCenter: parent.verticalCenter
        visible: control.text.length>0; implicitWidth: 30; implicitHeight: 30; iconName: "close"; hint: "Clear search"; subtle: true; compact: true
        onClicked: { control.clear();control.textEdited();control.forceActiveFocus() }
    }
    Icon { anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter; visible: control.leadingIcon.length > 0; name: control.leadingIcon; size: 17 }
}
