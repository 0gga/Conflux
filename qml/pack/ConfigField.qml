pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "../components"
Rectangle {
    id: root
    required property var field
    signal valueChanged(var value)
    property var items: { try { return JSON.parse(field.value) } catch (error) { return [] } }
    function updateItem(index, value) { items[index] = value; valueChanged(JSON.stringify(items)) }
    implicitHeight: body.implicitHeight + 32; radius: 14; color: Theme.colors.card; border.color: Theme.colors.border
    ColumnLayout {
        id: body; anchors.fill: parent; anchors.margins: 16; spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: root.field.label.replace(/[_-]+/g," "); color: Theme.colors.text; font.pixelSize: 14; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
            Text { text: root.field.type; color: Theme.colors.muted; font.pixelSize: 10 }
            ToggleSwitch { visible: root.field.type === "boolean"; checked: !!root.field.value; Accessible.name: root.field.label; onToggled: root.valueChanged(checked) }
        }
        Text { visible: !!root.field.description; Layout.fillWidth: true; text: root.field.description || ""; color: Theme.colors.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
        SelectBox { visible: !!root.field.choices; Layout.fillWidth: true; model: root.field.choices || []; currentIndex: Math.max(0, model.indexOf(root.field.value)); onActivated: root.valueChanged(currentText) }
        SearchField {
            id: valueInput; visible: root.field.type !== "boolean" && root.field.type !== "list" && !root.field.choices
            Layout.fillWidth: true; text: String(root.field.value); Accessible.name: root.field.label
            onTextEdited: root.valueChanged(text)
        }
        Slider {
            id: rangeSlider
            visible: root.field.minimum !== undefined && root.field.maximum !== undefined && (root.field.type === "number" || root.field.type === "integer")
            Layout.fillWidth: true; from: root.field.minimum || 0; to: root.field.maximum || 1; value: Number(valueInput.text); stepSize: root.field.type === "integer" ? 1 : 0
            background: Rectangle { x: rangeSlider.leftPadding; y: rangeSlider.topPadding + (rangeSlider.availableHeight - height) / 2; width: rangeSlider.availableWidth; height: 5; radius: 3; color: Theme.colors.input; Rectangle { width: rangeSlider.visualPosition * parent.width; height: parent.height; radius: 3; color: Theme.colors.accent } }
            handle: Rectangle { x: rangeSlider.leftPadding + rangeSlider.visualPosition * (rangeSlider.availableWidth - width); y: rangeSlider.topPadding + (rangeSlider.availableHeight - height) / 2; implicitWidth: 18; implicitHeight: 18; radius: 9; color: Theme.colors.accent; border.width: rangeSlider.visualFocus ? 2 : 0; border.color: Theme.colors.text }
            Accessible.name: root.field.label; onMoved: { valueInput.text = String(root.field.type === "integer" ? Math.round(value) : Number(value.toFixed(6))); root.valueChanged(valueInput.text) }
        }
        ColumnLayout {
            visible: root.field.type === "list"; Layout.fillWidth: true; spacing: 8
            Repeater {
                model: root.items
                delegate: RowLayout {
                    id: listEntry; required property var modelData; required property int index; Layout.fillWidth: true
                    ToggleSwitch { visible: typeof listEntry.modelData === "boolean"; checked: !!listEntry.modelData; onToggled: root.updateItem(listEntry.index, checked) }
                    SearchField { visible: typeof listEntry.modelData !== "boolean"; Layout.fillWidth: true; text: String(listEntry.modelData); Accessible.name: root.field.label + " item " + listEntry.index
                        onTextEdited: { const number = Number(text); if (typeof listEntry.modelData === "number" && (!text.length || !Number.isFinite(number))) root.valueChanged("Invalid number in list"); else root.updateItem(listEntry.index, typeof listEntry.modelData === "number" ? number : text) }
                    }
                    Item { visible: typeof listEntry.modelData === "boolean"; Layout.fillWidth: true }
                    ActionButton { iconName: "close"; hint: "Remove item"; subtle: true; onClicked: { const updated = root.items.slice(); updated.splice(listEntry.index,1); root.items = updated; root.valueChanged(JSON.stringify(updated)) } }
                }
            }
            ActionButton { iconName: "plus"; text: "Add item"; subtle: true; onClicked: { const updated = root.items.slice(); updated.push(typeof updated[0] === "number" ? 0 : typeof updated[0] === "boolean" ? false : ""); root.items = updated; root.valueChanged(JSON.stringify(updated)) } }
        }
    }
}
