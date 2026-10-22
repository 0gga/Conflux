pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
import "." as Components
PopupButton {
    id: root
    required property string label
    required property var choices
    property var values: []
    property bool loaderNames: false
    property bool searchable: choices.length > 8
    signal selectionRequested(var selection)
    function formatValue(value) {
        if (value === "datapack") return "Data Pack"
        return loaderNames ? Branding.label(value) : value.charAt(0).toUpperCase()+value.slice(1)
    }
    popup: menu; compact: true
    text: label + (values.length === 1 ? ": " + formatValue(values[0]) : values.length > 1 ? ": " + values.length + " selected" : "")
    iconName: menu.visible ? "up" : "down"
    accented: values.length > 0
    font.weight: Font.Medium
    implicitWidth: Math.min(340, Math.max(110, filterLabel.implicitWidth + (values.length ? 96 : 54)))
    contentItem: RowLayout {
        spacing: 8
        Text { id: filterLabel; Layout.fillWidth: true; text: root.text; color: root.foreground; font: root.font; elide: Text.ElideRight; textFormat: Text.PlainText }
        ActionButton {
            objectName: "choiceFilterClear"
            visible: root.values.length > 0
            iconName: "close"; hint: "Clear " + root.label.toLowerCase(); selectionOutlineOnly: true; subtle: true; compact: true
            implicitWidth: 24; implicitHeight: 24
            onClicked: { root.selectionRequested([]); menu.close() }
        }
        Rectangle { visible: root.values.length > 0; Layout.preferredWidth: 1; Layout.preferredHeight: 18; color: Theme.colors.border }
        Icon { name: root.iconName; size: 15; color: root.foreground }
    }
    FocusPopup {
        id: menu; objectName: "choiceFilterMenu"
        parent: root; y: root.height+5; width: root.width; padding: 8; margins: 8
        onOpened: if (search.visible) search.forceActiveFocus()
        onClosed: search.text = ""
        background: Rectangle { radius: ControlStyle.panelRadius; color: Theme.colors.card; border.color: Theme.colors.border }
        contentItem: ColumnLayout {
            spacing: 6
            Components.SearchField {
                id: search; objectName: "choiceFilterSearch"
                visible: root.searchable; Layout.fillWidth: true
                placeholderText: "Search " + root.label.toLowerCase() + "..."; leadingIcon: "search"
            }
            RowLayout {
                Layout.fillWidth: true
                Text { objectName: "choiceFilterCount"; Layout.fillWidth: true; text: root.values.length + " selected"; color: Theme.colors.muted; font.pixelSize: 12; leftPadding: 8 }
                ActionButton { objectName: "choiceFilterMenuClear"; text: "Clear"; selectionOutlineOnly: true; subtle: true; compact: true; enabled: root.values.length>0; onClicked: root.selectionRequested([]) }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.colors.border }
            MenuList {
                id: options; objectName: "choiceFilterOptions"
                Layout.fillWidth: true; implicitHeight: Math.min(280,contentHeight); clip: true
                model: TextSearch.filter(root.choices,search.text)
                delegate: MenuOption {
                    id: option
                    required property string modelData
                    objectName: "releaseFilterOption-" + modelData
                    width: options.optionWidth; text: root.formatValue(modelData); compact: true; subtle: true; alignLeft: true
                    selected: root.values.indexOf(modelData)!==-1; selectionOutlineOnly: true
                    contentItem: RowLayout {
                        Text { Layout.fillWidth: true; text: option.text; color: option.foreground; font: option.font; elide: Text.ElideRight; textFormat: Text.PlainText }
                        Icon { name: "check"; visible: option.selected; size: 16; color: Theme.colors.accent }
                    }
                    onClicked: {
                        let next=root.values.slice(); const index=next.indexOf(modelData)
                        if(index===-1) next.push(modelData);else next.splice(index,1)
                        root.selectionRequested(next)
                    }
                }
            }
            Text { visible: options.count === 0; text: "No matching options"; color: Theme.colors.muted; font.pixelSize: 12; padding: 8 }
        }
    }
}
