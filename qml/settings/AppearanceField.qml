pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
import "../components"
Rectangle {
    id: root
    required property var field
    objectName: "appearanceField_" + field.key
    implicitHeight: body.implicitHeight+24; radius: ControlStyle.panelRadius; color: Theme.colors.card
    RowLayout {
        id: body; anchors.fill: parent; anchors.margins: 12; spacing: 8
        Loader {
            Layout.fillWidth: true; Layout.minimumWidth: 0
            sourceComponent: root.field.kind === "toggle" ? toggle : root.field.kind === "choice" ? choice : metric
        }
        ActionButton {
            objectName: "resetAppearance_" + root.field.key
            iconName: "refresh"; hint: "Reset " + root.field.label.toLowerCase(); compact: true; subtle: true
            Layout.preferredWidth: 30; Layout.preferredHeight: 30
            enabled: Appearance.values[root.field.key] !== root.field.initial
            onClicked: Appearance.resetValue(root.field.key)
        }
    }
    Component { id: metric; AppearanceMetric { field: root.field } }
    Component {
        id: toggle
        RowLayout {
            Text { Layout.fillWidth: true; text: root.field.label; color: Theme.colors.text; font.pixelSize: 12; wrapMode: Text.WordWrap }
            ToggleSwitch { objectName: "appearanceToggle_"+root.field.key; checked: !!Appearance.values[root.field.key]; Accessible.name: root.field.label; onToggled: Appearance.setValue(root.field.key,checked?1:0) }
        }
    }
    Component {
        id: choice
        ColumnLayout {
            spacing: 8
            Text { Layout.fillWidth: true; text: root.field.label; color: Theme.colors.text; font.pixelSize: 12 }
            SelectBox { Layout.fillWidth: true; model: root.field.choices; currentIndex: Appearance.values[root.field.key]; Accessible.name: root.field.label; onActivated: Appearance.setValue(root.field.key,currentIndex) }
        }
    }
}
