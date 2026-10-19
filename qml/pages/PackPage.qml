pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    id: root
    required property var app
    signal createRequested()
    signal discoverRequested()
    EmptyState { visible: !root.app.activePack.name; anchors.centerIn: parent; title: "Give your ideas a home."; symbol: "⑂"; message: "Create a modpack or open one from your library. Every pack has its own Git history." }
    ActionButton { visible: !root.app.activePack.name; anchors.horizontalCenter: parent.horizontalCenter; anchors.top: parent.verticalCenter; anchors.topMargin: 110; text: "+ New modpack"; primary: true; onClicked: root.createRequested() }
    ColumnLayout {
        visible: !!root.app.activePack.name
        anchors.fill: parent; anchors.margins: 32; spacing: 22
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 8
                Text { text: root.app.activePack.name || ""; font.pixelSize: 31; color: "#ecf4ee"; font.weight: Font.DemiBold }
                Text { text: (root.app.activePack.minecraft || "") + "  ·  " + (root.app.activePack.loader || "") + "  ·  " + root.app.activeMods.length + " pinned mods"; color: "#9bb3a4"; font.pixelSize: 13 }
            }
            Item { Layout.fillWidth: true }
            ActionButton { text: "Open folder"; subtle: true; onClicked: root.app.openFolder(root.app.activePack.path) }
            ActionButton { text: "+ Add mods"; primary: true; onClicked: root.discoverRequested() }
        }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 76; color: "#24372a"; radius: 10; border.color: "#3a5642"
            RowLayout {
                anchors.fill: parent; anchors.margins: 18; spacing: 16
                SourceBadge { source: "git" }
                ColumnLayout { Layout.fillWidth: true; spacing: 5; Text { text: "main  ·  " + (root.app.gitStatus.length ? "Unsaved changes" : "All changes saved"); color: "#c6e8d1"; font.pixelSize: 13; font.weight: Font.DemiBold } Text { text: "Local repository. GitHub connection is coming next."; color: "#8eaf9a"; font.pixelSize: 12 } }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 24
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 14
                Text { text: "Pinned content"; color: "#dfede3"; font.pixelSize: 19; font.weight: Font.DemiBold }
                Text { Layout.fillWidth: true; text: "Exact release references are tracked in Git. Dependencies and game installation are not downloaded in this foundation."; color: "#8fab99"; font.pixelSize: 12; wrapMode: Text.WordWrap; lineHeight: 1.3 }
                Item {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    ListView {
                        id: mods; anchors.fill: parent; clip: true; spacing: 10; model: root.app.activeMods
                        delegate: Rectangle {
                            id: modCard
                            required property var modelData
                            width: mods.width; height: 96; radius: 10; color: "#1c2a21"; border.color: "#344a3b"
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 16
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 8
                                    Text { Layout.fillWidth: true; text: modCard.modelData.projectTitle || modCard.modelData.projectId; color: "#e1eee5"; font.pixelSize: 15; font.weight: Font.DemiBold; elide: Text.ElideRight }
                                    Text { Layout.fillWidth: true; text: modCard.modelData.version; color: "#99b6a3"; font.pixelSize: 12; elide: Text.ElideRight }
                                    Text { visible: modCard.modelData.dependencies.length > 0; text: modCard.modelData.dependencies.length + " required dependencies to resolve"; color: "#d4ba82"; font.pixelSize: 10 }
                                }
                                SourceBadge { source: modCard.modelData.provider }
                                ActionButton { text: "Remove"; subtle: true; enabled: !root.app.repositoryBusy; onClicked: root.app.removeMod(modCard.modelData.provider, modCard.modelData.projectId) }
                            }
                        }
                    }
                    EmptyState { visible: root.app.activeMods.length === 0; anchors.centerIn: parent; title: "A blank canvas."; message: "Browse the catalogue and pin a release to start shaping your pack."; symbol: "+" }
                }
            }
            Rectangle {
                Layout.preferredWidth: 320; Layout.fillHeight: true; radius: 12; color: "#1a251e"; border.color: "#354c3d"
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 22; spacing: 16
                    Text { text: "Save your progress"; color: "#e5f0e9"; font.pixelSize: 18; font.weight: Font.DemiBold }
                    TextArea { id: message; objectName: "commitMessageInput"; Layout.fillWidth: true; Layout.preferredHeight: 90; placeholderText: "What changed?"; wrapMode: TextEdit.Wrap; font.pixelSize: 13; background: Rectangle { color: "#121c15"; radius: 8; border.color: "#3c5444" } }
                    ActionButton { Layout.fillWidth: true; text: root.app.repositoryBusy ? "Saving…" : "Save changes to Git"; primary: true; enabled: !root.app.repositoryBusy && message.text.trim().length > 0 && root.app.gitStatus.length > 0; onClicked: root.app.saveChanges(message.text) }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#34483a"; Layout.topMargin: 8; Layout.bottomMargin: 8 }
                    Text { text: "HISTORY"; color: "#8ca795"; font.pixelSize: 10; font.letterSpacing: 1.8 }
                    ScrollView {
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                        Text { width: 270; text: root.app.history; color: "#a9c1b1"; font.pixelSize: 12; wrapMode: Text.Wrap; lineHeight: 1.7; textFormat: Text.PlainText }
                    }
                    Text { Layout.fillWidth: true; text: "Your pack stays yours. The repository is a regular Git folder you can open in any Git client."; color: "#799b85"; font.pixelSize: 11; wrapMode: Text.WordWrap; lineHeight: 1.4 }
                }
            }
        }
    }
}
