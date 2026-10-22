pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "." as Components
PopupButton {
    id: root
    required property var choices
    property string value: ""
    property string label: "Option"
    property bool brandNames: false
    property bool searchable: true
    signal selectionRequested(string value)
    readonly property var filteredChoices: TextSearch.filter(choices, search.text)
    popup: menu
    text: brandNames ? Branding.label(value) : value
    Accessible.name: label + ": " + text
    contentItem: RowLayout {
        spacing: 10
        BrandBadge { visible: root.brandNames; brand: root.value; iconOnly: true; framed: false; tooltipsEnabled: false }
        Text { Layout.fillWidth: true; text: root.text; font: root.font; color: root.foreground; elide: Text.ElideRight; textFormat: Text.PlainText }
        Icon { name: menu.visible ? "up" : "down"; size: 16 }
    }
    FocusPopup {
        id: menu; objectName: "searchableSelectMenu"
        parent: root; y: root.height + 6; width: root.width; padding: 6
        onOpened: { options.currentIndex = Math.max(0, root.filteredChoices.indexOf(root.value)); if (root.searchable) search.forceActiveFocus(); else options.forceActiveFocus() }
        onClosed: search.text = ""
        background: Rectangle { radius: ControlStyle.panelRadius; color: Theme.colors.card; border.color: Theme.colors.border }
        contentItem: ColumnLayout {
            spacing: 6
            Components.SearchField {
                id: search; objectName: "searchableSelectSearch"
                Layout.fillWidth: true; visible: root.searchable
                leadingIcon: "search"; placeholderText: "Search " + root.label.toLowerCase() + "…"
                onTextChanged: options.currentIndex = 0
                Keys.onDownPressed: options.currentIndex = Math.min(root.filteredChoices.length-1,options.currentIndex+1)
                Keys.onUpPressed: options.currentIndex = Math.max(0,options.currentIndex-1)
                onAccepted: if (root.filteredChoices.length) { root.selectionRequested(root.filteredChoices[Math.max(0, options.currentIndex)]); menu.close(); root.forceActiveFocus() }
            }
            MenuList {
                id: options; objectName: "searchableSelectOptions"
                Layout.fillWidth: true; implicitHeight: Math.min(250,contentHeight)
                clip: true; boundsBehavior: Flickable.StopAtBounds
                Keys.onReturnPressed: if (root.filteredChoices.length) { root.selectionRequested(root.filteredChoices[Math.max(0,currentIndex)]); menu.close(); root.forceActiveFocus() }
                Keys.onEnterPressed: if (root.filteredChoices.length) { root.selectionRequested(root.filteredChoices[Math.max(0,currentIndex)]); menu.close(); root.forceActiveFocus() }
                model: root.filteredChoices
                delegate: MenuOption {
                    id: option; required property string modelData; required property int index
                    objectName: "searchableSelectOption-" + modelData
                    width: options.optionWidth; text: root.brandNames ? Branding.label(modelData) : modelData
                    compact: true; subtle: true; alignLeft: true; selectionOutlineOnly: true
                    selected: root.value === modelData
                    contentItem: RowLayout {
                        spacing: 10
                        BrandBadge { visible: root.brandNames; brand: option.modelData; framed: false; iconOnly: true; tooltipsEnabled: false }
                        Text { Layout.fillWidth: true; text: option.text; color: option.foreground; font: option.font; elide: Text.ElideRight; textFormat: Text.PlainText }
                        Icon { name: "check"; size: 16; color: Theme.colors.accent; visible: option.selected }
                    }
                    onClicked: { root.selectionRequested(modelData); menu.close(); root.forceActiveFocus() }
                }
            }
            Text { visible: options.count === 0; text: "No matching versions"; color: Theme.colors.muted; font.pixelSize: 12; padding: 8 }
        }
    }
}
