// AMFB MASTER ANALYZER: what the master is doing, on one panel. Loudness (momentary, short-term, integrated, range, true peak) against a
// target, the left / right levels with peak and RMS and the stereo correlation, a spectrogram of the last seconds and the loudness history of
// the last minutes. It only reads the meters the engine already runs on the master: it changes nothing in the sound.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: pop
    objectName: "masterAnalyzer"
    modal: true; padding: 0; width: 1020; height: 680; anchors.centerIn: Overlay.overlay
    readonly property var tel: engine.telemetry
    property bool paused: false
    property int targetIndex: 0
    readonly property var targets: [ { n: "STREAMING", lufs: -14, peak: -1 }, { n: "BROADCAST", lufs: -23, peak: -1 }, { n: "PODCAST", lufs: -16, peak: -1 }, { n: "LIVE / CLUB", lufs: -9, peak: -0.3 } ]
    readonly property var target: targets[targetIndex]
    property var history: []        // [{m, s}] four per second, three minutes
    property var columns: []        // spectrogram columns, newest last
    function val(k) { const v = tel[k]; return v === undefined || v < -99 ? -120 : v }
    function num(v, d) { return v < -99 ? "—" : Number(v).toFixed(d === undefined ? 1 : d) }
    // against the target: within 1 LU = on target, above = too loud, more than 3 below = quiet
    function tone(v, t) { return v < -99 ? Theme.dim : v > t + 1 ? Theme.danger : v < t - 3 ? Theme.cyan : v < t - 1 ? Theme.amber : Theme.emerald }
    function clearAll() { history = []; columns = []; hist.requestPaint(); sgram.requestPaint() }
    Timer { interval: 250; running: pop.visible && !pop.paused; repeat: true
        onTriggered: { const h = pop.history; h.push({ m: pop.val("momentary"), s: pop.val("shortTerm") }); if (h.length > 720) h.shift(); pop.history = h; hist.requestPaint() } }
    Timer { interval: 100; running: pop.visible && !pop.paused; repeat: true
        onTriggered: { const s = engine.spectrum(0); if (!s || !s.length) return; const c = pop.columns; c.push(s); if (c.length > 150) c.shift(); pop.columns = c; sgram.requestPaint() } }

    component Head: Text { color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
    component Small: Text { color: Theme.muted; font.family: Theme.mono; font.pixelSize: 10 }
    component Pill: Rectangle { property string label; property bool on: false; property color onColor: Theme.cyan; signal clicked(); implicitWidth: Math.max(50, pl.implicitWidth + 18); implicitHeight: 26; radius: 6; color: on ? onColor : (ph.hovered ? Theme.ov(0.14) : Theme.ov(0.08)); border.color: on ? onColor : Theme.line
        Text { id: pl; anchors.centerIn: parent; text: i18n.t(parent.label, i18n.lang); color: parent.on ? "#000" : Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } HoverHandler { id: ph } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    component Card: Rectangle { radius: 8; color: Theme.ov(0.035); border.color: Theme.lineSoft }
    component Big: Column { property string title; property real v: -120; property string unit: "LUFS"; spacing: 0
        Small { anchors.horizontalCenter: parent.horizontalCenter; text: i18n.t(parent.title, i18n.lang) }
        Row { anchors.horizontalCenter: parent.horizontalCenter; spacing: 3
            Text { text: pop.num(parent.parent.v); color: pop.tone(parent.parent.v, pop.target.lufs); font.family: Theme.mono; font.pixelSize: 34; font.bold: true }
            Small { anchors.baseline: parent.children[0].baseline; text: parent.parent.unit } } }
    component Level: ColumnLayout { property string name; property real peak: -120; property real rms: -120; spacing: 3
        Small { Layout.alignment: Qt.AlignHCenter; text: pop.num(parent.peak); color: parent.peak > pop.target.peak ? Theme.danger : Theme.ink }
        Small { Layout.alignment: Qt.AlignHCenter; text: pop.num(parent.rms) }
        Rectangle { Layout.alignment: Qt.AlignHCenter; Layout.fillHeight: true; implicitWidth: 34; radius: 3; color: Theme.deep; border.color: Theme.lineSoft
            readonly property real span: 60
            function yOf(db) { return (1 - Math.max(0, Math.min(1, (db + span) / span))) * (height - 2) + 1 }
            Rectangle { x: 1; width: parent.width - 2; y: parent.yOf(parent.parent.rms); height: Math.max(0, parent.height - 1 - y); radius: 2; color: Theme.ov(0.55) }
            Rectangle { x: 1; width: parent.width - 2; y: parent.yOf(parent.parent.peak); height: Math.max(0, parent.yOf(parent.parent.rms) - y); color: Theme.ov(0.22) }
            Rectangle { x: 1; width: parent.width - 2; y: parent.yOf(parent.parent.peak); height: 2; color: parent.parent.peak > pop.target.peak ? Theme.danger : Theme.ink }
            Rectangle { x: 1; width: parent.width - 2; y: parent.yOf(pop.target.peak); height: 1; color: Theme.danger; opacity: 0.6 } }
        Head { Layout.alignment: Qt.AlignHCenter; text: parent.name } }

    background: Rectangle { radius: 12; color: Theme.card; border.color: Theme.cyan }
    contentItem: ColumnLayout { spacing: 8
        RowLayout { Layout.fillWidth: true; Layout.margins: 14; Layout.bottomMargin: 0; spacing: 8
            Text { text: i18n.t("AMFB", i18n.lang); color: Theme.orange; font.family: Theme.mono; font.pixelSize: 15; font.weight: Font.Black; font.letterSpacing: 4 }
            Text { text: i18n.t("MASTER ANALYZER", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 13; font.bold: true; font.letterSpacing: 3 }
            Item { Layout.fillWidth: true }
            Small { text: i18n.t("TARGET", i18n.lang) }
            Repeater { model: pop.targets; delegate: Pill { objectName: "maTarget" + index; label: i18n.t(modelData.n, i18n.lang) + " " + modelData.lufs; on: pop.targetIndex === index; onClicked: pop.targetIndex = index } }
            Item { implicitWidth: 8 }
            Pill { objectName: "maPause"; label: pop.paused ? i18n.t("RESUME", i18n.lang) : i18n.t("PAUSE", i18n.lang); on: pop.paused; onColor: Theme.amber; onClicked: pop.paused = !pop.paused }
            Pill { objectName: "maClear"; label: i18n.t("CLEAR", i18n.lang); onClicked: pop.clearAll() }
            Pill { objectName: "maClose"; label: i18n.t("CLOSE", i18n.lang); onClicked: pop.close() } }
        RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; Layout.leftMargin: 14; Layout.rightMargin: 14; spacing: 8
            ColumnLayout { Layout.preferredWidth: 400; Layout.maximumWidth: 400; Layout.fillHeight: true; spacing: 8
                Card { Layout.fillWidth: true; implicitHeight: 172
                    Head { x: 14; y: 10; text: i18n.t("LOUDNESS", i18n.lang) }
                    Row { anchors.horizontalCenter: parent.horizontalCenter; y: 38; spacing: 22
                        Big { title: i18n.t("SHORT-TERM", i18n.lang); v: pop.val("shortTerm") }
                        Big { title: i18n.t("INTEGRATED", i18n.lang); v: pop.val("integrated") }
                        Big { title: i18n.t("MOMENTARY", i18n.lang); v: pop.val("momentary") } }
                    Row { anchors.horizontalCenter: parent.horizontalCenter; y: 112; spacing: 16
                        Small { text: i18n.t("RANGE ", i18n.lang) + (pop.tel.lra === undefined ? "—" : Number(pop.tel.lra).toFixed(1)) + " LU"; color: Theme.ink; font.pixelSize: 10 }
                        Small { text: i18n.t("TRUE PEAK ", i18n.lang) + pop.num(pop.val("truePeak")) + " dBTP"; color: pop.val("truePeak") > pop.target.peak ? Theme.danger : Theme.ink; font.pixelSize: 10 }
                        Small { text: i18n.t("TARGET ", i18n.lang) + pop.target.lufs + " LUFS · " + pop.target.peak + " dBTP"; font.pixelSize: 10 } }
                    Small { anchors.horizontalCenter: parent.horizontalCenter; y: 140; color: pop.tone(pop.val("integrated"), pop.target.lufs); text: pop.val("integrated") < -99 ? i18n.t("Play something: the integrated value needs a few seconds of programme.", i18n.lang) : (pop.val("integrated") > pop.target.lufs + 1 ? i18n.t("Louder than the target by ", i18n.lang) : pop.val("integrated") < pop.target.lufs - 1 ? i18n.t("Quieter than the target by ", i18n.lang) : i18n.t("On target, within ", i18n.lang)) + Math.abs(pop.val("integrated") - pop.target.lufs).toFixed(1) + " LU" } }
                Card { Layout.fillWidth: true; Layout.fillHeight: true
                    Head { x: 14; y: 10; text: i18n.t("LEVELS", i18n.lang) }
                    Small { x: 14; y: 30; text: i18n.t("peak\nRMS", i18n.lang); lineHeight: 1.25 }
                    Item { x: 22; y: 76; width: 30; height: parent.height - 76 - 44
                        Repeater { model: [0, -12, -24, -36, -48, -60]; delegate: Small { y: (-modelData / 60) * parent.height - 6; width: 30; horizontalAlignment: Text.AlignRight; text: modelData; font.pixelSize: 9; color: Theme.dim } } }
                    RowLayout { anchors.fill: parent; anchors.topMargin: 28; anchors.margins: 12; anchors.leftMargin: 60; spacing: 26
                        Level { Layout.fillHeight: true; name: "L"; peak: pop.val("masterLDb"); rms: pop.val("masterLRmsDb") }
                        Level { Layout.fillHeight: true; name: "R"; peak: pop.val("masterRDb"); rms: pop.val("masterRRmsDb") }
                        ColumnLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 6
                            Item { Layout.fillHeight: true }
                            Small { text: i18n.t("STEREO CORRELATION", i18n.lang) }
                            Rectangle { Layout.fillWidth: true; implicitHeight: 12; radius: 3; color: Theme.deep; border.color: Theme.lineSoft
                                readonly property real c: pop.tel.correlation === undefined ? 0 : Math.max(-1, Math.min(1, pop.tel.correlation))
                                Rectangle { x: parent.width / 2 - 0.5; width: 1; height: parent.height; color: Theme.line }
                                Rectangle { y: 2; height: parent.height - 4; radius: 2; color: parent.c < 0 ? Theme.danger : Theme.emerald; x: parent.c < 0 ? parent.width / 2 + parent.c * (parent.width / 2 - 2) : parent.width / 2; width: Math.abs(parent.c) * (parent.width / 2 - 2) } }
                            RowLayout { Layout.fillWidth: true; Small { text: "−1" } Item { Layout.fillWidth: true } Small { text: (pop.tel.correlation === undefined ? "—" : Number(pop.tel.correlation).toFixed(2)); color: Theme.ink } Item { Layout.fillWidth: true } Small { text: "+1" } }
                            Small { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: i18n.t("Below zero for long: left and right cancel in mono.", i18n.lang); color: Theme.dim; font.pixelSize: 9 }
                            Item { Layout.fillHeight: true } } } } }
            Card { Layout.fillWidth: true; Layout.fillHeight: true
                Head { x: 14; y: 10; text: i18n.t("SPECTROGRAM", i18n.lang) }
                Small { anchors.right: parent.right; anchors.rightMargin: 14; y: 12; text: i18n.t("the master · last 15 seconds · low at the bottom", i18n.lang) }
                Canvas { id: sgram; objectName: "maSpectrogram"; anchors.fill: parent; anchors.topMargin: 32; anchors.margins: 12
                    onPaint: { const g = getContext("2d"); g.fillStyle = "#06080c"; g.fillRect(0, 0, width, height); const c = pop.columns, n = 150; if (!c.length) return;
                        const cw = width / n; for (let x = 0; x < c.length; ++x) { const col = c[x], bh = height / col.length, px = (n - c.length + x) * cw;
                            for (let b = 0; b < col.length; ++b) { let v = col[b]; v = v <= 0 ? (v + 84) / 84 : Math.min(1, v); if (v <= 0.02) continue; v = Math.min(1, v);
                                g.fillStyle = Qt.hsla(0.58 - 0.5 * v, 0.9, 0.12 + 0.5 * v, 1); g.fillRect(px, height - (b + 1) * bh, cw + 0.6, bh + 0.6) } }
                        g.font = "9px monospace"; g.fillStyle = "rgba(255,255,255,0.55)"; g.strokeStyle = "rgba(255,255,255,0.10)"; g.lineWidth = 1;   // 20 Hz at the bottom, 20 kHz at the top, logarithmic like the analyser's bands
                        for (const f of [100, 1000, 10000]) { const y = height * (1 - Math.log(f / 20) / Math.log(1000)); g.beginPath(); g.moveTo(0, y); g.lineTo(width, y); g.stroke(); g.fillText(f >= 1000 ? f / 1000 + "k" : f, 4, y - 3) } } } } }
        Card { Layout.fillWidth: true; Layout.preferredHeight: 170; Layout.margins: 14; Layout.topMargin: 0
            Head { x: 14; y: 10; text: i18n.t("HISTORY", i18n.lang) }
            Row { anchors.right: parent.right; anchors.rightMargin: 14; y: 12; spacing: 12
                Small { text: "— " + i18n.t("short-term", i18n.lang); color: Theme.amber } Small { text: "— " + i18n.t("momentary", i18n.lang); color: Theme.ink } Small { text: "- - " + i18n.t("target", i18n.lang); color: Theme.danger } Small { text: i18n.t("last 3 minutes", i18n.lang) } }
            Canvas { id: hist; objectName: "maHistory"; anchors.fill: parent; anchors.topMargin: 32; anchors.margins: 12
                onPaint: { const g = getContext("2d"); g.fillStyle = "#06080c"; g.fillRect(0, 0, width, height); const lo = -50, hi = 0, w = width - 30, yOf = v => (1 - (Math.max(lo, Math.min(hi, v)) - lo) / (hi - lo)) * height;
                    g.font = "9px monospace"; g.strokeStyle = "rgba(255,255,255,0.08)"; g.fillStyle = "rgba(255,255,255,0.45)"; g.lineWidth = 1;
                    for (let d = -40; d <= -10; d += 10) { g.beginPath(); g.moveTo(0, yOf(d)); g.lineTo(w, yOf(d)); g.stroke(); g.fillText(d, w + 4, yOf(d) + 3) }
                    g.strokeStyle = "rgba(239,79,95,0.8)"; g.setLineDash([4, 4]); g.beginPath(); g.moveTo(0, yOf(pop.target.lufs)); g.lineTo(w, yOf(pop.target.lufs)); g.stroke(); g.setLineDash([]);
                    const h = pop.history, n = 720; const line = (key, color, lw) => { g.strokeStyle = color; g.lineWidth = lw; g.beginPath(); let pen = false;
                        for (let i = 0; i < h.length; ++i) { const v = h[i][key]; if (v < -99) { pen = false; continue } const x = (n - h.length + i) / n * w, y = yOf(v); if (pen) g.lineTo(x, y); else { g.moveTo(x, y); pen = true } } g.stroke() };
                    line("m", "rgba(232,237,245,0.75)", 1); line("s", "#f5b524", 2);
                    g.fillStyle = "rgba(255,255,255,0.45)"; for (let k = 0; k <= 3; ++k) { const x = k / 3 * w; g.fillText(k === 3 ? "now" : "-" + (3 - k) + ":00", Math.min(w - 20, x + 3), height - 3); g.strokeStyle = "rgba(255,255,255,0.06)"; g.beginPath(); g.moveTo(x, 0); g.lineTo(x, height); g.stroke() } } } }
    }
    onTargetIndexChanged: hist.requestPaint()
}
