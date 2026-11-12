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
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(50, bl.implicitWidth + 16); implicitHeight: 24; radius: 4; color: on ? onColor : Theme.ov(0.08); border.color: Theme.lineSoft
        Text { id: bl; anchors.centerIn: parent; text: label; color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    component Card: Rectangle { default property alias content: body.data; Layout.fillWidth: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; implicitHeight: body.implicitHeight + 24; ColumnLayout { id: body; anchors.fill: parent; anchors.margins: 12; spacing: 8 } }
    component KV: RowLayout { property string k; property string v; property color c: Theme.cyan; spacing: 10; Text { Layout.preferredWidth: 120; text: k; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Text { text: v; color: c; font.family: Theme.mono; font.pixelSize: 10; font.bold: true } }
    Flickable { anchors.fill: parent; anchors.margins: 10; contentHeight: grid.implicitHeight; clip: true; boundsBehavior: Flickable.StopAtBounds
        GridLayout { id: grid; width: parent.width; columns: 2; columnSpacing: 8; rowSpacing: 8
            Card { Head { text: i18n.t("DEVICE", i18n.lang) }
                KV { k: "STATE"; v: engine.running ? "RUNNING on " + engine.backend.toUpperCase() : "STOPPED"; c: engine.running ? Theme.emerald : Theme.amber }
                KV { k: "INPUT"; v: page.last && page.last.in ? page.last.in : "—" }
                KV { k: "OUTPUT"; v: page.last && page.last.out ? page.last.out : "—" }
                KV { k: "CHANNELS"; v: engine.running ? engine.deviceInputs + " in / " + engine.deviceOutputs + " out" : "—" }
                KV { k: "LAST START"; v: engine.lastRoute ? engine.lastRoute : "—"; c: Theme.muted }
                RowLayout { spacing: 6
                    Btn { label: engine.running ? i18n.t("STOP", i18n.lang) : i18n.t("START", i18n.lang); on: true; onColor: engine.running ? Theme.danger : Theme.emerald; onClicked: engine.running ? engine.stop() : settings.startLastDevice() } }
                Rectangle {   // INPUT / OUTPUT CHECK: every channel of the running interface or mixer, live, with the real-time verdict
                    id: ioCard; objectName: "ioCheckCard"; Layout.fillWidth: true; implicitHeight: ioCol.implicitHeight + 16; radius: 6; color: Theme.ov(0.05); border.color: io.realtime ? Theme.emerald : Theme.amber; visible: engine.running
                    property int tick: 0
                    readonly property var io: { tick; engine.running; return engine.ioCheck() }
                    Timer { interval: 250; running: ioCard.visible && page.visible; repeat: true; onTriggered: ioCard.tick++ }
                    ColumnLayout { id: ioCol; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8; spacing: 5
                        RowLayout { spacing: 8
                            Text { text: i18n.t("INPUT / OUTPUT CHECK", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; font.letterSpacing: 1.5 }
                            Text { text: (ioCard.io.backend || "").toUpperCase() + " · " + (ioCard.io.device || ""); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                            Item { Layout.fillWidth: true }
                            Text { text: i18n.t("dropouts ", i18n.lang) + (ioCard.io.xruns || 0) + "  ·  " + i18n.t("load ", i18n.lang) + Number(ioCard.io.loadPct || 0).toFixed(0) + " %  ·  " + i18n.t("block ", i18n.lang) + Number(ioCard.io.blockMs || 0).toFixed(1) + " ms"; color: ioCard.io.realtime ? Theme.emerald : Theme.amber; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } }
                        RowLayout { spacing: 6; Text { Layout.preferredWidth: 26; text: i18n.t("IN", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                            Flow { Layout.fillWidth: true; spacing: 3
                                Repeater { model: ioCard.io.inputs || []; delegate: Rectangle { width: 46; height: 26; radius: 3; color: modelData.live ? Qt.rgba(0.063, 0.725, 0.506, 0.25) : Theme.ov(0.06); border.color: modelData.strip === modelData.ch ? Theme.emerald : modelData.strip > 0 ? Theme.amber : Theme.lineSoft
                                    Text { anchors.horizontalCenter: parent.horizontalCenter; y: 2; text: modelData.ch + (modelData.strip > 0 ? "→" + modelData.strip : ""); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                                    Text { anchors.horizontalCenter: parent.horizontalCenter; y: 14; text: modelData.live ? Number(modelData.db).toFixed(0) + " dB" : "—"; color: modelData.live ? Theme.emerald : Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } } } } }
                        RowLayout { spacing: 6; Text { Layout.preferredWidth: 26; text: i18n.t("OUT", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                            Flow { Layout.fillWidth: true; spacing: 3
                                Repeater { model: ioCard.io.outputs || []; delegate: Rectangle { width: 86; height: 26; radius: 3; color: modelData.live ? Qt.rgba(0.063, 0.725, 0.506, 0.25) : Theme.ov(0.06); border.color: Theme.lineSoft
                                    Text { anchors.horizontalCenter: parent.horizontalCenter; y: 2; text: modelData.ch + " · " + modelData.source; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                                    Text { anchors.horizontalCenter: parent.horizontalCenter; y: 14; text: modelData.live ? Number(modelData.db).toFixed(0) + " dB" : "—"; color: modelData.live ? Theme.emerald : Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } } } } }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: i18n.t(ioCard.io.verdict || "", i18n.lang); color: ioCard.io.realtime ? Theme.ink : Theme.amber; font.family: Theme.mono; font.pixelSize: 8 } } }
                Repeater {   // A MIXER OF ANOTHER MAKER on its ASIO driver: open every channel and put desk channel n on strip n
                    model: { engine.devices; engine.running; return engine.mixersFound() }
                    delegate: Rectangle { objectName: "mixerCard"; Layout.fillWidth: true; implicitHeight: mxCol.implicitHeight + 16; radius: 6; color: Theme.ov(0.05); border.color: modelData.running ? Theme.emerald : Theme.cyan
                        ColumnLayout { id: mxCol; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8; spacing: 4
                            RowLayout { spacing: 8
                                Text { text: i18n.t("MIXER FOUND", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; font.letterSpacing: 1.5 }
                                Text { text: modelData.maker + " " + modelData.model; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true }
                                Text { text: modelData.inChannels + i18n.t(" in / ", i18n.lang) + modelData.outChannels + i18n.t(" out · ASIO: ", i18n.lang) + modelData.name; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                                Item { Layout.fillWidth: true }
                                Btn { label: modelData.running ? i18n.t("ASSIGN CHANNELS AGAIN", i18n.lang) : i18n.t("CONNECT + ASSIGN CHANNELS", i18n.lang); on: true; onColor: modelData.running ? Theme.emerald : Theme.cyan
                                    onClicked: { if (modelData.running) engine.assignMixerChannels(modelData.prefix, true); else engine.connectMixer(modelData.id, settings.defaultFrames) } } }
                            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7
                                text: i18n.t("Opens every channel of the desk through its own ASIO driver (32 at most): desk channel 1 on strip 1, 2 on 2 and so on, the master on the desk's USB returns 1-2. ", i18n.lang) + i18n.t(modelData.note, i18n.lang) } } } }
                Rectangle { objectName: "asioDriversCard"; Layout.fillWidth: true; implicitHeight: asioCol.implicitHeight + 16; radius: 6; color: Theme.ov(0.03); border.color: Theme.amber; border.width: 1
                    ColumnLayout { id: asioCol; anchors.fill: parent; anchors.margins: 8; spacing: 4
                        RowLayout { spacing: 6
                            Text { text: i18n.t("ASIO DRIVERS", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
                            Text { Layout.fillWidth: true; text: i18n.t("Manual only: nothing is picked or opened by itself. OPEN starts a driver, the same button turns it off.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; elide: Text.ElideRight }
                            Btn { objectName: "audioSettingsBtn"; label: i18n.t("AUDIO / MIDI SETTINGS", i18n.lang); on: true; onColor: Theme.amber; onClicked: page.Window.window.openAudioSettings() }
                            Btn { objectName: "asioScanBtn"; label: i18n.t("SCAN (DISCOVERY ONLY)", i18n.lang); onClicked: engine.refreshDevices() } }
                        Repeater { model: { engine.devices; engine.running; return engine.asioDrivers() }
                            delegate: RowLayout { Layout.fillWidth: true; spacing: 6
                                Rectangle { width: 6; height: 6; radius: 3; color: modelData.current ? Theme.emerald : Theme.ov(0.2) }
                                Text { Layout.fillWidth: true; text: modelData.name; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; elide: Text.ElideRight }
                                Text { text: modelData.inChannels + modelData.outChannels === 0 ? i18n.t("in use elsewhere: channels are read when you press OPEN", i18n.lang) : modelData.inChannels + i18n.t(" in / ", i18n.lang) + modelData.outChannels + i18n.t(" out", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                                Btn { objectName: "asioOpen_" + index; label: modelData.current ? i18n.t("ON · TURN OFF", i18n.lang) : i18n.t("OPEN", i18n.lang); on: true; onColor: modelData.current ? Theme.emerald : Theme.cyan; onClicked: { if (modelData.current) engine.stop(); else engine.openAsioDriver(modelData.id, settings.defaultFrames) } } } }
                        Repeater { model: { engine.devices; engine.running; return engine.dacsFound() }
                            delegate: RowLayout { Layout.fillWidth: true; spacing: 6
                                Rectangle { width: 6; height: 6; radius: 3; color: modelData.current ? Theme.emerald : Theme.ov(0.2) }
                                Text { text: i18n.t("DAC", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                                Text { Layout.fillWidth: true; text: modelData.name; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; elide: Text.ElideRight }
                                Text { text: modelData.backend.toUpperCase() + " · " + modelData.outChannels + i18n.t(" out", i18n.lang) + " · " + (modelData.maxRate / 1000) + " kHz"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                                Btn { objectName: "dacOpen_" + index; label: modelData.current ? i18n.t("ON · TURN OFF", i18n.lang) : i18n.t("OPEN", i18n.lang); on: true; onColor: modelData.current ? Theme.emerald : Theme.cyan; onClicked: { if (modelData.current) engine.stop(); else engine.openDac(modelData.backend, modelData.id, settings.defaultFrames) } } } }
                        Text { visible: engine.asioDrivers().length === 0 && (engine.devices, true); text: i18n.t("No ASIO driver found on this computer. Install the interface's own ASIO driver, then press RESCAN.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 } } }
                RowLayout { spacing: 6
                    Text { text: i18n.t("ASIO ROUTING", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                    Btn { objectName: "openRoutingBtn"; label: i18n.t("OPEN", i18n.lang); on: engine.openRouting; onColor: Theme.emerald; onClicked: engine.openRouting = true }
                    Btn { label: i18n.t("GUARDED", i18n.lang); on: !engine.openRouting; onColor: Theme.amber; onClicked: engine.openRouting = false }
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: engine.openRouting ? i18n.t("The ASIO driver you pick is opened first: ASIO4ALL, FlexASIO or the manufacturer's own (Realtek included). The other paths stay as the fallback.", i18n.lang) : i18n.t("ASIO4ALL is passed over when a manufacturer driver is present, and onboard Realtek goes to its Windows endpoint.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
                Note { text: i18n.t("Pick the interface itself on CONNECTIONS or LIVE SOURCES. AUTO ASIO opens the best driver (native ASIO before wrappers, Realtek pinned to WASAPI) when nothing is remembered; SMART START walks the driver ladder when the first choice fails. The ASIO control panel of a driver opens from the driver's own tray tool; the engine does not pop it up.", i18n.lang) } }
            Card { Head { text: i18n.t("CLOCK & BLOCK", i18n.lang) }
                KV { k: "SAMPLE RATE"; v: engine.running ? engine.sampleRate + " Hz" : "—" }
                KV { k: "BLOCK"; v: engine.running ? engine.blockFrames + " frames = " + (engine.blockFrames / engine.sampleRate * 1000).toFixed(2) + " ms" : "—" }
                RowLayout { spacing: 4; Text { text: i18n.t("RATE", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; Layout.preferredWidth: 40 }
                    Repeater { model: [44100, 48000, 88200, 96000, 192000]; delegate: Btn { label: modelData / 1000 + "k"; on: engine.running && engine.sampleRate === modelData; onClicked: page.reopen(modelData, engine.blockFrames || settings.defaultFrames) } } }
                RowLayout { spacing: 4; Text { text: i18n.t("BLOCK", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; Layout.preferredWidth: 40 }
                    Repeater { model: [32, 64, 128, 256, 512, 1024]; delegate: Btn { label: modelData; on: engine.running && engine.blockFrames === modelData; onClicked: page.reopen(engine.sampleRate || settings.defaultRate, modelData) } } }
                Note { text: i18n.t("A click re-opens the remembered device at that rate or block (SMART START). Lower blocks mean less latency and more risk of overruns; watch the profiler below. 96 / 192 kHz run natively in double precision.", i18n.lang) } }
            Card { RowLayout { Head { text: i18n.t("DSP PROFILER", i18n.lang) } Item { Layout.fillWidth: true } Btn { label: i18n.t("RESET", i18n.lang); onClicked: engine.resetProfiler() } }
                KV { k: "LOAD"; v: engine.running ? page.tel.loadPct.toFixed(1) + " % of the block" : "—"; c: page.tel.loadPct > 70 ? Theme.danger : page.tel.loadPct > 40 ? Theme.amber : Theme.emerald }
                Rectangle { Layout.fillWidth: true; implicitHeight: 10; radius: 3; color: Theme.ov(0.06); Rectangle { width: parent.width * Math.min(1, page.tel.loadPct / 100); height: parent.height; radius: 3; color: page.tel.loadPct > 70 ? Theme.danger : page.tel.loadPct > 40 ? Theme.amber : Theme.emerald } }
                KV { k: "BLOCK TIME"; v: page.tel.blockUs.toFixed(1) + " µs  (worst " + page.tel.maxBlockUs.toFixed(1) + " µs)" }
                KV { k: "JITTER"; v: page.tel.jitterUs.toFixed(1) + " µs between blocks" }
                KV { k: "OVERRUNS"; v: page.tel.xruns + " since start · " + page.tel.blocks + " blocks"; c: page.tel.xruns > 0 ? Theme.amber : Theme.cyan }
                Note { text: i18n.t("Load is the DSP time of the last block divided by the block's duration: above 70 % the block may not finish in time. Worst case and jitter reset with RESET. Overruns come from the driver (ASIO / WASAPI / ALSA report them).", i18n.lang) } }
            Card { Head { text: i18n.t("OUTPUT STAGE", i18n.lang) }
                RowLayout { spacing: 6; Text { text: i18n.t("DC REMOVAL", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; Layout.preferredWidth: 120 } Btn { label: engine.dcBlock ? i18n.t("ON", i18n.lang) : i18n.t("OFF", i18n.lang); on: engine.dcBlock; onColor: Theme.emerald; onClicked: engine.dcBlock = !engine.dcBlock } }
                RowLayout { spacing: 6; Text { text: i18n.t("DSP MODE", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; Layout.preferredWidth: 120 }
                    Btn { objectName: "dspDigital"; label: i18n.t("DIGITAL", i18n.lang); on: engine.dspMode === "digital"; onColor: Theme.cyan; onClicked: engine.dspMode = "digital" }
                    Btn { objectName: "dspAnalog"; label: i18n.t("ANALOG", i18n.lang); on: engine.dspMode === "analog"; onColor: Theme.amber; onClicked: engine.dspMode = "analog" } }
                Note { text: i18n.t("DIGITAL leaves the mix exactly as computed. ANALOG rounds the peaks off softly on the master, the way an analogue output stage does; quiet passages are untouched.", i18n.lang) }
                RowLayout { spacing: 6; Text { text: i18n.t("DITHER", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; Layout.preferredWidth: 120 }
                    Repeater { model: [{ t: "FLOAT (none)", v: 0 }, { t: "16-BIT", v: 16 }, { t: "24-BIT", v: 24 }]; delegate: Btn { label: modelData.t; on: engine.outputBits === modelData.v; onColor: Theme.cyan; onClicked: engine.outputBits = modelData.v } } }
                Note { text: i18n.t("DC removal is a 5 Hz high-pass on the master before the auto level and the limiter: an offset from a player or a plug-in never eats headroom. Dither (TPDF) is the last thing before the converter: pick the depth your device takes in integer mode (WASAPI shared and most ASIO drivers take float: leave FLOAT). Denormals are flushed in the DSP already.", i18n.lang) } }
            Card { Layout.columnSpan: 2; Head { text: i18n.t("DRIVER LADDER (what the next start tries, in order)", i18n.lang) }
                Repeater { model: page.plan; delegate: RowLayout { spacing: 8
                    Text { text: (index + 1) + "."; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                    Text { text: modelData.mode + "  " + modelData.backend.toUpperCase() + (modelData.exclusive ? i18n.t(" exclusive", i18n.lang) : ""); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; Layout.preferredWidth: 220 }
                    Text { text: modelData.why + (modelData.fromLock ? i18n.t("  [learned]", i18n.lang) : "") + (modelData.pinned ? i18n.t("  [pinned]", i18n.lang) : ""); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; Layout.fillWidth: true; elide: Text.ElideRight } } }
                Text { visible: page.plan.length === 0; text: i18n.t("No device remembered yet: start one on CONNECTIONS.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 } }
        } }
}
