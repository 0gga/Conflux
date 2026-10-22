pragma ComponentBehavior: Bound
import QtQuick
import Conflux.Style
Item {
    id: root
    property var authors: []
    property string author: ""
    property var authorIds: ({})
    property string prefix: ""
    property color color: Theme.colors.muted
    property alias font: metrics.font
    readonly property var entries: authors && authors.length ? authors : [{name: author || "Unknown author", ids: authorIds || {}}]
    signal authorRequested(var author)
    implicitHeight: metrics.height
    implicitWidth: metrics.advanceWidth(prefix + entries.map(function(entry) { return entry.name }).join(", "))
    baselineOffset: prefixLabel.baselineOffset
    clip: true
    FontMetrics { id: metrics; font.pixelSize: 12 }
    Row {
        Text { id: prefixLabel; text: root.prefix; font: root.font; color: root.color; textFormat: Text.PlainText }
        Repeater {
            model: root.entries
            delegate: Row {
                id: entry
                required property var modelData
                required property int index
                Text { id: separator; text: entry.index ? ", " : ""; font: root.font; color: root.color; textFormat: Text.PlainText }
                ContentLink {
                    objectName: "authorNameLink"
                    text: entry.modelData.name || "Unknown author"
                    color: root.color
                    font.family: root.font.family; font.pixelSize: root.font.pixelSize
                    font.weight: root.font.weight; font.italic: root.font.italic
                    font.letterSpacing: root.font.letterSpacing
                    width: Math.min(implicitWidth, Math.max(0, root.width - entry.x - separator.width))
                    enabled: width > 0 && !!entry.modelData.ids && Object.keys(entry.modelData.ids).length > 0
                    onClicked: root.authorRequested(entry.modelData)
                }
            }
        }
    }
}
