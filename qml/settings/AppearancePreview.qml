pragma ComponentBehavior: Bound
import QtQuick
import Conflux.Style
import "../components"
import "../explore"
Rectangle {
    id: root
    objectName: "appearanceLivePreview"
    color: Theme.colors.background; border.color: Theme.colors.border; radius: 14
    implicitHeight: 250
    clip: true
    readonly property real previewScale: Math.min(1,(width-24)/1040)
    QtObject {
        id: example
        property var filters: ({type:"mod",source:"merged",versions:["1.21.1"],loaders:["neoforge","fabric"],unlocked:false,tags:[],hidden:false,hideInstalled:false})
        property var options: ({versions:["1.21.1","1.20.1","1.19.2"],tags:[],curseforgeTags:[]})
        function configure(change) { filters=Object.assign({},filters,change) }
    }
    Item {
        x: 12; y: 12; width: 1040; height: (root.height-24)/root.previewScale
        scale: root.previewScale; transformOrigin: Item.TopLeft
        Rectangle {
            id: filterPanel; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
            width: Appearance.filterWidthFor(parent.width); radius: 14; color: Theme.colors.panel; border.color: Theme.colors.border
            BrowseFilters { anchors.fill: parent; anchors.margins: 14; catalogue: example; hasInstance: true; connectorAvailable: true }
        }
        Column {
            anchors.left: parent.left; anchors.right: filterPanel.left; anchors.rightMargin: 18
            spacing: Appearance.values.cardGap
            ProjectCard {
                width: parent.width; minecraft: "1.21.1"; selectedLoaders: example.filters.loaders
                project: ({title:"Example mod",author:"Example author",summary:"A preview of your content size, colors and spacing.",provider:"modrinth",sources:["modrinth","curseforge"],loaders:["neoforge","fabric","quilt"],environment:"client",categories:["optimization","utility"],downloadStats:[{provider:"modrinth",count:"12K"},{provider:"curseforge",count:"9K"}],updated:"2026-10-20",icon:"data:image/svg+xml,"+encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><rect width="100" height="100" fill="'+Theme.colors.accent+'"/><path fill="'+Theme.colors.accentText+'" d="M50 16 82 34v34L50 86 18 68V34zm0 12L30 40v22l20 12 20-12V40z"/></svg>')})
                installState: ({label:"Install",canInstall:true})
            }
            ProjectCard {
                width: parent.width; minecraft: "1.21.1"; selectedLoaders: example.filters.loaders
                project: ({title:"Unavailable project",author:"Example author",summary:"Preview the opacity of unavailable content.",provider:"modrinth",sources:["modrinth"],loaders:["neoforge"],environment:"client-server",categories:["technology"]})
                installState: ({label:"Unavailable",installed:false,canInstall:false})
            }
        }
    }
}
