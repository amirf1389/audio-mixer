// CLOUD: the update information in one place. This version, the newest version on the update server, the signed feature feed
// (what each release brought) and what is being built next. Nothing is uploaded from here: the console only reads the signed list.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AmixOS

Flickable {
    id: page
    objectName: "cloudPage"
    contentHeight: col.implicitHeight + 24; clip: true; boundsBehavior: Flickable.StopAtBounds
    function t(s) { return i18n.t(s, i18n.lang) }
    readonly property var upcoming: [
        "Single AmF X1.exe with the interface built in",
        "Phase correction and AUTO MIX switches on every channel",
        "Firmware update check for audio interfaces (download only, never flashed from the console)",
        "Processor load, temperature and power panel",
        "Sample player on a channel: click, switch on, choose the sample",
        "Room correction curve and microphone calibration files on ACOUSTICS",
        "Online store for extensions",
        "CASCADE: two consoles as one desk"
    ]
    component Card: Rectangle { id: cd; property string title; property color tone: Theme.cyan; default property alias body: cdCol.data
        Layout.fillWidth: true; implicitHeight: cdCol.implicitHeight + 28; radius: Theme.radius; color: Theme.card; border.color: Theme.line
        Rectangle { width: 3; height: parent.height - 16; y: 8; radius: 2; color: cd.tone }
        ColumnLayout { id: cdCol; anchors.fill: parent; anchors.margins: 14; spacing: 8
            Text { text: cd.title; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 12; font.bold: true; font.letterSpacing: 1.5 } } }

    ColumnLayout {
        id: col; x: 12; y: 12; width: page.width - 24; spacing: 12
        Card {
            title: page.t("CLOUD · UPDATES")
            RowLayout { spacing: 16
                Text { text: page.t("This version: ") + updater.current; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11 }
                Text { text: page.t("On the server: ") + (updater.latest !== "" ? updater.latest : page.t("not checked yet")); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 11 }
                Item { Layout.fillWidth: true }
                Rectangle { id: gb19; implicitWidth: ck.implicitWidth + 22; implicitHeight: 26; radius: 7; color: Theme.ov(0.06); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb19.color) } GradientStop { position: 1; color: Theme.btnBottom(gb19.color) } } border.color: Theme.cyan
                    Text { id: ck; anchors.centerIn: parent; text: page.t("CHECK NOW"); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: updater.check() } }
            }
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.pixelSize: 10; text: updater.message }
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.dim; font.pixelSize: 9
                   text: page.t("The list comes from the update server and is checked against the vendor's signature before it is shown. Nothing from this computer is uploaded.") }
        }
        Card {
            title: page.t("LOCAL STORE · ON THIS COMPUTER"); tone: Theme.cyan
            property int rev: 0
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.pixelSize: 10
                   text: page.t("Extensions and built-in effects installed with AmF X1. Switch one off to take it out of the console; switch it on to bring it back. Nothing is downloaded from here.") }
            GridLayout {
                id: storeGrid; Layout.fillWidth: true; columns: page.width > 1100 ? 3 : 2; columnSpacing: 8; rowSpacing: 6
                Repeater {
                    model: { storeGrid.parent.rev; return engine.storeItems() }
                    Rectangle {
                        required property var modelData
                        Layout.fillWidth: true; implicitHeight: 46; radius: 6; color: Theme.card; border.color: modelData.on && modelData.installed ? Theme.cyan : Theme.line
                        RowLayout { anchors.fill: parent; anchors.margins: 8; spacing: 8
                            Text { text: parent.parent.modelData.kind === "extension" ? "⬢" : "✦"; color: Theme.amber; font.pixelSize: 14 }
                            ColumnLayout { Layout.fillWidth: true; spacing: 1
                                Text { text: parent.parent.parent.modelData.name; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                Text { readonly property var md: parent.parent.parent.modelData; text: !md.installed ? page.t("not installed next to the console") : (md.kind === "extension" && md.loads === false ? page.t("cannot load: ") + md.error : page.t(md.note)); color: md.kind === "extension" && md.installed && md.loads === false ? Theme.danger : Theme.dim; font.pixelSize: 8; elide: Text.ElideRight; Layout.fillWidth: true } }
                            Rectangle { readonly property var it: parent.parent.modelData
                                objectName: "store_" + it.id; enabled: it.installed; opacity: it.installed ? 1 : 0.4
                                implicitWidth: 46; implicitHeight: 20; radius: 10; color: it.on ? Theme.cyan : Theme.ov(0.1)
                                Text { anchors.centerIn: parent; text: parent.it.on ? page.t("ON") : page.t("OFF"); color: parent.it.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { engine.setStoreEnabled(parent.it.id, !parent.it.on); storeGrid.parent.rev++ } } } }
                    }
                }
            }
        }
        Card {
            title: page.t("UPCOMING UPDATES"); tone: Theme.amber
            Repeater { model: page.upcoming
                Text { required property string modelData; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink; font.pixelSize: 11; text: "•  " + page.t(modelData) } }
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.dim; font.pixelSize: 9; text: page.t("Planned, not promised: an item arrives when it has passed its tests.") }
        }
        Card {
            title: page.t("WHAT EACH RELEASE BROUGHT"); tone: Theme.violet
            Text { visible: updater.features.length === 0; color: Theme.muted; font.pixelSize: 10; text: page.t("Press CHECK NOW to load the signed feature list.") }
            Repeater { model: updater.features
                ColumnLayout { required property var modelData; Layout.fillWidth: true; spacing: 4
                    Text { text: modelData.version + "  ·  " + (modelData.date || "") + "  ·  " + (modelData.title || ""); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 10; font.bold: true }
                    Repeater { model: (modelData.items || []).slice(0, 12)
                        Text { required property string modelData; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.pixelSize: 9; text: "– " + modelData } } } }
        }
    }
}
