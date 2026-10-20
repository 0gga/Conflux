import QtQuick
import QtQuick.Controls

ScrollView {
    id: root
    clip: true
    readonly property Flickable viewport: contentItem as Flickable
    Binding {
        target: root.viewport
        property: "boundsBehavior"
        value: Flickable.StopAtBounds
        when: root.viewport !== null
    }
}
