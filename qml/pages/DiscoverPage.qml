pragma ComponentBehavior: Bound
import QtQuick
import Conflux.Style
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    id: root
    required property var app
    property string source: "merged"
    signal createRequested()
    signal workspaceRequested()
    signal showcaseRequested(var project)
    function runSearch() { app.search(query.text, source, game.text, loader.currentIndex === 0 ? "" : loader.currentText) }
    function focusSearch() { query.forceActiveFocus() }
    Component.onCompleted: runSearch()
    Connections {
        target: root.app
        function onPackOpened() {
            game.text = root.app.activePack.minecraft || ""
            loader.currentIndex = Math.max(0, loader.model.indexOf(root.app.activePack.loader))
            root.runSearch()
        }
    }
    Timer { id: debounce; interval: 450; onTriggered: root.runSearch() }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 32; spacing: 20
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 7
                Text { text: "Find your next addition."; color: Theme.colors.text; font.pixelSize: 30; font.weight: Font.DemiBold; font.letterSpacing: -0.7 }
                Text { text: "One catalogue. More possibilities."; color: Theme.colors.muted; font.pixelSize: 14 }
            }
            Item { Layout.fillWidth: true }
            Text { visible: !!root.app.activePack.name; text: (root.app.activePack.managed ? "Adding to  " : "Exploring for  ") + (root.app.activePack.name || ""); color: Theme.colors.accent; font.pixelSize: 12 }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 12
            SearchField {
                id: query; objectName: "modSearchInput"; Layout.fillWidth: true; implicitHeight: 44
                leadingIcon: "search"; placeholderText: "Search mods, discover something new…"; font.pixelSize: 14
                onTextEdited: debounce.restart(); onAccepted: root.runSearch()
            }
            SearchField {
                id: game; Layout.preferredWidth: 130; implicitHeight: 44; placeholderText: "Any MC version"
                text: root.app.activePack.minecraft || ""; onTextEdited: debounce.restart(); onAccepted: root.runSearch()
            }
            SelectBox { id: loader; Layout.preferredWidth: 135; implicitHeight: 44; model: ["Any loader", "fabric", "neoforge", "forge", "quilt"]; onActivated: root.runSearch() }
            ActionButton { text: "Search"; primary: true; onClicked: root.runSearch() }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 7
            Repeater {
                model: [{key:"merged",label:"All sources"}, {key:"modrinth",label:"Modrinth"}, {key:"curseforge",label:"CurseForge"}]
                delegate: ActionButton {
                    required property var modelData
                    text: modelData.label; subtle: true; compact: true; selected: root.source === modelData.key
                    onClicked: { root.source = modelData.key; root.runSearch() }
                }
            }
            Item { Layout.fillWidth: true }
            BusyIndicator { running: root.app.catalogueBusy; visible: running; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
            Text { text: root.app.catalogueBusy ? "Searching…" : root.app.projects.length + " results"; color: Theme.colors.muted; font.pixelSize: 12 }
        }
        Text { visible: root.app.catalogueNotice.length > 0; Layout.fillWidth: true; text: root.app.catalogueNotice; wrapMode: Text.WordWrap; color: Theme.colors.muted; font.pixelSize: 12 }
        RowLayout {
            visible: root.app.installBusy
            Layout.fillWidth: true
            BusyIndicator { running: root.app.installBusy; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
            Text { Layout.fillWidth: true; text: root.app.installationStatus; color: Theme.colors.accent; font.pixelSize: 12; elide: Text.ElideRight }
            ActionButton { text: "Cancel"; subtle: true; enabled: root.app.installCancellable; onClicked: root.app.cancelInstallation() }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 20
            Item {
                Layout.fillWidth: true; Layout.fillHeight: true
                ListView {
                    id: results
                    anchors.fill: parent; clip: true; spacing: 10; model: root.app.projects
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: resultCard
                        required property var modelData
                        required property int index
                        readonly property bool selected: root.app.selectedProject.id === modelData.id && root.app.selectedProject.provider === modelData.provider
                        width: results.width - 12; height: root.app.selectedProject.id ? 124 : 116; radius: 14
                        color: selected ? Theme.colors.input : resultMouse.containsMouse ? Theme.colors.hover : Theme.colors.card
                        border.color: selected ? Theme.colors.accent : Theme.colors.border
                        Rectangle { anchors.left: parent.left; anchors.leftMargin: 1; anchors.verticalCenter: parent.verticalCenter; width: 3; height: 30; radius: 1.5; color: Theme.colors.accent; visible: resultCard.selected }
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 16; spacing: 16
                            Rectangle {
                                Layout.alignment: Qt.AlignTop; implicitWidth: 52; implicitHeight: 52; radius: 14; color: Theme.colors.border; clip: true
                                Text { anchors.centerIn: parent; text: resultCard.modelData.title.substring(0,1); color: Theme.colors.text; font.pixelSize: 25; font.weight: Font.DemiBold }
                                Image { anchors.fill: parent; source: resultCard.modelData.icon; fillMode: Image.PreserveAspectFit; asynchronous: true; visible: status === Image.Ready }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 5
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 10
                                    Text { text: resultCard.modelData.title; Layout.fillWidth: true; elide: Text.ElideRight; color: Theme.colors.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                                    Text { visible: !root.app.selectedProject.id; text: "↓ " + resultCard.modelData.downloads; color: Theme.colors.accent; font.pixelSize: 11 }
                                }
                                Text { text: "by " + resultCard.modelData.author; color: Theme.colors.accent; font.pixelSize: 11 }
                                Text { Layout.fillWidth: true; Layout.fillHeight: true; text: resultCard.modelData.summary; color: Theme.colors.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                                RowLayout {
                                    spacing: 6
                                    Repeater { model: resultCard.modelData.sources; delegate: SourceBadge { required property string modelData; source: modelData } }
                                }
                            }
                        }
                        MouseArea { id: resultMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.showcaseRequested(resultCard.modelData) }
                    }
                }
                EmptyState {
                    anchors.centerIn: parent; width: parent.width - 40
                    visible: !root.app.catalogueBusy && root.app.projects.length === 0
                    symbol: "⌕"; title: "Nothing here yet"
                    message: root.source === "curseforge" && !root.app.curseforgeEnabled ? "Add your approved CurseForge API key in Settings to search this source." : "Try another search, Minecraft version, or loader."
                }
            }
            Rectangle {
                visible: !!root.app.selectedProject.id
                Layout.preferredWidth: 390; Layout.fillHeight: true; radius: 14; color: Theme.colors.card; border.color: Theme.colors.border
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 20; spacing: 14
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "PROJECT"; color: Theme.colors.accent; font.pixelSize: 10; font.letterSpacing: 1.6 }
                        Item { Layout.fillWidth: true }
                        ActionButton { iconName: "close"; hint: "Close project"; subtle: true; compact: true; onClicked: root.app.closeProject() }
                    }
                    Text { Layout.fillWidth: true; text: root.app.selectedProject.title || ""; color: Theme.colors.text; font.pixelSize: 25; font.weight: Font.DemiBold; wrapMode: Text.WordWrap }
                    RowLayout {
                        Repeater { model: root.app.selectedProject.sources || []; delegate: SourceBadge { required property string modelData; source: modelData } }
                        Item { Layout.fillWidth: true }
                        ActionButton { text: "View page ↗"; subtle: true; implicitHeight: 30; onClicked: root.app.openWeb(root.app.selectedProject.url) }
                    }
                    BusyIndicator { visible: root.app.detailBusy; running: visible; Layout.alignment: Qt.AlignHCenter }
                    ScrollView {
                        id: detailScroll; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                        ColumnLayout {
                            width: detailScroll.availableWidth; spacing: 16
                            Text { Layout.fillWidth: true; text: root.app.selectedProject.summary || ""; color: Theme.colors.muted; wrapMode: Text.WordWrap; font.pixelSize: 13; lineHeight: 1.3 }
                            Text { visible: root.app.detailNotice.length > 0; Layout.fillWidth: true; text: root.app.detailNotice; color: Theme.colors.warning; font.pixelSize: 12; wrapMode: Text.WordWrap }
                            Text { text: "RELEASES"; color: Theme.colors.accent; font.pixelSize: 10; font.letterSpacing: 1.8; Layout.topMargin: 8 }
                            Text { Layout.fillWidth: true; text: root.app.activePack.managed ? "Add to " + root.app.activePack.name + ". Required dependencies are included automatically." : root.app.activePack.name ? "This launcher pack is read-only. Create a Conflux pack to install mods here." : "Create or open a Conflux modpack to add mods."; color: Theme.colors.muted; font.pixelSize: 12; wrapMode: Text.WordWrap }
                            Repeater {
                                model: root.app.releases.slice(0, 30)
                                delegate: Rectangle {
                                    id: releaseRow
                                    required property var modelData
                                    required property int index
                                    Layout.fillWidth: true; implicitHeight: 88; color: Theme.colors.panel; border.color: Theme.colors.border; radius: 12
                                    RowLayout {
                                        anchors.fill: parent; anchors.margins: 12; spacing: 8
                                        ColumnLayout {
                                            Layout.fillWidth: true; spacing: 6
                                            Text { Layout.fillWidth: true; text: releaseRow.modelData.version; color: Theme.colors.text; elide: Text.ElideRight; font.pixelSize: 13; font.weight: Font.DemiBold }
                                            Text { Layout.fillWidth: true; text: releaseRow.modelData.loaders.join(" / ") + "  ·  " + releaseRow.modelData.gameVersions.slice(-3).join(", "); color: Theme.colors.accent; font.pixelSize: 10; elide: Text.ElideRight }
                                            Text { text: releaseRow.modelData.published + (releaseRow.modelData.dependencies.length ? "  ·  " + releaseRow.modelData.dependencies.length + " required dependencies" : ""); color: Theme.colors.accent; font.pixelSize: 10 }
                                        }
                                        ActionButton { text: releaseRow.modelData.provider === "modrinth" ? "Add +" : "Soon"; implicitWidth: 64; enabled: releaseRow.modelData.provider === "modrinth" && !!root.app.activePack.managed && !root.app.repositoryBusy && !!releaseRow.modelData.url; onClicked: root.app.pinRelease(releaseRow.index) }
                                    }
                                }
                            }
                            Text { visible: !root.app.detailBusy && root.app.releases.length === 0; text: "No releases match these filters."; color: Theme.colors.muted; font.pixelSize: 12 }
                            Text { visible: root.app.releases.length > 30; text: "Showing the latest 30 releases. Narrow your filters to find another."; Layout.fillWidth: true; color: Theme.colors.accent; font.pixelSize: 11; wrapMode: Text.WordWrap }
                            Text { text: "ABOUT"; color: Theme.colors.accent; font.pixelSize: 10; font.letterSpacing: 1.8; Layout.topMargin: 8 }
                            Text { Layout.fillWidth: true; text: root.app.description || "Description will appear here."; color: Theme.colors.muted; font.pixelSize: 12; wrapMode: Text.Wrap; lineHeight: 1.35; maximumLineCount: 60; elide: Text.ElideRight; textFormat: Text.PlainText }
                            Text { visible: root.app.gallery.length > 0; text: "GALLERY"; color: Theme.colors.accent; font.pixelSize: 10; font.letterSpacing: 1.8 }
                            Repeater { model: root.app.gallery.slice(0, 6); delegate: Image { required property string modelData; Layout.fillWidth: true; Layout.preferredHeight: 170; source: modelData; asynchronous: true; fillMode: Image.PreserveAspectFit } }
                            Text { Layout.fillWidth: true; text: "Public comments are not available through the supported APIs."; color: Theme.colors.accent; font.pixelSize: 11; wrapMode: Text.WordWrap }
                        }
                    }
                    ActionButton { visible: !root.app.activePack.name; Layout.fillWidth: true; text: "+ Create a modpack"; primary: true; onClicked: root.createRequested() }
                    ActionButton { visible: !!root.app.activePack.name; Layout.fillWidth: true; text: "Open modpack  →"; onClicked: root.workspaceRequested() }
                }
            }
        }
    }
}
