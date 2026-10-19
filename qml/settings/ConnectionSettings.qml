pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "../components"
ColumnLayout {
    id: root
    required property var app
    spacing: 16
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: launcherSettings.implicitHeight + 48; color: Theme.colors.card; border.color: Theme.colors.border; radius: 12
                    ColumnLayout {
                        id: launcherSettings
                        anchors.fill: parent; anchors.margins: 24; spacing: 12
                        Text { text: "Launcher folders"; color: Theme.colors.text; font.pixelSize: 19; font.weight: Font.DemiBold }
                        Text { text: "Installations are discovered without changing your launcher files."; color: Theme.colors.accent; font.pixelSize: 12 }
                        RowLayout { SourceBadge { source: "modrinth" } Text { text: "App data folder"; color: Theme.colors.muted; font.pixelSize: 12 } }
                        SearchField { id: mrRoot; Layout.fillWidth: true; text: root.app.modrinthRoot; selectByMouse: true }
                        RowLayout { SourceBadge { source: "curseforge" } Text { text: "Minecraft Instances folder"; color: Theme.colors.muted; font.pixelSize: 12 } }
                        SearchField { id: cfRoot; Layout.fillWidth: true; text: root.app.curseforgeRoot; selectByMouse: true }
                        ActionButton { text: root.app.libraryBusy ? "Scanning…" : "Save folders & rescan"; enabled: !root.app.libraryBusy; onClicked: root.app.setInstallationRoots(mrRoot.text, cfRoot.text) }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: providerSettings.implicitHeight + 48; color: Theme.colors.card; border.color: Theme.colors.border; radius: 12
                    ColumnLayout {
                        id: providerSettings
                        anchors.fill: parent; anchors.margins: 24; spacing: 14
                        RowLayout {
                            SourceBadge { source: "curseforge" }
                            Text { text: root.app.curseforgeEnabled ? "Connected for this session" : "API key required"; color: root.app.curseforgeEnabled ? Theme.colors.accent : Theme.colors.muted; font.pixelSize: 12 }
                            Item { Layout.fillWidth: true }
                        }
                        Text { Layout.fillWidth: true; text: "Use an API key approved for this app's integration. The key stays in memory for this session. Access remains subject to CurseForge's API agreement and author distribution settings."; color: Theme.colors.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; lineHeight: 1.4 }
                        RowLayout {
                            Layout.fillWidth: true
                            SearchField { id: apiKey; Layout.fillWidth: true; placeholderText: "Approved API key"; echoMode: TextInput.Password; selectByMouse: true }
                            ActionButton { text: "Connect"; enabled: apiKey.text.trim().length > 0; onClicked: { root.app.configureCurseforge(apiKey.text); apiKey.clear() } }
                            ActionButton { text: "Disconnect"; visible: root.app.curseforgeEnabled; subtle: true; onClicked: root.app.configureCurseforge("") }
                        }
                        Text { text: "Modrinth browsing works without signing in."; color: Theme.colors.accent; font.pixelSize: 11 }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: gitSettings.implicitHeight + 48; color: Theme.colors.card; border.color: Theme.colors.border; radius: 12
                    ColumnLayout {
                        id: gitSettings
                        anchors.fill: parent; anchors.margins: 24; spacing: 12
                        Text { text: "Git & GitHub"; color: Theme.colors.text; font.pixelSize: 19; font.weight: Font.DemiBold }
                        Text { Layout.fillWidth: true; text: "Local Git repositories and commits are ready. GitHub sign-in, push, pull, and conflict resolution are the next integration step."; color: Theme.colors.muted; font.pixelSize: 12; wrapMode: Text.WordWrap }
                        ActionButton { text: "GitHub connection · coming next"; enabled: false }
                    }
                }

}
