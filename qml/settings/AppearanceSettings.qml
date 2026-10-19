pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Conflux.Style
import "../components"
ColumnLayout {
    spacing: 16
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: themeSettings.implicitHeight + 48; color: Theme.colors.card; border.color: Theme.colors.border; radius: 12
                    ColumnLayout {
                        id: themeSettings; anchors.fill: parent; anchors.margins: 24; spacing: 12
                        Text { text: "Appearance"; color: Theme.colors.text; font.pixelSize: 19; font.weight: Font.DemiBold }
                        Text { Layout.fillWidth: true; text: "Four color groups style the whole app. Copy dark-grey.json in the themes folder to create a theme."; color: Theme.colors.muted; font.pixelSize: 12; wrapMode: Text.WordWrap }
                        RowLayout {
                            SelectBox { id: themeChoice; Layout.preferredWidth: 240; model: Theme.available; currentIndex: Math.max(0, Theme.available.indexOf(Theme.file)); onActivated: Theme.select(currentText) }
                            ActionButton { text: "Reload theme"; onClicked: Theme.reload() }
                            ActionButton { text: "Open themes folder"; subtle: true; onClicked: Theme.openFolder() }
                        }
                        Text { text: "Active: " + Theme.name; color: Theme.colors.muted; font.pixelSize: 12 }
                        Text { visible: Theme.error.length > 0; Layout.fillWidth: true; text: Theme.error; wrapMode: Text.WordWrap; color: Theme.colors.warning; font.pixelSize: 12 }
                    }
                }

}
