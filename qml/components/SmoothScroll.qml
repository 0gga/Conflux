pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import Conflux.Style
Item {
    id: root
    required property Flickable viewport
    parent: viewport
    anchors.fill: parent
    z: -1
    property real destination: 0
    readonly property bool scrolling: animation.running
    readonly property real minimum: viewport ? viewport.originY - viewport.topMargin : 0
    readonly property real maximum: viewport ? Math.max(minimum, viewport.originY + viewport.contentHeight - viewport.height + viewport.bottomMargin) : 0
    signal finished()
    function clamp(value) { return Math.max(minimum, Math.min(maximum, value)) }
    function stop() {
        animation.stop()
        if (viewport) { viewport.cancelFlick(); destination = clamp(viewport.contentY) }
    }
    function move(delta, precise) {
        if (!viewport || !delta) return
        const base = animation.running ? destination : viewport.contentY
        animation.stop()
        viewport.cancelFlick()
        destination = clamp((precise ? viewport.contentY : base) + delta)
        if (precise || !Appearance.values.smoothScrolling || Appearance.values.reduceMotion) {
            viewport.contentY = destination
            finished()
        } else if (Math.abs(destination - viewport.contentY) > 0.1) {
            animation.from = viewport.contentY
            animation.to = destination
            animation.start()
        }
    }
    function constrain() {
        if (!viewport) return
        if (destination < minimum || destination > maximum) stop()
        if (!viewport.dragging) viewport.contentY = clamp(viewport.contentY)
    }
    property NumberAnimation animation: NumberAnimation {
        target: root.viewport; property: "contentY"
        duration: Appearance.values.scrollDuration
        easing.type: Easing.OutCubic
        onFinished: root.finished()
    }
    property WheelHandler wheel: WheelHandler {
        parent: root.viewport
        target: null
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        acceptedModifiers: Qt.NoModifier
        enabled: root.viewport !== null && root.viewport.visible && root.viewport.enabled
        onWheel: function(event) {
            if (root.maximum <= root.minimum || (!event.pixelDelta.y && !event.angleDelta.y)) { event.accepted = false; return }
            const precise = event.pixelDelta.y !== 0
            root.move(precise ? -event.pixelDelta.y : -event.angleDelta.y / 120 * Appearance.values.scrollDistance, precise)
            event.accepted = true
        }
    }
    property Connections viewportEvents: Connections {
        target: root.viewport
        function onDraggingChanged() { if (root.viewport.dragging) root.stop() }
        function onContentHeightChanged() { root.constrain() }
        function onHeightChanged() { root.constrain() }
        function onVisibleChanged() { if (!root.viewport.visible) root.stop() }
    }
    property Connections windowEvents: Connections {
        target: root.viewport ? root.viewport.Window.window : null
        function onActiveFocusItemChanged() { if (root.scrolling) root.stop() }
        function onActiveChanged() { if (root.scrolling) root.stop() }
    }
    property Connections motionSettings: Connections {
        target: Appearance
        function onChanged() {
            if (root.scrolling && (!Appearance.values.smoothScrolling || Appearance.values.reduceMotion)) {
                root.animation.stop(); root.viewport.contentY = root.clamp(root.destination); root.finished()
            }
        }
    }
    property Connections scrollbarEvents: Connections {
        target: root.viewport ? root.viewport.ScrollBar.vertical : null
        function onPressedChanged() { if (root.viewport.ScrollBar.vertical.pressed) root.stop() }
    }
}
