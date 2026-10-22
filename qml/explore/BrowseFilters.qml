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
        Text { Layout.maximumWidth: filterBody.width; elide: Text.ElideRight; text: "Filters"; color: Theme.colors.text; font.pixelSize: 18; font.weight: Font.DemiBold }
        RowLayout {
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: filterBody.width
            Text { Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: filterBody.width; text: "Hide installed content"; wrapMode: Text.WordWrap; color: Theme.colors.text; font.pixelSize: 12 }
            ToggleSwitch { objectName: "browseHideInstalled"; enabled: !root.catalogue.filters.hidden; checked: root.catalogue.hideInstalledActive; Accessible.name: "Hide installed content"; onToggled: root.catalogue.configure({hideInstalled:checked}) }
        }
        ActionButton { objectName: "browseShowHidden"; Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: filterBody.width; text: root.catalogue.filters.hidden ? "Showing hidden content" : "Show hidden content"; iconName: "hide"; selected: !!root.catalogue.filters.hidden; subtle: true; compact: true; onClicked: root.catalogue.configure({hidden:!root.catalogue.filters.hidden}) }
        Rectangle { Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: filterBody.width; implicitHeight: 1; color: Theme.colors.border }
        Text { Layout.maximumWidth: filterBody.width; elide: Text.ElideRight; text: "Game versions"; color: Theme.colors.text; font.pixelSize: 14; font.weight: Font.DemiBold }
        Item {
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: filterBody.width; implicitHeight: versionFlow.implicitHeight
            Flow {
                id: versionFlow; width: parent.width; spacing: 5
                Repeater { model: root.catalogue.filters.versions || []; delegate: ActionButton { required property string modelData; text: modelData; compact: true; pill: true; selected: true; enabled: !root.hasInstance || !!root.catalogue.filters.unlocked; hint: "Remove version"; onClicked: root.toggle("versions",modelData) } }
            }
        }
        ActionButton { Layout.maximumWidth: filterBody.width; visible: root.hasInstance; text: root.catalogue.filters.unlocked ? "Use instance version" : "Unlock game version"; iconName: "lock"; compact: true; onClicked: root.catalogue.configure({unlocked:!root.catalogue.filters.unlocked}) }
        SearchField { id: versionsSearch; visible: !root.hasInstance || !!root.catalogue.filters.unlocked; Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: filterBody.width; placeholderText: "Find versions..."; leadingIcon: "search" }
        Rectangle {
            visible: !root.hasInstance || !!root.catalogue.filters.unlocked
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: filterBody.width
            implicitHeight: Math.min(Appearance.values.versionListHeight, versionsList.contentHeight + 12)
            radius: ControlStyle.controlRadius; color: Theme.colors.input
            ContentListView {
                id: versionsList; outsideColor: Theme.colors.input; cornerRadius: ControlStyle.controlRadius; objectName: "browseVersionList"
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
        ActionButton { Layout.maximumWidth: filterBody.width; visible: !root.hasInstance || !!root.catalogue.filters.unlocked; text: "Clear versions"; compact: true; subtle: true; onClicked: root.catalogue.configure({versions:[]}) }
        Text { visible: root.catalogue.filters.type === "mod"; text: "Loaders"; color: Theme.colors.text; font.pixelSize: 14; font.weight: Font.DemiBold }
        Repeater {
            model: ["fabric","forge","neoforge","quilt"]
            delegate: RowLayout {
                required property string modelData
                visible: root.catalogue.filters.type === "mod"
                Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: filterBody.width
                SelectionBox { checked: (root.catalogue.filters.loaders || []).indexOf(parent.modelData)!==-1; Accessible.name: Branding.label(parent.modelData); onClicked: root.toggle("loaders",parent.modelData) }
                BrandBadge { objectName: "browseLoader_" + brand; brand: parent.modelData; TapHandler { onTapped: root.toggle("loaders",parent.brand) } HoverHandler { cursorShape: Qt.PointingHandCursor } }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: filterBody.width; implicitHeight: 1; color: Theme.colors.border }
        Text { Layout.maximumWidth: filterBody.width; elide: Text.ElideRight; text: "Tags"; color: Theme.colors.text; font.pixelSize: 14; font.weight: Font.DemiBold }
        SearchField { id: tagSearch; Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: filterBody.width; placeholderText: "Find tags..."; leadingIcon: "search" }
        Repeater {
            model: (root.catalogue.options.tags || []).concat(root.catalogue.options.curseforgeTags || []).filter(function(tag) { return (root.catalogue.filters.source === "merged" || tag.provider === root.catalogue.filters.source) && (tag.type===root.catalogue.filters.type || (root.catalogue.filters.type==="datapack" && tag.type==="mod")) && TextSearch.matches(tag.title,tagSearch.text) })
            delegate: RowLayout {
                id: tagOption
                required property var modelData
                Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: filterBody.width
                SelectionBox { checked: (root.catalogue.filters.tags || []).indexOf(parent.modelData.key)!==-1; Accessible.name: parent.modelData.title; onClicked: root.toggle("tags",parent.modelData.key) }
                Text {
                    objectName: "browseTag_" + tagOption.modelData.key
                    Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: filterBody.width
                    text: tagOption.modelData.title.charAt(0).toUpperCase() + tagOption.modelData.title.slice(1).replace(/-/g," ") + (tagOption.modelData.provider === "curseforge" ? " (CurseForge)" : "")
                    color: Theme.colors.muted; font.pixelSize: 12; wrapMode: Text.WordWrap
                    TapHandler { onTapped: root.toggle("tags",tagOption.modelData.key) }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
            }
        }
        ActionButton { Layout.maximumWidth: filterBody.width; text: "Clear tags"; compact: true; subtle: true; visible: (root.catalogue.filters.tags || []).length>0; onClicked: root.catalogue.configure({tags:[]}) }
    }
    }
}
