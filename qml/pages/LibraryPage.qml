pragma ComponentBehavior: Bound
import QtQuick
import Conflux.Style
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
Item {
    id: root
    required property var app
    property string source: "all"
    property bool hiddenTab: false
    property string query: ""
    readonly property var rows: app.libraryPacks.filter(function(pack) {
        return !!pack.hidden === root.hiddenTab && (root.source === "all" || pack.provider === root.source)
            && pack.title.toLowerCase().indexOf(root.query.toLowerCase()) !== -1
    })
    signal createRequested()
    signal discoverRequested()
    signal packRequested(string path, bool explore)
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 32; spacing: 22
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 8
                RowLayout {
                    spacing: 12
                    Text { text: "Library"; color: Theme.colors.text; font.pixelSize: 31; font.weight: Font.DemiBold; font.letterSpacing: -0.7 }
                    Rectangle { implicitWidth: total.implicitWidth + 18; implicitHeight: 26; radius: 8; color: Theme.colors.input; Text { id: total; anchors.centerIn: parent; text: root.app.visiblePackCount; color: Theme.colors.muted; font.pixelSize: 12 } }
                }
                Text { text: "Your modpacks, across every launcher."; color: Theme.colors.muted; font.pixelSize: 13 }
            }
            Item { Layout.fillWidth: true }
            ActionButton { iconName: "refresh"; text: root.app.libraryBusy ? "Scanning…" : "Refresh"; enabled: !root.app.libraryBusy; subtle: true; onClicked: root.app.refreshLibrary() }
        }
        Rectangle {
            objectName: "libraryOnboarding"
            visible: root.app.visiblePackCount === 0 && !root.app.libraryBusy
            Layout.fillWidth: true; implicitHeight: 150; color: Theme.colors.card; border.color: Theme.colors.border; radius: 12
            RowLayout {
                anchors.fill: parent; anchors.margins: 24; spacing: 24
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 12
                    Text { text: "A home for every mod."; color: Theme.colors.text; font.pixelSize: 25; font.weight: Font.DemiBold }
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: root.app.hiddenPackCount > 0 ? "Your packs are hidden. Restore them in Hidden, or start something new." : "Create a pack or connect your Modrinth and CurseForge folders in Settings."; color: Theme.colors.muted; font.pixelSize: 13 }
                    Text { text: "MODRINTH  +  CURSEFORGE  +  GIT"; color: Theme.colors.accent; font.pixelSize: 10; font.letterSpacing: 2 }
                }
                ActionButton { text: "Explore mods →"; primary: true; onClicked: root.discoverRequested() }
            }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            SectionTabs {
                entries: [{key:"visible",label:"Modpacks",icon:"content"},{key:"hidden",label:"Hidden · " + root.app.hiddenPackCount,icon:"hide"}]
                current: root.hiddenTab ? "hidden" : "visible"
                onSelected: function(key) { root.hiddenTab = key === "hidden" }
            }
            Item { Layout.fillWidth: true }
            SearchField { Layout.preferredWidth: 260; leadingIcon: "search"; placeholderText: "Search your library…"; selectByMouse: true; onTextChanged: root.query = text }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 4
            Text { text: "Sources"; color: Theme.colors.muted; font.pixelSize: 12; Layout.rightMargin: 12 }
            Repeater {
                model: ["all", "conflux", "modrinth", "curseforge"]
                delegate: ActionButton {
                    required property string modelData
                    text: modelData === "all" ? "All sources" : modelData === "conflux" ? "Conflux" : modelData === "modrinth" ? "Modrinth" : "CurseForge"
                    compact: true; subtle: true; selected: root.source === modelData; onClicked: root.source = modelData
                }
            }
            Item { Layout.fillWidth: true }
            Text { text: root.rows.length + " modpacks"; color: Theme.colors.muted; font.pixelSize: 12 }
        }
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            ListView {
                id: packs; anchors.fill: parent; clip: true; spacing: 12; model: root.rows
                ScrollBar.vertical: ScrollBar {}
                delegate: LibraryRow {
                    id: packRow
                    required property var modelData
                    width: packs.width
                    titleText: modelData.title
                    detailText: (modelData.gameVersion || "Minecraft version unknown") + (modelData.loader ? "  ·  " + modelData.loader : "") + "  ·  " + modelData.modCount + " mods"
                    source: modelData.provider; thumbnail: modelData.icon || ""; hiddenPack: modelData.hidden
                    onActivated: root.packRequested(modelData.path, false)
                    onExploreRequested: root.packRequested(modelData.path, true)
                    onHideRequested: root.app.setPackHidden(modelData.path, !modelData.hidden)
                }
            }
            EmptyState {
                anchors.centerIn: parent; visible: root.rows.length === 0 && !root.app.libraryBusy
                title: root.hiddenTab ? "No hidden modpacks" : root.app.visiblePackCount > 0 ? "No matching modpacks" : "Your next adventure starts here."
                message: root.hiddenTab ? "Packs you hide will appear here. Their files stay in place." : "Try another source, clear your search, or create a modpack."
                symbol: "▦"
            }
        }
    }
}
