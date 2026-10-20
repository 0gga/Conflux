pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import Conflux.Style
ComboBox {
    id: control
    implicitHeight: 42; implicitWidth: 160; font.pixelSize: 13; hoverEnabled: true
    opacity: enabled ? 1 : 0.38
    HoverHandler { cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
    background: Rectangle {
        radius: 11; color: control.down ? Theme.colors.selection : control.hovered ? Theme.colors.hover : Theme.colors.input
        border.color: control.visualFocus || control.popup.visible ? Theme.colors.accent : Theme.colors.border
    }
    contentItem: Text { text: control.displayText; color: Theme.colors.text; font: control.font; verticalAlignment: Text.AlignVCenter; leftPadding: 14; rightPadding: 36; elide: Text.ElideRight }
    indicator: Icon { x: control.width - width - 13; y: (control.height - height) / 2; name: "down"; size: 16 }
    delegate: ItemDelegate {
        id: option
        required property var modelData
        required property int index
        width: control.width - 12; height: 38; text: String(modelData); highlighted: control.highlightedIndex === index
        leftPadding: 12; rightPadding: 34
        contentItem: Text { text: option.text; color: Theme.colors.text; font.pixelSize: 13; font.weight: option.index === control.currentIndex ? Font.DemiBold : Font.Normal; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { radius: 8; color: option.highlighted ? Theme.colors.selection : option.index === control.currentIndex ? Theme.colors.input : "transparent" }
        Icon { anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter; name: "check"; size: 15; color: Theme.colors.accent; visible: option.index === control.currentIndex }
    }
    popup: Popup {
        y: control.height + 6; width: control.width; padding: 6; implicitHeight: Math.min(list.contentHeight + 12, 280)
        background: Rectangle { radius: 12; color: Theme.colors.card; border.color: Theme.colors.border }
        contentItem: ListView { boundsBehavior: Flickable.StopAtBounds; id: list; clip: true; implicitHeight: contentHeight; spacing: 2; model: control.popup.visible ? control.delegateModel : null; currentIndex: control.highlightedIndex; ScrollIndicator.vertical: ScrollIndicator {} }
    }
}
