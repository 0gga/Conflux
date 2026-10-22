pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "../components"
import "../explore"
Item {
    id: root
    objectName: "exploreContentPage"
    required property var app
    required property var content
    required property var browse
    required property var catalogue
    required property var worlds
    property string targetPath: ""
    property int scrollRevision: -1
    readonly property string contentType: catalogue.filters.type
    // Narrow notification dependency: result/busy/filter-map updates must not replace an active draft.
    readonly property string committedQuery: catalogue.filters.query || ""
    readonly property var instances: app.libraryPacks.filter(function(pack) { return !pack.hidden })
    readonly property real filterPanelWidth: Appearance.filterWidthFor(width - 56) * Appearance.values.filterScale / 100
    readonly property bool compactFilters: width - 56 < 720 + filterPanelWidth + 18
    onCompactFiltersChanged: filterPopup.close()
    signal createRequested()
    signal workspaceRequested()
    signal showcaseRequested(var project, string tab)
    signal authorRequested(var author)
    signal instanceRequested(string path)
    function focusSearch() { query.forceActiveFocus() }
    function restoreScroll() {
        if (!visible || catalogue.busy || results.moving) return
        results.contentY=Math.max(0,Math.min(catalogue.scrollPosition,results.contentHeight-results.height))
    }
    onVisibleChanged: { if (visible) Qt.callLater(restoreScroll); else { debounce.stop(); results.stopScrolling() } }
    onTargetPathChanged: { debounce.stop(); results.stopScrolling() }
    Component.onCompleted: catalogue.loadOptions()
    Connections {
        target: root.catalogue
        function onChanged() { if (root.catalogue.busy) results.stopScrolling(); Qt.callLater(root.restoreScroll) }
        function onScrollChanged() { Qt.callLater(root.restoreScroll) }
    }
    Timer { id: debounce; interval: 400; onTriggered: root.catalogue.configure({query:query.text}) }
    ColumnLayout {
        anchors.fill: parent; anchors.leftMargin: 28; anchors.rightMargin: 28; anchors.topMargin: 10; anchors.bottomMargin: 20; spacing: 12
        RowLayout {
            id: header; Layout.fillWidth: true; spacing: 12
                SectionTabs {
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    objectName: "browseContentTypes"
                    entries: (root.catalogue.author.name ? [{key:"all",label:"All content",icon:"content"}] : []).concat([{key:"mod",label:"Mods",icon:"mod"},{key:"resourcepack",label:"Resource Packs",icon:"resource"},{key:"datapack",label:"Data Packs",icon:"world"},{key:"shader",label:"Shaders",icon:"shader"}])
                    current: root.contentType
                    onSelected: function(key) { debounce.stop(); results.stopScrolling(); root.catalogue.configure({type:key}) }
                }
                SelectBox {
                    objectName: "browseInstanceSelector"; Layout.preferredWidth: Math.min(root.filterPanelWidth, (root.width - 56) * 0.42); Layout.minimumWidth: 0
                    model: ["Browse without an instance"].concat(root.instances.map(function(pack) { return pack.title + " - " + Branding.label(pack.provider) }))
                    currentIndex: root.instances.findIndex(function(pack) { return pack.path === root.targetPath })+1
                    onActivated: root.instanceRequested(currentIndex > 0 ? root.instances[currentIndex-1].path : "")
                }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 18
            ColumnLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.fillHeight: true; spacing: 12
                SearchField {
                    id: query; objectName: "modSearchInput"; Layout.fillWidth: true; implicitHeight: 44; leadingIcon: "search"
                    text: root.committedQuery; placeholderText: root.contentType === "all" ? "Search this author’s content..." : "Search " + Branding.label(root.contentType).toLowerCase() + "..."
                    onTextEdited: debounce.restart(); onAccepted: { debounce.stop();root.catalogue.configure({query:text}) }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    PaginationBar { Layout.fillWidth: true; Layout.minimumWidth: 0; catalogue: root.catalogue; contextBusy: root.browse.busy }
                    ActionButton {
                        id: filtersButton; objectName: "browseFiltersButton"; visible: root.compactFilters
                        text: "Filters"; iconName: "filter"; compact: true; selected: filterPopup.opened
                        Layout.alignment: Qt.AlignTop
                        onClicked: filterPopup.opened ? filterPopup.close() : filterPopup.open()
                    }
                }
                ActiveBrowseFilters { Layout.fillWidth: true; catalogue: root.catalogue; instanceLocked: root.browse.ready && !root.catalogue.filters.unlocked }
                Text { Layout.fillWidth: true; visible: root.catalogue.notice.length>0; text: root.catalogue.notice; color: Theme.colors.muted; font.pixelSize: 11; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
                Item {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    ContentListView {
                        id: results; objectName: "browseResults"; anchors.fill: parent; clip: true; spacing: Appearance.values.cardGap
                        model: root.catalogue.projects
                        onMovingChanged: if(moving) root.scrollRevision=root.catalogue.viewRevision
                        onContentYChanged: if(moving && root.visible && !root.catalogue.busy && root.scrollRevision===root.catalogue.viewRevision)root.catalogue.scrollPosition=Math.max(0,contentY)
                        onMovementEnded: if(root.visible && !root.catalogue.busy && root.scrollRevision===root.catalogue.viewRevision) root.catalogue.scrollPosition=Math.max(0,contentY)

                        delegate: ProjectCard {
                            required property var modelData
                            width: results.viewportWidth; project: modelData
                            minecraft: (root.catalogue.filters.versions || [])[0] || ""
                            preferredLoader: root.browse.pack.loader || ""
                            selectedLoaders: root.catalogue.filters.loaders || []
                            hiddenMode: !!root.catalogue.filters.hidden
                            installState: { const ready=root.browse.ready; const busy=root.browse.busy; const state=root.browse.state(modelData); const revision=root.content.lockRevision; const sourceLock=root.content.sourceLock(modelData); state.locked=!!state.locked || root.content.projectLocked(modelData); state.sourceLocked=!!sourceLock.launcherLocked;state.sourceMissing=!!sourceLock.missing;state.canInstall = state.canInstall && !root.app.navigationBusy && !state.locked; if(state.locked&&!state.installed)state.label="Locked"; return state }
                            onOpened: { root.catalogue.scrollPosition=results.contentY;root.showcaseRequested(modelData,"description") }
                            onVersionsRequested: { root.catalogue.scrollPosition=results.contentY;root.showcaseRequested(modelData,"versions") }
                            onAuthorRequested: function(author) { root.authorRequested(author) }
                            onInstallRequested: root.browse.install(modelData)
                            onUnlockRequested: root.content.setProjectLocked(modelData,false)
                            onRestoreLockedRequested: root.content.restoreLocked(modelData)
                            onVisibilityRequested: function(hidden) { root.catalogue.hide(modelData,hidden) }
                            onTagRequested: function(tag) { root.catalogue.configure({tags:[(modelData.provider==="modrinth"?"mr:":"cf:")+tag]}) }
                        }
                    }
                    EmptyState { anchors.centerIn: parent; visible: !root.catalogue.busy && root.catalogue.projects.length===0; title: root.catalogue.filters.hidden ? "No hidden content matches" : "No matching content"; message: "Try another search or filter."; symbol: "search" }
                }
            }
            ColumnLayout {
                visible: !root.compactFilters
                Layout.preferredWidth: root.filterPanelWidth
                Layout.minimumWidth: Layout.preferredWidth; Layout.maximumWidth: Layout.preferredWidth
                Layout.fillHeight: true; spacing: 12
                Rectangle {
                    id: sidebar; objectName: "browseFilterPanel"
                    Layout.fillWidth: true; Layout.fillHeight: true; radius: ControlStyle.panelRadius; color: Theme.colors.panel; border.color: Theme.colors.border

                }
            }
        }
    }
    FocusPopup {
        id: filterPopup; objectName: "browseFiltersPopup"; parent: root
        x: Math.max(8, root.width - width - 28); y: Math.min(110, Math.max(8, root.height - height - 8))
        width: Math.min(Math.max(300, root.filterPanelWidth), root.width - 16); height: Math.max(80, Math.min(640, root.height - 24)); padding: 0
        onClosed: if (root.compactFilters && root.visible) filtersButton.forceActiveFocus()
        background: Rectangle { color: Theme.colors.panel; radius: ControlStyle.panelRadius; border.color: Theme.colors.border }
    }
    BrowseFilters {
        objectName: "browseFilters"
        parent: root.compactFilters ? filterPopup.contentItem : sidebar
        visible: !root.compactFilters || filterPopup.visible
        anchors.fill: parent; anchors.margins: 14
        catalogue: root.catalogue; hasInstance: root.browse.ready; connectorAvailable: root.browse.connectorAvailable
    }
}
