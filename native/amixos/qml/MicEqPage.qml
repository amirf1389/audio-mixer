// MIC EQ: the 10 EQ bands of a strip as a graphic equaliser on the ISO octave centres (31.5 Hz .. 16 kHz, Q 1.41) plus a HIGH-PASS, with
// the live 1/3-octave spectrum of that strip after its DSP (cyan) and the EQ's own response (amber). RESET, BYPASS (keeps the gains),
// COPY TO ALL strips and MIC PRESETS. The same bands appear on TONE (EQ) as parametric bands, so both pages edit one equaliser.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "micEqPage"
    property int strip: engine.selectedStrip
    property int bank: 0
    property var gains: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
    property bool bypassed: false
    property var spec: []
    property var curve: []
    property var ch: ({})
    readonly property var st: engine.stripState[strip]
    readonly property var meter: engine.meters[strip]
    readonly property var centres: ["31", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"]
    readonly property var presets: [
        { n: "FLAT", g: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0], hp: 0 },
        { n: "VOCAL CLARITY", g: [-6, -3, -1, -2, 0, 1, 3, 4, 2, 1], hp: 100 },
        { n: "PODCAST", g: [-8, -4, 1, 2, 0, -1, 2, 3, 1, -1], hp: 80 },
        { n: "BROADCAST", g: [-6, -2, 3, 1, -1, 0, 2, 4, 3, 0], hp: 80 },
        { n: "DE-MUD", g: [-4, -3, -2, -5, -3, 0, 0, 0, 0, 0], hp: 60 },
        { n: "AIR", g: [0, 0, 0, 0, 0, 0, 0, 1, 3, 5], hp: 0 },
        { n: "TELEPHONE", g: [-24, -24, -12, 0, 4, 4, 0, -12, -24, -24], hp: 300 }]
    function reload() { gains = engine.graphicEq(strip); ch = engine.channelParams(strip); let any = false; for (let b = 0; b < 10; ++b) if (engine.eqBand(strip, b).type > 0) any = true; let nonzero = false; for (let b = 0; b < 10; ++b) if (Math.abs(gains[b]) > 0.01) nonzero = true; bypassed = nonzero && !any; curve = engine.responseCurve(strip, 120); canvas.requestPaint() }
    function apply(g, on) { gains = g; engine.setGraphicEq(strip, g, on); bypassed = !on; curve = engine.responseCurve(strip, 120); canvas.requestPaint() }
    function setHp(f) { const c = page.ch; engine.setCrossover(strip, c.xoverType || 0, f > 0 ? 2 : 0, f > 0 ? f : (c.hpFreq || 80), c.lpOrder || 0, c.lpFreq || 20000); reload() }
    onStripChanged: { engine.setScopeStrip(strip); reload() }
    Component.onCompleted: { engine.setScopeStrip(strip); reload() }
    Timer { interval: 60; running: page.visible; repeat: true; onTriggered: { page.spec = engine.spectrum(2); canvas.requestPaint() } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Text { text: i18n.t("MIC EQ:", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Repeater { model: ["CH 01-16", "CH 17-32"]; delegate: Rectangle { implicitWidth: 66; implicitHeight: 20; radius: 4; color: page.bank === index ? Theme.ov(0.16) : Theme.ov(0.06); Text { anchors.centerIn: parent; text: modelData; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.bank = index } } }
                Repeater { model: 16; delegate: Rectangle { readonly property int s: page.bank * 16 + index; readonly property var ss: engine.stripState[s]; implicitWidth: 30; implicitHeight: 20; radius: 4; color: page.strip === s ? Theme.amber : Theme.ov(0.06); border.color: ss && ss.dsp ? Theme.cyan : "transparent"
                    Text { anchors.centerIn: parent; text: s + 1; color: page.strip === s ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { page.strip = s; engine.selectedStrip = s } } } }
                Item { Layout.fillWidth: true }
                Text { text: page.st ? (i18n.t("CH ", i18n.lang) + (page.strip + 1) + "  " + page.st.name) : ""; color: Theme.stripColors[page.st ? page.st.color : 0]; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
                // the spectrum + response
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 220; radius: Theme.radius; color: Theme.chrome; border.color: Theme.line; clip: true
                    Canvas {
                        id: canvas; anchors.fill: parent; anchors.margins: 8
                        onPaint: {
                            const ctx = getContext("2d"); const w = width, h = height; ctx.clearRect(0, 0, w, h)
                            ctx.strokeStyle = Theme.ov(0.07); ctx.lineWidth = 1
                            for (let j = 0; j <= 6; ++j) { const y = Math.round(h * j / 6) + 0.5; ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke() }
                            const sp = page.spec; const bw = w / 31
                            if (sp && sp.length === 31) { for (let b = 0; b < 31; ++b) { const db = Math.max(-72, Math.min(0, sp[b])); const bh = (db + 72) / 72 * h; ctx.fillStyle = "rgba(0,242,255,0.45)"; ctx.fillRect(b * bw + 1, h - bh, bw - 2, bh) } }
                            const cv = page.curve; if (cv && cv.length > 1) { ctx.strokeStyle = "#fbbf24"; ctx.lineWidth = 2; ctx.beginPath(); for (let i = 0; i < cv.length; ++i) { const x = w * Math.log(cv[i].f / 20) / Math.log(1000), y = h / 2 - Math.max(-24, Math.min(24, cv[i].db)) / 24 * h / 2; if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y) } ctx.stroke() }
                            ctx.strokeStyle = Theme.ov(0.25); ctx.lineWidth = 1; ctx.beginPath(); ctx.moveTo(0, h / 2 + 0.5); ctx.lineTo(w, h / 2 + 0.5); ctx.stroke()
                        }
                    }
                    Text { anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 10; text: i18n.t("CYAN  the strip after its DSP (1/3-octave, 0 .. -72 dBFS)     AMBER  what the EQ + high-pass do (±24 dB)", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
                    Text { anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 10; text: page.bypassed ? i18n.t("BYPASSED", i18n.lang) : (page.st && page.st.dsp ? i18n.t("EQ ON", i18n.lang) : i18n.t("FLAT", i18n.lang)); color: page.bypassed ? Theme.amber : Theme.emerald; font.family: Theme.mono; font.pixelSize: 7; font.bold: true }
                }
                // the 10 faders
                Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 12; spacing: 4
                        Repeater {
                            model: 10
                            delegate: ColumnLayout {
                                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 4
                                Text { Layout.alignment: Qt.AlignHCenter; text: (page.gains[index] >= 0 ? "+" : "") + Number(page.gains[index]).toFixed(1); color: Math.abs(page.gains[index]) > 0.05 ? Theme.cyan : Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                                Slider { Layout.fillHeight: true; Layout.alignment: Qt.AlignHCenter; orientation: Qt.Vertical; from: -24; to: 24; stepSize: 0.5; value: page.gains[index]
                                    onMoved: { let g = page.gains.slice(); g[index] = value; page.apply(g, true) } }
                                Text { Layout.alignment: Qt.AlignHCenter; text: page.centres[index]; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                            }
                        }
                    }
                }
            }
            // controls
            Rectangle {
                Layout.preferredWidth: 250; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 12; spacing: 8
                    RowLayout { spacing: 6; Layout.fillWidth: true
                        Rectangle { Layout.fillWidth: true; implicitHeight: 24; radius: 4; color: page.bypassed ? Theme.amber : Theme.ov(0.08); border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: i18n.t("BYPASS", i18n.lang); color: page.bypassed ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.apply(page.gains, page.bypassed) } }
                        Rectangle { Layout.fillWidth: true; implicitHeight: 24; radius: 4; color: Theme.ov(0.08); border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: i18n.t("RESET", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { page.apply([0, 0, 0, 0, 0, 0, 0, 0, 0, 0], true); page.setHp(0) } } }
                        Rectangle { Layout.fillWidth: true; implicitHeight: 24; radius: 4; color: Theme.ov(0.08); border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: i18n.t("COPY TO ALL", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.copyEqToAll(page.strip) } }
                    }
                    Text { text: i18n.t("HIGH-PASS (cut rumble / handling noise)", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.letterSpacing: 1 }
                    Flow { Layout.fillWidth: true; spacing: 4
                        Repeater { model: [0, 60, 80, 100, 120, 160, 300]; delegate: Rectangle { readonly property bool on: page.ch.hpOrder > 0 ? Math.abs(page.ch.hpFreq - modelData) < 1 : modelData === 0; implicitWidth: 48; implicitHeight: 22; radius: 4; color: on ? Theme.cyan : Theme.ov(0.06); border.color: Theme.lineSoft
                            Text { anchors.centerIn: parent; text: modelData === 0 ? i18n.t("OFF", i18n.lang) : modelData + i18n.t(" Hz", i18n.lang); color: on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.setHp(modelData) } } } }
                    Text { text: i18n.t("LEVELS (REAL TIME)", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.letterSpacing: 1; Layout.topMargin: 6 }
                    RowLayout { Layout.fillWidth: true; Text { Layout.preferredWidth: 28; text: i18n.t("IN", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } MeterBar { Layout.fillWidth: true; implicitHeight: 10; peakDb: page.meter ? page.meter.inDb : -120 } Text { Layout.preferredWidth: 46; text: page.meter && page.meter.inDb > -119 ? page.meter.inDb.toFixed(1) : i18n.t("-INF", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8; horizontalAlignment: Text.AlignRight } }
                    RowLayout { Layout.fillWidth: true; Text { Layout.preferredWidth: 28; text: i18n.t("OUT", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } MeterBar { Layout.fillWidth: true; implicitHeight: 10; peakDb: page.meter ? page.meter.peakDb : -120; rmsDb: page.meter ? page.meter.rmsDb : -120; clip: page.meter ? page.meter.clip : false } Text { Layout.preferredWidth: 46; text: page.meter && page.meter.peakDb > -119 ? page.meter.peakDb.toFixed(1) : i18n.t("-INF", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8; horizontalAlignment: Text.AlignRight } }
                    Text { text: i18n.t("MIC PRESETS", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.letterSpacing: 1; Layout.topMargin: 6 }
                    Repeater { model: page.presets; delegate: Rectangle { Layout.fillWidth: true; implicitHeight: 24; radius: 4; color: Theme.ov(0.05); border.color: Theme.lineSoft
                        Text { anchors.left: parent.left; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter; text: modelData.n; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                        Text { anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter; text: modelData.hp > 0 ? i18n.t("HP ", i18n.lang) + modelData.hp : ""; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
                        MouseArea { anchors.fill: parent; onClicked: { page.apply(modelData.g, true); page.setHp(modelData.hp) } } } }
                    Item { Layout.fillHeight: true }
                    Text { Layout.fillWidth: true; text: engine.running ? i18n.t("The strip reads device input ", i18n.lang) + (engine.inputRoutes[page.strip] + 1) + i18n.t(" (ROUTING). Use headphones with a live microphone.", i18n.lang) : i18n.t("No device open: open one on CONNECTIONS or LIVE SOURCES; the EQ is stored and applied when it starts.", i18n.lang); color: engine.running ? Theme.dim : Theme.amber; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }
                }
            }
        }
    }
}
