pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    id: root
    required property var app
    property string source: "all"
    signal createRequested()
    signal discoverRequested()
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 32; spacing: 22
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 8
                Text { text: "Your worlds start here."; color: "#eef4ef"; font.pixelSize: 31; font.weight: Font.DemiBold; font.letterSpacing: -0.8 }
                Text { text: "Build something worth getting lost in."; color: "#96ac9e"; font.pixelSize: 14 }
            }
            Item { Layout.fillWidth: true }
            ActionButton { text: root.app.libraryBusy ? "Scanning…" : "↻  Refresh"; enabled: !root.app.libraryBusy; subtle: true; onClicked: root.app.refreshLibrary() }
        }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 138; radius: 12
            gradient: Gradient { orientation: Gradient.Horizontal; GradientStop { position: 0; color: "#253c2d" } GradientStop { position: 1; color: "#1a2921" } }
            border.color: "#34543e"
            RowLayout {
                anchors.fill: parent; anchors.margins: 24; spacing: 24
                ColumnLayout {
                    spacing: 9
                    Text { text: "A home for every mod."; color: "#d9f2e1"; font.pixelSize: 23; font.weight: Font.DemiBold }
                    Text { text: "Discover across platforms. Shape your pack. Keep every change."; color: "#a0c0ac"; font.pixelSize: 13 }
                    Text { text: "MODRINTH  +  CURSEFORGE  +  GIT"; color: "#75ba8d"; font.pixelSize: 10; font.letterSpacing: 2; Layout.topMargin: 6 }
                }
                Item { Layout.fillWidth: true }
                ActionButton { text: "Explore mods  →"; primary: true; onClicked: root.discoverRequested() }
            }
        }
        ScrollView {
            id: scroll
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            ColumnLayout {
                width: scroll.availableWidth; spacing: 16
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Your modpacks"; color: "#e7f0e9"; font.pixelSize: 19; font.weight: Font.DemiBold }
                    Text { text: root.app.packs.length; color: "#7d9a87"; font.pixelSize: 13 }
                    Item { Layout.fillWidth: true }
                    ActionButton { text: "+ Create"; subtle: true; onClicked: root.createRequested() }
                }
                Rectangle {
                    visible: root.app.packs.length === 0
                    Layout.fillWidth: true; implicitHeight: 106; radius: 10; color: "#19231c"; border.color: "#324238"
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 22; spacing: 18
                        Text { text: "⑂"; font.pixelSize: 34; color: "#79b28d" }
                        ColumnLayout {
                            spacing: 7
                            Text { text: "Make your first modpack"; color: "#d8e7dd"; font.pixelSize: 15; font.weight: Font.DemiBold }
                            Text { text: "Start a Git repository and pin the mods you want."; color: "#92ab9b"; font.pixelSize: 12 }
                        }
                        Item { Layout.fillWidth: true }
                        ActionButton { text: "Get started"; onClicked: root.createRequested() }
                    }
                }
                Repeater {
                    model: root.app.packs
                    delegate: Rectangle {
                        id: packCard
                        required property var modelData
                        Layout.fillWidth: true; implicitHeight: 90; color: "#1c2820"; border.color: packMouse.containsMouse ? "#62866c" : "#33483a"; radius: 10
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 18; spacing: 16
                            Rectangle { implicitWidth: 48; implicitHeight: 48; radius: 12; color: "#304d38"; Text { anchors.centerIn: parent; text: packCard.modelData.name.substring(0,1).toUpperCase(); font.pixelSize: 23; font.weight: Font.Bold; color: "#b9e7c7" } }
                            ColumnLayout { Layout.fillWidth: true; spacing: 6; Text { text: packCard.modelData.name; color: "#e6efe9"; font.pixelSize: 16; font.weight: Font.DemiBold } Text { text: packCard.modelData.minecraft + "  ·  " + packCard.modelData.loader + "  ·  " + packCard.modelData.modCount + " mods"; color: "#8ea697"; font.pixelSize: 12 } }
                            SourceBadge { source: "git" }
                            Text { text: "→"; color: "#96b7a1"; font.pixelSize: 22 }
                        }
                        MouseArea { id: packMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.app.openPack(packCard.modelData.path) }
                    }
                }
                RowLayout {
                    Layout.topMargin: 18; Layout.fillWidth: true
                    Text { text: "Launcher installations"; color: "#e7f0e9"; font.pixelSize: 19; font.weight: Font.DemiBold }
                    Item { Layout.fillWidth: true }
                    Repeater {
                        model: ["all", "modrinth", "curseforge"]
                        delegate: ActionButton {
                            required property string modelData
                            text: modelData === "all" ? "All" : modelData === "modrinth" ? "Modrinth" : "CurseForge"
                            subtle: root.source !== modelData; onClicked: root.source = modelData
                        }
                    }
                }
                Text { text: "Connected from your launchers. Open a folder to inspect its files."; color: "#8da596"; font.pixelSize: 12 }
                Repeater {
                    model: root.app.installations.filter(function(row) { return root.source === "all" || row.provider === root.source })
                    delegate: Rectangle {
                        id: installationCard
                        required property var modelData
                        Layout.fillWidth: true; implicitHeight: 86; radius: 10; color: "#19221c"; border.color: "#2c3a30"
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 16; spacing: 16
                            Rectangle {
                                implicitWidth: 48; implicitHeight: 48; radius: 10; color: installationCard.modelData.provider === "modrinth" ? "#284331" : "#3c3025"; clip: true
                                Text { anchors.centerIn: parent; text: installationCard.modelData.title.substring(0, 1); color: "#c8d9cc"; font.pixelSize: 23; font.weight: Font.DemiBold }
                                Image { anchors.fill: parent; source: installationCard.modelData.icon; fillMode: Image.PreserveAspectCrop; asynchronous: true; visible: status === Image.Ready }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 6
                                Text { text: installationCard.modelData.title; color: "#dce7df"; font.pixelSize: 15; font.weight: Font.DemiBold }
                                Text { text: (installationCard.modelData.gameVersion || "Minecraft") + (installationCard.modelData.loader ? "  ·  " + installationCard.modelData.loader : "") + "  ·  " + installationCard.modelData.modCount + " mod files"; color: "#89a18f"; font.pixelSize: 12 }
                            }
                            SourceBadge { source: installationCard.modelData.provider }
                            ActionButton { text: "Open folder"; subtle: true; onClicked: root.app.openFolder(installationCard.modelData.path) }
                        }
                    }
                }
                Text { visible: root.app.installations.length === 0 && !root.app.libraryBusy; text: "No installations found. Set your launcher folders in Settings."; color: "#98b09f"; font.pixelSize: 13; Layout.topMargin: 12 }
                Item { implicitHeight: 20 }
            }
        }
    }
}
