// CASCADE: two AmF X1 consoles linked as one desk. In development in this version: the page says what it will do and what works today.
import QtQuick
import QtQuick.Layouts
import AmixOS

Flickable {
    id: page
    objectName: "cascadePage"
    contentHeight: col.implicitHeight + 24; clip: true; boundsBehavior: Flickable.StopAtBounds
    function t(s) { return i18n.t(s, i18n.lang) }
    ColumnLayout {
        id: col; x: 12; y: 12; width: page.width - 24; spacing: 12
        Rectangle { Layout.fillWidth: true; implicitHeight: c1.implicitHeight + 28; radius: Theme.radius; color: Theme.card; border.color: Theme.amber
            ColumnLayout { id: c1; anchors.fill: parent; anchors.margins: 14; spacing: 8
                RowLayout { spacing: 10
                    Text { text: page.t("CASCADE"); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 13; font.bold: true; font.letterSpacing: 1.5 }
                    Rectangle { id: gb18; implicitWidth: dv.implicitWidth + 14; implicitHeight: 18; radius: 7; color: Qt.rgba(1, 0.7, 0.2, 0.18); gradient: Gradient { GradientStop { position: 0; color: Qt.lighter(gb18.color, 1.25) } GradientStop { position: 1; color: Qt.darker(gb18.color, 1.12) } } border.color: Theme.amber
                        Text { id: dv; anchors.centerIn: parent; text: page.t("IN DEVELOPMENT"); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } } }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.pixelSize: 11
                       text: page.t("Two AmF X1 consoles on one network working as one desk: the second console's master arrives on two strips of the first, and scenes, mutes and DCA groups follow the first console.") }
                Text { text: page.t("WHAT WORKS TODAY"); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 10; font.bold: true }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink; font.pixelSize: 10
                       text: page.t("• Network audio between consoles: NETWORK → AUDIO + MIDI sends a console's master and receives it on a strip.\n• Remote control of a second console with its pairing PIN on NETWORK.") }
                Text { text: page.t("STILL TO COME"); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 10; font.bold: true }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink; font.pixelSize: 10
                       text: page.t("• One sample clock for both consoles.\n• Scenes, mutes and DCA groups that follow the first console.\n• Finding the other console by itself on the network.") }
            }
        }
    }
}
