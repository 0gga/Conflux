pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "../components"
BoundedScrollView {
    id: root
    required property var catalogue
    property bool hasInstance: false
    property bool connectorAvailable: false
    readonly property real filterScale: Appearance.values.filterScale / 100
    function toggle(key,value) {
        let values=(catalogue.filters[key] || []).slice()
        const index=values.indexOf(value)
        if(index===-1)values.push(value);else values.splice(index,1)
        let change={};change[key]=values;catalogue.configure(change)
    }
    Item {
        width: root.availableWidth
        implicitHeight: filterBody.implicitHeight * root.filterScale
    ColumnLayout {
        id: filterBody
        width: parent.width / root.filterScale
        scale: root.filterScale; transformOrigin: Item.TopLeft
        spacing: Appearance.values.filterSpacing
        Text { text: "Filters"; color: Theme.colors.text; font.pixelSize: 18; font.weight: Font.DemiBold }
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: "Hide installed content"; wrapMode: Text.WordWrap; color: Theme.colors.text; font.pixelSize: 12 }
            ToggleSwitch { checked: !!root.catalogue.filters.hideInstalled; Accessible.name: "Hide installed content"; onToggled: root.catalogue.configure({hideInstalled:checked}) }
        }
        ActionButton { Layout.fillWidth: true; text: root.catalogue.filters.hidden ? "Showing hidden content" : "Show hidden content"; iconName: "hide"; selected: !!root.catalogue.filters.hidden; subtle: true; compact: true; onClicked: root.catalogue.configure({hidden:!root.catalogue.filters.hidden}) }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.colors.border }
        Text { text: "Game versions"; color: Theme.colors.text; font.pixelSize: 14; font.weight: Font.DemiBold }
        Item {
            Layout.fillWidth: true; implicitHeight: versionFlow.implicitHeight
            Flow {
                id: versionFlow; width: parent.width; spacing: 5
                Repeater { model: root.catalogue.filters.versions || []; delegate: ActionButton { required property string modelData; text: modelData; compact: true; pill: true; selected: true; enabled: !root.hasInstance || !!root.catalogue.filters.unlocked; hint: "Remove version"; onClicked: root.toggle("versions",modelData) } }
            }
        }
        ActionButton { visible: root.hasInstance; text: root.catalogue.filters.unlocked ? "Use instance version" : "Unlock game version"; iconName: "lock"; compact: true; onClicked: root.catalogue.configure({unlocked:!root.catalogue.filters.unlocked}) }
        SearchField { id: versionsSearch; visible: !root.hasInstance || !!root.catalogue.filters.unlocked; Layout.fillWidth: true; placeholderText: "Find versions..."; leadingIcon: "search" }
        Rectangle {
            visible: !root.hasInstance || !!root.catalogue.filters.unlocked
            Layout.fillWidth: true
            implicitHeight: Math.min(Appearance.values.versionListHeight, versionsList.contentHeight + 12)
            radius: 10; color: Theme.colors.input
            ContentListView {
                id: versionsList; outsideColor: Theme.colors.input; cornerRadius: 6; objectName: "browseVersionList"
                anchors.fill: parent; anchors.margins: 6
                clip: true; spacing: 3
                model: TextSearch.filter(root.catalogue.options.versions || [],versionsSearch.text)

                delegate: ActionButton {
                    required property string modelData
                    width: versionsList.viewportWidth
                    text: modelData; compact: true; subtle: true; alignLeft: true
                    selected: (root.catalogue.filters.versions || []).indexOf(modelData)!==-1
                    iconName: selected ? "check" : ""
                    Accessible.description: selected ? "Selected game version" : "Select game version"
                    onClicked: root.toggle("versions",modelData)
                }
            }
        }
        ActionButton { visible: !root.hasInstance || !!root.catalogue.filters.unlocked; text: "Clear versions"; compact: true; subtle: true; onClicked: root.catalogue.configure({versions:[]}) }
        Text { visible: root.catalogue.filters.type === "mod"; text: "Loaders"; color: Theme.colors.text; font.pixelSize: 14; font.weight: Font.DemiBold }
        Repeater {
            model: ["fabric","forge","neoforge","quilt"]
            delegate: RowLayout {
                required property string modelData
                visible: root.catalogue.filters.type === "mod"
                Layout.fillWidth: true
                SelectionBox { checked: (root.catalogue.filters.loaders || []).indexOf(parent.modelData)!==-1; Accessible.name: Branding.label(parent.modelData); onClicked: root.toggle("loaders",parent.modelData) }
                BrandBadge { brand: parent.modelData }
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.colors.border }
        Text { text: "Tags"; color: Theme.colors.text; font.pixelSize: 14; font.weight: Font.DemiBold }
        SearchField { id: tagSearch; Layout.fillWidth: true; placeholderText: "Find tags..."; leadingIcon: "search" }
        Repeater {
            model: (root.catalogue.options.tags || []).concat(root.catalogue.options.curseforgeTags || []).filter(function(tag) { return (root.catalogue.filters.source === "merged" || tag.provider === root.catalogue.filters.source) && (tag.type===root.catalogue.filters.type || (root.catalogue.filters.type==="datapack" && tag.type==="mod")) && TextSearch.matches(tag.title,tagSearch.text) })
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                SelectionBox { checked: (root.catalogue.filters.tags || []).indexOf(parent.modelData.key)!==-1; Accessible.name: parent.modelData.title; onClicked: root.toggle("tags",parent.modelData.key) }
                Text { Layout.fillWidth: true; text: parent.modelData.title.charAt(0).toUpperCase()+parent.modelData.title.slice(1).replace(/-/g," ") + (parent.modelData.provider === "curseforge" ? " (CurseForge)" : ""); color: Theme.colors.muted; font.pixelSize: 12; wrapMode: Text.WordWrap }
            }
        }
        ActionButton { text: "Clear tags"; compact: true; subtle: true; visible: (root.catalogue.filters.tags || []).length>0; onClicked: root.catalogue.configure({tags:[]}) }
    }
    }
}
