pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "../components"

Item {
    id: root
    required property var app
    required property var showcase
    required property var contentController
    property string tab: "description"
    property string versionSource: "all"
    signal tabRequested(string key)
    readonly property var versions: showcase.versions.map(function(group) {
        return {label:group.label, variants:group.variants.filter(function(release) {
            return root.versionSource === "all" || (release.sources || [release.provider]).indexOf(root.versionSource) !== -1
        })}
    }).filter(function(group) { return group.variants.length > 0 })
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 28; spacing: 18
        RowLayout {
            Layout.fillWidth: true; spacing: 16
            ContentIcon { source: root.showcase.project.icon || ""; kind: "mod"; size: 64 }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 8
                Text { Layout.fillWidth: true; text: root.showcase.project.title || "Mod showcase"; color: Theme.colors.text; font.pixelSize: 29; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
                RowLayout { Repeater { model: root.showcase.project.sources || []; delegate: SourceBadge { required property string modelData; source: modelData } } }
                Text { visible: !!root.showcase.project.summary; Layout.fillWidth: true; text: root.showcase.project.summary || ""; color: Theme.colors.muted; font.pixelSize: 13; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
            }
            BusyIndicator { visible: root.showcase.busy; running: visible; Layout.preferredWidth: 32; Layout.preferredHeight: 32 }
            ActionButton { text: "View project page"; subtle: true; visible: !!root.showcase.project.url; onClicked: root.app.openWeb(root.showcase.project.url) }
        }
        SectionTabs { entries: [{key:"description",label:"Description",icon:"info"},{key:"gallery",label:"Gallery",icon:"image"},{key:"versions",label:"Versions",icon:"versions"}]; current: root.tab; onSelected: function(key) { root.tabRequested(key) } }
        Text { Layout.fillWidth: true; visible: root.showcase.notice.length > 0; text: root.showcase.notice; color: Theme.colors.warning; font.pixelSize: 12; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
        Text { Layout.fillWidth: true; visible: root.contentController.notice.length > 0; text: root.contentController.notice; color: Theme.colors.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
        StackLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            currentIndex: ["description","gallery","versions"].indexOf(root.tab)
            ScrollView {
                id: descriptionScroll; clip: true
                TextEdit {
                    width: descriptionScroll.availableWidth; height: contentHeight
                    text: root.showcase.description ? root.showcase.description.replace(/<a /g,'<a style="color: ' + Theme.colors.accent + ';" ') : (root.showcase.busy ? "Finding the verified project and loading its showcase…" : "No description is available.")
                    color: Theme.colors.text; selectionColor: Theme.colors.selection; selectedTextColor: Theme.colors.text
                    font.pixelSize: 14; textFormat: TextEdit.RichText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                    onLinkActivated: function(link) { root.app.openWeb(link) }
                }
            }
            Item {
                GridView {
                    id: gallery; anchors.fill: parent; clip: true; model: root.showcase.gallery
                    cellWidth: width / Math.max(1,Math.floor(width / 340)); cellHeight: 250
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        required property string modelData
                        width: gallery.cellWidth - 12; height: gallery.cellHeight - 12; radius: 14; color: Theme.colors.card; border.color: Theme.colors.border
                        Image { anchors.fill: parent; anchors.margins: 8; source: parent.modelData; asynchronous: true; fillMode: Image.PreserveAspectFit }
                    }
                }
                EmptyState { anchors.centerIn: parent; visible: !root.showcase.busy && root.showcase.gallery.length === 0; title: "No gallery images"; message: "This project has no available gallery images."; symbol: "image" }
            }
            ColumnLayout {
                spacing: 14
                RowLayout {
                    Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: root.versions.length + " versions · matching this pack's Minecraft version and loader"; color: Theme.colors.muted; font.pixelSize: 12; wrapMode: Text.WordWrap }
                    SelectBox { model: ["All sources", "Modrinth", "CurseForge"]; onActivated: root.versionSource = ["all","modrinth","curseforge"][currentIndex] }
                }
                ListView {
                    id: versionList; Layout.fillWidth: true; Layout.fillHeight: true; spacing: 10; clip: true; model: root.versions; ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: version; required property var modelData
                        property int variantIndex: 0
                        readonly property var release: modelData.variants[Math.min(variantIndex,modelData.variants.length - 1)] || ({})
                        width: versionList.width; height: version.modelData.variants.length > 1 ? 132 : 96; radius: 14; color: Theme.colors.card; border.color: Theme.colors.border
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 16; spacing: 16
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 8
                                Text { Layout.fillWidth: true; text: version.modelData.label; color: Theme.colors.text; font.pixelSize: 15; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
                                Text { Layout.fillWidth: true; text: (version.release.loaders || []).join(" / ") + " · " + (version.release.gameVersions || []).join(", ") + " · " + (version.release.published || ""); color: Theme.colors.muted; font.pixelSize: 12; elide: Text.ElideRight }
                                SelectBox { visible: version.modelData.variants.length > 1; Layout.fillWidth: true; model: version.modelData.variants.map(function(row) { return (row.provider === "modrinth" ? "Modrinth" : "CurseForge") + " · " + row.filename }); onActivated: version.variantIndex = currentIndex }
                            }
                            SourceBadge { source: version.release.provider || "modrinth" }
                            ActionButton { text: "Add to pack"; iconName: "plus"; primary: true; visible: !!root.app.activePack.path && version.release.provider === "modrinth"; enabled: !root.contentController.busy && !root.app.repositoryBusy; onClicked: root.contentController.installRelease(version.release,root.showcase.project) }
                            ActionButton { text: "View release"; onClicked: root.app.openWeb(version.release.pageUrl) }
                        }
                    }
                }
            }
        }
    }
}
