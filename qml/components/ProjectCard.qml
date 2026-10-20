pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Conflux.Style

Rectangle {
    id: root
    required property var project
    required property var installState
    property string minecraft: ""
    property string preferredLoader: ""
    readonly property var supportedLoaders: (project.loaders || []).filter(function(loader) { return loader !== "minecraft" && loader !== "datapack" })
    signal opened
    signal visibilityRequested(bool hidden)
    signal tagRequested(string tag)
    property bool hiddenMode: false
    signal installRequested
    implicitHeight: Math.max(154, details.implicitHeight + 36)
    radius: 16
    color: hover.containsMouse ? Theme.colors.hover : Theme.colors.card
    border.color: Theme.colors.border
    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.opened()
    }
    ActionButton {
        id: more; objectName: "browseMoreButton"; anchors.left: parent.left; anchors.leftMargin: 5; anchors.top: parent.top; anchors.topMargin: 16
        width: 30; implicitWidth: 30; iconName: "more"; compact: true; subtle: true; hint: "Content actions"; onClicked: menu.open()
    }
    Popup {
        id: menu; parent: more; y: more.height+4; width: 185; padding: 6
        background: Rectangle { color: Theme.colors.card; border.color: Theme.colors.border; radius: 12 }
        contentItem: ColumnLayout {
            spacing: 3
            ActionButton { Layout.fillWidth: true; text: "View project"; iconName: "compass"; alignLeft: true; subtle: true; onClicked: { menu.close();root.opened() } }
            ActionButton { objectName: "browseHideContent"; Layout.fillWidth: true; text: root.hiddenMode ? "Restore content" : "Hide content"; iconName: root.hiddenMode ? "restore" : "hide"; alignLeft: true; subtle: true; onClicked: { menu.close();root.visibilityRequested(!root.hiddenMode) } }
        }
    }
    ContentIcon {
        id: artwork
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: 18
        anchors.leftMargin: 42
        size: 88
        source: root.project.icon || ""
        kind: root.project.type === "shader" ? "shader" : root.project.type === "resourcepack" ? "resource" : root.project.type === "datapack" ? "world" : "mod"
    }
    Column {
        id: details
        anchors.left: artwork.right
        anchors.leftMargin: 16
        anchors.right: actions.left
        anchors.rightMargin: 16
        anchors.top: parent.top
        anchors.topMargin: 18
        spacing: 8
        Row {
            width: parent.width
            spacing: 8
            Text {
                text: root.project.title
                width: Math.min(implicitWidth, parent.width * 0.68)
                color: Theme.colors.text
                font.pixelSize: 18
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
            Text {
                width: Math.max(0, parent.width - parent.children[0].width - parent.spacing)
                text: "by " + (root.project.author || "Unknown author")
                color: Theme.colors.muted
                font.pixelSize: 13
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
        }
        Text {
            width: parent.width
            text: root.project.summary || ""
            color: Theme.colors.muted
            font.pixelSize: 13
            maximumLineCount: 2
            elide: Text.ElideRight
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
        }
        Flow {
            width: parent.width
            spacing: 5
            Repeater {
                model: root.project.sources && root.project.sources.length ? root.project.sources : [root.project.provider]
                delegate: BrandBadge {
                    required property string modelData
                    brand: modelData
                    iconOnly: true
                }
            }
            Rectangle {
                visible: !!root.project.environment && root.project.environment !== "unknown"
                width: environment.implicitWidth + 30
                height: 26
                radius: 12
                color: Theme.colors.input
                Icon {
                    anchors.left: parent.left
                    anchors.leftMargin: 7
                    anchors.verticalCenter: parent.verticalCenter
                    name: "world"
                    size: 14
                }
                Text {
                    id: environment
                    anchors.left: parent.left
                    anchors.leftMargin: 25
                    anchors.verticalCenter: parent.verticalCenter
                    text: Branding.label(root.project.environment || "")
                    color: Theme.colors.muted
                    font.pixelSize: 11
                }
            }
            Repeater {
                model: (root.project.categories || []).filter(function (tag) {
                    return (root.project.loaders || []).indexOf(tag) === -1;
                })
                delegate: Rectangle {
                    required property string modelData
                    width: tag.implicitWidth + 16
                    height: 26
                    radius: 12
                    color: Theme.colors.input
                    MouseArea { anchors.fill: parent; enabled: root.project.provider === "modrinth"; cursorShape: Qt.PointingHandCursor; onClicked: root.tagRequested(parent.modelData) }
                    Text {
                        id: tag
                        anchors.centerIn: parent
                        text: parent.modelData.charAt(0).toUpperCase() + parent.modelData.slice(1)
                        color: Theme.colors.muted
                        font.pixelSize: 11
                        textFormat: Text.PlainText
                    }
                }
            }
            CompatibilityBadges { minecraft: root.minecraft; loader: root.supportedLoaders.indexOf(root.preferredLoader) !== -1 ? root.preferredLoader : (root.supportedLoaders[0] || "") }
            Repeater {
                model: root.supportedLoaders.filter(function(loader) { return loader !== (root.supportedLoaders.indexOf(root.preferredLoader) !== -1 ? root.preferredLoader : root.supportedLoaders[0]) })
                delegate: BrandBadge {
                    required property string modelData
                    brand: modelData
                    iconOnly: true
                }
            }
        }
    }
    ColumnLayout {
        id: actions
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 18
        width: 130
        spacing: 7
        ActionButton {
            objectName: "browseInstallButton"
            Layout.alignment: Qt.AlignRight
            text: root.installState.label || "Install"
            iconName: root.installState.installed ? "check" : "plus"
            accented: true
            compact: true
            enabled: !!root.installState.canInstall
            onClicked: root.installRequested()
        }
        Item {
            Layout.fillHeight: true
        }
        Row {
            Layout.alignment: Qt.AlignRight
            spacing: 9
            Repeater {
                model: root.project.downloadStats || []
                delegate: BrandBadge {
                    required property var modelData
                    brand: modelData.provider
                    text: modelData.count
                    framed: false
                }
            }
        }
        Text {
            Layout.alignment: Qt.AlignRight
            text: root.project.updated ? "Updated " + root.project.updated.slice(0, 10) : ""
            color: Theme.colors.muted
            font.pixelSize: 11
        }
    }
}
