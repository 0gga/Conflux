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
    property var selectedLoaders: []
    property bool hiddenMode: false
    readonly property real contentScale: Appearance.values.contentScale / 100
    readonly property real inset: Appearance.values.cardPadding * contentScale
    readonly property var supportedLoaders: (project.loaders || []).filter(function(loader) {
        return loader !== "minecraft" && loader !== "datapack" && (!root.selectedLoaders.length || root.selectedLoaders.indexOf(loader) !== -1)
    })
    signal opened
    signal visibilityRequested(bool hidden)
    signal tagRequested(string tag)
    signal installRequested
    implicitHeight: Math.max(Appearance.values.artworkSize * contentScale, heading.implicitHeight + 34 * contentScale, actions.implicitHeight) + 2 * inset
    radius: 14
    color: hover.containsMouse ? Theme.colors.hover : Theme.colors.card
    border.width: 0
    readonly property bool unavailable: !installState.canInstall && !installState.installed
    opacity: unavailable ? Appearance.values.unavailableOpacity / 100 : 1
    function updatedAge(value) {
        const days=Math.max(0,Math.floor((Date.now()-Date.parse(value))/86400000))
        if (!isFinite(days)) return ""
        return days===0 ? "Today" : days===1 ? "1 day ago" : days<60 ? days+" days ago" : days<730 ? Math.floor(days/30)+" months ago" : Math.floor(days/365)+" years ago"
    }
    MouseArea { id: hover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.opened() }
    ContentIcon {
        id: artwork; objectName: "browseArtwork"
        anchors.left: parent.left; anchors.leftMargin: root.inset; anchors.top: parent.top; anchors.topMargin: root.inset
        size: Math.max(0,root.height - 2 * root.inset)
        source: root.project.icon || ""
        kind: root.project.type === "shader" ? "shader" : root.project.type === "resourcepack" ? "resource" : root.project.type === "datapack" ? "world" : "mod"
    }
    Column {
        id: heading
        anchors.left: artwork.right; anchors.leftMargin: root.inset
        anchors.right: actions.left; anchors.rightMargin: root.inset
        anchors.top: parent.top; anchors.topMargin: root.inset
        spacing: 4 * root.contentScale
        Row {
            width: parent.width; spacing: 7 * root.contentScale
            Text { id: title; objectName: "browseTitle"; text: root.project.title; width: Math.min(implicitWidth,parent.width*0.68); color: Theme.colors.text; font.pixelSize: 18 * root.contentScale; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
            Text { objectName: "browseAuthor"; anchors.baseline: title.baseline; width: Math.max(0,parent.width-title.width-parent.spacing); text: "by " + (root.project.author || "Unknown author"); color: Theme.colors.muted; font.pixelSize: 12 * root.contentScale; elide: Text.ElideRight; textFormat: Text.PlainText }
        }
        Text { width: parent.width; text: root.project.summary || ""; color: Theme.colors.muted; font.pixelSize: 12 * root.contentScale; elide: Text.ElideRight; textFormat: Text.PlainText }
    }
    Flickable {
        id: tags; objectName: "browseBadges"
        anchors.left: heading.left; anchors.right: heading.right; anchors.bottom: artwork.bottom
        height: 26 * root.contentScale; clip: true
        contentWidth: badges.implicitWidth; contentHeight: height
        boundsBehavior: Flickable.StopAtBounds; flickableDirection: Flickable.HorizontalFlick
        Row {
            id: badges; spacing: 5 * root.contentScale
            Repeater {
                model: root.project.sources && root.project.sources.length ? root.project.sources : (root.project.provider ? [root.project.provider] : [])
                delegate: BrandBadge { required property string modelData; objectName: "browsePlatformBadge"; brand: modelData; iconOnly: true; badgeScale: root.contentScale }
            }
            Repeater {
                model: root.supportedLoaders
                delegate: BrandBadge { required property string modelData; objectName: "browseLoaderBadge"; brand: modelData; iconOnly: true; badgeScale: root.contentScale }
            }
            Rectangle {
                objectName: "browseVersionBadge"
                visible: root.minecraft.length > 0
                width: versionText.implicitWidth + 16 * root.contentScale; height: tags.height; radius: height/2; color: Theme.colors.input
                Text { id: versionText; anchors.centerIn: parent; text: root.minecraft; color: Theme.colors.muted; font.pixelSize: 11 * root.contentScale }
                HoverHandler { id: versionHover }
                RoundedToolTip { visible: versionHover.hovered; text: "Minecraft " + root.minecraft }
            }
            Item { width: 3 * root.contentScale; height: 1 }
            Rectangle {
                objectName: "browseEnvironmentBadge"
                visible: !!root.project.environment && root.project.environment !== "unknown"
                width: tags.height; height: tags.height; radius: height/2; color: Theme.colors.input
                Icon { anchors.centerIn: parent; name: root.project.environment === "client" ? "monitor" : "world"; size: 14 * root.contentScale }
                HoverHandler { id: environmentHover }
                RoundedToolTip { visible: environmentHover.hovered; text: Branding.label(root.project.environment || "") }
            }
            Repeater {
                model: (root.project.categories || []).filter(function(tag) { return (root.project.loaders || []).indexOf(tag) === -1 })
                delegate: Rectangle {
                    required property string modelData
                    objectName: "browseCategoryBadge"
                    width: label.implicitWidth + 16 * root.contentScale; height: tags.height; radius: height/2; color: Theme.colors.input
                    MouseArea { anchors.fill: parent; enabled: root.project.provider === "modrinth"; cursorShape: Qt.PointingHandCursor; onClicked: root.tagRequested(parent.modelData) }
                    Text { id: label; anchors.centerIn: parent; text: parent.modelData.charAt(0).toUpperCase()+parent.modelData.slice(1); color: Theme.colors.muted; font.pixelSize: 11 * root.contentScale; textFormat: Text.PlainText }
                }
            }
        }
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded; height: 3 }
    }
    ColumnLayout {
        id: actions
        anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.margins: root.inset
        width: Math.max(140 * root.contentScale, installButton.implicitWidth + more.width + 6 * root.contentScale)
        spacing: 3 * root.contentScale
        ActionButton {
            id: installButton; objectName: "browseInstallButton"
            Layout.alignment: Qt.AlignRight; Layout.rightMargin: more.width + 6 * root.contentScale
            text: root.installState.label || "Install"; iconName: root.installState.installed ? "check" : "plus"
            accented: true; compact: true; font.pixelSize: 12 * root.contentScale; implicitHeight: 32 * root.contentScale
            enabled: !!root.installState.canInstall; onClicked: root.installRequested()
            opacity: root.installState.installed || enabled ? 1 : 0.7
        }
        Item { Layout.fillHeight: true }
        Row {
            Layout.alignment: Qt.AlignRight; spacing: 8 * root.contentScale
            Repeater { model: root.project.downloadStats || []; delegate: BrandBadge { required property var modelData; brand: modelData.provider; text: modelData.count; framed: false; badgeScale: root.contentScale } }
            ContentStatistic { visible: (root.project.followersRaw || 0)>0; iconName: "heart"; text: root.project.followers || ""; hint: "Followers"; statisticScale: root.contentScale }
        }
        ContentStatistic { Layout.alignment: Qt.AlignRight; visible: !!root.project.updated; iconName: "history"; text: root.updatedAge(root.project.updated || ""); hint: root.project.updated || ""; statisticScale: root.contentScale }
    }
    PopupButton {
        popup: menu
        id: more; objectName: "browseMoreButton"
        anchors.right: parent.right; anchors.rightMargin: root.inset; anchors.top: parent.top; anchors.topMargin: root.inset
        width: 28 * root.contentScale; implicitWidth: width; implicitHeight: 32 * root.contentScale
        iconName: "more"; compact: true; subtle: true; hint: "Content actions"
    }
    FocusPopup {
        id: menu; objectName: "browseContentMenu"; parent: more; x: more.width-width; y: more.height+4; width: 185; padding: 6
        background: Rectangle { color: Theme.colors.card; border.color: Theme.colors.border; radius: 12 }
        contentItem: ColumnLayout {
            spacing: 3
            ActionButton { Layout.fillWidth: true; text: "View project"; iconName: "compass"; alignLeft: true; subtle: true; onClicked: { menu.close();root.opened() } }
            ActionButton { objectName: "browseHideContent"; Layout.fillWidth: true; text: root.hiddenMode ? "Restore content" : "Hide content"; iconName: root.hiddenMode ? "restore" : "hide"; alignLeft: true; subtle: true; onClicked: { menu.close();root.visibilityRequested(!root.hiddenMode) } }
        }
    }
}
