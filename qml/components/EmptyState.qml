pragma ComponentBehavior: Bound
import QtQuick
import Conflux.Style
import QtQuick.Layouts

ColumnLayout {
    id: state
    property string symbol: "+"
    property string title: "A fresh start"
    property string message: ""
    spacing: 12
    Rectangle {
        Layout.alignment: Qt.AlignHCenter
        implicitWidth: 64; implicitHeight: 64; radius: 20; color: Theme.colors.input
        Icon { anchors.centerIn: parent; name: state.symbol === "+" ? "plus" : state.symbol === "▦" ? "library" : state.symbol; size: 28; color: Theme.colors.muted }
    }
    Text { Layout.alignment: Qt.AlignHCenter; text: state.title; color: Theme.colors.text; font.pixelSize: 19; font.weight: Font.DemiBold }
    Text {
        Layout.alignment: Qt.AlignHCenter; Layout.maximumWidth: 400
        text: state.message; color: Theme.colors.muted; font.pixelSize: 13; wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter; lineHeight: 1.3
    }
}
