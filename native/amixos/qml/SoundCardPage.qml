// SOUND CARD: the open device, its clock and block size (change and re-open), the DSP profiler (block time, worst case, load, jitter,
// overruns), the output stage (DC removal, dither depth), AUTO DETECT / SMART START switches and the driver ladder the next start would walk.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "soundCardPage"
    readonly property var tel: engine.telemetry
    readonly property var last: settings.lastDevice
    property var plan: []
    function reopen(rate, frames) {
        const d = page.last; if (!d || (!d.in && !d.out)) { return }
        const ok = engine.openSmart(d.backend, d.in, d.out, rate, frames, d.inChannels || 2, d.outChannels || 2)
        if (ok) settings.rememberDevice(d.backend, d.in, d.out, rate, frames, d.inChannels || 2, d.outChannels || 2)
    }
    Component.onCompleted: if (page.last && page.last.backend) page.plan = engine.driverPlan(page.last.backend, page.last.in || "", page.last.out || "")
    component Head: Text { color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
    component Note: Text { Layout.fillWidth: true; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(50, bl.implicitWidth + 16); implicitHeight: 24; radius: 4; color: on ? onColor : Qt.rgba(1, 1, 1, 0.08); border.color: Theme.lineSoft
        Text { id: bl; anchors.centerIn: parent; text: label; color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    component Card: Rectangle { default property alias content: body.data; Layout.fillWidth: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; implicitHeight: body.implicitHeight + 24; ColumnLayout { id: body; anchors.fill: parent; anchors.margins: 12; spacing: 8 } }
    component KV: RowLayout { property string k; property string v; property color c: Theme.cyan; spacing: 10; Text { Layout.preferredWidth: 120; text: k; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Text { text: v; color: c; font.family: Theme.mono; font.pixelSize: 10; font.bold: true } }
    Flickable { anchors.fill: parent; anchors.margins: 10; contentHeight: grid.implicitHeight; clip: true; boundsBehavior: Flickable.StopAtBounds
        GridLayout { id: grid; width: parent.width; columns: 2; columnSpacing: 8; rowSpacing: 8
            Card { Head { text: "DEVICE" }
                KV { k: "STATE"; v: engine.running ? "RUNNING on " + engine.backend.toUpperCase() : "STOPPED"; c: engine.running ? Theme.emerald : Theme.amber }
                KV { k: "INPUT"; v: page.last && page.last.in ? page.last.in : "—" }
                KV { k: "OUTPUT"; v: page.last && page.last.out ? page.last.out : "—" }
                KV { k: "CHANNELS"; v: engine.running ? engine.deviceInputs + " in / " + engine.deviceOutputs + " out" : "—" }
                KV { k: "LAST START"; v: engine.lastRoute ? engine.lastRoute : "—"; c: Theme.muted }
                RowLayout { spacing: 6
                    Btn { label: engine.running ? "STOP" : "START (SMART)"; on: true; onColor: engine.running ? Theme.danger : Theme.emerald; onClicked: engine.running ? engine.stop() : settings.startLastDevice() }
                    Btn { label: "AUTO DETECT"; onClicked: engine.autoStart(settings.defaultFrames) }
                    Btn { label: settings.autoAsio ? "AUTO ASIO: ON" : "AUTO ASIO: OFF"; on: settings.autoAsio; onColor: Theme.cyan; onClicked: settings.autoAsio = !settings.autoAsio }
                    Btn { label: settings.smartStart ? "SMART START: ON" : "SMART START: OFF"; on: settings.smartStart; onColor: Theme.cyan; onClicked: settings.smartStart = !settings.smartStart } }
                Note { text: "Pick the interface itself on CONNECTIONS or LIVE SOURCES. AUTO ASIO opens the best driver (native ASIO before wrappers, Realtek pinned to WASAPI) when nothing is remembered; SMART START walks the driver ladder when the first choice fails. The ASIO control panel of a driver opens from the driver's own tray tool; the engine does not pop it up." } }
            Card { Head { text: "CLOCK & BLOCK" }
                KV { k: "SAMPLE RATE"; v: engine.running ? engine.sampleRate + " Hz" : "—" }
                KV { k: "BLOCK"; v: engine.running ? engine.blockFrames + " frames = " + (engine.blockFrames / engine.sampleRate * 1000).toFixed(2) + " ms" : "—" }
                RowLayout { spacing: 4; Text { text: "RATE"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; Layout.preferredWidth: 40 }
                    Repeater { model: [44100, 48000, 88200, 96000, 192000]; delegate: Btn { label: modelData / 1000 + "k"; on: engine.running && engine.sampleRate === modelData; onClicked: page.reopen(modelData, engine.blockFrames || settings.defaultFrames) } } }
                RowLayout { spacing: 4; Text { text: "BLOCK"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; Layout.preferredWidth: 40 }
                    Repeater { model: [32, 64, 128, 256, 512, 1024]; delegate: Btn { label: modelData; on: engine.running && engine.blockFrames === modelData; onClicked: page.reopen(engine.sampleRate || settings.defaultRate, modelData) } } }
                Note { text: "A click re-opens the remembered device at that rate or block (SMART START). Lower blocks mean less latency and more risk of overruns; watch the profiler below. 96 / 192 kHz run natively in double precision." } }
            Card { RowLayout { Head { text: "DSP PROFILER" } Item { Layout.fillWidth: true } Btn { label: "RESET"; onClicked: engine.resetProfiler() } }
                KV { k: "LOAD"; v: engine.running ? page.tel.loadPct.toFixed(1) + " % of the block" : "—"; c: page.tel.loadPct > 70 ? Theme.danger : page.tel.loadPct > 40 ? Theme.amber : Theme.emerald }
                Rectangle { Layout.fillWidth: true; implicitHeight: 10; radius: 3; color: Qt.rgba(1, 1, 1, 0.06); Rectangle { width: parent.width * Math.min(1, page.tel.loadPct / 100); height: parent.height; radius: 3; color: page.tel.loadPct > 70 ? Theme.danger : page.tel.loadPct > 40 ? Theme.amber : Theme.emerald } }
                KV { k: "BLOCK TIME"; v: page.tel.blockUs.toFixed(1) + " µs  (worst " + page.tel.maxBlockUs.toFixed(1) + " µs)" }
                KV { k: "JITTER"; v: page.tel.jitterUs.toFixed(1) + " µs between blocks" }
                KV { k: "OVERRUNS"; v: page.tel.xruns + " since start · " + page.tel.blocks + " blocks"; c: page.tel.xruns > 0 ? Theme.amber : Theme.cyan }
                Note { text: "Load is the DSP time of the last block divided by the block's duration: above 70 % the block may not finish in time. Worst case and jitter reset with RESET. Overruns come from the driver (ASIO / WASAPI / ALSA report them)." } }
            Card { Head { text: "OUTPUT STAGE" }
                RowLayout { spacing: 6; Text { text: "DC REMOVAL"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; Layout.preferredWidth: 120 } Btn { label: engine.dcBlock ? "ON" : "OFF"; on: engine.dcBlock; onColor: Theme.emerald; onClicked: engine.dcBlock = !engine.dcBlock } }
                RowLayout { spacing: 6; Text { text: "DITHER"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; Layout.preferredWidth: 120 }
                    Repeater { model: [{ t: "FLOAT (none)", v: 0 }, { t: "16-BIT", v: 16 }, { t: "24-BIT", v: 24 }]; delegate: Btn { label: modelData.t; on: engine.outputBits === modelData.v; onColor: Theme.cyan; onClicked: engine.outputBits = modelData.v } } }
                Note { text: "DC removal is a 5 Hz high-pass on the master before the auto level and the limiter: an offset from a player or a plug-in never eats headroom. Dither (TPDF) is the last thing before the converter: pick the depth your device takes in integer mode (WASAPI shared and most ASIO drivers take float: leave FLOAT). Denormals are flushed in the DSP already." } }
            Card { Layout.columnSpan: 2; Head { text: "DRIVER LADDER (what the next start tries, in order)" }
                Repeater { model: page.plan; delegate: RowLayout { spacing: 8
                    Text { text: (index + 1) + "."; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                    Text { text: modelData.mode + "  " + modelData.backend.toUpperCase() + (modelData.exclusive ? " exclusive" : ""); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; Layout.preferredWidth: 220 }
                    Text { text: modelData.why + (modelData.fromLock ? "  [learned]" : "") + (modelData.pinned ? "  [pinned]" : ""); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; Layout.fillWidth: true; elide: Text.ElideRight } } }
                Text { visible: page.plan.length === 0; text: "No device remembered yet: start one on CONNECTIONS."; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 } }
        } }
}
