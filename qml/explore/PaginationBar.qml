pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
import "../components"
RowLayout {
    id: root
    required property var catalogue
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
    ActionButton { iconName: "back"; hint: "Previous page"; compact: true; subtle: true; enabled: !root.catalogue.busy && root.catalogue.page>1; onClicked: root.catalogue.previous() }
    Text { objectName: "browsePageNumber"; text: String(root.catalogue.page); color: Theme.colors.accent; font.pixelSize: 15; font.weight: Font.DemiBold }
    ActionButton { objectName: "browseNextPage"; iconName: "forward"; hint: "Next page"; compact: true; subtle: true; enabled: !root.catalogue.busy && root.catalogue.hasNext; onClicked: root.catalogue.next() }
}
