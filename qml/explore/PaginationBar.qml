pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "../components"
GridLayout {
    id: root
    required property var catalogue
    property bool contextBusy: false
    columns: width < choices.implicitWidth + navigation.implicitWidth + columnSpacing ? 1 : 2
    columnSpacing: 12
    rowSpacing: 8
    RowLayout {
        id: choices
        Layout.fillWidth: true
        spacing: 8
        SelectBox {
            objectName: "browseSortSelector"
            Layout.preferredWidth: 190; Layout.minimumWidth: 90; Layout.fillWidth: true
            model: ["Relevance", "Downloads", "Followers", "Date published", "Date updated"]
            currentIndex: ["relevance","downloads","follows","newest","updated"].indexOf(root.catalogue.filters.sort)
            onActivated: root.catalogue.configure({sort:["relevance","downloads","follows","newest","updated"][currentIndex]})
        }
        SelectBox {
            objectName: "browseSizeSelector"
            Layout.preferredWidth: 140; Layout.minimumWidth: 90; model: ["10 per page","20 per page","50 per page","100 per page"]
            currentIndex: [10,20,50,100].indexOf(root.catalogue.filters.size)
            onActivated: root.catalogue.configure({size:[10,20,50,100][currentIndex]})
        }
    }
    RowLayout {
        id: navigation
        Layout.fillWidth: true
        spacing: 8
        BusyIndicator { visible: root.catalogue.busy || root.contextBusy; running: visible; Layout.preferredWidth: 20; Layout.preferredHeight: 20 }
        SelectBox {
            objectName: "browseSourceSelector"
            Layout.preferredWidth: 140; Layout.minimumWidth: 90; Layout.fillWidth: true; model: ["All sources","Modrinth","CurseForge"]
            currentIndex: ["merged","modrinth","curseforge"].indexOf(root.catalogue.filters.source)
            onActivated: root.catalogue.configure({source:["merged","modrinth","curseforge"][currentIndex],tags:[]})
        }
        PageNavigation { catalogue: root.catalogue; compact: root.width < 720 }
        ActionButton { objectName: "browseRefreshButton"; iconName: "refresh"; hint: "Refresh catalogue"; compact: true; subtle: true; enabled: !root.catalogue.busy; onClicked: root.catalogue.refresh() }
    }
}
