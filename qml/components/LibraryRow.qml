pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style

Rectangle {
    id: row
    property string titleText: ""
    property string detailText: ""
    property string thumbnail: ""
    property string loader: ""
    property string minecraft: ""
    property string source: "conflux"
    property bool entireRowInteractive: true
    property bool hiddenPack: false
    signal activated()
    signal exploreRequested()
    signal hideRequested()
    signal folderRequested()
    implicitHeight: Appearance.values.libraryRowHeight
    radius: ControlStyle.panelRadius
    color: hover.hovered ? Theme.colors.panel : Theme.colors.card
    border.color: hover.hovered ? Qt.lighter(Theme.colors.border, 1.5) : Theme.colors.border
    Behavior on border.color { ColorAnimation { duration: Appearance.values.reduceMotion ? 0 : 100 } }
    HoverHandler { id: hover }
    MouseArea { anchors.fill: parent; enabled: row.entireRowInteractive; cursorShape: Qt.PointingHandCursor; onClicked: row.activated() }
    ContentIcon {
        id: icon
        anchors.left: parent.left; anchors.leftMargin: 16; anchors.verticalCenter: parent.verticalCenter
        width: 48; height: 48; size: 48; source: row.thumbnail; kind: "content"
    }
    Column {
        anchors.left: icon.right; anchors.leftMargin: 16; anchors.right: sourceColumn.left; anchors.rightMargin: 20
        anchors.verticalCenter: parent.verticalCenter; spacing: 7
        Text { objectName: "libraryRowTitle"; width: parent.width; text: row.titleText; color: Theme.colors.text; font.pixelSize: 15; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
        Row {
            width: parent.width; height: 26; spacing: 8
            CompatibilityBadges { id: compatibility; objectName: "libraryRowCompatibility"; minecraft: row.minecraft; loader: row.loader; showUnknownVersion: true }
            Text { objectName: "libraryRowDetails"; width: Math.max(0,parent.width-compatibility.width-8); anchors.verticalCenter: parent.verticalCenter; text: row.detailText; color: Theme.colors.muted; font.pixelSize: 12; elide: Text.ElideRight; textFormat: Text.PlainText }
        }
    }
    Item {
        id: sourceColumn; objectName: "libraryRowSourceColumn"
        anchors.right: explore.left; anchors.rightMargin: 20; anchors.verticalCenter: parent.verticalCenter
        width: row.width < 650 ? 32 : 118; height: 26
        SourceBadge { anchors.left: parent.left; source: row.source; iconOnly: row.width < 650 }
    }
    ActionButton {
        id: explore; objectName: "libraryRowExplore"
        anchors.right: more.left; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
        width: row.width < 620 ? 40 : 154; text: row.width < 620 ? "" : "Explore content"; hint: "Explore content"; iconName: "compass"; subtle: true; onClicked: row.exploreRequested()
    }
    PopupButton {
        id: more; objectName: "libraryRowMore"; popup: menu
        anchors.right: parent.right; anchors.rightMargin: 16; anchors.verticalCenter: parent.verticalCenter
        width: 40; implicitWidth: 40; iconName: "more"; hint: "Modpack actions"; subtle: true
    }
    FocusPopup {
        id: menu; objectName: "libraryRowMenu"; parent: more
        x: more.width-width; y: more.height+6; width: 240; padding: 6
        background: Rectangle { radius: ControlStyle.panelRadius; color: Theme.colors.card; border.color: Theme.colors.border }
        contentItem: ColumnLayout {
            spacing: 2
            MenuOption { Layout.fillWidth: true; text: "Open modpack"; iconName: "content"; subtle: true; alignLeft: true; selectionOutlineOnly: true; onClicked: { menu.close(); row.activated() } }
            MenuOption { Layout.fillWidth: true; text: "Explore content"; iconName: "compass"; subtle: true; alignLeft: true; selectionOutlineOnly: true; onClicked: { menu.close(); row.exploreRequested() } }
            MenuOption { objectName: "libraryRowFolder"; Layout.fillWidth: true; text: "Open instance folder"; iconName: "folder"; subtle: true; alignLeft: true; selectionOutlineOnly: true; onClicked: { menu.close(); row.folderRequested() } }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.colors.border }
            MenuOption { objectName: "libraryRowHide"; Layout.fillWidth: true; text: row.hiddenPack ? "Restore modpack" : "Hide modpack"; iconName: row.hiddenPack ? "restore" : "hide"; subtle: true; alignLeft: true; selectionOutlineOnly: true; onClicked: { menu.close(); row.hideRequested() } }
        }
    }
}
