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
    readonly property string contentType: catalogue.filters.type
    readonly property var instances: app.libraryPacks.filter(function(pack) { return !pack.hidden })
    signal createRequested()
    signal workspaceRequested()
    signal showcaseRequested(var project, string tab)
    signal authorRequested(var author)
    signal instanceRequested(string path)
    function focusSearch() { query.forceActiveFocus() }
    function restoreScroll() {
        if (!visible || results.moving) return
        results.contentY=Math.max(0,Math.min(catalogue.scrollPosition,results.contentHeight-results.height))
    }
    onVisibleChanged: if (visible) Qt.callLater(restoreScroll)
    Component.onCompleted: catalogue.loadOptions()
    Connections {
        target: root.catalogue
        function onChanged() { Qt.callLater(root.restoreScroll) }
        function onScrollChanged() { Qt.callLater(root.restoreScroll) }
    }
    Timer { id: debounce; interval: 400; onTriggered: root.catalogue.configure({query:query.text}) }
    ColumnLayout {
        anchors.fill: parent; anchors.leftMargin: 28; anchors.rightMargin: 28; anchors.topMargin: 10; anchors.bottomMargin: 20; spacing: 12
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 18
            ColumnLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.fillHeight: true; spacing: 12
                SectionTabs {
                    objectName: "browseContentTypes"
                    entries: (root.catalogue.author.name ? [{key:"all",label:"All content",icon:"content"}] : []).concat([{key:"mod",label:"Mods",icon:"mod"},{key:"resourcepack",label:"Resource Packs",icon:"resource"},{key:"datapack",label:"Data Packs",icon:"world"},{key:"shader",label:"Shaders",icon:"shader"}])
                    current: root.contentType
                    onSelected: function(key) { root.catalogue.configure({type:key}) }
                }
                SearchField {
                    id: query; objectName: "modSearchInput"; Layout.fillWidth: true; implicitHeight: 44; leadingIcon: "search"
                    text: root.catalogue.filters.query || ""; placeholderText: root.contentType === "all" ? "Search this author’s content..." : "Search " + Branding.label(root.contentType).toLowerCase() + "..."
                    onTextEdited: debounce.restart(); onAccepted: { debounce.stop();root.catalogue.configure({query:text}) }
                }
                PaginationBar { Layout.fillWidth: true; catalogue: root.catalogue; contextBusy: root.browse.busy }
                ActiveBrowseFilters { Layout.fillWidth: true; catalogue: root.catalogue; instanceLocked: root.browse.ready && !root.catalogue.filters.unlocked }
                Text { Layout.fillWidth: true; visible: root.catalogue.notice.length>0; text: root.catalogue.notice; color: Theme.colors.muted; font.pixelSize: 11; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
                Item {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    ContentListView {
                        id: results; objectName: "browseResults"; anchors.fill: parent; clip: true; spacing: Appearance.values.cardGap
                        model: root.catalogue.projects
                        onContentYChanged: if(moving && root.visible)root.catalogue.scrollPosition=Math.max(0,contentY)
                        onMovementEnded: root.catalogue.scrollPosition=Math.max(0,contentY)

                        delegate: ProjectCard {
                            required property var modelData
                            width: results.viewportWidth; project: modelData
                            minecraft: (root.catalogue.filters.versions || [])[0] || ""
                            preferredLoader: root.browse.pack.loader || ""
                            selectedLoaders: root.catalogue.filters.loaders || []
                            hiddenMode: !!root.catalogue.filters.hidden
                            installState: { const ready=root.browse.ready; const busy=root.browse.busy; const state=root.browse.state(modelData); state.canInstall = state.canInstall && !root.app.navigationBusy; return state }
                            onOpened: { root.catalogue.scrollPosition=results.contentY;root.showcaseRequested(modelData,"description") }
                            onVersionsRequested: { root.catalogue.scrollPosition=results.contentY;root.showcaseRequested(modelData,"versions") }
                            onAuthorRequested: function(author) { root.authorRequested(author) }
                            onInstallRequested: root.browse.install(modelData)
                            onVisibilityRequested: function(hidden) { root.catalogue.hide(modelData,hidden) }
                            onTagRequested: function(tag) { root.catalogue.configure({tags:[(modelData.provider==="modrinth"?"mr:":"cf:")+tag]}) }
                        }
                    }
                    EmptyState { anchors.centerIn: parent; visible: !root.catalogue.busy && root.catalogue.projects.length===0; title: root.catalogue.filters.hidden ? "No hidden content matches" : "No matching content"; message: "Try another search or filter."; symbol: "search" }
                }
            }
            ColumnLayout {
                Layout.preferredWidth: Appearance.filterWidthFor(root.width - 56)
                Layout.minimumWidth: Layout.preferredWidth; Layout.maximumWidth: Layout.preferredWidth
                Layout.fillHeight: true; spacing: 12
                SelectBox {
                    objectName: "browseInstanceSelector"; Layout.fillWidth: true
                    model: ["Browse without an instance"].concat(root.instances.map(function(pack) { return pack.title + " - " + Branding.label(pack.provider) }))
                    currentIndex: root.instances.findIndex(function(pack) { return pack.path === root.targetPath })+1
                    onActivated: root.instanceRequested(currentIndex > 0 ? root.instances[currentIndex-1].path : "")
                }
                Rectangle {
                    objectName: "browseFilterPanel"
                    Layout.fillWidth: true; Layout.fillHeight: true; radius: 16; color: Theme.colors.panel; border.color: Theme.colors.border
                    BrowseFilters { anchors.fill: parent; anchors.margins: 14; catalogue: root.catalogue; hasInstance: root.browse.ready; connectorAvailable: root.browse.connectorAvailable }
                }
            }
        }
    }
}
