pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
import "../components"
Item {
    id: root
    required property var controller
    implicitWidth: 36; implicitHeight: 36
    PopupButton {
        id: errorButton; anchors.fill: parent; popup: details; subtle: true; compact: true
        visible: root.controller.notice.length>0; iconName: "warning"; hint: "View error"
    }
    FocusPopup {
        id: details; parent: root; x: root.width-width; y: root.height+5; width: 330; padding: 14
        background: Rectangle { radius: ControlStyle.panelRadius; color: Theme.colors.card; border.color: Theme.colors.border }
        contentItem: Text { text: root.controller.notice; wrapMode: Text.WordWrap; color: Theme.colors.warning; font.pixelSize: 12; textFormat: Text.PlainText }
    }
}
