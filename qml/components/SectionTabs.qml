pragma ComponentBehavior: Bound
import QtQuick
import Conflux.Style
Rectangle {
    id: root
    required property var entries
    property string current: ""
    property bool subtabs: false
    signal selected(string key)
    implicitHeight: 44; implicitWidth: buttons.implicitWidth + 8
    radius: subtabs ? ControlStyle.subtabRadius : ControlStyle.tabRadius; color: Theme.colors.panel; border.color: Theme.colors.border
    Flickable {
        id: viewport; anchors.fill: parent; anchors.margins: 4; clip: true
        contentWidth: buttons.implicitWidth; contentHeight: height
        flickableDirection: Flickable.HorizontalFlick; boundsBehavior: Flickable.StopAtBounds
        Row {
            id: buttons; spacing: 4
            Repeater { model: root.entries; delegate: ActionButton {
                id: tab
                required property var modelData
                objectName: "sectionTab_" + modelData.key
                text: modelData.label; iconName: modelData.icon || ""
                compact: true; cornerRadius: root.subtabs ? ControlStyle.subtabRadius : ControlStyle.tabRadius; subtle: true; selected: root.current === modelData.key
                function reveal() { if(selected || activeFocus) viewport.contentX=Math.max(0,Math.min(x+width-viewport.width,viewport.contentWidth-viewport.width)) }
                onSelectedChanged: Qt.callLater(reveal)
                onActiveFocusChanged: Qt.callLater(reveal)
                onClicked: root.selected(modelData.key)
            } }
        }
    }
}
