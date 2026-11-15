// BLUETOOTH: the Bluetooth headsets, speakers and hands-free devices Windows exposes as audio endpoints (A2DP for music, HFP for a
// headset microphone). They are opened through DirectSound (no exclusive lock, Windows mixes them) or WASAPI shared; ASIO never sees
// them. Latency is the Bluetooth codec's (100-250 ms): fine for playback and monitoring a stream, not for live performing. Pairing stays
// in Windows Settings; this page lists what is paired and connected, and starts the engine on it with the clock and block of SETTINGS.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "bluetoothPage"
    property int tick: 0
    readonly property var bt: (tick, engine.bluetoothDevices())
    property string outId: ""
    property string inId: "-"
    property string backend: "dsound"
    property string via: ""            // "" = any path, "wasapi" = Windows audio (shared), "dsound" = DirectSound
    readonly property var shown: bt.filter(function (d) { return page.via === "" || d.backend === page.via })
    // DISCOVERY: a Bluetooth device is paired and connected by the operating system; once it is, it appears here by itself (the device list is
    // watched for changes). PAIR A DEVICE opens the system's own Bluetooth page. CONNECT picks the first output shown (and its own microphone) and starts.
    function pair() { if (Qt.platform.os === "windows") Qt.openUrlExternally("ms-settings:bluetooth"); else if (Qt.platform.os === "osx") Qt.openUrlExternally("x-apple.systempreferences:com.apple.preferences.Bluetooth"); else page.note = "Pair the device in the system's Bluetooth settings: it then appears here." }
    function connectBest() {
        let out = null; for (let i = 0; i < shown.length && !out; ++i) if (shown[i].outChannels > 0 && shown[i].kind !== "headset") out = shown[i]
        for (let i = 0; i < shown.length && !out; ++i) if (shown[i].outChannels > 0) out = shown[i]
        if (!out) { page.note = "No Bluetooth output on this path. Pair and connect the device, then RESCAN."; return }
        page.backend = out.backend; page.outId = out.id; page.inId = "-"; page.open()
    }
    Connections { target: engine; function onDevicesChanged() { page.tick++ } }
    function open() { const ok = engine.openBluetooth(page.backend, page.inId, page.outId, Math.max(512, settings.defaultFrames || 512)); page.note = engine.bluetoothNote(); if (ok) { const io = engine.ioInfo(); settings.rememberDevice(engine.backend, io.inputs > 0 ? page.inId : "-", page.outId, engine.sampleRate, engine.blockFrames, io.inputs > 0 ? 2 : 0, 2) } }
    property string note: ""
    component Head: Text { color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
    component Note: Text { Layout.fillWidth: true; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 24; radius: 8; gradient: Gradient { GradientStop { position: 0; color: on ? Theme.btnTop(onColor) : Theme.ov(0.12) } GradientStop { position: 1; color: on ? Theme.btnBottom(onColor) : Theme.ov(0.035) } } color: on ? onColor : Theme.ov(0.08); border.color: Theme.lineSoft
        Rectangle { anchors.fill: parent; anchors.margins: 1; radius: Math.max(0, parent.radius - 1); color: "transparent"; border.width: 1; border.color: Qt.rgba(1, 1, 1, parent.on ? 0.30 : 0.07) }
        Text { id: bl; anchors.centerIn: parent; text: i18n.t(label, i18n.lang); color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle { Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 8
                Text { text: i18n.t("BLUETOOTH:", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Text { text: page.bt.length + i18n.t(" Bluetooth audio device", i18n.lang) + (page.bt.length === 1 ? "" : "s") + i18n.t(" seen", i18n.lang); color: page.bt.length ? Theme.emerald : Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Item { Layout.fillWidth: true }
                Text { text: engine.running ? engine.status : i18n.t("ENGINE IDLE", i18n.lang); color: engine.running ? Theme.emerald : Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                Text { text: i18n.t("VIA", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Repeater { model: [["", "ANY"], ["wasapi", "WASAPI"], ["dsound", "DIRECTSOUND"]]; delegate: Btn { objectName: "btVia_" + (modelData[0] || "any"); label: i18n.t(modelData[1], i18n.lang); on: page.via === modelData[0]; onColor: Theme.cyan; onClicked: page.via = modelData[0] } }
                Btn { objectName: "btConnectBtn"; label: i18n.t("CONNECT", i18n.lang); on: page.shown.length > 0; onColor: Theme.emerald; onClicked: page.connectBest() }
                Btn { objectName: "btPairBtn"; label: i18n.t("PAIR A DEVICE", i18n.lang); onClicked: page.pair() }
                Btn { label: i18n.t("RESCAN", i18n.lang); onClicked: engine.refreshDevices() } } }
        RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 8
                    Head { text: i18n.t("PAIRED & CONNECTED", i18n.lang) }
                    Note { text: i18n.t("Every endpoint whose name says Bluetooth, hands-free, A2DP, headset or a known brand. DirectSound entries first (the path with no lock); WASAPI shared entries also work. Pair a new device in Windows Settings › Bluetooth, then RESCAN.", i18n.lang) }
                    ListView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: page.shown; spacing: 4
                        delegate: Rectangle { width: ListView.view.width; height: 44; radius: 6; property bool pickedOut: page.outId === modelData.id && page.backend === modelData.backend; property bool pickedIn: page.inId === modelData.id && page.backend === modelData.backend
                            color: pickedOut || pickedIn ? Qt.rgba(0.2, 0.9, 0.5, 0.12) : Theme.ov(0.04); border.color: pickedOut || pickedIn ? Theme.emerald : Theme.lineSoft
                            RowLayout { anchors.fill: parent; anchors.margins: 8; spacing: 8
                                Text { text: "⮁"; color: Theme.cyan; font.pixelSize: 14 }
                                ColumnLayout { spacing: 1; Layout.fillWidth: true
                                    Text { text: i18n.t(modelData.name ?? "", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Text { text: modelData.backend.toUpperCase() + " · " + (modelData.outChannels > 0 ? modelData.outChannels + i18n.t(" out", i18n.lang) : "") + (modelData.inChannels > 0 ? (modelData.outChannels > 0 ? " · " : "") + modelData.inChannels + i18n.t(" in (hands-free mic)", i18n.lang) : "") + (modelData.defaultOut ? i18n.t(" · Windows default", i18n.lang) : ""); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
                                Btn { visible: modelData.outChannels > 0; label: pickedOut ? i18n.t("✓ OUTPUT", i18n.lang) : i18n.t("USE AS OUTPUT", i18n.lang); on: pickedOut; onColor: Theme.emerald; onClicked: { page.outId = modelData.id; page.backend = modelData.backend; if (page.inId !== "-") { let ok = false; for (let k = 0; k < page.bt.length; ++k) if (page.bt[k].id === page.inId && page.bt[k].backend === modelData.backend) ok = true; if (!ok) page.inId = "-" } } }
                                Btn { visible: modelData.inChannels > 0; label: pickedIn ? i18n.t("✓ INPUT", i18n.lang) : i18n.t("USE AS INPUT", i18n.lang); on: pickedIn; onColor: Theme.cyan; onClicked: { page.inId = pickedIn ? "-" : modelData.id; page.backend = modelData.backend } } } } }
                    Text { visible: page.bt.length === 0; Layout.fillWidth: true; text: i18n.t("No Bluetooth audio device is connected to this computer (or this is not Windows). Pair one in Windows Settings › Bluetooth & devices, connect it, then RESCAN.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; wrapMode: Text.WordWrap } } }
            Rectangle { Layout.preferredWidth: 300; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 8
                    Head { text: i18n.t("START ON BLUETOOTH", i18n.lang) }
                    Text { text: i18n.t("OUTPUT  ", i18n.lang) + (page.outId ? page.outId : i18n.t("pick one", i18n.lang)); color: page.outId ? Theme.emerald : Theme.dim; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideMiddle; Layout.fillWidth: true }
                    Text { text: i18n.t("INPUT  ", i18n.lang) + (page.inId !== "-" ? page.inId : i18n.t("none (the mixer's inputs stay on the sound card's strips)", i18n.lang)); color: page.inId !== "-" ? Theme.cyan : Theme.dim; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideMiddle; Layout.fillWidth: true }
                    Text { text: i18n.t("PATH  ", i18n.lang) + page.backend.toUpperCase() + " · " + (settings.defaultRate || 48000) + i18n.t(" Hz · ", i18n.lang) + Math.max(256, settings.defaultFrames || 512) + i18n.t(" frames", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                    Btn { label: i18n.t("▶ START ENGINE ON BLUETOOTH", i18n.lang); on: page.outId !== ""; onColor: Theme.emerald; onClicked: if (page.outId) page.open() }
                    Btn { label: i18n.t("⏹ STOP", i18n.lang); visible: engine.running; onClicked: engine.stop() }
                    Text { Layout.fillWidth: true; visible: page.note !== ""; text: page.note; color: engine.running ? Theme.emerald : Theme.danger; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; wrapMode: Text.WordWrap }
                    Text { Layout.fillWidth: true; text: engine.lastRoute; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.lineSoft }
                    Head { text: i18n.t("WHAT TO EXPECT", i18n.lang) }
                    Note { text: i18n.t("• A2DP (music profile) is stereo output only; its microphone needs the hands-free profile (HFP), which Windows exposes as a separate mono 'Hands-Free' endpoint and drops the output to phone quality while it is open.\n• Latency 100-250 ms from the codec (SBC / AAC / aptX): monitoring a stream or playback is fine, playing an instrument live is not.\n• DirectSound has no exclusive lock: Windows keeps mixing other programs into the device. ASIO never sees Bluetooth.\n• Blocks under 256 frames gain nothing here; 512 is a good start.", i18n.lang) } } } }
    }
}
