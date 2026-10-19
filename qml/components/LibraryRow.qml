pragma ComponentBehavior: Bound
import QtQuick

Rectangle {
    id: row
    property string titleText: ""
    property string detailText: ""
    property string thumbnail: ""
    property string source: "modrinth"
    property string actionText: "Open folder"
    property bool entireRowInteractive: false
    signal activated()
    implicitHeight: 84
    radius: 10
    color: entireRowInteractive && hover.hovered ? "#223127" : "#19221c"
    border.color: entireRowInteractive && hover.hovered ? "#62866c" : "#2c3a30"
    HoverHandler { id: hover }
    MouseArea {
        anchors.fill: parent
        enabled: row.entireRowInteractive
        cursorShape: Qt.PointingHandCursor
        onClicked: row.activated()
    }
    Rectangle {
        id: icon
        anchors.left: parent.left; anchors.leftMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        width: 48; height: 48; radius: 10; clip: true
        color: row.source === "curseforge" ? "#3c3025" : "#284331"
        Text { anchors.centerIn: parent; text: row.titleText.substring(0, 1).toUpperCase(); color: "#c8d9cc"; font.pixelSize: 23; font.weight: Font.DemiBold }
        Image { anchors.fill: parent; source: row.thumbnail; fillMode: Image.PreserveAspectFit; asynchronous: true; visible: status === Image.Ready }
    }
    Column {
        anchors.left: icon.right; anchors.leftMargin: 16
        anchors.right: sourceColumn.left; anchors.rightMargin: 24
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6
        Text { objectName: "libraryRowTitle"; width: parent.width; text: row.titleText; color: "#dce7df"; font.pixelSize: 15; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
        Text { objectName: "libraryRowDetails"; width: parent.width; text: row.detailText; color: "#89a18f"; font.pixelSize: 12; elide: Text.ElideRight; textFormat: Text.PlainText }
    }
    Item {
        id: sourceColumn
        objectName: "libraryRowSourceColumn"
        anchors.right: action.left; anchors.rightMargin: 24
        anchors.verticalCenter: parent.verticalCenter
        width: 104; height: 24
        SourceBadge { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; source: row.source }
    }
    ActionButton {
        id: action
        objectName: "libraryRowAction"
        anchors.right: parent.right; anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        width: 116
        text: row.actionText; subtle: true
        onClicked: row.activated()
    }
}
