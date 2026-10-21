pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "../showcase"
ColumnLayout {
    id: root; objectName: "changelogView"
    required property var showcase
    property string mode: "latest"
    signal linkActivated(string url)
    signal imageRequested(string url)
    readonly property var selectedRelease: showcase.changelogRelease || ({})
    readonly property var releases: showcase.changelogVersions || []
    function activate() {
        if (!visible) return
        if (mode === "latest" && releases.length) showcase.selectChangelog(releases[0])
        else if (mode === "all") Qt.callLater(revealSelection)
    }
    function revealSelection() {
        if (visible && mode === "all" && showcase.changelogIndex >= 0)
            history.viewport.positionViewAtIndex(showcase.changelogIndex, ListView.Beginning)
    }
    onVisibleChanged: activate()
    onModeChanged: activate()
    Component.onCompleted: activate()
    Connections {
        target: root.showcase
        function onChanged() { if (!root.selectedRelease.id) root.activate() }
    }
    SectionTabs {
        subtabs: true; entries: [{key:"latest",label:"Latest"},{key:"all",label:"All versions"}]
        current: root.mode; onSelected: function(key) { root.mode = key }
    }
    Text { visible: !root.releases.length && !root.showcase.busy; text: "No versions available"; color: Theme.colors.muted; font.pixelSize: 13 }
    BoundedScrollView {
        id: latest; objectName: "changelogScroll"
        visible: root.mode === "latest"; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
        ColumnLayout {
            width: latest.availableWidth; spacing: 12
            Text { Layout.fillWidth: true; text: root.selectedRelease.version || ""; font.pixelSize: 20; font.weight: Font.DemiBold; color: Theme.colors.text; elide: Text.ElideRight }
            BusyIndicator { visible: root.showcase.changelogBusy; running: visible; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
            Text { Layout.fillWidth: true; visible: !!root.showcase.changelogNotice; text: root.showcase.changelogNotice || ""; wrapMode: Text.Wrap; color: Theme.colors.warning; font.pixelSize: 13 }
            ActionButton { visible: !!root.showcase.changelogNotice; iconName: "refresh"; hint: "Retry changelog"; onClicked: root.showcase.requestChangelog(root.showcase.changelogIndex,true) }
            Text { visible: !root.showcase.changelogBusy && !root.showcase.changelogNotice && !!root.selectedRelease.id && !(root.showcase.changelogBlocks || []).length; text: "No changelog provided"; color: Theme.colors.muted; font.pixelSize: 13 }
            DescriptionView { Layout.fillWidth: true; blocks: root.showcase.changelogBlocks || []; onLinkActivated: function(url) { root.linkActivated(url) }; onImageRequested: function(url) { root.imageRequested(url) } }
        }
    }
    ContentListView {
        id: history; objectName: "changelogHistory"
        visible: root.mode === "all"; Layout.fillWidth: true; Layout.fillHeight: true
        model: visible && root.visible ? root.showcase.changelogModel : null
        cornerRadius: 0; spacing: 0
        onCountChanged: Qt.callLater(root.revealSelection)
        delegate: Column {
            id: entry
            required property var modelData
            required property int index
            width: history.viewportWidth; spacing: 12
            topPadding: index ? 24 : 0; bottomPadding: 24
            Component.onCompleted: root.showcase.requestChangelog(index)
            Text { objectName: "changelogVersionHeading"; width: parent.width; text: entry.modelData.version; color: Theme.colors.text; font.pixelSize: 20; font.weight: Font.DemiBold; wrapMode: Text.Wrap }
            BusyIndicator { visible: entry.modelData.busy; running: visible; width: 24; height: 24 }
            Text { width: parent.width; visible: !!entry.modelData.error; text: entry.modelData.error || ""; wrapMode: Text.Wrap; color: Theme.colors.warning; font.pixelSize: 13 }
            ActionButton { visible: !!entry.modelData.error; iconName: "refresh"; hint: "Retry changelog"; onClicked: root.showcase.requestChangelog(entry.index,true) }
            Text { visible: entry.modelData.loaded && !entry.modelData.blocks.length; text: "No changelog provided"; color: Theme.colors.muted; font.pixelSize: 13 }
            DescriptionView { width: parent.width; blocks: entry.modelData.blocks; onLinkActivated: function(url) { root.linkActivated(url) }; onImageRequested: function(url) { root.imageRequested(url) } }
            Rectangle { objectName: "changelogDivider"; visible: entry.index < history.count-1; width: parent.width; height: 1; color: Theme.colors.border }
        }
    }
}
