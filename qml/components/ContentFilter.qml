pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
FocusPopup {
    id: root
    required property var controller
    property string category: ""
    width: 270; padding: 8
    implicitHeight: Math.min(420,body.implicitHeight+16)
    background: Rectangle { color: Theme.colors.card; border.color: Theme.colors.border; radius: 14 }
    onClosed: category = ""
    contentItem: ColumnLayout {
        id: body; spacing: 4
        ActionButton { visible: root.category.length > 0; text: "Back to filters"; iconName: "back"; subtle: true; alignLeft: true; Layout.fillWidth: true; onClicked: root.category = "" }
        ColumnLayout {
            visible: root.category.length === 0; Layout.fillWidth: true; spacing: 2
            Repeater {
                model: [{key:"author",label:"Author"},{key:"environment",label:"Environment"},{key:"state",label:"State"}]
                delegate: ActionButton {
                    required property var modelData
                    text: modelData.label; subtle: true; alignLeft: true; Layout.fillWidth: true
                    onClicked: root.category = modelData.key
                    Icon { anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter; name: "forward"; size: 16 }
                }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.colors.border; Layout.topMargin: 4; Layout.bottomMargin: 4 }
            Repeater {
                model: [{key:"openSource",label:"Open source"},{key:"external",label:"External"},{key:"locked",label:"Locked"},{key:"recent",label:"Updated in the last 7 days"}]
                delegate: ActionButton {
                    required property var modelData
                    text: modelData.label; subtle: true; alignLeft: true; Layout.fillWidth: true
                    onClicked: root.controller.setFilter(modelData.key,!root.controller.filters[modelData.key])
                    Icon { anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter; name: "check"; size: 17; color: Theme.colors.accent; visible: !!root.controller.filters[parent.modelData.key] }
                }
            }
        }
        BoundedScrollView {
            id: options; visible: root.category.length > 0; Layout.fillWidth: true
            implicitHeight: Math.min(330,choices.implicitHeight); clip: true
            ColumnLayout {
                id: choices; width: options.availableWidth; spacing: 2
                Repeater {
                    model: root.category === "author" ? ["",...root.controller.authors] : root.category === "environment" ? ["","client","server","both","unknown"] : ["","enabled","disabled"]
                    delegate: ActionButton {
                        required property string modelData
                        text: modelData.length ? modelData.charAt(0).toUpperCase()+modelData.slice(1) : "Any"; subtle: true; alignLeft: true; Layout.fillWidth: true
                        selected: root.controller.filters[root.category] === modelData
                        onClicked: { root.controller.setFilter(root.category,modelData); root.category = "" }
                    }
                }
            }
        }
    }
}
