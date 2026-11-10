// RTA EQ: the AmF X1 beta's "10-BAND PROGRAM BUS EQ & DIAMOND RTA" page, on the native engine. PAGE 1: the DIAMOND response curve
// (10 ISO bands 31 Hz .. 16 kHz, +-12 dB, drag a diamond) over a 64-bar REAL-TIME SPECTRUM behind it (the strip after its DSP, from the
// engine's analyser; FREEZE holds it). PAGE 2: the ten ISO FADERS. SPLIT: both. The toolbar carries GOLDEN EQ (Amix by AMFB tuned:
// +1,+5,+3,+3,+4,+2,+2,+3,+3,+1), CUSTOM EQ, SAVE CUSTOM, ANIX W-SHAPE, the 15 standard + 15 microphone presets and FLAT; below, the
// ten CUSTOM PRESET USER SLOTS (LOAD / SAVE / DELETE). The bands are the strip's own EQ (the same ten the TONE and MIC EQ pages edit).
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Flickable {
    id: page
    objectName: "rtaEqPage"
    property string view: "diamond"          // diamond | faders | both
    property bool rtaOn: true
    property bool frozen: false
    property int strip: engine.selectedStrip
    property var gains: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
    property var spec: []
    property string presetName: ""
    property int band: 0                        // the band the PARAMETRIC sliders edit (click a diamond or a fader label)
    property var bandNow: ({ type: 0, freq: 1000, q: 1.41, gainDb: 0 })
    readonly property var typeNames: ["OFF", "PEAK", "LOW SHELF", "HIGH SHELF", "LOW PASS", "HIGH PASS", "NOTCH", "ALL-PASS 1", "ALL-PASS 2"]
    property int slotSel: 0
    property int slotTick: 0
    readonly property var bands: [31, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]
    readonly property var bandText: ["31", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"]
    readonly property var presets: [
        { n: "Golden EQ (Amix by AMFB tuned)", g: [1, 5, 3, 3, 4, 2, 2, 3, 3, 1] }, { n: "Anix W Shape", g: [6, 3.5, -2, -4.5, 4, 3.5, -3, -1, 5, 6.5] }, { n: "Flat", g: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0] },
        { n: "Vocal Air", g: [0, -1.5, -1, 0, 1.5, 2.5, 3.5, 5, 6.5, 7] }, { n: "Bass Punch", g: [6.5, 5, 3.5, 1.5, 0, -1, 0, 1, 2, 0] }, { n: "Club / Dance", g: [5, 4, 2, 0, -2, 0, 2, 3.5, 4.5, 4] },
        { n: "Rock Drive", g: [4, 3, 1, -1, -2, 0, 2, 3.5, 4, 4.5] }, { n: "Jazz Warm", g: [3, 2, 1, 1, 2, 2, 1, 1, 2, 1.5] }, { n: "Acoustic Clean", g: [2, 1, 0, -1, 0, 1, 2, 3, 4, 3.5] },
        { n: "Speech / Broadcast", g: [-4, -2, 0, 2, 3, 3.5, 2, 1, -1, -3] }, { n: "Warm Master", g: [2, 2, 1, 0, 0, 0, 1, 1.5, 2, 2] }, { n: "Treble Boost", g: [-2, -1, 0, 0, 0, 1, 3, 5, 7, 8] },
        { n: "Bright Acoustic", g: [-1, 0, 0, 1, 1, 2, 3, 4, 5, 5.5] }, { n: "Loudness Smiley", g: [5.5, 4.5, 2, 0, -2.5, -1.5, 1, 3.5, 5, 6] }, { n: "Mid Scoop", g: [4, 3, 1, -3, -4.5, -3, 0, 2, 4, 4.5] },
        { n: "Presence Crisp", g: [-1, -1, 0, 0, 1, 3, 5, 4, 3, 2] }, { n: "Low Cut / HPF", g: [-12, -8, -3.5, 0, 0, 0, 0, 0, 0, 0] },
        { n: "MIC • Male Vocal", g: [-12, -9, -3, -1.5, 0, 1.5, 2.5, 3.5, 2, 1] }, { n: "MIC • Female Vocal", g: [-12, -10, -4, -2, -1, 1, 2.5, 3, 3, 2] }, { n: "MIC • Live Singer / Karaoke", g: [-9, -5, -1, 0, 0, 1.5, 3, 4, 3, 2] },
        { n: "MIC • Handheld Dynamic (SM58 style)", g: [-9, -6, -3, -2, 0, 1, 2.5, 3, 1, 0.5] }, { n: "MIC • Podcast / Voice-over", g: [-10, -6, -2, 1.5, 2, 1, 2, 2.5, 1, -1] }, { n: "MIC • Broadcast Radio", g: [-8, -5, 0, 2.5, 3, 1, 2, 3, 1.5, 0] },
        { n: "MIC • Studio Condenser", g: [-6, -3, 0, -1, 0, 0, 1, 1.5, 3, 4] }, { n: "MIC • Headset / Lavalier", g: [-12, -10, -4, 0, 1, 2, 3, 3, 0, -3] }, { n: "MIC • Guitar Amp (SM57 style)", g: [-10, -6, -1, 1, 1, 1, 2, 3, -1, -4] },
        { n: "MIC • Acoustic Guitar", g: [-8, -4, 0, -1, -2, 0, 1, 2, 3, 3] }, { n: "MIC • Kick Drum", g: [4, 5, 2, -3, -5, -1, 2, 3, 0, -2] }, { n: "MIC • Snare / Toms", g: [-4, -2, 1, 3, -2, 0, 2, 3, 2, 0] },
        { n: "MIC • De-Mud & De-Box", g: [-6, -3, -1, -4, -3, 0, 1, 2, 2, 1] }, { n: "MIC • De-Ess", g: [0, 0, 0, 0, 0, 0, -1, -3.5, -5, -2] }, { n: "MIC • Feedback Safe (live stage)", g: [-12, -8, -2, -1, -1, 0, -1, -3, -2, -4] } ]
    contentHeight: col.implicitHeight + 24
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    function reload() { const o = []; for (let b = 0; b < 10; ++b) { const e = engine.eqBand(strip, b); o.push(e.type > 0 ? Math.max(-12, Math.min(12, e.gainDb)) : 0) } gains = o; bandNow = engine.eqBand(strip, band); curve.requestPaint() }
    function setParam(type, f, q, g) { engine.setEq(strip, band, type, f, q, g); presetName = "" }
    function apply(g) { const o = []; let any = false; for (let b = 0; b < 10; ++b) { o.push(Math.max(-12, Math.min(12, Number(g[b]) || 0))); if (o[b] !== 0) any = true } gains = o; engine.setGraphicEq(strip, o, any || true); curve.requestPaint() }
    function setBand(b, v) { const e = engine.eqBand(strip, b); const g = Math.round(Math.max(-12, Math.min(12, v)) * 2) / 2; band = b; engine.setEq(strip, b, g === 0 && e.type <= 1 ? 0 : (e.type > 0 ? e.type : 1), e.type > 0 ? e.freq : bands[b], e.type > 0 ? e.q : 1.41, g); presetName = "" }
    function loadPreset(name) { for (let i = 0; i < presets.length; ++i) if (presets[i].n === name) { apply(presets[i].g); presetName = name; return } }
    function dbText(v) { return (v > 0 ? "+" : "") + Number(v).toFixed(1) + "dB" }
    Component.onCompleted: { engine.setScopeStrip(strip); reload() }
    onStripChanged: { engine.setScopeStrip(strip); reload() }
    onBandChanged: bandNow = engine.eqBand(strip, band)
    Connections { target: engine; function onStripsChanged() { page.reload() } function onPresetsChanged() { page.slotTick++ } }
    Timer { interval: 16; running: page.visible && page.rtaOn && !page.frozen; repeat: true; onTriggered: { page.spec = engine.spectrum(2); rta.requestPaint() } }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; property color offColor: Qt.rgba(1, 1, 1, 0.08); signal clicked(); implicitWidth: bl.implicitWidth + 16; implicitHeight: 22; radius: 4; color: on ? onColor : offColor; border.color: on ? onColor : Theme.lineSoft
        Text { id: bl; anchors.centerIn: parent; text: label; color: parent.on ? "#000" : Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    ColumnLayout {
        id: col
        width: parent.width - 20
        x: 10; y: 10
        spacing: 8
        // header: title, PAGES, the preset toolbar (as the beta had it)
        Rectangle { Layout.fillWidth: true; implicitHeight: hdr.implicitHeight + 20; radius: Theme.radius; color: Theme.card; border.color: Theme.line
            ColumnLayout { id: hdr; anchors.fill: parent; anchors.margins: 10; spacing: 6
                RowLayout { Layout.fillWidth: true; spacing: 8
                    Text { text: "◭ 10-BAND PROGRAM BUS EQ & DIAMOND RTA"; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
                    Text { text: "CH " + (page.strip + 1); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                    Item { Layout.fillWidth: true }
                    Text { text: "PAGES:"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; font.letterSpacing: 2 }
                    Btn { label: "PAGE 1: DIAMOND + FFT RTA"; on: page.view === "diamond"; onClicked: page.view = "diamond" }
                    Btn { label: "PAGE 2: ISO FADERS"; on: page.view === "faders"; onClicked: page.view = "faders" }
                    Btn { label: "SPLIT: BOTH"; on: page.view === "both"; onClicked: page.view = "both" } }
                Flow { Layout.fillWidth: true; spacing: 6
                    Btn { label: "★ GOLDEN EQ (AMIX BY AMFB TUNED)"; onColor: Theme.amber; offColor: Qt.rgba(0.984, 0.749, 0.141, 0.18); on: page.presetName === "Golden EQ (Amix by AMFB tuned)"; onClicked: page.loadPreset("Golden EQ (Amix by AMFB tuned)")
                        ToolTip.visible: gh.hovered; ToolTip.text: "Amix by AMFB tuned (+1,+5,+3,+3,+4,+2,+2,+3,+3,+1 dB)"; HoverHandler { id: gh } }
                    Btn { label: "CUSTOM EQ"; onColor: Theme.cyan; offColor: Qt.rgba(0, 0.95, 1, 0.14); on: page.presetName === "Custom EQ"; onClicked: { const s = engine.geqSlots()[0]; page.apply(s.gains); page.presetName = "Custom EQ" }
                        ToolTip.visible: ch.hovered; ToolTip.text: "Load custom slot 1 (the user profile)"; HoverHandler { id: ch } }
                    Btn { label: "💾 SAVE CUSTOM"; offColor: Qt.rgba(0, 0.95, 1, 0.14); onClicked: engine.saveGeqSlot(1, "Custom EQ", page.gains) }
                    Btn { label: "ANIX W-SHAPE"; onColor: Theme.amber; offColor: Qt.rgba(0.984, 0.749, 0.141, 0.18); on: page.presetName === "Anix W Shape"; onClicked: page.loadPreset("Anix W Shape") }
                    ComboBox { id: presetBox; implicitWidth: 290; implicitHeight: 22; font.family: Theme.mono; font.pixelSize: 8; displayText: page.presetName ? page.presetName : "Standard + Mic Presets (30+)"
                        model: { const l = []; for (let i = 0; i < page.presets.length; ++i) l.push((i < 2 ? "★ " : "") + page.presets[i].n); return l }
                        onActivated: function (i) { page.loadPreset(page.presets[i].n) } }
                    Btn { label: "FLAT"; onClicked: { page.apply([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]); page.presetName = "Flat" } } } } }
        // 1. DIAMOND PEQ RESPONSE CURVE & 64-BIN RTA SPECTRUM OVERLAY
        Rectangle { visible: page.view !== "faders"; Layout.fillWidth: true; implicitHeight: page.view === "both" ? 240 : 360; radius: Theme.radius; color: "#05070a"; border.color: Theme.line; clip: true
            Canvas { id: rta; anchors.fill: parent; visible: page.rtaOn
                onPaint: { const c = getContext("2d"); c.reset(); const sp = page.spec; if (!sp || sp.length !== 31) return; const bars = 64, bw = width / bars, step = width / 11
                    // the bars sit on the curve's own frequency axis (band k at x = step * (k + 1)); the 31 third-octave bands of the analyser (20 Hz .. 20 kHz) are read at that frequency
                    function hzAtX(x) { const t = x / step - 1, k = Math.max(0, Math.min(8, Math.floor(t))); return page.bands[k] * Math.pow(page.bands[k + 1] / page.bands[k], t - k) }
                    for (let i = 0; i < bars; i++) { const f0 = hzAtX(i * bw), f1 = hzAtX((i + 1) * bw); let val = -120
                        const b0 = Math.max(0, Math.min(30, Math.floor(Math.log(f0 / 20) / Math.log(10) * 10))), b1 = Math.max(b0, Math.min(30, Math.ceil(Math.log(f1 / 20) / Math.log(10) * 10)))
                        for (let b = b0; b <= b1; b++) if (sp[b] > val) val = sp[b]
                        const h = Math.max(0, Math.min(1, (val + 72) / 72)) * height * 0.85
                        const g = c.createLinearGradient(0, height - h, 0, height); g.addColorStop(0, "rgba(255,106,0,0.45)"); g.addColorStop(1, "rgba(0,242,255,0.08)"); c.fillStyle = g; c.fillRect(i * bw, height - h, bw - 1, h)
                        c.fillStyle = "rgba(255,255,255,0.7)"; c.fillRect(i * bw, height - h, bw - 1, 1.5) } } }
            Canvas { id: curve; anchors.fill: parent
                onPaint: { const c = getContext("2d"); c.reset(); const midY = height / 2, step = width / 11
                    c.lineWidth = 1; c.font = "7px monospace"
                    for (const db of [-12, -6, 0, 6, 12]) { const y = midY - db / 12 * height * 0.42; c.strokeStyle = "rgba(255,255,255,0.08)"; c.beginPath(); c.moveTo(0, y); c.lineTo(width, y); c.stroke(); c.fillStyle = "rgba(255,255,255,0.3)"; c.fillText((db > 0 ? "+" : "") + db + "dB", 4, y - 2) }
                    const pts = []; for (let i = 0; i < 10; i++) { const x = step * (i + 1), y = midY - page.gains[i] / 12 * height * 0.42; pts.push({ x: x, y: y }); c.strokeStyle = "rgba(255,255,255,0.04)"; c.beginPath(); c.moveTo(x, 0); c.lineTo(x, height); c.stroke(); c.fillStyle = "rgba(255,255,255,0.35)"; c.fillText(page.bandText[i] + "Hz", x - 8, height - 6) }
                    const grad = c.createLinearGradient(0, 0, width, 0); grad.addColorStop(0, "#00f2ff"); grad.addColorStop(0.5, "#ffb020"); grad.addColorStop(1, "#ff6a00")
                    c.strokeStyle = grad; c.lineWidth = 3; c.beginPath(); c.moveTo(0, midY)
                    for (let i = 0; i < pts.length; i++) { if (i === 0) c.quadraticCurveTo(pts[0].x / 2, midY, pts[0].x, pts[0].y); else { const p = pts[i - 1]; c.quadraticCurveTo(p.x, p.y, (p.x + pts[i].x) / 2, (p.y + pts[i].y) / 2) } }
                    const last = pts[9]; c.quadraticCurveTo(last.x, last.y, width, midY); c.stroke()
                    // the diamonds
                    for (let i = 0; i < 10; i++) { const p = pts[i]; c.fillStyle = i === drag.band ? "#fff" : "#ffb020"; c.beginPath(); c.moveTo(p.x, p.y - 7); c.lineTo(p.x + 7, p.y); c.lineTo(p.x, p.y + 7); c.lineTo(p.x - 7, p.y); c.closePath(); c.fill(); c.strokeStyle = "#000"; c.lineWidth = 1; c.stroke()
                        c.fillStyle = "#fbbf24"; c.fillText(page.dbText(page.gains[i]), p.x - 14, p.y - 11) } }
                MouseArea { id: drag; anchors.fill: parent; property int band: -1
                    function bandAt(x) { const step = width / 11; let best = 0, bd = 1e9; for (let i = 0; i < 10; i++) { const d = Math.abs(x - step * (i + 1)); if (d < bd) { bd = d; best = i } } return best }
                    onPressed: function (m) { band = bandAt(m.x); page.band = band; move(m) } onPositionChanged: function (m) { if (pressed) move(m) } onReleased: { band = -1; curve.requestPaint() }
                    function move(m) { const midY = height / 2; page.setBand(band, (midY - m.y) / (height * 0.42) * 12) }
                    onWheel: function (w) { const b = bandAt(w.x); page.setBand(b, page.gains[b] + (w.angleDelta.y > 0 ? 0.5 : -0.5)) } } }
            Row { anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 8; spacing: 6
                Text { text: "◆ DIAMOND PEQ RESPONSE CURVE"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 7; font.bold: true; font.letterSpacing: 1 }
                Text { text: "•"; color: Theme.dim; font.pixelSize: 7 }
                Text { text: "REAL-TIME 64-BIN FFT SPECTRUM ANALYZER"; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 7; font.bold: true; font.letterSpacing: 1 } }
            Row { anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8; spacing: 6
                Text { anchors.verticalCenter: parent.verticalCenter; text: "RTA ENGINE: " + (page.rtaOn ? (page.frozen ? "FROZEN" : "ACTIVE (60 FPS)") : "OFF"); color: page.rtaOn && !page.frozen ? Theme.emerald : Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.bold: true }
                Btn { label: page.rtaOn ? "RTA: ON" : "RTA: OFF"; on: page.rtaOn; implicitHeight: 18; onClicked: page.rtaOn = !page.rtaOn }
                Btn { label: "❄ FREEZE"; on: page.frozen; onColor: Theme.cyan; implicitHeight: 18; onClicked: page.frozen = !page.frozen } } }
        // 2. the ten ISO faders
        Rectangle { visible: page.view !== "diamond"; Layout.fillWidth: true; implicitHeight: page.view === "both" ? 190 : 330; radius: Theme.radius; color: Theme.card; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.margins: 10; spacing: 6
                Repeater { model: 10
                    delegate: ColumnLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 2
                        Text { Layout.alignment: Qt.AlignHCenter; text: page.bandText[index] + "Hz"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                        Slider { id: sl; Layout.fillHeight: true; Layout.alignment: Qt.AlignHCenter; orientation: Qt.Vertical; from: -12; to: 12; stepSize: 0.5; value: page.gains[index]; onMoved: page.setBand(index, value)
                            background: Rectangle { x: sl.leftPadding + sl.availableWidth / 2 - 3; y: sl.topPadding; width: 6; height: sl.availableHeight; radius: 3; color: "#05070a"; border.color: Theme.lineSoft
                                Rectangle { width: parent.width; y: Math.min(parent.height / 2, sl.visualPosition * parent.height); height: Math.abs(parent.height / 2 - sl.visualPosition * parent.height); radius: 3; color: page.gains[index] >= 0 ? Theme.amber : Theme.cyan } }
                            handle: Rectangle { x: sl.leftPadding + sl.availableWidth / 2 - width / 2; y: sl.topPadding + sl.visualPosition * (sl.availableHeight - height); width: 26; height: 14; radius: 3; color: "#1b2230"; border.color: Theme.ink; Rectangle { anchors.centerIn: parent; width: 18; height: 2; color: Theme.amber } } }
                        Text { Layout.alignment: Qt.AlignHCenter; text: page.dbText(page.gains[index]); color: page.gains[index] !== 0 ? Theme.amber : Theme.dim; font.family: Theme.mono; font.pixelSize: 8 } } } } }
        // 2b. PARAMETRIC: the selected band's type, frequency, Q and gain as sliders (the diamond moves the gain; this moves everything)
        Rectangle { Layout.fillWidth: true; implicitHeight: par.implicitHeight + 20; radius: Theme.radius; color: Theme.card; border.color: Theme.line
            ColumnLayout { id: par; anchors.fill: parent; anchors.margins: 10; spacing: 6
                RowLayout { Layout.fillWidth: true; spacing: 6
                    Text { text: "PARAMETRIC CONTROL"; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
                    Text { text: "band"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
                    Repeater { model: 10; delegate: Rectangle { implicitWidth: 30; implicitHeight: 20; radius: 4; color: page.band === index ? Theme.amber : Qt.rgba(1, 1, 1, 0.08); border.color: page.gains[index] !== 0 ? Theme.cyan : "transparent"
                        Text { anchors.centerIn: parent; text: "B" + (index + 1); color: page.band === index ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.band = index } } }
                    Item { Layout.fillWidth: true }
                    Text { text: page.typeNames[page.bandNow.type] + " · " + Math.round(page.bandNow.freq) + " Hz · Q " + Number(page.bandNow.q).toFixed(2) + " · " + page.dbText(page.bandNow.gainDb); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8 } }
                GridLayout { columns: 4; columnSpacing: 10; rowSpacing: 2; Layout.fillWidth: true
                    Text { text: "TYPE"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                    Text { text: "FREQUENCY  " + Math.round(page.bandNow.freq) + " Hz"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                    Text { text: "Q  " + Number(page.bandNow.q).toFixed(2) + "  (bandwidth " + (page.bandNow.q > 0 ? (1.44 / page.bandNow.q).toFixed(2) : "-") + " oct)"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                    Text { text: "GAIN  " + page.dbText(page.bandNow.gainDb); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                    ComboBox { Layout.fillWidth: true; implicitHeight: 24; font.family: Theme.mono; font.pixelSize: 8; model: page.typeNames; currentIndex: page.bandNow.type; onActivated: page.setParam(currentIndex, page.bandNow.freq, page.bandNow.q, page.bandNow.gainDb) }
                    Slider { Layout.fillWidth: true; from: Math.log(20); to: Math.log(20000); value: Math.log(Math.max(20, page.bandNow.freq)); onMoved: page.setParam(page.bandNow.type || 1, Math.exp(value), page.bandNow.q, page.bandNow.gainDb) }
                    Slider { Layout.fillWidth: true; from: 0.1; to: 12; value: page.bandNow.q; onMoved: page.setParam(page.bandNow.type || 1, page.bandNow.freq, value, page.bandNow.gainDb) }
                    Slider { Layout.fillWidth: true; from: -12; to: 12; stepSize: 0.5; value: page.bandNow.gainDb; onMoved: page.setParam(page.bandNow.type || 1, page.bandNow.freq, page.bandNow.q, value) } }
                RowLayout { spacing: 6
                    Btn { label: "ISO CENTRE"; onClicked: page.setParam(page.bandNow.type || 1, page.bands[page.band], 1.41, page.bandNow.gainDb) }
                    Btn { label: "Q 1.41 (2/3 oct)"; onClicked: page.setParam(page.bandNow.type || 1, page.bandNow.freq, 1.41, page.bandNow.gainDb) }
                    Btn { label: "BAND OFF"; onClicked: page.setParam(0, page.bandNow.freq, page.bandNow.q, 0) }
                    Text { text: "A band moved off its ISO centre keeps its gain on the diamond; the FREQUENCY and Q here are the band's real ones (the same ten bands as TONE (EQ))."; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } } } }
        // 3. CUSTOM PRESET USER SLOTS
        Rectangle { Layout.fillWidth: true; implicitHeight: slots.implicitHeight + 20; radius: Theme.radius; color: Theme.card; border.color: Theme.line
            ColumnLayout { id: slots; anchors.fill: parent; anchors.margins: 10; spacing: 6
                RowLayout { Layout.fillWidth: true; spacing: 8
                    Text { text: "CUSTOM PRESET USER SLOTS: 10"; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
                    Item { Layout.fillWidth: true }
                    Rectangle { Layout.preferredWidth: 180; implicitHeight: 22; radius: 4; color: "#05070a"; border.color: nameIn.activeFocus ? Theme.cyan : Theme.lineSoft
                        TextInput { id: nameIn; anchors.fill: parent; anchors.margins: 5; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; selectByMouse: true; maximumLength: 24
                            Text { visible: !parent.text.length; text: "name for SAVE"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9 } } }
                    Btn { label: "▲"; onClicked: page.contentY = 0 } Btn { label: "▼"; onClicked: page.contentY = Math.max(0, page.contentHeight - page.height) }
                    Text { text: "LOAD • SAVE • DELETE • SCROLL THE LIST"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
                GridLayout { Layout.fillWidth: true; columns: 5; columnSpacing: 6; rowSpacing: 6
                    Repeater { model: (page.slotTick, engine.geqSlots())
                        delegate: Rectangle { Layout.fillWidth: true; implicitHeight: 78; radius: 6; color: page.slotSel === modelData.slot ? Qt.rgba(0, 0.95, 1, 0.12) : Qt.rgba(1, 1, 1, 0.04); border.color: page.slotSel === modelData.slot ? Theme.cyan : Theme.lineSoft
                            Canvas { anchors.fill: parent; anchors.margins: 4; opacity: 0.5; property var g: modelData.gains; onGChanged: requestPaint()
                                onPaint: { const c = getContext("2d"); c.reset(); if (modelData.empty) return; c.strokeStyle = "#00f2ff"; c.lineWidth = 1.5; c.beginPath(); for (let i = 0; i < 10; i++) { const x = (i + 0.5) / 10 * width, y = height / 2 - g[i] / 12 * height * 0.4; if (i === 0) c.moveTo(x, y); else c.lineTo(x, y) } c.stroke() } }
                            ColumnLayout { anchors.fill: parent; anchors.margins: 7; spacing: 2
                                RowLayout { Layout.fillWidth: true; Text { text: "SLOT " + modelData.slot; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 6; font.bold: true } Item { Layout.fillWidth: true } Text { text: modelData.empty ? "EMPTY" : modelData.gains.map(function (v) { return (v > 0 ? "+" : "") + v }).join(" "); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 5; elide: Text.ElideRight; Layout.maximumWidth: 150 } }
                                Text { Layout.fillWidth: true; elide: Text.ElideRight; text: modelData.name; color: modelData.empty ? Theme.dim : Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                                Item { Layout.fillHeight: true }
                                RowLayout { spacing: 4
                                    Btn { label: "LOAD"; implicitHeight: 18; onColor: Theme.emerald; on: !modelData.empty; onClicked: { if (!modelData.empty) { page.apply(modelData.gains); page.presetName = modelData.name; page.slotSel = modelData.slot } } }
                                    Btn { label: "SAVE"; implicitHeight: 18; onClicked: { engine.saveGeqSlot(modelData.slot, nameIn.text.length ? nameIn.text : modelData.name, page.gains); page.slotSel = modelData.slot; nameIn.text = "" } }
                                    Btn { label: "DELETE"; implicitHeight: 18; offColor: Qt.rgba(1, 0.165, 0.373, 0.15); onClicked: { engine.clearGeqSlot(modelData.slot); if (page.slotSel === modelData.slot) page.slotSel = 0 } } } }
                            MouseArea { anchors.fill: parent; z: -1; onClicked: page.slotSel = modelData.slot } } } }
                Text { Layout.fillWidth: true; text: "The ten bands are the strip's own equaliser (peaking, Q 1.41 on the ISO centres), the same ten that TONE (EQ) and MIC EQ edit; the spectrum is the strip after its DSP, from the engine's analyser. Golden EQ and Anix W-Shape are the AmF X1 beta curves."; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap } } }
    }
}
