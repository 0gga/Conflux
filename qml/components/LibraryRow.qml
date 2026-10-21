pragma ComponentBehavior: Bound
import QtQuick
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
    implicitHeight: Appearance.values.libraryRowHeight
    radius: 16
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
        width: 118; height: 26
        SourceBadge { anchors.left: parent.left; source: row.source }
    }
    ActionButton {
        id: explore; objectName: "libraryRowExplore"
        anchors.right: hide.left; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
        width: 154; text: "Explore content"; iconName: "compass"; subtle: true; onClicked: row.exploreRequested()
    }
    ActionButton {
        id: hide; objectName: "libraryRowHide"
        anchors.right: parent.right; anchors.rightMargin: 16; anchors.verticalCenter: parent.verticalCenter
        width: 40; implicitWidth: 40; iconName: row.hiddenPack ? "restore" : "hide"; hint: row.hiddenPack ? "Restore pack" : "Hide pack"; subtle: true; onClicked: row.hideRequested()
    }
}
