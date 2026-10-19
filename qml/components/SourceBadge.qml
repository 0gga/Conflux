pragma ComponentBehavior: Bound
import QtQuick
import Conflux.Style
Rectangle {
    id: badge
    property string source: "conflux"
    implicitWidth: label.implicitWidth + 18; implicitHeight: 24; radius: 5
    color: Theme.colors.input
    Text {
        id: label; anchors.centerIn: parent
        text: badge.source === "curseforge" ? "CurseForge" : badge.source === "modrinth" ? "Modrinth" : "Conflux"
        color: Theme.colors.text; font.pixelSize: 11; font.weight: Font.DemiBold
    }
}
