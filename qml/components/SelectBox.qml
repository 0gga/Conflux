pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import Conflux.Style
ComboBox {
    id: control
    implicitHeight: 42; implicitWidth: 160; font.pixelSize: 13; hoverEnabled: true
    padding: 0
    opacity: enabled ? 1 : 0.38
    HoverHandler { cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
    background: Rectangle {
        radius: ControlStyle.controlRadius; color: control.down ? Theme.colors.selection : (control.enabled && control.hovered) ? Theme.colors.hover : Theme.colors.input
        border.color: control.visualFocus || control.popup.visible ? Theme.colors.accent : Theme.colors.border
    }
    contentItem: Text { text: control.displayText; color: Theme.colors.text; font: control.font; verticalAlignment: Text.AlignVCenter; leftPadding: 14; rightPadding: 36; elide: Text.ElideRight }
    indicator: Icon { x: control.width - width - 13; y: (control.height - height) / 2; name: "down"; size: 16 }
    delegate: MenuOption {
        id: option; objectName: "selectBoxOption-"+index
        required property var modelData
        required property int index
        width: list.optionWidth; text: String(modelData); highlighted: menu.pendingIndex === index
        selected: index === control.currentIndex
        onHoveredChanged: if (hovered) menu.pendingIndex=index
        leftPadding: 12; rightPadding: 32
        contentItem: Text { text: option.text; color: Theme.colors.text; font.pixelSize: 13; font.weight: option.selected ? Font.DemiBold : Font.Normal; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
        Icon { anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter; name: "check"; size: 15; color: Theme.colors.accent; visible: option.selected }
    }

    popup: FocusPopup {
        id: menu
        property int pendingIndex: 0
        onAboutToShow: pendingIndex=Math.max(0,control.currentIndex)
        y: control.height + 6; width: control.width; padding: 6; implicitHeight: Math.min(list.contentHeight + 12, 280)
        background: Rectangle { radius: ControlStyle.panelRadius; color: Theme.colors.card; border.color: Theme.colors.border }
        contentItem: MenuList {
            id: list; focus: true; clip: true; boundsBehavior: Flickable.StopAtBounds; implicitHeight: contentHeight; spacing: 2
            model: control.popup.visible ? control.delegateModel : null; currentIndex: menu.pendingIndex
            keyNavigationEnabled: false
            Keys.onPressed: function(event) {
                if(event.key===Qt.Key_Down || event.key===Qt.Key_Up || event.key===Qt.Key_Home || event.key===Qt.Key_End) {
                    menu.pendingIndex=event.key===Qt.Key_Home ? 0 : event.key===Qt.Key_End ? control.count-1 : Math.max(0,Math.min(control.count-1,menu.pendingIndex+(event.key===Qt.Key_Down ? 1 : -1)))
                    event.accepted=true
                } else if(event.key===Qt.Key_Return || event.key===Qt.Key_Enter || event.key===Qt.Key_Space) {
                    control.currentIndex=menu.pendingIndex;control.activated(control.currentIndex);control.popup.close();control.forceActiveFocus();event.accepted=true
                } else if(event.key===Qt.Key_Escape) {
                    control.popup.close();control.forceActiveFocus();event.accepted=true
                }
            }
        }
    }
}
