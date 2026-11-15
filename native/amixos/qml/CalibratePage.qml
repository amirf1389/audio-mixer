// CALIBRATE: clock & engine (device, rate, block size, buffer latency), ROUND-TRIP LATENCY (the polarity pulse on the master, a loopback
// cable from an output into a strip's input, the delay measured on the scope rings), NOISE FLOOR (RMS of a strip and of the master over
// one second with nothing playing), a LOUDNESS LOG (one row per second, CSV into the data folder) and SESSION (autosave every 60 s,
// restore, backup / restore of the whole console, crash notice).
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "calibratePage"
    property int strip: engine.selectedStrip
    property string rtText: "not measured"
    property color rtColor: Theme.dim
    property bool measuring: false
    property string noiseText: "not measured"
    property var logRows: []
    property bool logging: false
    property real logStart: 0
    property real maxM: -120
    property real maxTp: -120
    readonly property var tel: engine.telemetry
    readonly property var last: settings.lastDevice
    readonly property real bufferMs: engine.running && engine.sampleRate > 0 ? engine.blockFrames / engine.sampleRate * 1000 : 0
    function measureRt() {
        if (!engine.running) { rtText = "open a device first"; rtColor = Theme.amber; return }
        engine.setScopeStrip(strip); engine.testType = 3; engine.testDb = -12; engine.testOn = true; measuring = true; rtText = "MEASURING... (pulse on the master, listening on CH " + (strip + 1) + ")"; rtColor = Theme.cyan; rtTimer.start()
    }
    Timer { id: rtTimer; interval: 1500; onTriggered: { const r = engine.roundTrip(); engine.testOn = false; page.measuring = false
            if (r.ok) { page.rtText = r.ms.toFixed(2) + " ms round trip  (" + Math.round(r.ms * r.rate / 1000) + " samples at " + r.rate + " Hz, block " + r.frames + " = " + page.bufferMs.toFixed(2) + " ms; echo " + r.returnDb.toFixed(1) + " dBFS)"; page.rtColor = Theme.emerald }
            else { page.rtText = "NO LOOPBACK: no echo of the pulse on CH " + (page.strip + 1) + ". Connect an output that carries MASTER to the input this strip reads (ROUTING) and try again."; page.rtColor = Theme.amber } } }
    function measureNoise() {
        if (!engine.running) { noiseText = "open a device first"; return }
        engine.setScopeStrip(strip); noiseTimer.start(); noiseText = "MEASURING 1 s..."
    }
    Timer { id: noiseTimer; interval: 1200; onTriggered: { const s = engine.scopeFrame(2, 1000, 2, false), m = engine.scopeFrame(0, 1000, 2, false)
            const db = v => v > 1e-7 ? (20 * Math.log(v) / Math.LN10).toFixed(1) + " dBFS" : "below -140 dBFS"
            page.noiseText = "CH " + (page.strip + 1) + " input: " + db(s.vrms) + " RMS (peak " + db(s.vpp / 2) + ")  ·  master: " + db(m.vrms) + " RMS" } }
    Timer { interval: 1000; running: page.logging; repeat: true; onTriggered: { const t = engine.telemetry; const row = [((Date.now() - page.logStart) / 1000).toFixed(0), t.momentary, t.shortTerm, t.integrated, t.lra, t.truePeak]; let r = page.logRows.slice(); r.push(row); if (r.length > 21600) r.shift(); page.logRows = r; if (t.momentary > page.maxM) page.maxM = t.momentary; if (t.truePeak > page.maxTp) page.maxTp = t.truePeak } }
    function csv() { let s = "seconds,momentary_LUFS,short_term_LUFS,integrated_LUFS,LRA_LU,true_peak_dBTP\n"; for (let i = 0; i < logRows.length; ++i) { const r = logRows[i]; s += r[0] + "," + r[1].toFixed(2) + "," + r[2].toFixed(2) + "," + r[3].toFixed(2) + "," + r[4].toFixed(2) + "," + r[5].toFixed(2) + "\n" } return s }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 22; radius: 8; gradient: Gradient { GradientStop { position: 0; color: on ? Theme.btnTop(onColor) : Theme.ov(0.12) } GradientStop { position: 1; color: on ? Theme.btnBottom(onColor) : Theme.ov(0.035) } } color: on ? onColor : Theme.ov(0.08); border.color: Theme.lineSoft
        Rectangle { anchors.fill: parent; anchors.margins: 1; radius: Math.max(0, parent.radius - 1); color: "transparent"; border.width: 1; border.color: Qt.rgba(1, 1, 1, parent.on ? 0.30 : 0.07) }
        Text { id: bl; anchors.centerIn: parent; text: i18n.t(label, i18n.lang); color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    component Head: Text { color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
    component Note: Text { Layout.fillWidth: true; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }
    component Val: Text { color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
    component Card: Rectangle { default property alias content: body.data; Layout.fillWidth: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; implicitHeight: body.implicitHeight + 24; ColumnLayout { id: body; anchors.fill: parent; anchors.margins: 12; spacing: 8 } }
    Flickable {
        anchors.fill: parent; anchors.margins: 10; contentHeight: grid.implicitHeight; clip: true; boundsBehavior: Flickable.StopAtBounds
        GridLayout {
            id: grid; width: parent.width; columns: 2; columnSpacing: 8; rowSpacing: 8
            Card {
                Head { text: i18n.t("CLOCK & ENGINE", i18n.lang) }
                GridLayout { columns: 4; columnSpacing: 14; rowSpacing: 4
                    Text { text: i18n.t("ENGINE", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Val { text: engine.running ? i18n.t("RUNNING · ", i18n.lang) + engine.backend : i18n.t("STOPPED", i18n.lang); color: engine.running ? Theme.emerald : Theme.amber }
                    Text { text: i18n.t("DEVICE", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Val { text: page.last && page.last.in ? page.last.in : (page.last && page.last.out ? page.last.out : "—"); elide: Text.ElideMiddle; Layout.maximumWidth: 220 }
                    Text { text: i18n.t("SAMPLE RATE", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Val { text: engine.running ? engine.sampleRate + i18n.t(" Hz", i18n.lang) : "—" }
                    Text { text: i18n.t("BLOCK", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Val { text: engine.running ? engine.blockFrames + i18n.t(" frames = ", i18n.lang) + page.bufferMs.toFixed(2) + i18n.t(" ms", i18n.lang) : "—" }
                    Text { text: i18n.t("BUFFER LATENCY", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Val { text: engine.running ? "≈ " + (2 * page.bufferMs).toFixed(1) + i18n.t(" ms in + out (driver safety buffers add to it)", i18n.lang) : "—" }
                    Text { text: i18n.t("OVERRUNS", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Val { text: page.tel.xruns + i18n.t(" since start · ", i18n.lang) + page.tel.blocks + i18n.t(" blocks", i18n.lang); color: page.tel.xruns > 0 ? Theme.amber : Theme.cyan }
                }
                Note { text: i18n.t("Smaller blocks give less latency and more overruns. 128 frames at 48 kHz is 2.7 ms per block; a USB interface usually runs clean at 128..256, a built-in sound card wants 512. Set it on CONNECTIONS.", i18n.lang) }
            }
            Card {
                Head { text: i18n.t("ROUND-TRIP LATENCY", i18n.lang) }
                RowLayout { spacing: 8
                    Text { text: i18n.t("LISTEN ON CH", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                    SpinBox { palette.base: Theme.deep; palette.text: Theme.ink; palette.button: Theme.cardRaised; palette.buttonText: Theme.ink; palette.highlight: Theme.orange; palette.mid: Theme.line; from: 1; to: engine.stripState.length; value: page.strip + 1; onValueModified: page.strip = value - 1; implicitWidth: 90 }
                    Btn { label: page.measuring ? i18n.t("MEASURING", i18n.lang) : i18n.t("MEASURE", i18n.lang); on: !page.measuring; onColor: Theme.cyan; onClicked: if (!page.measuring) page.measureRt() } }
                Text { Layout.fillWidth: true; text: page.rtText; color: page.rtColor; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; wrapMode: Text.WordWrap }
                Note { text: i18n.t("Connect a cable from an output that carries MASTER (OUT 1 by default on ROUTING? no: MONITOR is on 1-2; set an output to MASTER L) to the device input this strip reads. The pulse is sent at -12 dBFS for 1.5 s and the delay to its echo is the whole path: output buffers, converters, cable, input buffers.", i18n.lang) }
            }
            Card {
                Head { text: i18n.t("NOISE FLOOR", i18n.lang) }
                RowLayout { spacing: 8
                    Text { text: i18n.t("CH ", i18n.lang) + (page.strip + 1) + i18n.t(" (set above) and the master, 1 second", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                    Btn { label: i18n.t("MEASURE", i18n.lang); on: true; onColor: Theme.cyan; onClicked: page.measureNoise() } }
                Text { Layout.fillWidth: true; text: page.noiseText; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; wrapMode: Text.WordWrap }
                Note { text: i18n.t("Measure with the microphone connected, the room quiet and no music. A good interface with a dynamic mic gives -80 dBFS or lower; -60 and above means a noisy preamp, a hum loop or the gain too high.", i18n.lang) }
            }
            Card {
                Head { text: i18n.t("LOUDNESS LOG", i18n.lang) }
                RowLayout { spacing: 6
                    Btn { label: page.logging ? i18n.t("STOP", i18n.lang) : i18n.t("START LOG", i18n.lang); on: page.logging; onColor: Theme.danger; onClicked: { if (!page.logging) { page.logRows = []; page.logStart = Date.now(); page.maxM = -120; page.maxTp = -120 } page.logging = !page.logging } }
                    Btn { label: i18n.t("SAVE CSV", i18n.lang); on: page.logRows.length > 0; onColor: Theme.cyan; onClicked: if (page.logRows.length) settings.saveText("loudness-log-" + new Date().toISOString().replace(/[:.]/g, "-").slice(0, 19) + ".csv", page.csv()) }
                    Text { text: page.logRows.length + i18n.t(" s logged · max momentary ", i18n.lang) + (page.maxM > -119 ? page.maxM.toFixed(1) + i18n.t(" LUFS", i18n.lang) : "—") + i18n.t(" · max true peak ", i18n.lang) + (page.maxTp > -119 ? page.maxTp.toFixed(1) + i18n.t(" dBTP", i18n.lang) : "—"); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } }
                RowLayout { spacing: 14
                    Repeater { model: [{ t: "MOMENTARY", v: page.tel.momentary, u: " LUFS" }, { t: "SHORT TERM", v: page.tel.shortTerm, u: " LUFS" }, { t: "INTEGRATED", v: page.tel.integrated, u: " LUFS" }, { t: "LRA", v: page.tel.lra, u: " LU" }, { t: "TRUE PEAK", v: page.tel.truePeak, u: " dBTP" }]
                        delegate: ColumnLayout { spacing: 1; Text { text: i18n.t(modelData.t ?? "", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } Text { text: (modelData.v > -119 ? modelData.v.toFixed(1) : "—") + modelData.u; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true } } } }
                Note { text: i18n.t("One row per second: seconds, momentary, short-term, integrated, LRA, true peak. The CSV goes to ", i18n.lang) + settings.dataFolder + i18n.t(" (open it from SETTINGS). ", i18n.lang) + settings.message }
            }
            Card {
                Layout.columnSpan: 2
                Head { text: i18n.t("SESSION", i18n.lang) }
                RowLayout { spacing: 6; Layout.fillWidth: true
                    Btn { label: i18n.t("BACKUP CONSOLE", i18n.lang); on: true; onColor: Theme.cyan; onClicked: settings.exportConsole("") }
                    Btn { label: i18n.t("RESTORE BACKUP", i18n.lang); onClicked: settings.importConsole("") }
                    Btn { label: i18n.t("RESTORE AUTOSAVE", i18n.lang); on: settings.autosave.exists; onColor: Theme.amber; onClicked: settings.restoreAutosave() }
                    Btn { label: i18n.t("OPEN DATA FOLDER", i18n.lang); onClicked: settings.openDataFolder() }
                    Item { Layout.fillWidth: true }
                    Text { text: settings.autosave.exists ? i18n.t("autosave ", i18n.lang) + settings.autosave.at + " · " + settings.autosave.bytes + i18n.t(" bytes", i18n.lang) : i18n.t("no autosave yet (written every 60 s)", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } }
                Text { visible: settings.lastSessionCrashed; Layout.fillWidth: true; text: i18n.t("The last session ended unexpectedly. RESTORE AUTOSAVE brings the console back as it was within the last minute of that session; the console as it is now is written to before-restore.json first.", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8; wrapMode: Text.WordWrap }
                Note { text: i18n.t("BACKUP writes the whole console, the scenes and the presets to ~/amixos-console.json; RESTORE reads that file back. The licence is never in a backup.", i18n.lang) }
            }
        }
    }
}
