pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import Conflux.Style
import "../components"
Item {
    id: root
    required property var controller
    property string shownNotice: ""
    implicitWidth: 76; implicitHeight: 36
    Row {
        anchors.fill: parent; spacing: 4
        Item {
            width: 36; height: 36
            BusyIndicator { anchors.centerIn: parent; width: 24; height: 24; visible: root.controller.busy; running: visible; Accessible.name: root.controller.activityText }
            PopupButton {
                anchors.fill: parent; popup: details; subtle: true; compact: true
                visible: !root.controller.busy && (root.controller.notice.length > 0 || root.controller.activity !== "idle")
                iconName: root.controller.notice.length ? "warning" : root.controller.activity === "cancelled" ? "close" : "check"
                hint: root.controller.notice.length ? "View operation details" : root.controller.activity === "cancelled" ? "Cancelled" : "Complete"
            }
        }
        ActionButton { width: 36; height: 36; iconName: "close"; hint: "Cancel operation"; subtle: true; compact: true; visible: root.controller.busy; enabled: root.controller.cancellable; onClicked: root.controller.cancel() }
    }
    FocusPopup {
        id: details; parent: root; x: root.width-width; y: root.height+5; width: 330; padding: 14
        background: Rectangle { radius: 14; color: Theme.colors.card; border.color: Theme.colors.border }
        contentItem: Text { text: root.controller.notice || (root.controller.activity === "cancelled" ? "Cancelled" : "Complete"); wrapMode: Text.WordWrap; color: root.controller.notice.length ? Theme.colors.warning : Theme.colors.muted; font.pixelSize: 12; textFormat: Text.PlainText }
    }
    Connections { target: root.controller; function onChanged() { if (!root.controller.busy && root.controller.notice.length && root.shownNotice !== root.controller.notice) details.open(); root.shownNotice = root.controller.notice } }
}
