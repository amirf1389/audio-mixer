// ACOUSTICS (extension): room and loudspeaker measurement. MEASURE (log sweep / stepped sine / an imported sweep WAV: amplitude, phase, THD,
// THD+N), RTA (to 1/48 octave, RMS averaging, level alignment), IMPULSE (ETC, Schroeder, RT60, delay, waterfall, spectrogram), ROOM (modes
// and predicted bass from the dimensions and the speaker / listener places), AUTO EQ (parametric filters toward a target, exported or set on a strip).
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "acousticsPage"
    property int tab: 0
    property int strip: 0
    property int ppo: 48
    property real seconds: 5
    property real f1: 20
    property real f2: 20000
    property real level: -12
    property var rtaData: ({})
    property int rtaBpo: 48
    property real rtaAvg: 0.8
    property var water: ({})
    property var spec: ({})
    property var roomData: ({})
    property var rm: [5, 4, 2.7, 0.5, 1, 1, 1.2, 2.5, 3, 1.2]
    property string eqTarget: "flat"
    property real eqLo: 20
    property real eqHi: 400
    readonly property var tabs: ["MEASURE", "RTA", "IMPULSE", "ROOM", "AUTO EQ"]
    function t(s) { return i18n.t(s, i18n.lang) }
    function fx(f, lo, hi, w) { return Math.log(f / lo) / Math.log(hi / lo) * w }
    function axes(ctx, w, h, lo, hi, dLo, dHi) {
        ctx.clearRect(0, 0, w, h); ctx.lineWidth = 1; ctx.font = "9px monospace"; ctx.strokeStyle = Theme.ov(0.10); ctx.fillStyle = Theme.dim
        const fs = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]
        for (let i = 0; i < fs.length; i++) { if (fs[i] < lo || fs[i] > hi) continue; const x = fx(fs[i], lo, hi, w); ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke(); ctx.fillText(fs[i] >= 1000 ? fs[i] / 1000 + "k" : fs[i], x + 2, h - 3) }
        const step = (dHi - dLo) > 60 ? 20 : (dHi - dLo) > 24 ? 10 : 5
        for (let d = Math.ceil(dLo / step) * step; d <= dHi; d += step) { const y = h - (d - dLo) / (dHi - dLo) * h; ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke(); ctx.fillText(d, 2, y - 2) }
    }
    function curve(ctx, w, h, f, v, lo, hi, dLo, dHi, color, width) {
        if (!f || !v || f.length < 2 || v.length < f.length) return
        ctx.strokeStyle = color; ctx.lineWidth = width || 1.5; ctx.beginPath(); let first = true
        for (let i = 0; i < f.length; i++) { if (f[i] < lo || f[i] > hi) continue; const x = fx(f[i], lo, hi, w), y = h - (Math.max(dLo, Math.min(dHi, v[i])) - dLo) / (dHi - dLo) * h; if (first) { ctx.moveTo(x, y); first = false } else ctx.lineTo(x, y) }
        ctx.stroke()
    }
    function range(v, pad) { let hi = -1e9; if (v) for (let i = 0; i < v.length; i++) if (v[i] > hi) hi = v[i]; if (hi < -900) hi = 0; hi = Math.ceil((hi + pad) / 10) * 10; return [hi - 70, hi] }
    function heat(ctx, w, h, g, span) {
        ctx.clearRect(0, 0, w, h); if (!g || !g.rows || !g.rows.length) return
        let top = -1e9; for (let r = 0; r < g.rows.length; r++) for (let c = 0; c < g.rows[r].length; c++) if (g.rows[r][c] > top) top = g.rows[r][c]
        const rh = h / g.rows.length, n = g.freq.length, lo = g.freq[0], hi = g.freq[n - 1]
        for (let r = 0; r < g.rows.length; r++) for (let c = 0; c < n; c++) { const k = Math.max(0, Math.min(1, (g.rows[r][c] - (top - span)) / span)); const x0 = fx(g.freq[c], lo, hi, w), x1 = c + 1 < n ? fx(g.freq[c + 1], lo, hi, w) : w
            ctx.fillStyle = Qt.hsla(0.66 - 0.66 * k, 0.9, 0.12 + 0.45 * k, 1); ctx.fillRect(x0, r * rh, x1 - x0 + 1, rh + 1) }
    }
    Timer { interval: 150; running: page.visible && page.tab === 1 && engine.running; repeat: true; onTriggered: { page.rtaData = measure.rta(page.strip, page.rtaBpo, page.rtaAvg); rtaPlot.requestPaint() } }
    Connections { target: measure; function onChanged() { respPlot.requestPaint(); etcPlot.requestPaint(); eqPlot.requestPaint(); if (measure.state === "done") { page.water = measure.waterfall(24); page.spec = measure.spectrogram(); waterPlot.requestPaint(); specPlot.requestPaint() } } }
    Component.onCompleted: { roomData = measure.room(rm[0], rm[1], rm[2], rm[3], rm[4], rm[5], rm[6], rm[7], rm[8], rm[9]) }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 22; radius: 4; color: on ? onColor : Theme.ov(0.08); border.color: Theme.lineSoft
        Text { id: bl; anchors.centerIn: parent; text: label; color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    component Lab: Text { color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
    component Val: Text { color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
    component Note: Text { Layout.fillWidth: true; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }
    component Num: TextField { implicitWidth: 64; implicitHeight: 22; font.family: Theme.mono; font.pixelSize: 9; color: Theme.ink; selectByMouse: true; background: Rectangle { radius: 4; color: Theme.ov(0.06); border.color: Theme.lineSoft } }
    component Plot: Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.chrome; border.color: Theme.line; clip: true }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle { Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Text { text: page.t("ACOUSTICS:"); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Repeater { model: page.tabs; delegate: Btn { label: page.t(modelData); on: page.tab === index; onClicked: page.tab = index } }
                Item { Layout.fillWidth: true }
                Lab { text: page.t("MIC ON CH") } Num { text: page.strip + 1; implicitWidth: 40; onEditingFinished: page.strip = Math.max(0, Math.min(31, parseInt(text) - 1 || 0)) }
                Btn { label: measure.calibrated ? page.t("MIC CAL: ON") : page.t("MIC CAL: NONE"); on: measure.calibrated; onColor: Theme.emerald; onClicked: if (measure.calibrated) measure.clearCalibration(); else calRow.visible = !calRow.visible }
                Lab { text: page.t("SPL OFFSET") } Num { text: measure.splOffset.toFixed(1); onEditingFinished: measure.splOffset = parseFloat(text) || 0 } } }
        RowLayout { id: calRow; visible: false; Layout.fillWidth: true; spacing: 6
            Num { id: calPath; Layout.fillWidth: true; placeholderText: page.t("path of the microphone calibration file (.txt / .cal: frequency dB per line)") }
            Btn { label: page.t("LOAD"); on: calPath.text.length > 2; onClicked: if (measure.loadCalibration(calPath.text)) calRow.visible = false } }
        Text { Layout.fillWidth: true; text: measure.message; visible: measure.message.length > 0; color: measure.state === "measuring" ? Theme.amber : Theme.emerald; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; elide: Text.ElideRight }
        StackLayout { Layout.fillWidth: true; Layout.fillHeight: true; currentIndex: page.tab
            // 0 MEASURE
            ColumnLayout { spacing: 8
                RowLayout { spacing: 6
                    Lab { text: page.t("FROM") } Num { text: page.f1; onEditingFinished: page.f1 = parseFloat(text) || 20 } Lab { text: page.t("TO (Hz)") } Num { text: page.f2; onEditingFinished: page.f2 = parseFloat(text) || 20000 }
                    Lab { text: page.t("SECONDS") } Num { text: page.seconds; implicitWidth: 44; onEditingFinished: page.seconds = parseFloat(text) || 5 }
                    Lab { text: page.t("LEVEL dBFS") } Num { text: page.level; implicitWidth: 44; onEditingFinished: page.level = parseFloat(text) || -12 }
                    Lab { text: page.t("POINTS / OCTAVE") } Repeater { model: [12, 24, 48, 96]; delegate: Btn { label: modelData + ""; on: page.ppo === modelData; onClicked: page.ppo = modelData } }
                    Item { Layout.fillWidth: true } }
                RowLayout { spacing: 6
                    Btn { label: page.t("▶ LOG SWEEP"); on: measure.state !== "measuring"; onColor: Theme.cyan; onClicked: measure.runSweep(page.strip, page.seconds, page.f1, page.f2, page.level, page.ppo) }
                    Btn { label: page.t("▶ STEPPED SINE"); on: measure.state !== "measuring"; onColor: Theme.cyan; onClicked: measure.runStepped(page.strip, Math.min(page.ppo, 96), page.f1, page.f2, page.level) }
                    Btn { visible: measure.state === "measuring"; label: page.t("CANCEL") + "  " + Math.round(measure.progress * 100) + " %"; on: true; onColor: Theme.danger; onClicked: measure.cancel() }
                    Rectangle { width: 1; height: 18; color: Theme.lineSoft }
                    Lab { text: page.t("OFFLINE") } Num { id: wavPath; Layout.fillWidth: true; placeholderText: page.t("path of a recorded sweep WAV (same FROM / TO / SECONDS as above)") }
                    Btn { label: page.t("ANALYSE WAV"); on: wavPath.text.length > 2; onClicked: measure.importSweep(wavPath.text, page.seconds, page.f1, page.f2, page.ppo) }
                    Btn { label: page.t("WRITE SWEEP WAV"); onClicked: measure.exportSweep("", page.seconds, page.f1, page.f2, page.level) } }
                Plot { Canvas { id: respPlot; anchors.fill: parent; anchors.margins: 6
                        onPaint: { const ctx = getContext("2d"), r = measure.result, rg = page.range(r.mag, 6); page.axes(ctx, width, height, page.f1, page.f2, rg[0], rg[1]); page.curve(ctx, width, height, r.freq, r.mag, page.f1, page.f2, rg[0], rg[1], Theme.cyan, 2)
                            if (r.thd && r.thd.length) { const t = []; for (let i = 0; i < r.thd.length; i++) t.push(rg[0] + Math.min(70, Math.max(0, r.thd[i] * 7))); page.curve(ctx, width, height, r.freq, t, page.f1, page.f2, rg[0], rg[1], Theme.orange, 1) }
                            if (r.kind === "sweep") { const p = []; for (let i = 0; i < r.phase.length; i++) p.push(rg[0] + (r.phase[i] + 180) / 360 * 70); page.curve(ctx, width, height, r.freq, p, page.f1, page.f2, rg[0], rg[1], Theme.ov(0.25), 1) } } }
                    Text { anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8; text: page.t("CYAN amplitude (dB)   ORANGE THD (10 % = full height)   GREY phase (±180°)"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
                Note { text: page.t("The test signal plays on the master (every output that carries MASTER) and is recorded from the chosen strip before its fader: route the measurement microphone or the loopback cable to that strip on ROUTING. A USB measurement microphone is just a sound card input; an analogue one goes through an interface with its calibration file loaded (MIC CAL). Stepped sine reads THD and THD+N per tone and takes longer; at 96 points per octave the span is limited to 480 tones.") } }
            // 1 RTA
            ColumnLayout { spacing: 8
                RowLayout { spacing: 6
                    Lab { text: page.t("RESOLUTION 1 /") } Repeater { model: [1, 3, 6, 12, 24, 48]; delegate: Btn { label: modelData + ""; on: page.rtaBpo === modelData; onClicked: page.rtaBpo = modelData } }
                    Lab { text: page.t("RMS AVERAGING") } Repeater { model: [[0, "OFF"], [0.6, "FAST"], [0.8, "MEDIUM"], [0.95, "SLOW"]]; delegate: Btn { label: page.t(modelData[1]); on: page.rtaAvg === modelData[0]; onClicked: page.rtaAvg = modelData[0] } }
                    Rectangle { width: 1; height: 18; color: Theme.lineSoft }
                    Lab { text: page.t("ALIGN PEAK TO") } Num { id: alignTo; text: "94"; implicitWidth: 44 } Btn { label: page.t("ALIGN"); onClicked: measure.alignRta(parseFloat(alignTo.text) || 94) } Btn { label: page.t("0 OFFSET"); onClicked: measure.splOffset = 0 }
                    Item { Layout.fillWidth: true } Lab { text: engine.running ? page.t("LIVE") : page.t("NO DEVICE"); color: engine.running ? Theme.emerald : Theme.amber } }
                Plot { Canvas { id: rtaPlot; anchors.fill: parent; anchors.margins: 6
                        onPaint: { const ctx = getContext("2d"), d = page.rtaData, rg = page.range(d.db, 6); page.axes(ctx, width, height, 20, 20000, rg[0], rg[1]); if (!d.freq) return
                            ctx.fillStyle = Theme.cyan; const n = d.freq.length; for (let i = 0; i < n; i++) { const x0 = page.fx(d.freq[i], 20, 20000, width), x1 = i + 1 < n ? page.fx(d.freq[i + 1], 20, 20000, width) : width, y = height - (Math.max(rg[0], Math.min(rg[1], d.db[i])) - rg[0]) / 70 * height; ctx.fillRect(x0, y, Math.max(1, x1 - x0 - (page.rtaBpo <= 6 ? 1 : 0)), height - y) } } } }
                Note { text: page.t("The analyser reads the chosen strip before its fader. ALIGN sets the SPL offset so the loudest band shows the level your SPL meter or calibrator reads (94 dB at 1 kHz for a calibrator); the same offset then applies to the sweep results.") } }
            // 2 IMPULSE
            ColumnLayout { spacing: 8
                RowLayout { spacing: 14
                    Lab { text: page.t("DELAY") } Val { text: measure.decay.delayMs !== undefined ? measure.decay.delayMs.toFixed(2) + " ms  (" + (measure.decay.delayMs * 0.343).toFixed(2) + " m)" : "—" }
                    Lab { text: "T20" } Val { text: measure.decay.t20 ? measure.decay.t20.toFixed(2) + " s" : "—" } Lab { text: "T30 (RT60)" } Val { text: measure.decay.t30 ? measure.decay.t30.toFixed(2) + " s" : "—" } Lab { text: "Topt" } Val { text: measure.decay.topt ? measure.decay.topt.toFixed(2) + " s" : "—" }
                    Btn { label: page.t("APPLY DELAY TO CH") + " " + (page.strip + 1); visible: measure.decay.delayMs > 0; onClicked: engine.setDelay(page.strip, Math.min(500, measure.decay.delayMs)) }
                    Item { Layout.fillWidth: true } }
                Plot { Layout.preferredHeight: 160; Canvas { id: etcPlot; anchors.fill: parent; anchors.margins: 6
                        onPaint: { const ctx = getContext("2d"), d = measure.decay, w = width, h = height; ctx.clearRect(0, 0, w, h); if (!d.etc) return; let top = -1e9; for (let i = 0; i < d.etc.length; i++) top = Math.max(top, d.etc[i])
                            const draw = function (v, col, off) { ctx.strokeStyle = col; ctx.lineWidth = 1.2; ctx.beginPath(); for (let i = 0; i < v.length; i++) { const x = i / v.length * w, y = Math.min(h, (off - v[i]) / 80 * h); if (i) ctx.lineTo(x, y); else ctx.moveTo(x, y) } ctx.stroke() }
                            draw(d.etc, Theme.cyan, top); draw(d.schroeder, Theme.orange, 0); ctx.fillStyle = Theme.dim; ctx.font = "9px monospace"; ctx.fillText("0 … " + d.seconds.toFixed(1) + " s   ETC (cyan), Schroeder (orange), 80 dB", 4, 10) } } }
                RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
                    Plot { Canvas { id: waterPlot; anchors.fill: parent; anchors.margins: 6; onPaint: page.heat(getContext("2d"), width, height, page.water, 50) } Text { anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8; text: page.t("WATERFALL  20 Hz - 2 kHz, time downward"); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 7 } }
                    Plot { Canvas { id: specPlot; anchors.fill: parent; anchors.margins: 6; onPaint: page.heat(getContext("2d"), width, height, page.spec, 70) } Text { anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8; text: page.t("SPECTROGRAM of the recording, time downward"); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 7 } } }
                Note { text: page.t("From the last sweep. The delay is the acoustic path (loopback: the round-trip latency); APPLY sets it as the strip delay for time alignment. RT60 needs a decay of 25 dB (T20) or 35 dB (T30) above the noise: a quiet room and a loud sweep.") } }
            // 3 ROOM
            ColumnLayout { spacing: 8
                RowLayout { spacing: 6
                    Repeater { model: ["LENGTH", "WIDTH", "HEIGHT (m)", "RT60 (s)", "SPEAKER X", "Y", "Z", "LISTENER X", "Y", "Z"]
                        delegate: RowLayout { spacing: 3; Lab { text: page.t(modelData) } Num { implicitWidth: 46; text: page.rm[index]; onEditingFinished: { const a = page.rm.slice(); a[index] = parseFloat(text) || a[index]; page.rm = a; page.roomData = measure.room(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]); roomPlot.requestPaint() } } } }
                    Item { Layout.fillWidth: true } }
                RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
                    Plot { Canvas { id: roomPlot; anchors.fill: parent; anchors.margins: 6
                            onPaint: { const ctx = getContext("2d"), d = page.roomData; page.axes(ctx, width, height, 20, 400, -30, 20); page.curve(ctx, width, height, d.freq, d.db, 20, 400, -30, 20, Theme.cyan, 2)
                                if (d.modes) { ctx.strokeStyle = Theme.orange; ctx.lineWidth = 1; for (let i = 0; i < d.modes.length; i++) { if (d.modes[i].kind !== "axial") continue; const x = page.fx(d.modes[i].freq, 20, 400, width); ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, 10); ctx.stroke() } } } }
                        Text { anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8; text: page.t("PREDICTED BASS AT THE LISTENER (0 dB = mean)   ORANGE ticks: axial modes"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
                    Rectangle { Layout.preferredWidth: 210; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; clip: true
                        ListView { anchors.fill: parent; anchors.margins: 8; model: page.roomData.modes || []; spacing: 1
                            header: Text { text: page.t("ROOM MODES"); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; bottomPadding: 4 }
                            delegate: Text { text: modelData.freq.toFixed(1) + " Hz   " + modelData.n + "   " + modelData.kind; color: modelData.kind === "axial" ? Theme.orange : Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } } } }
                Note { text: page.t("A rectangular room with rigid walls: the mode frequencies are exact for that shape, the response is a prediction (modal sum with the damping of the RT60). Move the speaker and the listener to see which modes each place excites; real rooms differ, so confirm with a sweep.") } }
            // 4 AUTO EQ
            ColumnLayout { spacing: 8
                RowLayout { spacing: 6
                    Lab { text: page.t("TARGET") } Btn { label: page.t("FLAT"); on: page.eqTarget === "flat"; onClicked: page.eqTarget = "flat" } Btn { label: page.t("HOUSE CURVE"); on: page.eqTarget === "house"; onClicked: page.eqTarget = "house" }
                    Lab { text: page.t("BASS LIFT dB") } Num { id: lift; text: "4"; implicitWidth: 40 } Lab { text: page.t("LEVEL ± dB") } Num { id: tLevel; text: "0"; implicitWidth: 40 }
                    Lab { text: page.t("RANGE Hz") } Num { text: page.eqLo; implicitWidth: 50; onEditingFinished: page.eqLo = parseFloat(text) || 20 } Num { text: page.eqHi; implicitWidth: 56; onEditingFinished: page.eqHi = parseFloat(text) || 400 }
                    Lab { text: page.t("FILTERS") } Num { id: nFilt; text: "8"; implicitWidth: 36 }
                    Btn { label: page.t("CALCULATE"); on: true; onColor: Theme.cyan; onClicked: measure.autoEq(page.eqTarget, parseFloat(tLevel.text) || 0, parseFloat(lift.text) || 0, page.eqLo, page.eqHi, parseInt(nFilt.text) || 8) }
                    Item { Layout.fillWidth: true } }
                RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
                    Plot { Canvas { id: eqPlot; anchors.fill: parent; anchors.margins: 6
                            onPaint: { const ctx = getContext("2d"), r = measure.result, c = measure.eqCurve, lo = Math.max(10, page.eqLo / 2), hi = Math.min(22000, page.eqHi * 2), rg = page.range(r.mag, 6); page.axes(ctx, width, height, lo, hi, rg[1] - 40, rg[1])
                                page.curve(ctx, width, height, r.freq, r.mag, lo, hi, rg[1] - 40, rg[1], Theme.ov(0.35), 1.5); page.curve(ctx, width, height, c.freq, c.target, lo, hi, rg[1] - 40, rg[1], Theme.orange, 1); page.curve(ctx, width, height, c.freq, c.corrected, lo, hi, rg[1] - 40, rg[1], Theme.emerald, 2) } }
                        Text { anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8; text: page.t("GREY measured   ORANGE target   GREEN after the filters"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
                    Rectangle { Layout.preferredWidth: 250; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                        ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 4
                            Text { text: page.t("PARAMETRIC FILTERS"); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                            Repeater { model: measure.filters; delegate: Text { text: (index + 1) + "  PK  " + modelData.freq.toFixed(1) + " Hz   " + (modelData.gain > 0 ? "+" : "") + modelData.gain.toFixed(1) + " dB   Q " + modelData.q.toFixed(2); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8 } }
                            Text { visible: measure.filters.length === 0; text: page.t("none yet: measure, then CALCULATE"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                            Item { Layout.fillHeight: true }
                            Btn { Layout.fillWidth: true; label: page.t("SET ON THE EQ OF CH") + " " + (page.strip + 1); on: measure.filters.length > 0; onColor: Theme.emerald; onClicked: measure.applyFilters(page.strip) }
                            Btn { Layout.fillWidth: true; label: page.t("EXPORT TEXT (REW / miniDSP / Equalizer APO)"); on: measure.filters.length > 0; onClicked: measure.exportFilters("", false) }
                            Btn { Layout.fillWidth: true; label: page.t("EXPORT JSON"); on: measure.filters.length > 0; onClicked: measure.exportFilters("", true) } } } }
                Note { text: page.t("Peaking filters fitted to the last measured response inside the range (20 - 400 Hz by default: the range where a room, not the speaker, sets the response). Cuts are preferred to boosts (at most +6 dB). The export is the generic filter text external DSP units and Equalizer APO read; the files go to the data folder.") } }
        }
    }
}
