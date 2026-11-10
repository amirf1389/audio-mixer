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
    Connections { target: engine; function onDevicesChanged() { page.tick++ } }
    function open() { const i = page.inId !== "-" ? 2 : 0; const ok = engine.open(page.backend, page.inId, page.outId, settings.defaultRate || 48000, Math.max(256, settings.defaultFrames || 512), i, 2); if (ok) settings.rememberDevice(page.backend, page.inId, page.outId, settings.defaultRate || 48000, Math.max(256, settings.defaultFrames || 512), i, 2) }
    component Head: Text { color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
    component Note: Text { Layout.fillWidth: true; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 24; radius: 5; color: on ? onColor : Theme.ov(0.08); border.color: Theme.lineSoft
        Text { id: bl; anchors.centerIn: parent; text: label; color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle { Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 8
                Text { text: "BLUETOOTH:"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Text { text: page.bt.length + " Bluetooth audio device" + (page.bt.length === 1 ? "" : "s") + " seen"; color: page.bt.length ? Theme.emerald : Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Item { Layout.fillWidth: true }
                Text { text: engine.running ? engine.status : "ENGINE IDLE"; color: engine.running ? Theme.emerald : Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                Btn { label: "RESCAN"; onClicked: engine.refreshDevices() } } }
        RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 8
                    Head { text: "PAIRED & CONNECTED" }
                    Note { text: "Every endpoint whose name says Bluetooth, hands-free, A2DP, headset or a known brand. DirectSound entries first (the path with no lock); WASAPI shared entries also work. Pair a new device in Windows Settings › Bluetooth, then RESCAN." }
                    ListView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: page.bt; spacing: 4
                        delegate: Rectangle { width: ListView.view.width; height: 44; radius: 6; property bool pickedOut: page.outId === modelData.id && page.backend === modelData.backend; property bool pickedIn: page.inId === modelData.id && page.backend === modelData.backend
                            color: pickedOut || pickedIn ? Qt.rgba(0.2, 0.9, 0.5, 0.12) : Theme.ov(0.04); border.color: pickedOut || pickedIn ? Theme.emerald : Theme.lineSoft
                            RowLayout { anchors.fill: parent; anchors.margins: 8; spacing: 8
                                Text { text: "⮁"; color: Theme.cyan; font.pixelSize: 14 }
                                ColumnLayout { spacing: 1; Layout.fillWidth: true
                                    Text { text: modelData.name; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Text { text: modelData.backend.toUpperCase() + " · " + (modelData.outChannels > 0 ? modelData.outChannels + " out" : "") + (modelData.inChannels > 0 ? (modelData.outChannels > 0 ? " · " : "") + modelData.inChannels + " in (hands-free mic)" : "") + (modelData.defaultOut ? " · Windows default" : ""); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
                                Btn { visible: modelData.outChannels > 0; label: pickedOut ? "✓ OUTPUT" : "USE AS OUTPUT"; on: pickedOut; onColor: Theme.emerald; onClicked: { page.outId = modelData.id; page.backend = modelData.backend; if (page.inId !== "-") { let ok = false; for (let k = 0; k < page.bt.length; ++k) if (page.bt[k].id === page.inId && page.bt[k].backend === modelData.backend) ok = true; if (!ok) page.inId = "-" } } }
                                Btn { visible: modelData.inChannels > 0; label: pickedIn ? "✓ INPUT" : "USE AS INPUT"; on: pickedIn; onColor: Theme.cyan; onClicked: { page.inId = pickedIn ? "-" : modelData.id; page.backend = modelData.backend } } } } }
                    Text { visible: page.bt.length === 0; Layout.fillWidth: true; text: "No Bluetooth audio device is connected to this computer (or this is not Windows). Pair one in Windows Settings › Bluetooth & devices, connect it, then RESCAN."; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; wrapMode: Text.WordWrap } } }
            Rectangle { Layout.preferredWidth: 300; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 8
                    Head { text: "START ON BLUETOOTH" }
                    Text { text: "OUTPUT  " + (page.outId ? page.outId : "pick one"); color: page.outId ? Theme.emerald : Theme.dim; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideMiddle; Layout.fillWidth: true }
                    Text { text: "INPUT  " + (page.inId !== "-" ? page.inId : "none (the mixer's inputs stay on the sound card's strips)"); color: page.inId !== "-" ? Theme.cyan : Theme.dim; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideMiddle; Layout.fillWidth: true }
                    Text { text: "PATH  " + page.backend.toUpperCase() + " · " + (settings.defaultRate || 48000) + " Hz · " + Math.max(256, settings.defaultFrames || 512) + " frames"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                    Btn { label: "▶ START ENGINE ON BLUETOOTH"; on: page.outId !== ""; onColor: Theme.emerald; onClicked: if (page.outId) page.open() }
                    Btn { label: "⏹ STOP"; visible: engine.running; onClicked: engine.stop() }
                    Text { Layout.fillWidth: true; text: engine.lastRoute; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.lineSoft }
                    Head { text: "WHAT TO EXPECT" }
                    Note { text: "• A2DP (music profile) is stereo output only; its microphone needs the hands-free profile (HFP), which Windows exposes as a separate mono 'Hands-Free' endpoint and drops the output to phone quality while it is open.\n• Latency 100-250 ms from the codec (SBC / AAC / aptX): monitoring a stream or playback is fine, playing an instrument live is not.\n• DirectSound has no exclusive lock: Windows keeps mixing other programs into the device. ASIO never sees Bluetooth.\n• Blocks under 256 frames gain nothing here; 512 is a good start." } } } }
    }
}
