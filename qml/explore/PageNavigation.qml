pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "../components"
RowLayout {
    id: root
    required property var catalogue
    readonly property int currentPage: catalogue.page
    readonly property int pageCount: Math.max(1, catalogue.pageCount)
    readonly property var entries: {
        const pages = [1]
        for (let page = Math.max(2,currentPage-1); page <= Math.min(pageCount-1,currentPage+1); ++page) pages.push(page)
        if (pageCount > 1) pages.push(pageCount)
        pages.sort(function(a,b) { return a-b })
        const result = []
        for (let index = 0; index < pages.length; ++index) {
            if (index && pages[index] > pages[index-1]+1) result.push(-1)
            result.push(pages[index])
        }
        return result
    }
    spacing: 4
    ActionButton {
        objectName: "browsePreviousPage"; iconName: "back"; hint: "Previous page"; compact: true; subtle: true
        visible: root.currentPage > 1; enabled: !root.catalogue.busy
        onClicked: root.catalogue.previous()
    }
    Repeater {
        model: root.entries
        delegate: Item {
            id: entry
            required property int modelData
            implicitWidth: modelData < 0 ? 24 : Math.max(36, number.implicitWidth)
            implicitHeight: 36
            Text { anchors.centerIn: parent; visible: entry.modelData < 0; text: "…"; color: Theme.colors.muted; font.pixelSize: 15 }
            ActionButton {
                id: number; objectName: "browsePageShortcut"; anchors.fill: parent; visible: entry.modelData > 0 && entry.modelData !== root.currentPage
                text: String(entry.modelData); subtle: true; compact: true; pill: true
                implicitWidth: Math.max(36,contentItem.implicitWidth+20)
                leftPadding: 10; rightPadding: 10; enabled: !root.catalogue.busy
                hint: "Page " + entry.modelData; onClicked: root.catalogue.goToPage(entry.modelData)
            }
            TextField {
                id: field; objectName: entry.modelData === root.currentPage ? "browsePageNumber" : "inactivePageField"
                anchors.fill: parent; visible: entry.modelData === root.currentPage
                text: String(root.currentPage); enabled: !root.catalogue.busy; selectByMouse: true
                horizontalAlignment: TextInput.AlignHCenter; verticalAlignment: TextInput.AlignVCenter
                leftPadding: 6; rightPadding: 6; color: Theme.colors.accent; font.pixelSize: 14; font.weight: Font.DemiBold
                validator: IntValidator { bottom: 1; top: root.pageCount }
                inputMethodHints: Qt.ImhDigitsOnly
                Accessible.name: "Page number"; Accessible.description: "Enter a page from 1 to " + root.pageCount
                onActiveFocusChanged: if (activeFocus) selectAll()
                onEditingFinished: {
                    const requested = Number(text)
                    if (acceptableInput && requested !== root.currentPage) root.catalogue.goToPage(requested)
                    text = String(root.currentPage)
                }
                background: Rectangle {
                    radius: height/2; color: Theme.colors.selection
                    border.width: 1; border.color: field.activeFocus ? Theme.colors.accent : "transparent"
                }
            }
        }
    }
    ActionButton {
        objectName: "browseNextPage"; iconName: "forward"; hint: "Next page"; compact: true; subtle: true
        visible: root.catalogue.hasNext; enabled: !root.catalogue.busy; onClicked: root.catalogue.next()
    }
}
