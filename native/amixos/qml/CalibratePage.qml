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
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 22; radius: 4; color: on ? onColor : Theme.ov(0.08); border.color: Theme.lineSoft
        Text { id: bl; anchors.centerIn: parent; text: label; color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    component Head: Text { color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
    component Note: Text { Layout.fillWidth: true; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }
    component Val: Text { color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
    component Card: Rectangle { default property alias content: body.data; Layout.fillWidth: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; implicitHeight: body.implicitHeight + 24; ColumnLayout { id: body; anchors.fill: parent; anchors.margins: 12; spacing: 8 } }
    Flickable {
        anchors.fill: parent; anchors.margins: 10; contentHeight: grid.implicitHeight; clip: true; boundsBehavior: Flickable.StopAtBounds
        GridLayout {
            id: grid; width: parent.width; columns: 2; columnSpacing: 8; rowSpacing: 8
            Card {
                Head { text: "CLOCK & ENGINE" }
                GridLayout { columns: 4; columnSpacing: 14; rowSpacing: 4
                    Text { text: "ENGINE"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Val { text: engine.running ? "RUNNING · " + engine.backend : "STOPPED"; color: engine.running ? Theme.emerald : Theme.amber }
                    Text { text: "DEVICE"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Val { text: page.last && page.last.in ? page.last.in : (page.last && page.last.out ? page.last.out : "—"); elide: Text.ElideMiddle; Layout.maximumWidth: 220 }
                    Text { text: "SAMPLE RATE"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Val { text: engine.running ? engine.sampleRate + " Hz" : "—" }
                    Text { text: "BLOCK"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Val { text: engine.running ? engine.blockFrames + " frames = " + page.bufferMs.toFixed(2) + " ms" : "—" }
                    Text { text: "BUFFER LATENCY"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Val { text: engine.running ? "≈ " + (2 * page.bufferMs).toFixed(1) + " ms in + out (driver safety buffers add to it)" : "—" }
                    Text { text: "OVERRUNS"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Val { text: page.tel.xruns + " since start · " + page.tel.blocks + " blocks"; color: page.tel.xruns > 0 ? Theme.amber : Theme.cyan }
                }
                Note { text: "Smaller blocks give less latency and more overruns. 128 frames at 48 kHz is 2.7 ms per block; a USB interface usually runs clean at 128..256, a built-in sound card wants 512. Set it on CONNECTIONS." }
            }
            Card {
                Head { text: "ROUND-TRIP LATENCY" }
                RowLayout { spacing: 8
                    Text { text: "LISTEN ON CH"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                    SpinBox { from: 1; to: engine.stripState.length; value: page.strip + 1; onValueModified: page.strip = value - 1; implicitWidth: 90 }
                    Btn { label: page.measuring ? "MEASURING" : "MEASURE"; on: !page.measuring; onColor: Theme.cyan; onClicked: if (!page.measuring) page.measureRt() } }
                Text { Layout.fillWidth: true; text: page.rtText; color: page.rtColor; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; wrapMode: Text.WordWrap }
                Note { text: "Connect a cable from an output that carries MASTER (OUT 1 by default on ROUTING? no: MONITOR is on 1-2; set an output to MASTER L) to the device input this strip reads. The pulse is sent at -12 dBFS for 1.5 s and the delay to its echo is the whole path: output buffers, converters, cable, input buffers." }
            }
            Card {
                Head { text: "NOISE FLOOR" }
                RowLayout { spacing: 8
                    Text { text: "CH " + (page.strip + 1) + " (set above) and the master, 1 second"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                    Btn { label: "MEASURE"; on: true; onColor: Theme.cyan; onClicked: page.measureNoise() } }
                Text { Layout.fillWidth: true; text: page.noiseText; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; wrapMode: Text.WordWrap }
                Note { text: "Measure with the microphone connected, the room quiet and no music. A good interface with a dynamic mic gives -80 dBFS or lower; -60 and above means a noisy preamp, a hum loop or the gain too high." }
            }
            Card {
                Head { text: "LOUDNESS LOG" }
                RowLayout { spacing: 6
                    Btn { label: page.logging ? "STOP" : "START LOG"; on: page.logging; onColor: Theme.danger; onClicked: { if (!page.logging) { page.logRows = []; page.logStart = Date.now(); page.maxM = -120; page.maxTp = -120 } page.logging = !page.logging } }
                    Btn { label: "SAVE CSV"; on: page.logRows.length > 0; onColor: Theme.cyan; onClicked: if (page.logRows.length) settings.saveText("loudness-log-" + new Date().toISOString().replace(/[:.]/g, "-").slice(0, 19) + ".csv", page.csv()) }
                    Text { text: page.logRows.length + " s logged · max momentary " + (page.maxM > -119 ? page.maxM.toFixed(1) + " LUFS" : "—") + " · max true peak " + (page.maxTp > -119 ? page.maxTp.toFixed(1) + " dBTP" : "—"); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } }
                RowLayout { spacing: 14
                    Repeater { model: [{ t: "MOMENTARY", v: page.tel.momentary, u: " LUFS" }, { t: "SHORT TERM", v: page.tel.shortTerm, u: " LUFS" }, { t: "INTEGRATED", v: page.tel.integrated, u: " LUFS" }, { t: "LRA", v: page.tel.lra, u: " LU" }, { t: "TRUE PEAK", v: page.tel.truePeak, u: " dBTP" }]
                        delegate: ColumnLayout { spacing: 1; Text { text: modelData.t; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } Text { text: (modelData.v > -119 ? modelData.v.toFixed(1) : "—") + modelData.u; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true } } } }
                Note { text: "One row per second: seconds, momentary, short-term, integrated, LRA, true peak. The CSV goes to " + settings.dataFolder + " (open it from SETTINGS). " + settings.message }
            }
            Card {
                Layout.columnSpan: 2
                Head { text: "SESSION" }
                RowLayout { spacing: 6; Layout.fillWidth: true
                    Btn { label: "BACKUP CONSOLE"; on: true; onColor: Theme.cyan; onClicked: settings.exportConsole("") }
                    Btn { label: "RESTORE BACKUP"; onClicked: settings.importConsole("") }
                    Btn { label: "RESTORE AUTOSAVE"; on: settings.autosave.exists; onColor: Theme.amber; onClicked: settings.restoreAutosave() }
                    Btn { label: "OPEN DATA FOLDER"; onClicked: settings.openDataFolder() }
                    Item { Layout.fillWidth: true }
                    Text { text: settings.autosave.exists ? "autosave " + settings.autosave.at + " · " + settings.autosave.bytes + " bytes" : "no autosave yet (written every 60 s)"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } }
                Text { visible: settings.lastSessionCrashed; Layout.fillWidth: true; text: "The last session ended unexpectedly. RESTORE AUTOSAVE brings the console back as it was within the last minute of that session; the console as it is now is written to before-restore.json first."; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8; wrapMode: Text.WordWrap }
                Note { text: "BACKUP writes the whole console, the scenes and the presets to ~/amixos-console.json; RESTORE reads that file back. The licence is never in a backup." }
            }
        }
    }
}
