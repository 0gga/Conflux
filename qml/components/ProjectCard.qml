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
        return loader !== "minecraft" && ((root.project.type || "mod") !== "mod" || !root.selectedLoaders.length || root.selectedLoaders.indexOf(loader) !== -1)
    })
    signal versionsRequested
    signal authorRequested(var author)
    signal opened
    signal visibilityRequested(bool hidden)
    signal tagRequested(string tag)
    signal unlockRequested
    signal restoreLockedRequested
    signal installRequested
    implicitHeight: Math.max(Appearance.values.artworkSize * contentScale, heading.implicitHeight + 34 * contentScale, actions.implicitHeight) + 2 * inset
    radius: ControlStyle.panelRadius
    color: hover.hovered ? Theme.colors.hover : Theme.colors.card
    border.width: 0
    opacity: installState.installed ? Appearance.values.installedOpacity / 100 : 1
    function updatedAge(value) {
        return ContentTime.age(value, ContentTime.now)
    }
    function exactCount(value) { return Number(value || 0).toLocaleString(Qt.locale(), 'f', 0) }
    readonly property string downloadsHint: {
        const rows = (project.downloadStats || []).map(function(stat) {
            return Branding.label(stat.provider) + ": " + root.exactCount(stat.raw) + " downloads"
        })
        return root.exactCount(project.downloadsRaw) + " total downloads" + (rows.length ? "\n" + rows.join("\n") : "")
    }
    function updatedHint(value) {
        return ContentTime.updatedHint(value, ContentTime.now)
    }
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.opened() }
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
            ContentLink { onClicked: root.opened(); id: title; objectName: "browseTitle"; text: root.project.title; width: Math.min(implicitWidth,parent.width*0.68); color: Theme.colors.text; font.pixelSize: 18 * root.contentScale; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
            AuthorLinks { authors: root.project.authors || []; author: root.project.author || ""; authorIds: root.project.authorIds || {}; prefix: "by "; onAuthorRequested: function(author) { root.authorRequested(author) }; visible: !!Appearance.values.showAuthor; objectName: "browseAuthor"; anchors.baseline: title.baseline; width: Math.max(0,parent.width-title.width-parent.spacing); color: Theme.colors.muted; font.pixelSize: 12 * root.contentScale }
        }
        Text { objectName: "browseSummary"; visible: !!Appearance.values.showSummary; width: parent.width; text: root.project.summary || ""; color: Theme.colors.muted; font.pixelSize: 12 * root.contentScale; elide: Text.ElideRight; textFormat: Text.PlainText }
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
                delegate: BrandBadge { required property string modelData; objectName: "browsePlatformBadge"; brand: modelData; iconOnly: true; tooltipsEnabled: false; badgeScale: root.contentScale }
            }
            Repeater {
                model: root.supportedLoaders
                delegate: BrandBadge { required property string modelData; objectName: "browseLoaderBadge"; brand: modelData; iconOnly: true; tooltipsEnabled: false; badgeScale: root.contentScale }
            }
            Rectangle {
                objectName: "browseVersionBadge"
                visible: root.minecraft.length > 0
                width: versionText.implicitWidth + 16 * root.contentScale; height: tags.height; radius: height/2; color: Theme.colors.input
                ContentLink { onClicked: root.versionsRequested(); id: versionText; anchors.centerIn: parent; text: root.minecraft; color: Theme.colors.muted; font.pixelSize: 11 * root.contentScale }
                Accessible.name: "Minecraft " + root.minecraft
            }
            Item { width: 3 * root.contentScale; height: 1 }
            Rectangle {
                objectName: "browseEnvironmentBadge"
                visible: !!Appearance.values.showEnvironment && !!root.project.environment && root.project.environment !== "unknown"
                width: tags.height; height: tags.height; radius: height/2; color: Theme.colors.input
                Icon { anchors.centerIn: parent; name: root.project.environment === "client" ? "monitor" : "world"; size: 14 * root.contentScale }
                Accessible.name: Branding.label(root.project.environment || "")
            }
            Repeater {
                model: Appearance.values.showCategories ? (root.project.categories || []).filter(function(tag) { return (root.project.loaders || []).indexOf(tag) === -1 }) : []
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
        objectName: "browseActions"
        anchors.right: more.left; anchors.rightMargin: 6 * root.contentScale
        anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.topMargin: root.inset; anchors.bottomMargin: root.inset
        width: Math.max(140 * root.contentScale, installButton.implicitWidth, statistics.implicitWidth, updated.implicitWidth)
        spacing: 4 * root.contentScale
        ActionButton {
            tooltipsEnabled: false
            id: installButton; objectName: "browseInstallButton"
            Layout.alignment: Qt.AlignRight
            text: root.installState.label || "Install"; iconName: root.installState.installed ? "check" : root.installState.locked ? "lock" : "plus"
            accented: true; compact: true; font.pixelSize: 12 * root.contentScale; implicitHeight: 32 * root.contentScale
            enabled: !!root.installState.canInstall; onClicked: root.installRequested()
            opacity: root.installState.installed || enabled ? 1 : 0.7
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            id: statistics; objectName: "browseStatistics"
            Layout.alignment: Qt.AlignRight; spacing: 8 * root.contentScale
            ContentStatistic { objectName: "browseDownloads"; visible: !!Appearance.values.showDownloads; iconName: "download"; text: root.project.downloads || "0"; hint: root.downloadsHint; statisticScale: root.contentScale }
            ContentStatistic { objectName: "browseFollowers"; visible: !!Appearance.values.showFollowers && (root.project.followersRaw || 0)>0; iconName: "heart"; text: root.project.followers || ""; hint: root.exactCount(root.project.followersRaw) + " followers"; statisticScale: root.contentScale }
        }
        ContentStatistic { id: updated; objectName: "browseUpdated"; Layout.alignment: Qt.AlignRight; visible: !!Appearance.values.showUpdated && text.length > 0; iconName: "history"; text: root.updatedAge(root.project.updated || ""); hint: root.updatedHint(root.project.updated || ""); statisticScale: root.contentScale }
    }
    PopupButton {
        tooltipsEnabled: false
        popup: menu
        id: more; objectName: "browseMoreButton"
        anchors.right: parent.right; anchors.rightMargin: root.inset; anchors.top: parent.top; anchors.topMargin: root.inset
        width: 28 * root.contentScale; implicitWidth: width; implicitHeight: 32 * root.contentScale
        iconName: "more"; compact: true; subtle: true; hint: "Content actions"
    }
    FocusPopup {
        id: menu; objectName: "browseContentMenu"; parent: more; x: more.width-width; y: more.height+4; width: 185; padding: 6
        background: Rectangle { color: Theme.colors.card; border.color: Theme.colors.border; radius: ControlStyle.panelRadius }
        contentItem: ColumnLayout {
            spacing: 3
            MenuOption { Layout.fillWidth: true; text: "View project"; iconName: "compass"; alignLeft: true; subtle: true; onClicked: { menu.close();root.opened() } }
            MenuOption { Layout.fillWidth:true;visible:!!root.installState.locked && !root.installState.sourceLocked;text:"Unlock content";iconName:"unlock";subtle:true;alignLeft:true;onClicked:{menu.close();root.unlockRequested()} }
            MenuOption { Layout.fillWidth:true;visible:!!root.installState.sourceLocked && !!root.installState.sourceMissing;text:"Restore locked version";iconName:"restore";subtle:true;alignLeft:true;onClicked:{menu.close();root.restoreLockedRequested()} }
            MenuOption { Layout.fillWidth:true;visible:!!root.installState.sourceLocked && !root.installState.sourceMissing;text:"Locked by Modrinth";iconName:"lock";enabled:false;subtle:true;alignLeft:true }
            MenuOption { objectName: "browseHideContent"; Layout.fillWidth: true; text: root.hiddenMode ? "Restore content" : "Hide content"; iconName: root.hiddenMode ? "restore" : "hide"; alignLeft: true; subtle: true; onClicked: { menu.close();root.visibilityRequested(!root.hiddenMode) } }
        }
    }
}
