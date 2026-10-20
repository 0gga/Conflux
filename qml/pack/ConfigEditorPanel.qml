pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "../components"
Item {
    id: root
    required property var editor
    property string query: ""
    signal closeRequested()
    ColumnLayout {
        anchors.fill: parent; spacing: 16
        RowLayout {
            Layout.fillWidth: true; spacing: 12
            ActionButton { iconName: "back"; hint: "Back to configs"; subtle: true; onClicked: root.closeRequested() }
            ContentIcon { source: root.editor.icon; kind: "mod" }
            ColumnLayout { Layout.fillWidth: true; spacing: 5
                Text { Layout.fillWidth: true; text: root.editor.title || "Config editor"; color: Theme.colors.text; font.pixelSize: 18; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
                Text { Layout.fillWidth: true; text: root.editor.path; color: Theme.colors.muted; font.pixelSize: 11; elide: Text.ElideMiddle; textFormat: Text.PlainText }
            }
            ActionButton { iconName: "refresh"; hint: "Reload from disk"; subtle: true; enabled: !root.editor.dirty && !root.editor.busy; onClicked: root.editor.reload() }
            ActionButton { text: "Discard"; subtle: true; enabled: root.editor.dirty && !root.editor.busy; onClicked: root.editor.discard() }
            ActionButton { text: root.editor.busy ? "Working…" : "Save config"; iconName: "save"; primary: true; enabled: root.editor.dirty && !root.editor.busy; onClicked: root.editor.save() }
        }
        Text { Layout.fillWidth: true; text: root.editor.association + ". Review the mod and file before saving. Changes apply when the mod next reads this config."; color: Theme.colors.muted; font.pixelSize: 11; wrapMode: Text.WordWrap }
        SearchField { Layout.fillWidth: true; leadingIcon: "search"; placeholderText: "Find a setting…"; onTextChanged: root.query = text }
        Text { visible: root.editor.error.length > 0; Layout.fillWidth: true; text: root.editor.error; color: Theme.colors.danger; font.pixelSize: 12; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
        Text { visible: root.editor.status.length > 0; Layout.fillWidth: true; text: root.editor.status; color: Theme.colors.accent; font.pixelSize: 12; wrapMode: Text.WordWrap }
        Text { visible: root.editor.unsupported > 0; Layout.fillWidth: true; text: root.editor.unsupported + " complex or unsupported values are preserved and omitted from this form."; color: Theme.colors.muted; font.pixelSize: 11; wrapMode: Text.WordWrap }
        ListView { boundsBehavior: Flickable.StopAtBounds;
            id: list; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 10
            model: root.editor.fields.filter(function(field) { return (field.label + " " + field.section + " " + field.description).toLowerCase().indexOf(root.query.toLowerCase()) !== -1 })
            ScrollBar.vertical: ScrollBar {}
            section.property: "section"; section.criteria: ViewSection.FullString
            section.delegate: Text { required property string section; width: list.width; text: section; color: Theme.colors.muted; font.pixelSize: 11; font.weight: Font.DemiBold; topPadding: 12; bottomPadding: 8; textFormat: Text.PlainText }
            delegate: ConfigField { required property var modelData; width: list.width; field: Object.assign({},modelData,{value:root.editor.fieldValue(modelData.id)}); enabled: !root.editor.busy; onValueChanged: function(value) { root.editor.setField(modelData.id, value) } }
        }
    }
}
