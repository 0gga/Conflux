pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    id: root
    required property var app
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 32; spacing: 20
        Text { text: "Make yourself at home."; color: "#edf4ef"; font.pixelSize: 30; font.weight: Font.DemiBold }
        Text { text: "Connect your sources and choose where we look."; color: "#99b1a1"; font.pixelSize: 14 }
        ScrollView {
            id: scroll; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            ColumnLayout {
                width: scroll.availableWidth; spacing: 20
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: launcherSettings.implicitHeight + 48; color: "#1b2820"; border.color: "#344c3b"; radius: 12
                    ColumnLayout {
                        id: launcherSettings
                        anchors.fill: parent; anchors.margins: 24; spacing: 12
                        Text { text: "Launcher folders"; color: "#dfede4"; font.pixelSize: 19; font.weight: Font.DemiBold }
                        Text { text: "Installations are discovered without changing your launcher files."; color: "#92b09b"; font.pixelSize: 12 }
                        RowLayout { SourceBadge { source: "modrinth" } Text { text: "App data folder"; color: "#9eb7a6"; font.pixelSize: 12 } }
                        TextField { id: mrRoot; Layout.fillWidth: true; text: root.app.modrinthRoot; selectByMouse: true }
                        RowLayout { SourceBadge { source: "curseforge" } Text { text: "Minecraft Instances folder"; color: "#9eb7a6"; font.pixelSize: 12 } }
                        TextField { id: cfRoot; Layout.fillWidth: true; text: root.app.curseforgeRoot; selectByMouse: true }
                        ActionButton { text: root.app.libraryBusy ? "Scanning…" : "Save folders & rescan"; enabled: !root.app.libraryBusy; onClicked: root.app.setInstallationRoots(mrRoot.text, cfRoot.text) }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: providerSettings.implicitHeight + 48; color: "#1b2820"; border.color: "#344c3b"; radius: 12
                    ColumnLayout {
                        id: providerSettings
                        anchors.fill: parent; anchors.margins: 24; spacing: 14
                        RowLayout {
                            SourceBadge { source: "curseforge" }
                            Text { text: root.app.curseforgeEnabled ? "Connected for this session" : "API key required"; color: root.app.curseforgeEnabled ? "#94d9ae" : "#c9b795"; font.pixelSize: 12 }
                            Item { Layout.fillWidth: true }
                        }
                        Text { Layout.fillWidth: true; text: "Use an API key approved for this app's integration. The key stays in memory for this session. Access remains subject to CurseForge's API agreement and author distribution settings."; color: "#a4bcad"; font.pixelSize: 12; wrapMode: Text.WordWrap; lineHeight: 1.4 }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { id: apiKey; Layout.fillWidth: true; placeholderText: "Approved API key"; echoMode: TextInput.Password; selectByMouse: true }
                            ActionButton { text: "Connect"; enabled: apiKey.text.trim().length > 0; onClicked: { root.app.configureCurseforge(apiKey.text); apiKey.clear() } }
                            ActionButton { text: "Disconnect"; visible: root.app.curseforgeEnabled; subtle: true; onClicked: root.app.configureCurseforge("") }
                        }
                        Text { text: "Modrinth browsing works without signing in."; color: "#7eaa8e"; font.pixelSize: 11 }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: gitSettings.implicitHeight + 48; color: "#1b2820"; border.color: "#344c3b"; radius: 12
                    ColumnLayout {
                        id: gitSettings
                        anchors.fill: parent; anchors.margins: 24; spacing: 12
                        Text { text: "Git & GitHub"; color: "#dfede4"; font.pixelSize: 19; font.weight: Font.DemiBold }
                        Text { Layout.fillWidth: true; text: "Local Git repositories and commits are ready. GitHub sign-in, push, pull, and conflict resolution are the next integration step."; color: "#9fb9a7"; font.pixelSize: 12; wrapMode: Text.WordWrap }
                        ActionButton { text: "GitHub connection · coming next"; enabled: false }
                    }
                }
                Text { Layout.fillWidth: true; text: "Your modpacks live in " + root.app.packsRoot; color: "#83a18e"; font.pixelSize: 11; wrapMode: Text.Wrap }
                ActionButton { text: "Open modpacks folder"; onClicked: root.app.openFolder(root.app.packsRoot) }
            }
        }
    }
}
