pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import Conflux.Style
import "../components"
Item {
    id: root
    ColumnLayout {
        anchors.fill: parent; spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: "Appearance"; color: Theme.colors.text; font.pixelSize: 20; font.weight: Font.DemiBold }
            ActionButton { text: "Reset sizes"; compact: true; subtle: true; onClicked: Appearance.reset() }
        }
        Text { text: "Live preview · example content"; color: Theme.colors.muted; font.pixelSize: 11 }
        AppearancePreview { Layout.fillWidth: true; Layout.preferredHeight: Math.max(100,Math.min(250,root.height*0.32)) }
        BoundedScrollView {
            id: scroll; objectName: "appearanceControls"; Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 80
            ColumnLayout {
                width: scroll.availableWidth; spacing: 18
                Repeater {
                    model: ["Interface","Content cards","Filters"]
                    delegate: ColumnLayout {
                        id: metricGroup
                        required property string modelData
                        Layout.fillWidth: true; spacing: 8
                        Text { text: parent.modelData; color: Theme.colors.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                        Repeater { model: Appearance.controls.filter(function(field){return field.group===metricGroup.modelData}); delegate: AppearanceMetric { required property var modelData; Layout.fillWidth: true; field: modelData } }
                    }
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.colors.border }
                Text { objectName: "themeColorsHeading"; text: "Theme colors"; color: Theme.colors.text; font.pixelSize: 17; font.weight: Font.DemiBold }
                SelectBox { Layout.fillWidth: true; model: Theme.available; currentIndex: Math.max(0,Theme.available.indexOf(Theme.file)); onActivated: Theme.select(currentText) }
                RowLayout {
                    Layout.fillWidth: true
                    SearchField { id: themeName; objectName: "customThemeName"; Layout.fillWidth: true; placeholderText: "Name your theme"; maximumLength: 80 }
                    ActionButton { text: "Save theme"; iconName: "save"; primary: true; enabled: themeName.text.trim().length>0; onClicked: Theme.saveCustom(themeName.text) }
                }
                Text { Layout.fillWidth: true; text: Theme.edited ? "Unsaved colors · previewing throughout Conflux" : "Active: " + Theme.name; color: Theme.colors.muted; font.pixelSize: 12; wrapMode: Text.WordWrap }
                RowLayout {
                    ActionButton { text: "Discard color edits"; enabled: Theme.edited; compact: true; subtle: true; onClicked: Theme.discardChanges() }
                    ActionButton { text: "Candy red"; compact: true; onClicked: Theme.setColor("accent","#FF005E") }
                }
                Repeater { model: Theme.colorGroups; delegate: ThemeColorGroup { required property var modelData; Layout.fillWidth: true; group: modelData; onColorRequested: function(token) { picker.token=token;picker.selectedColor=Theme.colors[token];picker.open() } } }
                Text { visible: Theme.error.length>0; Layout.fillWidth: true; text: Theme.error; wrapMode: Text.WordWrap; color: Theme.colors.warning; font.pixelSize: 12 }
                RowLayout {
                    ActionButton { text: "Reload theme"; compact: true; onClicked: Theme.reload() }
                    ActionButton { text: "Themes folder"; compact: true; subtle: true; onClicked: Theme.openFolder() }
                }
            }
        }
    }
    ColorDialog { id: picker; property string token: "accent"; title: "Choose " + token + " color"; onAccepted: Theme.setColor(token,selectedColor.toString()) }
}
