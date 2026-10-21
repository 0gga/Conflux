pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
import "../components"
Item {
    id: root; objectName: "appearanceSettings"
    property string section: "interface"
    readonly property bool previewVisible: !!Appearance.values.previewVisible
    readonly property string group: section === "content" ? "Content cards" : section === "filters" ? "Filters" : "Interface"
    readonly property var fields: Appearance.controls.filter(function(field) {
        const query=search.text.trim().toLowerCase()
        return field.group === root.group && (!query || field.label.toLowerCase().indexOf(query)!==-1)
    })
    function selectSection(key) { section=key; search.clear(); scroll.viewport.contentY=0 }
    ColumnLayout {
        anchors.fill: parent; spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: "Appearance"; color: Theme.colors.text; font.pixelSize: 22; font.weight: Font.DemiBold }
            ActionButton { objectName: "toggleAppearancePreview"; text: "Preview"; iconName: "image"; selected: root.previewVisible; compact: true; subtle: true; onClicked: Appearance.setValue("previewVisible",root.previewVisible?0:1) }
            ActionButton { objectName: "resetAppearanceSection"; text: "Reset section"; hint: "Restore this section's defaults"; compact: true; subtle: true; visible: root.section!=="colors"; onClicked: Appearance.resetGroup(root.group) }
        }
        SectionTabs {
            objectName: "appearanceTabs"; Layout.minimumWidth: 0; Layout.maximumWidth: root.width; current: root.section
            entries: [{key:"interface",label:"Interface"},{key:"content",label:"Content"},{key:"filters",label:"Filters"},{key:"colors",label:"Colors"}]
            onSelected: function(key) { root.selectSection(key) }
        }
        AppearancePreview { section: root.section; visible: root.previewVisible; Layout.fillWidth: true; Layout.preferredHeight: Math.max(140,Math.min(280,root.height*0.36)) }
        SearchField { id: search; objectName: "appearanceSearch"; visible: root.section!=="colors"; Layout.fillWidth: true; leadingIcon: "search"; placeholderText: "Find a setting in " + root.group.toLowerCase() + "…" }
        BoundedScrollView {
            id: scroll; objectName: "appearanceControls"; Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 80
            ColumnLayout {
                width: scroll.availableWidth; spacing: 10
                Repeater { model: root.section === "colors" ? [] : root.fields; delegate: AppearanceField { required property var modelData; Layout.fillWidth: true; field: modelData } }
                Text { visible: root.section!=="colors" && root.fields.length===0; Layout.fillWidth: true; text: "No matching settings"; color: Theme.colors.muted; font.pixelSize: 13; padding: 12 }
                ThemeSettings { visible: root.section==="colors"; Layout.fillWidth: true }
            }
        }
    }
}
