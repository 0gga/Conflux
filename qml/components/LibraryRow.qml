pragma ComponentBehavior: Bound
import QtQuick
import Conflux.Style

Rectangle {
    id: row
    property string titleText: ""
    property string detailText: ""
    property string thumbnail: ""
    property string source: "conflux"
    property string actionText: "Configure"
    property bool entireRowInteractive: true
    property bool hiddenPack: false
    signal activated()
    signal exploreRequested()
    signal hideRequested()
    implicitHeight: 96
    radius: 10
    color: hover.hovered ? Theme.colors.hover : Theme.colors.card
    border.color: Theme.colors.border
    HoverHandler { id: hover }
    MouseArea { anchors.fill: parent; enabled: row.entireRowInteractive; cursorShape: Qt.PointingHandCursor; onClicked: row.activated() }
    Rectangle {
        id: icon
        anchors.left: parent.left; anchors.leftMargin: 16; anchors.verticalCenter: parent.verticalCenter
        width: 48; height: 48; radius: 10; clip: true; color: Theme.colors.input
        Text { anchors.centerIn: parent; text: row.titleText.substring(0, 1).toUpperCase(); color: Theme.colors.text; font.pixelSize: 23; font.weight: Font.DemiBold }
        Image { anchors.fill: parent; source: row.thumbnail; fillMode: Image.PreserveAspectFit; asynchronous: true; visible: status === Image.Ready }
    }
    Column {
        anchors.left: icon.right; anchors.leftMargin: 16; anchors.right: sourceColumn.left; anchors.rightMargin: 20
        anchors.verticalCenter: parent.verticalCenter; spacing: 7
        Text { objectName: "libraryRowTitle"; width: parent.width; text: row.titleText; color: Theme.colors.text; font.pixelSize: 15; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
        Text { objectName: "libraryRowDetails"; width: parent.width; text: row.detailText; color: Theme.colors.muted; font.pixelSize: 12; elide: Text.ElideRight; textFormat: Text.PlainText }
    }
    Item {
        id: sourceColumn; objectName: "libraryRowSourceColumn"
        anchors.right: explore.left; anchors.rightMargin: 20; anchors.verticalCenter: parent.verticalCenter
        width: 94; height: 24
        SourceBadge { anchors.left: parent.left; source: row.source }
    }
    ActionButton {
        id: explore; objectName: "libraryRowExplore"
        anchors.right: action.left; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
        width: 110; text: "Explore mods"; subtle: true; onClicked: row.exploreRequested()
    }
    ActionButton {
        id: action; objectName: "libraryRowAction"
        anchors.right: hide.left; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
        width: 100; text: row.actionText; onClicked: row.activated()
    }
    ActionButton {
        id: hide; objectName: "libraryRowHide"
        anchors.right: parent.right; anchors.rightMargin: 16; anchors.verticalCenter: parent.verticalCenter
        width: 68; implicitWidth: 68; text: row.hiddenPack ? "Restore" : "Hide"; subtle: true; onClicked: row.hideRequested()
    }
}
