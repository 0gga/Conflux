pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "../components"
import "../showcase"

Item {
    id: root
    required property var browse
    required property var app
    required property var showcase
    required property var contentController
    property string tab: "description"
    readonly property bool hasGallery: (showcase.gallery || []).length > 0
    readonly property string displayedTab: tab === "gallery" && !hasGallery ? "description" : tab
    function restoreAvailableTab() {
        if (showcase && tab === "gallery" && !hasGallery && !showcase.busy)
            Qt.callLater(function() { if (root.showcase && root.tab === "gallery" && !root.hasGallery && !root.showcase.busy) root.tabRequested("description") })
    }
    onTabChanged: restoreAvailableTab()
    Component.onCompleted: restoreAvailableTab()
    Connections { target: root.showcase; function onChanged() { root.restoreAvailableTab() } }
    onVisibleChanged: if (!visible) enlarged.close()
    required property var releaseFilters
    signal tabRequested(string key)
    readonly property var versions: releaseFilters.rows
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 28; spacing: 18
        RowLayout {
            Layout.fillWidth: true; spacing: 16
            ContentIcon { source: root.showcase.project.icon || ""; kind: "mod"; size: 64 }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 8
                Text { Layout.fillWidth: true; text: root.showcase.project.title || "Mod showcase"; color: Theme.colors.text; font.pixelSize: 29; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
                RowLayout { Repeater { model: root.showcase.project.sources || []; delegate: SourceBadge { required property string modelData; source: modelData; iconOnly: true } } }
                Text { visible: !!root.showcase.project.summary; Layout.fillWidth: true; text: root.showcase.project.summary || ""; color: Theme.colors.muted; font.pixelSize: 13; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
            }
            BusyIndicator { visible: root.showcase.busy; running: visible; Layout.preferredWidth: 32; Layout.preferredHeight: 32 }
            ActionButton { text: "View project page"; subtle: true; visible: !!root.showcase.project.url; onClicked: root.app.openWeb(root.showcase.project.url) }
        }
        SectionTabs { objectName: "showcaseTabs"; entries: [{key:"description",label:"Description",icon:"info"}].concat(root.hasGallery ? [{key:"gallery",label:"Gallery",icon:"image"}] : []).concat([{key:"versions",label:"Versions",icon:"versions"}]); current: root.displayedTab; onSelected: function(key) { root.tabRequested(key) } }
        Text { Layout.fillWidth: true; visible: root.showcase.notice.length > 0; text: root.showcase.notice; color: Theme.colors.warning; font.pixelSize: 12; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
        Text { Layout.fillWidth: true; visible: root.browse.notice.length > 0; text: root.browse.notice; color: Theme.colors.warning; font.pixelSize: 12; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
        Text { Layout.fillWidth: true; visible: root.contentController.notice.length > 0; text: root.contentController.notice; color: Theme.colors.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
        StackLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            currentIndex: ["description","gallery","versions"].indexOf(root.displayedTab)
            BoundedScrollView {
                id: descriptionScroll; clip: true
                DescriptionView {
                    width: descriptionScroll.availableWidth; blocks: root.showcase.descriptionBlocks || []
                    onLinkActivated: function(url) { root.app.openWeb(url) }
                    onImageRequested: function(url) { enlarged.show([url],0) }
                }
            }
            Item {
                GalleryView { anchors.fill: parent; images: root.showcase.gallery; onImageRequested: function(index) { enlarged.show(root.showcase.gallery,index) } }
            }
            ColumnLayout {
                spacing: 14
                Flow {
                    Layout.fillWidth: true; spacing: 6
                    ChoiceFilter { objectName: "versionChannelFilter"; label: "Channel"; choices: root.releaseFilters.options.channels || []; values: root.releaseFilters.filters.channels; onSelectionRequested: function(selection) { root.releaseFilters.configure({channels:selection}) } }
                    ChoiceFilter { searchable: true; label: "Game version"; choices: root.releaseFilters.options.versions || []; values: root.releaseFilters.filters.versions; onSelectionRequested: function(selection) { root.releaseFilters.configure({versions:selection}) } }
                    ChoiceFilter { objectName: "versionPlatformFilter"; visible: (root.releaseFilters.options.loaders || []).length > 0; label: "Platform"; loaderNames: true; choices: root.releaseFilters.options.loaders || []; values: root.releaseFilters.filters.loaders; onSelectionRequested: function(selection) { root.releaseFilters.configure({loaders:selection}) } }
                    SelectBox { implicitHeight: 36; model: ["All sources", "Modrinth", "CurseForge"]; currentIndex: ["all","modrinth","curseforge"].indexOf(root.releaseFilters.filters.source); onActivated: root.releaseFilters.configure({source:["all","modrinth","curseforge"][currentIndex]}) }
                    ActionButton { text: "Clear"; subtle: true; compact: true; onClicked: root.releaseFilters.clear() }
                }
                Text { visible: !root.showcase.busy && root.versions.length === 0; text: "No matching versions"; color: Theme.colors.muted; font.pixelSize: 13 }
                ContentListView {
                    id: versionList; Layout.fillWidth: true; Layout.fillHeight: true; spacing: 10; clip: true; model: root.versions;
                    delegate: Rectangle {
                        id: version; required property var modelData
                        property int variantIndex: 0
                        readonly property var release: modelData.variants[Math.min(variantIndex,modelData.variants.length - 1)] || ({})
                        readonly property bool installed: { const ready=root.browse.ready; const busy=root.browse.busy; return root.browse.installed(release,root.showcase.project.type || "mod") }
                        readonly property bool groupInstalled: { const ready=root.browse.ready; const busy=root.browse.busy; return modelData.variants.some(function(item) { return root.browse.installed(item,root.showcase.project.type || "mod") }) }
                        width: versionList.viewportWidth; height: version.modelData.variants.length > 1 ? 148 : 112; radius: 14; color: Theme.colors.card; border.color: Theme.colors.border
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 16; spacing: 16
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 8
                                Text { Layout.fillWidth: true; text: version.modelData.label; color: Theme.colors.text; font.pixelSize: 15; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
                                CompatibilityBadges { minecraft: (version.release.gameVersions || []).filter(function(value) { return root.releaseFilters.filters.versions.indexOf(value)!==-1 })[0] || (version.release.gameVersions || [])[0] || ""; loader: (version.release.loaders || []).filter(function(value) { return root.releaseFilters.filters.loaders.indexOf(value)!==-1 })[0] || (version.release.loaders || []).filter(function(value) { return value !== "minecraft" && value !== "datapack" })[0] || "" }
                                Text { Layout.fillWidth: true; text: (version.release.channel || "release").charAt(0).toUpperCase() + (version.release.channel || "release").slice(1) + (version.release.published ? " · " + version.release.published : ""); color: Theme.colors.muted; font.pixelSize: 11; elide: Text.ElideRight }
                                SelectBox { visible: version.modelData.variants.length > 1; Layout.fillWidth: true; model: version.modelData.variants.map(function(row) { return (row.provider === "modrinth" ? "Modrinth" : "CurseForge") + " · " + row.filename }); onActivated: version.variantIndex = currentIndex }
                            }
                            SourceBadge { source: version.release.provider || "modrinth"; iconOnly: true }
                            Text { visible: version.groupInstalled && !version.installed; text: "Installed"; color: Theme.colors.accent; font.pixelSize: 12 }
                            ActionButton { text: version.installed ? "Installed" : "Install version"; iconName: version.installed ? "check" : "plus"; primary: !version.installed; visible: version.release.provider === "modrinth"; enabled: root.browse.ready && !root.browse.busy && !root.app.navigationBusy && !root.browse.state(root.showcase.project).locked && !version.installed; onClicked: root.browse.installVersion(version.release,root.showcase.project) }
                            ActionButton { text: "View release"; onClicked: root.app.openWeb(version.release.pageUrl) }
                        }
                    }
                }
            }
        }
    }
    GalleryViewer { id: enlarged; onExternalRequested: function(url) { root.app.openWeb(url) } }
}
