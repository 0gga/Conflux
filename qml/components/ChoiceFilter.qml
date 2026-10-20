pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "." as Components
PopupButton {
    id: root
    required property string label
    required property var choices
    property var values: []
    property bool loaderNames: false
    signal selectionRequested(var selection)
    function formatValue(value) { return loaderNames ? Branding.label(value) : value.charAt(0).toUpperCase()+value.slice(1) }
    popup: menu; compact: true
    text: label + (values.length ? ": " + values.map(function(value) { return root.formatValue(value) }).join(", ") : "")
    iconName: menu.visible ? "up" : "down"
    accented: values.length > 0
    implicitWidth: Math.min(300, Math.max(100,filterLabel.implicitWidth+58))
    hint: text
    contentItem: RowLayout {
        spacing: 8
        Text { id: filterLabel; Layout.fillWidth: true; text: root.text; color: root.foreground; font: root.font; elide: Text.ElideRight; textFormat: Text.PlainText }
        Icon { name: root.iconName; size: 15; color: root.foreground }
    }
    FocusPopup {
        id: menu; parent: root; y: root.height+5; width: Math.max(220,Math.min(340,root.width)); padding: 6; margins: 8
        background: Rectangle { radius: 12; color: Theme.colors.card; border.color: Theme.colors.border }
        contentItem: ColumnLayout {
            spacing: 5
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; text: root.values.length + " selected"; color: Theme.colors.muted; font.pixelSize: 12; leftPadding: 8 }
                ActionButton { text: "Clear"; subtle: true; compact: true; enabled: root.values.length>0; onClicked: root.selectionRequested([]) }
            }
            Components.SearchField { id: search; visible: root.choices.length>8; Layout.fillWidth: true; placeholderText: "Search..."; leadingIcon: "search" }
            ListView {
                Layout.fillWidth: true; implicitHeight: Math.min(280,contentHeight); clip: true; boundsBehavior: Flickable.StopAtBounds
                model: root.choices.filter(function(value) {return root.formatValue(value).toLowerCase().indexOf(search.text.toLowerCase())!==-1})
                ScrollBar.vertical: ScrollBar {}
                delegate: ActionButton {
                    required property string modelData
                    objectName: "releaseFilterOption-" + modelData
                    width: ListView.view.width; text: root.formatValue(modelData); compact: true; subtle: true; alignLeft: true
                    selected: root.values.indexOf(modelData)!==-1; iconName: selected ? "check" : ""
                    onClicked: {
                        let next=root.values.slice(); const index=next.indexOf(modelData)
                        if(index===-1) next.push(modelData);else next.splice(index,1)
                        root.selectionRequested(next)
                    }
                }
            }
        }
    }
}
