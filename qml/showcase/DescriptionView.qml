pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
import "../components"
Column {
    id: root
    required property var blocks
    signal linkActivated(string url)
    signal imageRequested(string url)
    spacing: 12
    Repeater {
        model: root.blocks
        delegate: Loader {
            id: blockLoader
            required property var modelData
            width: root.width; height: item ? (item as Item).implicitHeight : 0
            sourceComponent: modelData.kind === "images" ? images : modelData.kind === "table" ? table : text
            Component {
                id: text
                Rectangle {
                    width: blockLoader.width; implicitHeight: body.contentHeight+body.topPadding+body.bottomPadding
                    radius: 10; color: blockLoader.modelData.code ? Theme.colors.input : "transparent"
                    TextEdit {
                        id: body; objectName: "descriptionTextBlock"; width: parent.width
                        leftPadding: (blockLoader.modelData.indent || 0)*14+(blockLoader.modelData.code || blockLoader.modelData.quote ? 12 : 0)
                        topPadding: blockLoader.modelData.code ? 10 : 0; bottomPadding: topPadding
                        text: (blockLoader.modelData.html || "").replace(/<a /g,'<a style="color: '+Theme.colors.accent+';" ')
                        textFormat: TextEdit.RichText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                        color: Theme.colors.text; selectionColor: Theme.colors.selection; selectedTextColor: Theme.colors.text
                        font.pixelSize: 14; font.family: blockLoader.modelData.code ? "Consolas" : "Segoe UI"
                        onLinkActivated: function(link) { root.linkActivated(link) }
                    }
                    Rectangle { visible: !!blockLoader.modelData.quote; width: 3; height: parent.height; radius: 1.5; color: Theme.colors.accent }
                }
            }
            Component {
                id: images
                Item {
                    width: blockLoader.width; implicitHeight: row.height
                    Row {
                        id: row; spacing: 8
                        x: blockLoader.modelData.alignment === "center" || blockLoader.modelData.items.length>1 ? (parent.width-width)/2 : 0
                        Repeater {
                            model: blockLoader.modelData.items
                            delegate: ContentIcon {
                                required property var modelData
                                objectName: "descriptionImage"
                                readonly property int imageCount: blockLoader.modelData.items.length
                                width: Math.min(blockLoader.width/imageCount-(imageCount>1 ? 8 : 0),modelData.width>0 ? modelData.width : naturalSize.width>0 ? naturalSize.width : imageCount>1 ? 64 : blockLoader.width)
                                height: naturalSize.width>0 ? width*naturalSize.height/naturalSize.width : modelData.height>0 ? modelData.height : Math.min(width,180)
                                source: modelData.source; intrinsicSize: true; preserveAspectFit: true; radius: 10; kind: "image"
                                Accessible.name: modelData.alt || "Description image"
                                TapHandler { onTapped: parent.modelData.link ? root.linkActivated(parent.modelData.link) : root.imageRequested(parent.modelData.source) }
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                            }
                        }
                    }
                }
            }
            Component {
                id: table
                Column {
                    width: blockLoader.width; spacing: 4
                    Repeater {
                        model: blockLoader.modelData.rows
                        delegate: RowLayout {
                            id: tableRow
                            required property var modelData
                            width: blockLoader.width; spacing: 4
                            Repeater {
                                model: tableRow.modelData.cells
                                delegate: Rectangle {
                                    required property var modelData
                                    Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.fillHeight: true
                                    implicitHeight: cell.height+16; radius: 8; color: Theme.colors.panel
                                    Loader {
                                        id: cell; x: 8; y: 8; width: parent.width-16
                                        height: item ? (item as Item).implicitHeight : 0
                                        property var cellBlocks: parent.modelData.blocks
                                        property bool initialized: false
                                        function reload() { setSource(Qt.resolvedUrl("DescriptionView.qml"),{blocks:cellBlocks}) }
                                        onCellBlocksChanged: if (initialized) reload()
                                        Component.onCompleted: { initialized=true;reload() }
                                        Connections {
                                            target: cell.item; ignoreUnknownSignals: true
                                            function onLinkActivated(url) { root.linkActivated(url) }
                                            function onImageRequested(url) { root.imageRequested(url) }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
