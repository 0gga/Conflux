pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "../components"
ColumnLayout {
    id: root
    required property var field
    spacing: 5
    RowLayout {
        Layout.fillWidth: true
        Text { Layout.fillWidth: true; text: root.field.label; color: Theme.colors.text; font.pixelSize: 12 }
        Text { text: Math.round(Appearance.values[root.field.key]) + root.field.suffix; color: Theme.colors.muted; font.pixelSize: 12 }
    }
    RowLayout {
        Layout.fillWidth: true; spacing: 8
        ActionButton { text: "−"; compact: true; subtle: true; implicitWidth: 32; enabled: Appearance.values[root.field.key] > root.field.min; onClicked: Appearance.setValue(root.field.key,Math.max(root.field.min,Appearance.values[root.field.key]-root.field.step)) }
        Slider {
            id: slider; Layout.fillWidth: true
            from: root.field.min; to: root.field.max; stepSize: root.field.step
            value: Appearance.values[root.field.key]
            Accessible.name: root.field.label
            onMoved: Appearance.setValue(root.field.key,value)
            background: Rectangle {
                x: slider.leftPadding; y: (slider.height-height)/2
                width: slider.availableWidth; height: 6; radius: 3; color: Theme.colors.input
                Rectangle { width: slider.visualPosition * parent.width; height: parent.height; radius: 3; color: Theme.colors.accent }
            }
            handle: Rectangle { x: slider.leftPadding+slider.visualPosition*(slider.availableWidth-width); y: (slider.height-height)/2; width: 18; height: 18; radius: 9; color: slider.pressed ? Theme.colors.text : Theme.colors.accent; border.color: slider.visualFocus ? Theme.colors.text : Theme.colors.border }
        }
        ActionButton { text: "+"; compact: true; subtle: true; implicitWidth: 32; enabled: Appearance.values[root.field.key] < root.field.max; onClicked: Appearance.setValue(root.field.key,Math.min(root.field.max,Appearance.values[root.field.key]+root.field.step)) }
    }
}
