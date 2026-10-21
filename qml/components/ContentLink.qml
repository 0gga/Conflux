pragma ComponentBehavior: Bound
import QtQuick
import Conflux.Style
Text {
    id: root
    signal clicked()
    color: Theme.colors.text
    textFormat: Text.PlainText
    elide: Text.ElideRight
    activeFocusOnTab: enabled
    font.underline: enabled && (hover.hovered || activeFocus)
    Accessible.role: Accessible.Link
    Accessible.name: text
    Accessible.onPressAction: root.clicked()
    Keys.onReturnPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()
    HoverHandler { id: hover; enabled: root.enabled; cursorShape: Qt.PointingHandCursor }
    MouseArea { anchors.fill: parent; enabled: root.enabled; cursorShape: Qt.PointingHandCursor; onClicked: root.clicked() }
}
