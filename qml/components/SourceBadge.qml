pragma ComponentBehavior: Bound
import QtQuick
import Conflux.Style
Rectangle {
    id: badge
    property string source: "conflux"
    implicitWidth: label.implicitWidth + 18; implicitHeight: 24; radius: 8
    color: Theme.colors.panel; border.color: Theme.colors.border
    Text {
        id: label; anchors.centerIn: parent
        text: badge.source === "curseforge" ? "CurseForge" : badge.source === "modrinth" ? "Modrinth" : "Conflux"
        color: Theme.colors.muted; font.pixelSize: 11; font.weight: Font.Medium
    }
}
