// DANTE + CLOCK: how a Dante network reaches this console, and the network clock it runs on.
//  1. DANTE SOUND CARD: Audinate's Dante Virtual Soundcard or Dante Via turns this computer into a Dante device; it appears here as an
//     ASIO or WASAPI sound card (up to 64 x 64 channels) and opens with one click. Its channels are patched in Dante Controller.
//  2. AES67 MODE: a Dante device switched to AES67 in Dante Controller sends and receives AES67 streams: AUDIO + MIDI plays them,
//     on the PTP clock shown below.
//  3. THE NETWORK CLOCK: the PTP v2 grandmasters heard on the network (listen only: this console never leads or follows the election).
// Dante is Audinate's; the console uses their own sound card or the open AES67 mode and does not imitate the protocol.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AmixOS

Flickable {
    id: dp
    objectName: "dantePanel"
    clip: true
    contentHeight: col.implicitHeight + 20
    signal openAudio()
    function t(s) { return i18n.t(s, i18n.lang) }
    readonly property var dante: { const l = []; for (const d of engine.devices) { const n = (d.name || "").toLowerCase(); if (n.indexOf("dante") >= 0) l.push(d) } return l }
    readonly property var masters: net.ptpMasters
    component Chip: Rectangle { property string label; property bool lit: false; property color hue: Theme.cyan; signal clicked()
        implicitWidth: Math.max(50, cl.implicitWidth + 16); implicitHeight: 24; radius: 8; gradient: Gradient { GradientStop { position: 0; color: lit ? Theme.btnTop(hue) : Qt.rgba(hue.r, hue.g, hue.b, 0.16) } GradientStop { position: 1; color: lit ? Theme.btnBottom(hue) : Qt.rgba(hue.r, hue.g, hue.b, 0.04) } } color: lit ? hue : Theme.ov(0.06); border.color: lit ? "transparent" : hue
        Rectangle { anchors.fill: parent; anchors.margins: 1; radius: Math.max(0, parent.radius - 1); color: "transparent"; border.width: 1; border.color: Qt.rgba(1, 1, 1, parent.lit ? 0.28 : 0.07) }
        Text { id: cl; anchors.centerIn: parent; text: dp.t(parent.label); color: parent.lit ? "#000" : parent.hue; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked() } }
    component Card: Rectangle { default property alias body: inner.data; property color hue: Theme.cyan; property string title; property string sub
        Layout.fillWidth: true; implicitHeight: inner.implicitHeight + 46; radius: Theme.radius; color: Theme.card; border.color: Qt.rgba(hue.r, hue.g, hue.b, 0.55)
        Rectangle { anchors.fill: parent; radius: parent.radius; gradient: Gradient { orientation: Gradient.Horizontal; GradientStop { position: 0; color: Qt.rgba(hue.r, hue.g, hue.b, 0.16) } GradientStop { position: 0.6; color: "transparent" } } }
        Text { x: 14; y: 10; text: dp.t(parent.title); color: parent.hue; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 2 }
        Text { x: 14; y: 26; width: parent.width - 28; text: dp.t(parent.sub); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideRight }
        ColumnLayout { id: inner; x: 14; y: 42; width: parent.width - 28; spacing: 6 } }

    ColumnLayout {
        id: col; width: dp.width - 4; spacing: 10
        Card { title: "1 · DANTE SOUND CARD"; hue: "#ef4444"; sub: "Dante Virtual Soundcard or Dante Via (from Audinate) makes this computer a Dante device: it shows up here as a sound card"
            Repeater { model: dp.dante
                delegate: RowLayout { Layout.fillWidth: true; spacing: 8
                    Rectangle { width: 8; height: 8; radius: 4; color: engine.running && engine.backend === modelData.backend ? Theme.emerald : "#ef4444" }
                    Text { text: modelData.name; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                    Text { text: modelData.backend.toUpperCase() + " · " + modelData.inChannels + " in / " + modelData.outChannels + " out"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 }
                    Chip { objectName: "danteOpen_" + index; label: "OPEN"; hue: "#ef4444"
                        onClicked: { if (modelData.backend === "asio") engine.openAsioDriver(modelData.id, 0); else engine.open(modelData.backend, modelData.inChannels > 0 ? modelData.id : "-", modelData.outChannels > 0 ? modelData.id : "", 48000, 480, modelData.inChannels > 0 ? Math.min(engine.inputCap, Math.max(2, modelData.inChannels)) : 0, Math.min(32, Math.max(2, modelData.outChannels))) } } } }
            Text { visible: dp.dante.length === 0; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9
                   text: dp.t("No Dante sound card on this computer. Install Dante Virtual Soundcard (or Dante Via) from Audinate, activate its licence, start it in ASIO or WDM mode, then press RESCAN. Patch its channels to the console in Dante Controller.") }
            RowLayout { spacing: 8
                Chip { label: "RESCAN"; hue: "#ef4444"; onClicked: engine.refreshDevices() }
                Chip { label: "SOUND CARD PAGE"; hue: Theme.muted; onClicked: { if (typeof root !== "undefined") root.current = "asio" } } } }

        Card { title: "2 · AES67 MODE"; hue: Theme.cyan; sub: "a Dante device switched to AES67 in Dante Controller talks to this console directly over the network, on the PTP clock below"
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9
                   text: dp.t("In Dante Controller: Device View → the device → AES67 Config → AES67 mode ON, reboot it, then create a multicast flow. It appears under STREAMS ON THE NETWORK in AUDIO + MIDI: press USE and START. To send to it, start the AES67 SEND there and subscribe to it in Dante Controller.") }
            RowLayout { spacing: 8
                Chip { label: "AUDIO + MIDI"; hue: Theme.cyan; onClicked: dp.openAudio() }
                Text { text: net.discoverOn ? dp.t("listening for streams: ") + net.streams.length + dp.t(" found") : dp.t("stream listening is off"); color: net.discoverOn ? Theme.emerald : Theme.dim; font.family: Theme.mono; font.pixelSize: 9 } } }

        Card { title: "3 · NETWORK CLOCK (PTP)"; hue: Theme.violet; sub: "the grandmaster every AES67 and Dante-AES67 device locks to: listen only, this console never takes part in the election"
            RowLayout { spacing: 8
                Chip { objectName: "ptpToggle"; label: net.ptpOn ? "LISTENING" : "LISTEN"; lit: net.ptpOn; hue: Theme.violet; onClicked: net.setPtp(!net.ptpOn) }
                Text { Layout.fillWidth: true; text: !net.ptpOn ? dp.t("off") : dp.masters.length === 0 ? dp.t("no PTP clock heard yet (on a Dante network, one appears when a device runs in AES67 mode)") : dp.masters.length + dp.t(" clock(s) heard"); color: net.ptpOn && dp.masters.length ? Theme.emerald : Theme.dim; font.family: Theme.mono; font.pixelSize: 9; elide: Text.ElideRight }
                Text { visible: !!net.message; text: net.message; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideRight; Layout.maximumWidth: 360 } }
            Repeater { model: dp.masters
                delegate: Rectangle { Layout.fillWidth: true; implicitHeight: 46; radius: 6; color: index === 0 ? Qt.rgba(0.545, 0.361, 0.965, 0.16) : Theme.ov(0.04); border.color: index === 0 ? Theme.violet : Theme.lineSoft
                    RowLayout { anchors.fill: parent; anchors.margins: 8; spacing: 12
                        Text { text: index === 0 ? "★ " + dp.t("GRANDMASTER") : dp.t("STANDBY"); color: index === 0 ? Theme.violet : Theme.dim; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; Layout.preferredWidth: 110 }
                        Column { Layout.fillWidth: true; spacing: 2
                            Text { text: modelData.id + "  ·  " + modelData.from; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true }
                            Text { text: dp.t("domain") + " " + modelData.domain + " · " + dp.t("priority") + " " + modelData.priority1 + "/" + modelData.priority2 + " · " + dp.t(modelData.clockName) + " · " + Number(modelData.syncPerSec).toFixed(1) + dp.t(" sync/s") + (modelData.steps > 0 ? " · " + modelData.steps + dp.t(" hop(s)") : ""); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } }
                        Rectangle { width: 10; height: 10; radius: 5; color: modelData.age < 3 ? Theme.emerald : Theme.amber } } } } }
        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8
               text: dp.t("Dante is a trademark of Audinate. This console reaches Dante through Audinate's own sound card software or the open AES67 mode; Dante's own protocol is not imitated.") }
    }
}
