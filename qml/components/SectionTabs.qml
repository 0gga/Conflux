pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
Rectangle {
    id: root
    required property var entries
    property string current: ""
    signal selected(string key)
    implicitHeight: 44; implicitWidth: buttons.implicitWidth + 8
    radius: 13; color: Theme.colors.panel; border.color: Theme.colors.border
    RowLayout {
        id: buttons; anchors.fill: parent; anchors.margins: 4; spacing: 4
        Repeater { model: root.entries; delegate: ActionButton {
            required property var modelData
            text: modelData.label; iconName: modelData.icon || ""
            compact: true; subtle: true; selected: root.current === modelData.key
            onClicked: root.selected(modelData.key)
        } }
    }
}
