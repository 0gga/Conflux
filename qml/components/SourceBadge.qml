pragma ComponentBehavior: Bound
import QtQuick

Rectangle {
    id: badge
    property string source: "modrinth"
    implicitWidth: label.implicitWidth + 18
    implicitHeight: 24
    radius: 5
    color: source === "curseforge" ? "#382a21" : source === "git" ? "#293044" : "#1c3528"
    Text {
        id: label
        anchors.centerIn: parent
        text: badge.source === "curseforge" ? "CurseForge" : badge.source === "git" ? "Git repository" : "Modrinth"
        color: badge.source === "curseforge" ? "#f3b582" : badge.source === "git" ? "#b4c4ff" : "#8fe3b4"
        font.pixelSize: 11
        font.weight: Font.DemiBold
    }
}
