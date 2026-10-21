pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import Conflux.Style
import "../components"
ColumnLayout {
    id: root; objectName: "themeSettings"; spacing: 12
    property string group: "surfaces"
    Text { objectName: "themeColorsHeading"; text: "Theme colors"; color: Theme.colors.text; font.pixelSize: 17; font.weight: Font.DemiBold }
    SelectBox { Layout.fillWidth: true; model: Theme.available; currentIndex: Math.max(0,Theme.available.indexOf(Theme.file)); onActivated: Theme.select(currentText) }
    RowLayout {
        Layout.fillWidth: true
        SearchField { id: themeName; objectName: "customThemeName"; Layout.fillWidth: true; placeholderText: "Name your theme"; maximumLength: 80 }
        ActionButton { text: "Save theme"; iconName: "save"; primary: true; enabled: themeName.text.trim().length>0; onClicked: Theme.saveCustom(themeName.text) }
    }
    Text { Layout.fillWidth: true; text: Theme.edited ? "Unsaved colors · live throughout Conflux" : "Active: " + Theme.name; color: Theme.colors.muted; font.pixelSize: 12; wrapMode: Text.WordWrap }
    RowLayout {
        ActionButton { text: "Discard edits"; enabled: Theme.edited; compact: true; subtle: true; onClicked: Theme.discardChanges() }
        ActionButton { text: "Candy red"; compact: true; onClicked: Theme.setColor("accent","#FF005E") }
    }
    SectionTabs {
        subtabs: true
        objectName: "themeColorTabs"; Layout.minimumWidth: 0; Layout.maximumWidth: root.width; current: root.group
        entries: [{key:"surfaces",label:"Surfaces"},{key:"text",label:"Text"},{key:"actions",label:"Actions"},{key:"status",label:"Status"}]
        onSelected: function(key) { root.group=key }
    }
    Repeater { model: Theme.colorGroups.filter(function(item){return item.key===root.group}); delegate: ThemeColorGroup { required property var modelData; Layout.fillWidth: true; group: modelData; onColorRequested: function(token) { picker.token=token;picker.selectedColor=Theme.colors[token];picker.open() } } }
    Text { visible: Theme.error.length>0; Layout.fillWidth: true; text: Theme.error; wrapMode: Text.WordWrap; color: Theme.colors.warning; font.pixelSize: 12 }
    RowLayout {
        ActionButton { text: "Reload theme"; compact: true; onClicked: Theme.reload() }
        ActionButton { text: "Themes folder"; compact: true; subtle: true; onClicked: Theme.openFolder() }
    }
    ColorDialog { id: picker; property string token: "accent"; title: "Choose " + token + " color"; onAccepted: Theme.setColor(token,selectedColor.toString()) }
}
