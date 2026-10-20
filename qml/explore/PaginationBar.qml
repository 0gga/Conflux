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
    columns: width < 740 ? 1 : 2
    columnSpacing: 12
    rowSpacing: 8
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        SelectBox {
            Layout.preferredWidth: 190
            model: ["Sort by: Relevance", "Sort by: Downloads", "Sort by: Followers", "Sort by: Date published", "Sort by: Date updated"]
            currentIndex: ["relevance","downloads","follows","newest","updated"].indexOf(root.catalogue.filters.sort)
            onActivated: root.catalogue.configure({sort:["relevance","downloads","follows","newest","updated"][currentIndex]})
        }
        SelectBox {
            Layout.preferredWidth: 140; model: ["View: 10","View: 20","View: 50","View: 100"]
            currentIndex: [10,20,50,100].indexOf(root.catalogue.filters.size)
            onActivated: root.catalogue.configure({size:[10,20,50,100][currentIndex]})
        }
        Item { Layout.fillWidth: true }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Item { Layout.fillWidth: true }
        BusyIndicator { visible: root.catalogue.busy || root.contextBusy; running: visible; Layout.preferredWidth: 20; Layout.preferredHeight: 20 }
        SelectBox {
            objectName: "browseSourceSelector"
            Layout.preferredWidth: 160; model: ["All sources","Modrinth","CurseForge"]
            currentIndex: ["merged","modrinth","curseforge"].indexOf(root.catalogue.filters.source)
            onActivated: root.catalogue.configure({source:["merged","modrinth","curseforge"][currentIndex],tags:[]})
        }
        ActionButton { iconName: "back"; hint: "Previous page"; compact: true; subtle: true; enabled: !root.catalogue.busy && root.catalogue.page>1; onClicked: root.catalogue.previous() }
        Text { objectName: "browsePageNumber"; text: String(root.catalogue.page); color: Theme.colors.accent; font.pixelSize: 15; font.weight: Font.DemiBold }
        ActionButton { objectName: "browseNextPage"; iconName: "forward"; hint: "Next page"; compact: true; subtle: true; enabled: !root.catalogue.busy && root.catalogue.hasNext; onClicked: root.catalogue.next() }
        ActionButton { iconName: "refresh"; hint: "Refresh catalogue"; compact: true; subtle: true; enabled: !root.catalogue.busy; onClicked: root.catalogue.refresh() }
    }
}
