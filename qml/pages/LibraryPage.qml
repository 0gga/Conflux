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
        anchors.fill: parent; anchors.margins: 32; anchors.rightMargin: 16; spacing: root.height < 500 ? 12 : 22
        RowLayout {
            objectName: "libraryHeader"; Layout.fillWidth: true; Layout.rightMargin: 16
            Text { text: "Library"; color: Theme.colors.text; font.pixelSize: 31; font.weight: Font.DemiBold; font.letterSpacing: -0.7 }
            Item { Layout.fillWidth: true }
            ActionButton { objectName: "libraryCreate"; iconName: "plus"; text: "New modpack"; primary: true; enabled: !root.app.repositoryBusy; onClicked: root.createRequested() }
            ActionButton { objectName: "libraryRefresh"; iconName: "refresh"; hint: "Refresh library"; enabled: !root.app.libraryBusy; subtle: true; onClicked: root.app.refreshLibrary() }
        }
        Rectangle {
            objectName: "libraryOnboarding"
            visible: root.app.visiblePackCount === 0 && root.app.hiddenPackCount > 0 && !root.hiddenTab && !root.app.libraryBusy
            Layout.fillWidth: true; Layout.rightMargin: 16; implicitHeight: 72; color: Theme.colors.card; border.color: Theme.colors.border; radius: ControlStyle.panelRadius
            RowLayout {
                anchors.fill: parent; anchors.margins: 24; spacing: 24
                Icon { name: "hide"; size: 24 }
                Text { Layout.fillWidth: true; text: "All modpacks are hidden"; color: Theme.colors.muted; font.pixelSize: 13 }
                ActionButton { text: "Show hidden"; iconName: "restore"; onClicked: root.hiddenTab=true }

            }
        }
        GridLayout {
            Layout.fillWidth: true; Layout.rightMargin: 16; columns: root.width < 650 ? 1 : 2; columnSpacing: 16; rowSpacing: 10
            Item {
                Layout.fillWidth: true; implicitWidth: libraryTabs.implicitWidth; implicitHeight: libraryTabs.implicitHeight
                SectionTabs {
                    id: libraryTabs
                    entries: [{key:"visible",label:"Modpacks",icon:"content"},{key:"hidden",label:"Hidden",icon:"hide"}]
                    current: root.hiddenTab ? "hidden" : "visible"
                    onSelected: function(key) { root.hiddenTab = key === "hidden" }
                }
            }
            SearchField { objectName: "librarySearch"; Layout.fillWidth: true; Layout.preferredWidth: 260; Layout.maximumWidth: root.width < 650 ? -1 : 360; Layout.alignment: Qt.AlignRight; leadingIcon: "search"; placeholderText: "Search your library…"; selectByMouse: true; onTextChanged: root.query = text }
        }
        Flow {
            Layout.fillWidth: true; Layout.rightMargin: 16; spacing: 4
            Repeater {
                model: ["all", "conflux", "modrinth", "curseforge"]
                delegate: ActionButton {
                    id: sourceChoice
                    required property string modelData
                    objectName: "librarySource-" + modelData
                    contentItem: Item {
                        implicitWidth: sourceText.implicitWidth + (sourceChoice.modelData === "all" ? 17 : 26) + 6
                        implicitHeight: 26
                        Row {
                            anchors.centerIn: parent; spacing: 6
                            Icon { visible: sourceChoice.modelData === "all"; name: "filter"; size: 17; anchors.verticalCenter: parent.verticalCenter }
                            BrandBadge { visible: sourceChoice.modelData !== "all"; brand: sourceChoice.modelData; iconOnly: true; framed: false; tooltipsEnabled: false; anchors.verticalCenter: parent.verticalCenter }
                            Text { id: sourceText; text: sourceChoice.text; font: sourceChoice.font; color: sourceChoice.foreground; anchors.verticalCenter: parent.verticalCenter }
                        }
                    }
                    text: modelData === "all" ? "All sources" : modelData === "conflux" ? "Conflux" : modelData === "modrinth" ? "Modrinth" : "CurseForge"
                    compact: true; subtle: true; selectionOutlineOnly: true; cornerRadius: ControlStyle.subtabRadius; selected: root.source === modelData; onClicked: root.source = modelData
                }
            }
        }
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            ContentListView {
                id: packs; anchors.fill: parent; clip: true; spacing: 12; model: root.rows

                delegate: LibraryRow {
                    id: packRow
                    required property var modelData
                    width: packs.viewportWidth
                    titleText: modelData.title
                    detailText: modelData.modCount + (modelData.modCount === 1 ? " mod" : " mods")
                    minecraft: modelData.gameVersion || ""
                    loader: modelData.loader || ""
                    source: modelData.provider; thumbnail: modelData.icon || ""; hiddenPack: modelData.hidden
                    onActivated: root.packRequested(modelData.path, false)
                    onExploreRequested: root.packRequested(modelData.path, true)
                    onHideRequested: root.app.setPackHidden(modelData.path, !modelData.hidden)
                    onFolderRequested: root.app.openFolder(modelData.path)
                }
            }
            EmptyState {
                anchors.centerIn: parent; visible: root.rows.length === 0 && !root.app.libraryBusy && !(root.app.visiblePackCount===0 && root.app.hiddenPackCount>0 && !root.hiddenTab)
                title: root.hiddenTab ? "No hidden modpacks" : root.app.visiblePackCount > 0 ? "No matching modpacks" : root.app.hiddenPackCount ? "" : "No modpacks yet"
                message: ""
                symbol: "▦"
            }
        }
    }
}
