pragma ComponentBehavior: Bound
import QtQuick
import Conflux.Style
import "../components"
import "../explore"
Rectangle {
    id: root
    objectName: "appearanceLivePreview"
    property string section: "content"
    color: Theme.colors.background; border.color: Theme.colors.border; radius: ControlStyle.controlRadius
    implicitHeight: 250
    clip: true
    readonly property real previewScale: Math.min(1,(width-24)/1040)
    Text { x: 12; y: 8; text: "Live preview · example content"; color: Theme.colors.muted; font.pixelSize: 11 }
    QtObject {
        id: example
        property var filters: ({type:"mod",source:"merged",versions:["1.21.1"],loaders:["neoforge","fabric"],unlocked:false,tags:[],hidden:false,hideInstalled:false})
        readonly property bool hideInstalledActive: !!filters.hideInstalled && !filters.hidden
        property var options: ({versions:["1.21.1","1.20.1","1.19.2"],tags:[],curseforgeTags:[]})
        function configure(change) { filters=Object.assign({},filters,change) }
    }
    Column {
        visible: root.section==="interface"; x: 12; y: 32; width: 1040
        scale: root.previewScale; transformOrigin: Item.TopLeft; spacing: 12
        LibraryRow { objectName: "previewLibraryRow"; width: parent.width; titleText: "Example modpack"; detailText: "128 mods"; minecraft: "1.21.1"; loader: "neoforge"; entireRowInteractive: false }
        Rectangle {
            width: parent.width; height: Appearance.values.contentRowHeight; radius: ControlStyle.controlRadius; color: Theme.colors.card
            SelectionBox { x: 14; anchors.verticalCenter: parent.verticalCenter; Accessible.name: "Example row" }
            ContentIcon { x: 54; anchors.verticalCenter: parent.verticalCenter; size: Math.min(44,parent.height-24); kind: "mod" }
            Text { x: 112; anchors.verticalCenter: parent.verticalCenter; text: "Example installed content"; color: Theme.colors.text; font.pixelSize: 13 }
            ToggleSwitch { anchors.right: parent.right; anchors.rightMargin: 20; anchors.verticalCenter: parent.verticalCenter; checked: true; Accessible.name: "Example enabled content" }
        }
    }
    Item {
        visible: root.section!=="interface"
        x: 12; y: 32; width: 1040; height: (root.height-44)/root.previewScale
        scale: root.previewScale; transformOrigin: Item.TopLeft
        Rectangle {
            id: filterPanel; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
            width: Appearance.filterWidthFor(parent.width); radius: ControlStyle.controlRadius; color: Theme.colors.panel; border.color: Theme.colors.border
            BrowseFilters { anchors.fill: parent; anchors.margins: 14; catalogue: example; hasInstance: true; connectorAvailable: true }
        }
        Column {
            anchors.left: parent.left; anchors.right: filterPanel.left; anchors.rightMargin: 18
            spacing: Appearance.values.cardGap
            ProjectCard {
                width: parent.width; minecraft: "1.21.1"; selectedLoaders: example.filters.loaders
                project: ({title:"Example mod",author:"Example author",summary:"A preview of your content size, colors and spacing.",provider:"modrinth",sources:["modrinth","curseforge"],loaders:["neoforge","fabric","quilt"],environment:"client",categories:["optimization","utility"],downloads:"21K",downloadsRaw:21000,downloadStats:[{provider:"modrinth",count:"12K",raw:12000},{provider:"curseforge",count:"9K",raw:9000}],updated:"2026-10-20",icon:"data:image/svg+xml,"+encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><rect width="100" height="100" fill="'+Theme.colors.accent+'"/><path fill="'+Theme.colors.accentText+'" d="M50 16 82 34v34L50 86 18 68V34zm0 12L30 40v22l20 12 20-12V40z"/></svg>')})
                installState: ({label:"Install",canInstall:true})
            }
            ProjectCard {
                width: parent.width; minecraft: "1.21.1"; selectedLoaders: example.filters.loaders
                project: ({title:"Installed project",author:"Example author",summary:"Preview the opacity of installed content.",provider:"modrinth",sources:["modrinth"],loaders:["neoforge"],environment:"client-server",categories:["technology"]})
                installState: ({label:"Installed",installed:true,canInstall:false})
            }
        }
    }
    ScrollViewportCorners { anchors.fill: parent; radius: root.radius; outsideColor: Theme.colors.background }

}
