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
    property var releasePolicy: null
    property string packPath: ""
    property string minecraft: ""
    property string loader: ""
    signal showcaseRequested(string path, string tab)
    signal authorRequested(var author)
    signal showFilesRequested(string path)
    signal browseRequested()
    signal pasteRequested()
    readonly property var rows: controller.rows
    readonly property int visibleSelected: rows.filter(function(row) { return root.controller.selected.indexOf(row.relativePath) !== -1 }).length
    ColumnLayout {
        anchors.fill: parent; spacing: 10
        RowLayout {
            Layout.fillWidth: true; spacing: 10
            SearchField { Layout.fillWidth: true; leadingIcon: "search"; placeholderText: "Search " + root.rows.length + " projects…"; text: root.controller.query; onTextEdited: root.controller.query = text }
            ActionButton { text: "Upload files"; iconName: "folder"; onClicked: upload.open() }
            ActionButton { objectName: "pasteContentList"; iconName: "plus"; hint: "Paste a content list"; onClicked: root.pasteRequested() }
            ActionButton { text: "Browse content"; iconName: "compass"; primary: true; onClicked: root.browseRequested() }
        }
        Flow {
            id: filterBar
            Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 12
            Flow {
                id: leftControls; spacing: 6
                function naturalWidth() { let size=0,count=0;for(const child of children) if(child.visible && child.width>0) {size+=child.width;++count}return size+spacing*Math.max(0,count-1) }
                width: Math.min(filterBar.width,naturalWidth()); height: childrenRect.height
                SelectBox { width: 145; implicitHeight: 36; model: ["Name (A–Z)","Name (Z–A)","Author (A–Z)","File name","Updates first"]; currentIndex: Math.max(0,["name-asc","name-desc","author","filename","updates"].indexOf(root.controller.sort)); onActivated: root.controller.sort = ["name-asc","name-desc","author","filename","updates"][currentIndex] }
                Repeater {
                    model: [{key:"all",label:"All"},{key:"mods",label:"Mods"},{key:"resourcepacks",label:"Resource Packs"},{key:"shaderpacks",label:"Shaders"},{key:"datapacks",label:"Data Packs"}]
                    delegate: ActionButton { required property var modelData; text: modelData.label; compact: true; pill: true; subtle: true; selectionOutlineOnly: true; selected: root.controller.category === modelData.key; onClicked: root.controller.category = modelData.key }
                }
                ActionButton { objectName: "clearContentFilters"; text: "Clear"; compact: true; subtle: true; visible: !!root.controller.filters.author || !!root.controller.filters.environment || !!root.controller.filters.state || root.controller.filters.openSource || root.controller.filters.external || root.controller.filters.locked || root.controller.filters.recent || root.controller.filters.hidden || root.controller.filters.updates || root.controller.filters.dependencies; onClicked: root.controller.clearFilters() }
                ReleaseStagePicker { policy: root.releasePolicy; packPath: root.packPath }
                PopupButton { id: filterButton; popup: filters; text: "Filter"; iconName: "plus"; compact: true }
            }
            Item {
                width: filterBar.width-leftControls.width-filterBar.spacing >= actions.implicitWidth+18 ? filterBar.width-leftControls.width-filterBar.spacing : filterBar.width
                height: 36
                RowLayout {
                    id: actions; anchors.fill: parent; spacing: 6
                    Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; Layout.leftMargin: 4; Layout.rightMargin: 10; color: Theme.colors.border }
                    ActionButton { objectName: "contentHiddenFilter"; text: "Hidden"; hint: root.controller.filters.hidden ? "Showing hidden content" : "Show hidden content"; iconName: "hide"; compact: true; subtle: true; selected: !!root.controller.filters.hidden; onClicked: root.controller.setFilter("hidden",!root.controller.filters.hidden) }
                    ActionButton { objectName: "contentUpdatesFilter"; text: "Updates"; iconName: "download"; hint: "Show content with updates"; compact: true; subtle: true; selected: !!root.controller.filters.updates; onClicked: root.controller.setFilter("updates",!root.controller.filters.updates) }
                    ActionButton { objectName: "contentUpdateAll"; iconName: "download"; text: "Update all"; compact: true; subtle: true; accented: true; enabled: root.controller.actionableUpdateCount > 0; hint: root.controller.actionableUpdateCount + " updates · locked and frozen content is skipped"; onClicked: root.controller.updateAll() }
                    Item { Layout.fillWidth: true }
                    ContentActivity { controller: root.controller }
                    ActionButton { objectName: "contentRefresh"; iconName: "refresh"; text: "Refresh"; compact: true; subtle: true; onClicked: root.controller.refresh() }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.fillHeight: true; color: Theme.colors.panel; radius: ControlStyle.panelRadius; border.width: 0; clip: true; objectName: "contentTable"
            ColumnLayout {
                anchors.fill: parent; spacing: 0
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: 48; color: Theme.colors.card; radius: ControlStyle.panelRadius
                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 24; color: Theme.colors.card }
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 32; spacing: 12
                        SelectionBox { checked: root.rows.length > 0 && root.visibleSelected === root.rows.length; partial: root.visibleSelected > 0 && root.visibleSelected < root.rows.length; Accessible.name: "Select visible content"; onClicked: root.controller.selectVisible(root.visibleSelected !== root.rows.length) }
                        Text { Layout.fillWidth: true; text: "Project"; color: Theme.colors.muted; font.pixelSize: 12; font.weight: Font.DemiBold }
                        Text { Layout.preferredWidth: Math.max(150,root.width*0.29); text: "Version"; color: Theme.colors.muted; font.pixelSize: 12; font.weight: Font.DemiBold }
                        Text { Layout.preferredWidth: 180; text: "Actions"; horizontalAlignment: Text.AlignRight; color: Theme.colors.muted; font.pixelSize: 12; font.weight: Font.DemiBold }
                    }
                }
                Item {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    ContentListView {
                        id: list; objectName: "instanceContentList"; bottomInset: selectionBar.visible ? selectionBar.height+24 : 0; cornerRadius: 0; anchors.fill: parent; clip: true; model: root.controller.model;
                        delegate: Rectangle {
                            id: entry; objectName: "contentRow"
                            required property var modelData
                            required property int index
                            readonly property bool selected: root.controller.selected.indexOf(modelData.relativePath) !== -1
                            width: list.viewportWidth; height: Appearance.values.contentRowHeight
                            color: selected ? Theme.colors.input : hover.hovered ? Theme.colors.card : "transparent"
                            HoverHandler { id: hover }
                            MouseArea { anchors.fill: parent; enabled: !!entry.modelData.projectId; cursorShape: Qt.PointingHandCursor; onClicked: root.showcaseRequested(entry.modelData.relativePath,"description") }
                            Rectangle { visible: entry.index < list.count - 1; anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; height: 1; color: Theme.colors.border; opacity: 0.6 }
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 16; spacing: 12
                                SelectionBox { objectName: "contentSelection"; checked: entry.selected; Accessible.name: "Select " + entry.modelData.title; onToggled: root.controller.select(entry.modelData.relativePath,checked) }
                                ContentIcon { size: Math.min(44,entry.height-24); source: entry.modelData.icon || ""; kind: entry.modelData.category === "mods" ? "mod" : entry.modelData.category === "shaderpacks" ? "shader" : "resource" }
                                ColumnLayout {
                                    Layout.fillWidth: true; Layout.minimumWidth: 40; spacing: 7
                                    RowLayout {
                                        Layout.fillWidth: true; spacing: 6
                                        ContentLink { objectName: "contentNameLink"; Layout.fillWidth: true; text: entry.modelData.title; enabled: !!entry.modelData.projectId; onClicked: root.showcaseRequested(entry.modelData.relativePath,"description"); color: Theme.colors.text; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
                                        ActionButton { objectName: "contentDependencyBadge"; visible: !!entry.modelData.isDependency; iconName: "content"; hint: entry.modelData.dependencyHint || "Dependency"; compact: true; subtle: true; Layout.preferredWidth: 26; Layout.preferredHeight: 24; Accessible.name: hint; onClicked: root.controller.setFilter("dependencies",true) }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true; spacing: 6
                                        ContentIcon { visible: !!entry.modelData.authorIcon; source: entry.modelData.authorIcon || ""; size: 18; radius: 9; kind: "mod" }
                                        AuthorLinks { objectName: "contentAuthorLink"; Layout.fillWidth: true; authors: entry.modelData.authors || []; author: entry.modelData.author || ""; authorIds: entry.modelData.authorIds || {}; onAuthorRequested: function(author) { root.authorRequested(author) }; color: Theme.colors.muted; font.pixelSize: 11 }
                                    }
                                }
                                ColumnLayout {
                                    Layout.preferredWidth: Math.max(150,root.width*0.29); Layout.maximumWidth: Layout.preferredWidth; spacing: 7
                                    ContentLink { objectName: "contentVersionLink"; Layout.fillWidth: true; text: entry.modelData.version || "Unknown version"; enabled: !!entry.modelData.projectId; onClicked: root.showcaseRequested(entry.modelData.relativePath,"versions"); color: Theme.colors.text; font.pixelSize: 12; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
                                    Text { Layout.fillWidth: true; text: entry.modelData.fileName; color: Theme.colors.muted; font.pixelSize: 11; elide: Text.ElideRight; textFormat: Text.PlainText }
                                }
                                RowLayout {
                                    Layout.preferredWidth: 180; spacing: 1
                                    ActionButton { objectName: "contentLock"; Layout.preferredWidth: 26; iconName: entry.modelData.locked ? "lock" : "unlock"; hint: entry.modelData.locked ? "Unlock content" : "Lock content"; selected: !!entry.modelData.locked; subtle: true; compact: true; onClicked: root.controller.setLocked(entry.modelData.relativePath,!entry.modelData.locked) }
                                    ActionButton { Layout.preferredWidth: 34; iconName: entry.modelData.updateAvailable ? "download" : "switch"; accented: !!entry.modelData.updateAvailable; subtle: true; compact: true; hint: entry.modelData.frozen ? "Version frozen" : entry.modelData.updateAvailable ? "Update to " + entry.modelData.updateRelease.version : "Switch version"; enabled: !entry.modelData.locked && !entry.modelData.frozen && !entry.modelData.pending && !!entry.modelData.projectId; onClicked: { if(entry.modelData.updateAvailable) root.controller.update(entry.modelData.relativePath); else { root.controller.openVersions(entry.modelData.relativePath); versions.open() } } }
                                    ToggleSwitch { objectName: "contentToggle"; Layout.preferredWidth: 48; checked: !!entry.modelData.enabled; enabled: !entry.modelData.locked && !!entry.modelData.sha1 && !entry.modelData.directory; Accessible.name: "Enable " + entry.modelData.title; onToggled: { root.controller.setEnabled(entry.modelData.relativePath,checked); checked = Qt.binding(function() { return !!entry.modelData.enabled }) } }
                                    ActionButton { Layout.preferredWidth: 34; iconName: "trash"; hint: "Remove content"; subtle: true; compact: true; enabled: !entry.modelData.locked && !!entry.modelData.sha1; onClicked: { removeDialog.path = entry.modelData.relativePath; removeDialog.modTitle = entry.modelData.title; removeDialog.open() } }
                                PopupButton { popup: rowMenu; id: moreButton; objectName: "contentMore"; Layout.preferredWidth: 34; iconName: "more"; hint: "More actions"; subtle: true; compact: true }
                                }
                            }
                            FocusPopup {
                                id: rowMenu; objectName: "contentMenu"; parent: moreButton; x: moreButton.width-width; y: moreButton.height+4; margins: 8; width: 210; padding: 6
                                background: Rectangle { color: Theme.colors.card; border.color: Theme.colors.border; radius: ControlStyle.panelRadius }
                                contentItem: ColumnLayout {
                                    spacing: 2
                                    MenuOption { Layout.fillWidth: true; text: entry.modelData.hidden ? "Restore content" : "Hide content"; iconName: entry.modelData.hidden ? "restore" : "hide"; subtle: true; alignLeft: true; onClicked: { rowMenu.close();root.controller.hide(entry.modelData.relativePath,!entry.modelData.hidden) } }
                                    MenuOption { Layout.fillWidth: true; text: "Show file"; iconName: "folder"; subtle: true; alignLeft: true; onClicked: { root.showFilesRequested(entry.modelData.relativePath); rowMenu.close() } }
                                    MenuOption { Layout.fillWidth: true; objectName: "copyContentLink"; text: "Copy link"; iconName: "copy"; subtle: true; alignLeft: true; enabled: !!entry.modelData.projectUrl; onClicked: { root.controller.copyLink(entry.modelData.relativePath); rowMenu.close() } }
                                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.colors.border; Layout.topMargin: 5; Layout.bottomMargin: 5 }
                                    MenuOption { Layout.fillWidth: true; text: entry.modelData.frozen ? "Unfreeze version" : "Freeze version"; iconName: "lock"; subtle: true; alignLeft: true; enabled: !!entry.modelData.projectId || !!entry.modelData.sha1; onClicked: { root.controller.freeze(entry.modelData.relativePath,!entry.modelData.frozen); rowMenu.close() } }
                                }
                            }
                        }
                    }
                    EmptyState { anchors.centerIn: parent; visible: root.rows.length === 0; title: root.controller.query.length ? "No matching content" : "No content"; message: ""; symbol: "content" }
                }
            }
            // Contiguous rows fill the table; only its outer frame has rounded corners.
            ScrollViewportCorners { objectName: "contentTableCorners"; anchors.fill: parent; radius: parent.radius; outsideColor: Theme.colors.background }
        }
    }
    ContentSelectionBar {
        id: selectionBar; objectName: "contentSelectionBar"; controller: root.controller
        anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 12
        width: Math.min(parent.width-24, 820); z: 20; visible: root.controller.selected.length > 0
        onRemoveRequested: { removeDialog.path = ""; removeDialog.modTitle = root.controller.selected.length + " selected projects"; removeDialog.open() }
    }
    ContentFilter { id: filters; objectName: "contentFilter"; parent: filterButton; y: filterButton.height+6; controller: root.controller }
    VersionPicker { id: versions; objectName: "versionPicker"; releasePolicy: root.releasePolicy; packPath: root.packPath; controller: root.controller; minecraft: root.minecraft; preferredLoader: root.loader }
    FileDialog { id: upload; title: "Add content to this pack"; fileMode: FileDialog.OpenFiles; nameFilters: ["Mods (*.jar)","Resource packs (*.zip)","Shader packs (*.zip)","Data packs (*.zip)"]; onAccepted: root.controller.importFiles(selectedFiles,["mods","resourcepacks","shaderpacks","datapacks"][selectedNameFilter.index]) }
    FocusDialog {
        id: removeDialog; property string path: ""; property string modTitle: ""
        anchors.centerIn: parent; modal: true; width: 460; padding: 24
        background: Rectangle { radius: ControlStyle.panelRadius; color: Theme.colors.card; border.color: Theme.colors.border }
        header: Text { text: "Remove content?"; color: Theme.colors.text; font.pixelSize: 21; padding: 24; bottomPadding: 8 }
        ColumnLayout {
            width: parent.width; spacing: 20
            Text { Layout.fillWidth: true; text: "Remove " + removeDialog.modTitle + "?"; color: Theme.colors.muted; font.pixelSize: 13; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
            RowLayout { Layout.alignment: Qt.AlignRight
                ActionButton { text: "Cancel"; subtle: true; onClicked: removeDialog.close() }
                ActionButton { text: "Remove"; iconName: "trash"; onClicked: { if(removeDialog.path.length) root.controller.remove(removeDialog.path); else root.controller.removeSelected(); removeDialog.close() } }
            }
        }
    }
}
