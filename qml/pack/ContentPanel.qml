pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Conflux.Style
import "../components"
Item {
    id: root; objectName: "contentPanel"
    required property var controller
    property string minecraft: ""
    property string loader: ""
    signal showcaseRequested(string path)
    signal showFilesRequested(string path)
    signal browseRequested()
    readonly property var rows: controller.rows
    readonly property int visibleSelected: rows.filter(function(row) { return root.controller.selected.indexOf(row.relativePath) !== -1 }).length
    ColumnLayout {
        anchors.fill: parent; spacing: 10
        RowLayout {
            Layout.fillWidth: true; spacing: 10
            SearchField { Layout.fillWidth: true; leadingIcon: "search"; placeholderText: "Search " + root.rows.length + " projects…"; text: root.controller.query; onTextEdited: root.controller.query = text }
            ActionButton { text: "Upload files"; iconName: "folder"; enabled: !root.controller.busy; onClicked: upload.open() }
            ActionButton { text: "Browse content"; iconName: "compass"; primary: true; onClicked: root.browseRequested() }
        }
        Flow {
            id: filterBar
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: root.width; spacing: 6
            SelectBox { width: 145; implicitHeight: 36; model: ["Name (A–Z)","Name (Z–A)","Author (A–Z)","File name","Updates first"]; currentIndex: Math.max(0,["name-asc","name-desc","author","filename","updates"].indexOf(root.controller.sort)); onActivated: root.controller.sort = ["name-asc","name-desc","author","filename","updates"][currentIndex] }
            Rectangle { implicitWidth: 1; implicitHeight: 22; color: Theme.colors.border; Layout.leftMargin: 3; Layout.rightMargin: 3 }
            Repeater {
                model: [{key:"all",label:"All"},{key:"mods",label:"Mods"},{key:"resourcepacks",label:"Resource Packs"},{key:"shaderpacks",label:"Shaders"}]
                delegate: ActionButton { required property var modelData; text: modelData.label; compact: true; pill: true; subtle: true; selected: root.controller.category === modelData.key; onClicked: root.controller.category = modelData.key }
            }
            ActionButton { id: filterButton; text: "Filter"; iconName: "plus"; compact: true; onClicked: { const point = mapToItem(root,0,height); filters.x = Math.min(point.x,root.width-filters.width); filters.y = point.y+6; filters.open() } }
            ActionButton { text: root.controller.filters.hidden ? "Hidden content" : "Hidden"; iconName: "hide"; compact: true; subtle: true; selected: !!root.controller.filters.hidden; onClicked: root.controller.setFilter("hidden",!root.controller.filters.hidden) }
            ActionButton { text: "Clear"; compact: true; subtle: true; visible: !!root.controller.filters.author || !!root.controller.filters.environment || !!root.controller.filters.state || root.controller.filters.updates || root.controller.filters.openSource || root.controller.filters.external; onClicked: root.controller.clearFilters() }
            ActionButton { iconName: "download"; text: "Update all"; compact: true; subtle: true; accented: true; enabled: root.controller.updateCount > 0 && !root.controller.busy; hint: root.controller.updateCount + " compatible updates · frozen versions are skipped"; onClicked: root.controller.updateAll() }
            ActionButton { iconName: "refresh"; text: "Refresh"; compact: true; subtle: true; enabled: !root.controller.busy; onClicked: root.controller.refresh() }
        }
        RowLayout {
            visible: root.controller.selected.length > 0; Layout.fillWidth: true
            Text { text: root.controller.selected.length + " selected"; color: Theme.colors.accent; font.pixelSize: 12 }
            ActionButton { text: "Clear selection"; compact: true; subtle: true; onClicked: root.controller.clearSelection() }
            Item { Layout.fillWidth: true }
            ActionButton { text: "Remove selected"; iconName: "trash"; compact: true; enabled: !root.controller.busy; onClicked: { removeDialog.path = ""; removeDialog.modTitle = root.controller.selected.length + " selected projects"; removeDialog.open() } }
        }
        RowLayout {
            Layout.fillWidth: true; visible: root.controller.busy || root.controller.notice.length > 0
            BusyIndicator { visible: root.controller.busy; running: visible; Layout.preferredWidth: 20; Layout.preferredHeight: 20 }
            Text { Layout.fillWidth: true; text: root.controller.notice; color: Theme.colors.muted; font.pixelSize: 11; wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight; textFormat: Text.PlainText }
            ActionButton { visible: root.controller.busy; enabled: root.controller.cancellable; text: "Cancel"; compact: true; subtle: true; onClicked: root.controller.cancel() }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.fillHeight: true; color: Theme.colors.panel; radius: 16; border.width: 0; clip: true; objectName: "contentTable"
            ColumnLayout {
                anchors.fill: parent; spacing: 0
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: 48; color: Theme.colors.card; radius: 16
                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 24; color: Theme.colors.card }
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 16; spacing: 12
                        SelectionBox { checked: root.rows.length > 0 && root.visibleSelected === root.rows.length; partial: root.visibleSelected > 0 && root.visibleSelected < root.rows.length; Accessible.name: "Select visible content"; onClicked: root.controller.selectVisible(root.visibleSelected !== root.rows.length) }
                        Text { Layout.fillWidth: true; text: "Project"; color: Theme.colors.muted; font.pixelSize: 12; font.weight: Font.DemiBold }
                        Text { Layout.preferredWidth: Math.max(150,root.width*0.29); text: "Version"; color: Theme.colors.muted; font.pixelSize: 12; font.weight: Font.DemiBold }
                        Text { Layout.preferredWidth: 180; text: "Actions"; horizontalAlignment: Text.AlignRight; color: Theme.colors.muted; font.pixelSize: 12; font.weight: Font.DemiBold }
                    }
                }
                Item {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    ListView { boundsBehavior: Flickable.StopAtBounds;
                        id: list; anchors.fill: parent; clip: true; model: root.rows; ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            id: entry; objectName: "contentRow"
                            required property var modelData
                            readonly property bool selected: root.controller.selected.indexOf(modelData.relativePath) !== -1
                            width: list.width; height: 82; radius: 10
                            color: selected ? Theme.colors.input : hover.hovered ? Theme.colors.card : "transparent"
                            HoverHandler { id: hover }
                            MouseArea { anchors.fill: parent; enabled: entry.modelData.category === "mods"; cursorShape: Qt.PointingHandCursor; onClicked: root.showcaseRequested(entry.modelData.relativePath) }
                            Rectangle { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; height: 1; color: Theme.colors.border; opacity: 0.6 }
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 16; spacing: 12
                                SelectionBox { objectName: "contentSelection"; checked: entry.selected; Accessible.name: "Select " + entry.modelData.title; onToggled: root.controller.select(entry.modelData.relativePath,checked) }
                                ContentIcon { size: 44; source: entry.modelData.icon || ""; kind: entry.modelData.category === "mods" ? "mod" : entry.modelData.category === "shaderpacks" ? "shader" : "resource" }
                                ColumnLayout {
                                    Layout.fillWidth: true; Layout.minimumWidth: 40; spacing: 7
                                    Text { Layout.fillWidth: true; text: entry.modelData.title; color: Theme.colors.text; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
                                    RowLayout {
                                        Layout.fillWidth: true; spacing: 6
                                        ContentIcon { visible: !!entry.modelData.authorIcon; source: entry.modelData.authorIcon || ""; size: 18; radius: 9; kind: "mod" }
                                        Text { Layout.fillWidth: true; text: entry.modelData.author || "Unknown author"; color: Theme.colors.muted; font.pixelSize: 11; elide: Text.ElideRight; textFormat: Text.PlainText }
                                    }
                                }
                                ColumnLayout {
                                    Layout.preferredWidth: Math.max(150,root.width*0.29); Layout.maximumWidth: Layout.preferredWidth; spacing: 7
                                    Text { Layout.fillWidth: true; text: entry.modelData.version || "Unknown version"; color: Theme.colors.text; font.pixelSize: 12; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
                                    Text { Layout.fillWidth: true; text: entry.modelData.fileName; color: Theme.colors.muted; font.pixelSize: 11; elide: Text.ElideRight; textFormat: Text.PlainText }
                                }
                                RowLayout {
                                    Layout.preferredWidth: 180; spacing: 1
                                    Icon { name: "lock"; size: 16; opacity: entry.modelData.frozen ? 1 : 0; color: Theme.colors.muted }
                                    ActionButton { Layout.preferredWidth: 34; iconName: entry.modelData.updateAvailable ? "download" : "switch"; accented: !!entry.modelData.updateAvailable; subtle: true; compact: true; hint: entry.modelData.frozen ? "Version frozen" : entry.modelData.updateAvailable ? "Update to " + entry.modelData.updateRelease.version : "Switch version"; enabled: !root.controller.busy && !entry.modelData.frozen && !!entry.modelData.projectId; onClicked: { if(entry.modelData.updateAvailable) root.controller.update(entry.modelData.relativePath); else { root.controller.openVersions(entry.modelData.relativePath); versions.open() } } }
                                    ToggleSwitch { objectName: "contentToggle"; Layout.preferredWidth: 48; checked: !!entry.modelData.enabled; enabled: !root.controller.busy && !!entry.modelData.sha1 && !entry.modelData.directory; Accessible.name: "Enable " + entry.modelData.title; onToggled: { root.controller.setEnabled(entry.modelData.relativePath,checked); checked = Qt.binding(function() { return !!entry.modelData.enabled }) } }
                                    ActionButton { Layout.preferredWidth: 34; iconName: "trash"; hint: "Remove content"; subtle: true; compact: true; enabled: !root.controller.busy && !!entry.modelData.sha1; onClicked: { removeDialog.path = entry.modelData.relativePath; removeDialog.modTitle = entry.modelData.title; removeDialog.open() } }
                                PopupButton { popup: rowMenu; id: moreButton; objectName: "contentMore"; Layout.preferredWidth: 34; iconName: "more"; hint: "More actions"; subtle: true; compact: true }
                                }
                            }
                            FocusPopup {
                                id: rowMenu; objectName: "contentMenu"; parent: moreButton; x: moreButton.width-width; y: moreButton.height+4; margins: 8; width: 210; padding: 6
                                background: Rectangle { color: Theme.colors.card; border.color: Theme.colors.border; radius: 13 }
                                contentItem: ColumnLayout {
                                    spacing: 2
                                    ActionButton { Layout.fillWidth: true; text: entry.modelData.hidden ? "Restore content" : "Hide content"; iconName: entry.modelData.hidden ? "restore" : "hide"; subtle: true; alignLeft: true; onClicked: { rowMenu.close();root.controller.hide(entry.modelData.relativePath,!entry.modelData.hidden) } }
                                    ActionButton { Layout.fillWidth: true; text: "Show file"; iconName: "folder"; subtle: true; alignLeft: true; onClicked: { root.showFilesRequested(entry.modelData.relativePath); rowMenu.close() } }
                                    ActionButton { Layout.fillWidth: true; objectName: "copyContentLink"; text: "Copy link"; iconName: "copy"; subtle: true; alignLeft: true; enabled: !!entry.modelData.projectUrl; onClicked: { root.controller.copyLink(entry.modelData.relativePath); rowMenu.close() } }
                                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.colors.border; Layout.topMargin: 5; Layout.bottomMargin: 5 }
                                    ActionButton { Layout.fillWidth: true; text: entry.modelData.frozen ? "Unfreeze version" : "Freeze version"; iconName: "lock"; subtle: true; alignLeft: true; enabled: !root.controller.busy && (!!entry.modelData.projectId || !!entry.modelData.sha1); onClicked: { root.controller.freeze(entry.modelData.relativePath,!entry.modelData.frozen); rowMenu.close() } }
                                }
                            }
                        }
                    }
                    EmptyState { anchors.centerIn: parent; visible: root.rows.length === 0; title: root.controller.busy ? "Reading your content…" : "No matching content"; message: "Try another filter, clear your search, or browse content."; symbol: "content" }
                }
            }
        }
    }
    ContentFilter { id: filters; objectName: "contentFilter"; controller: root.controller }
    VersionPicker { id: versions; objectName: "versionPicker"; controller: root.controller; minecraft: root.minecraft; preferredLoader: root.loader }
    FileDialog { id: upload; title: "Add content to this pack"; fileMode: FileDialog.OpenFiles; nameFilters: ["Mods (*.jar)","Resource packs (*.zip)","Shader packs (*.zip)"]; onAccepted: root.controller.importFiles(selectedFiles,["mods","resourcepacks","shaderpacks"][selectedNameFilter.index]) }
    FocusDialog {
        id: removeDialog; property string path: ""; property string modTitle: ""
        anchors.centerIn: parent; modal: true; width: 460; padding: 24
        background: Rectangle { radius: 18; color: Theme.colors.card; border.color: Theme.colors.border }
        header: Text { text: "Remove content?"; color: Theme.colors.text; font.pixelSize: 21; padding: 24; bottomPadding: 8 }
        ColumnLayout {
            width: parent.width; spacing: 20
            Text { Layout.fillWidth: true; text: "Remove " + removeDialog.modTitle + " from this pack? The original files will be backed up. Required dependencies are kept."; color: Theme.colors.muted; font.pixelSize: 13; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
            RowLayout { Layout.alignment: Qt.AlignRight
                ActionButton { text: "Cancel"; subtle: true; onClicked: removeDialog.close() }
                ActionButton { text: "Remove"; iconName: "trash"; enabled: !root.controller.busy; onClicked: { if(removeDialog.path.length) root.controller.remove(removeDialog.path); else root.controller.removeSelected(); removeDialog.close() } }
            }
        }
    }
}
