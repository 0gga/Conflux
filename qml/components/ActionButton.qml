pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
Button {
    id: control
    property bool primary: false
    property bool subtle: false
    property string iconName: ""
    property string hint: ""
    implicitHeight: 40; implicitWidth: Math.max(text.length ? 88 : 40, contentItem.implicitWidth + 28)
    leftPadding: 14; rightPadding: 14; hoverEnabled: true; opacity: enabled ? 1 : 0.42
    font.pixelSize: 13; font.weight: Font.DemiBold
    ToolTip.visible: hovered && hint.length > 0; ToolTip.text: hint; ToolTip.delay: 600
    contentItem: RowLayout {
        spacing: control.iconName.length && control.text.length ? 8 : 0
        Icon { visible: control.iconName.length > 0; name: control.iconName; size: 17; color: control.primary ? Theme.colors.accentText : Theme.colors.text; Layout.alignment: Qt.AlignVCenter }
        Text { visible: control.text.length > 0; text: control.text; font: control.font; color: control.primary ? Theme.colors.accentText : Theme.colors.text; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; Layout.fillWidth: true }
    }
    background: Rectangle { radius: 12; color: control.primary ? (control.hovered ? Qt.lighter(Theme.colors.accent,1.06) : Theme.colors.accent) : control.hovered ? Theme.colors.hover : control.subtle ? "transparent" : Theme.colors.selection }
}
