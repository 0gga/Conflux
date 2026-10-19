pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import Conflux.Style
ComboBox {
    id: control
    implicitHeight: 44; implicitWidth: 160; font.pixelSize: 13
    background: Rectangle { radius: 12; color: control.hovered ? Theme.colors.hover : Theme.colors.input; border.color: control.activeFocus ? Theme.colors.accent : "transparent" }
    contentItem: Text { text: control.displayText; color: Theme.colors.text; font: control.font; verticalAlignment: Text.AlignVCenter; leftPadding: 14; rightPadding: 36; elide: Text.ElideRight }
    indicator: Icon { x: control.width - width - 13; y: (control.height - height) / 2; name: "down"; size: 16 }
    delegate: ItemDelegate {
        id: option
        required property var modelData
        required property int index
        width: control.width - 12; text: String(modelData); highlighted: control.highlightedIndex === index
        contentItem: Text { text: option.text; color: Theme.colors.text; font.pixelSize: 13; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { radius: 8; color: option.highlighted ? Theme.colors.selection : "transparent" }
    }
    popup: Popup {
        y: control.height + 6; width: control.width; padding: 6; implicitHeight: Math.min(list.contentHeight + 12, 280)
        background: Rectangle { radius: 12; color: Theme.colors.card; border.color: Theme.colors.border }
        contentItem: ListView { id: list; clip: true; implicitHeight: contentHeight; model: control.popup.visible ? control.delegateModel : null; currentIndex: control.highlightedIndex; ScrollIndicator.vertical: ScrollIndicator {} }
    }
}
