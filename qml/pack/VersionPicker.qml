pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "../components"
FocusDialog {
    id: root
    required property var controller
    property string minecraft: ""
    property string preferredLoader: ""
    property int selectedVersion: -1
    width: 650; height: 520; padding: 24; modal: true; anchors.centerIn: parent
    background: Rectangle { color: Theme.colors.card; border.color: Theme.colors.border; radius: 18 }
    onOpened: selectedVersion = -1
    header: Text { text: "Switch version · " + (root.controller.versionTarget.title || "Mod"); color: Theme.colors.text; font.pixelSize: 21; font.weight: Font.DemiBold; padding: 24; bottomPadding: 8; elide: Text.ElideRight }
    ColumnLayout {
        width: parent.width; height: parent.height; spacing: 14
        Text { visible: root.controller.versionError.length > 0; Layout.fillWidth: true; text: root.controller.versionError; color: Theme.colors.warning; font.pixelSize: 12; wrapMode: Text.WordWrap }
        BusyIndicator { visible: root.controller.versionsBusy; running: visible; Layout.alignment: Qt.AlignHCenter }
        ContentListView { outsideColor: Theme.colors.card; cornerRadius: 8;
            id: list; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: root.controller.versionGroups; spacing: 6;
            delegate: Rectangle {
                id: version; required property var modelData; required property int index
                property int variantIndex: 0
                readonly property var release: modelData.variants[Math.min(variantIndex,modelData.variants.length-1)]
                width: list.viewportWidth; height: modelData.variants.length > 1 ? 136 : 98; radius: 10; color: root.selectedVersion === release.releaseIndex ? Theme.colors.selection : Theme.colors.input; border.color: root.selectedVersion === release.releaseIndex ? Theme.colors.accent : Theme.colors.border
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.selectedVersion = version.release.releaseIndex }
                RowLayout { anchors.fill: parent; anchors.margins: 12
                    ColumnLayout { Layout.fillWidth: true; spacing: 6
                        Text { Layout.fillWidth: true; text: version.modelData.label; color: Theme.colors.text; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight; textFormat: Text.PlainText }
                        Text { Layout.fillWidth: true; text: version.release.filename; color: Theme.colors.muted; font.pixelSize: 11; elide: Text.ElideRight; textFormat: Text.PlainText }
                        CompatibilityBadges { minecraft: root.minecraft || (version.release.gameVersions || [])[0] || ""; loader: (version.release.loaders || []).indexOf(root.preferredLoader) !== -1 ? root.preferredLoader : ((version.release.loaders || []).filter(function(loader) { return loader !== "minecraft" && loader !== "datapack" })[0] || "") }
                        SelectBox { visible: version.modelData.variants.length > 1; Layout.fillWidth: true; implicitHeight: 34; model: version.modelData.variants.map(function(row) { return (row.provider === "modrinth" ? "Modrinth" : "CurseForge") + " · " + row.loaders.map(function(loader) { return Branding.label(loader) }).join("/") + " · " + row.gameVersions.join(", ") }); onActivated: { version.variantIndex = currentIndex; root.selectedVersion = version.release.releaseIndex } }
                    }
                    SourceBadge { source: version.release.provider }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            ActionButton { text: "Cancel"; subtle: true; onClicked: root.close() }
            Item { Layout.fillWidth: true }
            ActionButton { text: "Install version"; primary: true; enabled: root.selectedVersion >= 0 && !root.controller.versionTarget.locked && !root.controller.versionTarget.frozen && root.controller.versions[root.selectedVersion].provider === "modrinth"; onClicked: { root.controller.installVersion(root.selectedVersion); root.close() } }
        }
    }
}
