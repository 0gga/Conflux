pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
Rectangle {
    id: root
    required property var entries
    property string current: ""
    signal selected(string key)
    implicitHeight: 48; implicitWidth: buttons.implicitWidth + 8; radius: 15; color: Theme.colors.panel
    RowLayout {
        id: buttons; anchors.fill: parent; anchors.margins: 4; spacing: 4
        Repeater { model: root.entries; delegate: ActionButton {
            required property var modelData
            text: modelData.label; iconName: modelData.icon || ""; subtle: root.current !== modelData.key
            onClicked: root.selected(modelData.key)
        } }
    }
}
