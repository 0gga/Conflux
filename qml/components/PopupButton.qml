pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
ActionButton {
    id: root
    required property var popup
    selected: popup.visible
    onClicked: popup.visible ? popup.close() : popup.open()
    Binding {
        target: root.popup
        property: "closePolicy"
        value: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    }
}
