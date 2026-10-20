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
    radius: 16
    color: hover.hovered ? Theme.colors.panel : Theme.colors.card
    border.color: hover.hovered ? Qt.lighter(Theme.colors.border, 1.5) : Theme.colors.border
    Behavior on border.color { ColorAnimation { duration: 100 } }
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
        width: 136; text: "Explore mods"; iconName: "compass"; subtle: true; onClicked: row.exploreRequested()
    }
    ActionButton {
        id: action; objectName: "libraryRowAction"
        anchors.right: hide.left; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
        width: 124; text: row.actionText; iconName: "settings"; onClicked: row.activated()
    }
    ActionButton {
        id: hide; objectName: "libraryRowHide"
        anchors.right: parent.right; anchors.rightMargin: 16; anchors.verticalCenter: parent.verticalCenter
        width: 40; implicitWidth: 40; iconName: row.hiddenPack ? "restore" : "hide"; hint: row.hiddenPack ? "Restore pack" : "Hide pack"; subtle: true; onClicked: row.hideRequested()
    }
}
