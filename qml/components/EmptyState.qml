pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: state
    property string symbol: "+"
    property string title: "A fresh start"
    property string message: ""
    spacing: 12
    Rectangle {
        Layout.alignment: Qt.AlignHCenter
        implicitWidth: 64; implicitHeight: 64; radius: 20; color: "#202e26"
        Text { anchors.centerIn: parent; text: state.symbol; font.pixelSize: 30; color: "#81dfad" }
    }
    Text { Layout.alignment: Qt.AlignHCenter; text: state.title; color: "#edf3ef"; font.pixelSize: 19; font.weight: Font.DemiBold }
    Text {
        Layout.alignment: Qt.AlignHCenter; Layout.maximumWidth: 400
        text: state.message; color: "#9daa9f"; font.pixelSize: 13; wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter; lineHeight: 1.3
    }
}
