import QtQuick
import QtQuick.Controls

ScrollView {
    id: root
    clip: true
    rightPadding: 16
    ScrollBar.vertical: ContentScrollBar { parent: root; x: root.width-width; y: root.topPadding; height: root.availableHeight }
    readonly property Flickable viewport: contentItem as Flickable
    Binding {
        target: root.viewport
        property: "boundsBehavior"
        value: Flickable.StopAtBounds
        when: root.viewport !== null
    }
}
