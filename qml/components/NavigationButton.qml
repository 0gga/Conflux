import QtQuick
import Conflux.Style

ActionButton {
    id: control
    subtle: true
    alignLeft: true
    implicitHeight: 46
    leftPadding: 16
    font.pixelSize: 14
    Rectangle {
        x: 0; anchors.verticalCenter: parent.verticalCenter
        width: 3; height: 18; radius: 1.5
        visible: control.selected; color: Theme.colors.accent
    }
}
