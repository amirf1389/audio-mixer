// TONE (EQ): the channel DSP of one strip: 10-band parametric EQ with the live response curve, crossover, delay, polarity, channel limiter
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Flickable {
    id: page
    property int strip: 0
    property int band: 0
    property var curve: []
    property var bandNow: ({ type: 0, freq: 1000, q: 1, gainDb: 0 })
    property var ch: ({})
    contentHeight: col.implicitHeight + 24
    clip: true
    function reload() { curve = engine.responseCurve(strip, 160); bandNow = engine.eqBand(strip, band); ch = engine.channelParams(strip); canvas.requestPaint() }
    Component.onCompleted: reload()
    onStripChanged: reload()
    onBandChanged: reload()
    Connections { target: engine; function onStripsChanged() { page.reload() } }
    readonly property var typeNames: ["OFF", "PEAK", "LOW SHELF", "HIGH SHELF", "LOW PASS", "HIGH PASS", "NOTCH", "ALL-PASS 1", "ALL-PASS 2"]
    ColumnLayout {
        id: col
        width: parent.width - 20
        x: 10; y: 10
        spacing: 10
        Panel {
            Layout.fillWidth: true
            title: "TONE (EQ) • CHANNEL DSP"
            subtitle: "Per channel: polarity → delay → crossover → 10-band parametric EQ → limiter. Double precision inside the engine; changes land at the next audio block."
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Text { text: "CHANNEL"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 }
                ComboBox {
                    implicitWidth: 150; model: engine.strips; currentIndex: page.strip
                    displayText: engine.stripState[currentIndex] ? engine.stripState[currentIndex].name : ""
                    delegate: ItemDelegate { width: parent.width; text: engine.stripState[index] ? engine.stripState[index].name : index; font.family: Theme.mono }
                    onActivated: page.strip = currentIndex
                }
                Item { Layout.fillWidth: true }
                Rectangle { implicitWidth: 60; implicitHeight: 22; radius: 4; color: page.ch.polarityInvert ? Theme.violet : Qt.rgba(1, 1, 1, 0.08); Text { anchors.centerIn: parent; text: "Ø 180°"; color: page.ch.polarityInvert ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setPolarity(page.strip, !page.ch.polarityInvert) } }
            }
            // the curve
            Canvas {
                id: canvas
                Layout.fillWidth: true
                implicitHeight: 220
                onPaint: {
                    var c = getContext("2d"); c.reset(); c.fillStyle = "#05070a"; c.fillRect(0, 0, width, height);
                    var L = 40, R = width - 10, T = 10, B = height - 22, dbTop = 24, dbBot = -24;
                    function fx(f) { return L + (Math.log(f / 20) / Math.log(1000)) * (R - L) }
                    function fy(db) { return T + (dbTop - db) / (dbTop - dbBot) * (B - T) }
                    c.strokeStyle = "rgba(255,255,255,0.08)"; c.lineWidth = 1; c.fillStyle = "#64748b"; c.font = "8px monospace";
                    [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000].forEach(function (f) { c.beginPath(); c.moveTo(fx(f), T); c.lineTo(fx(f), B); c.stroke(); c.fillText(f >= 1000 ? (f / 1000) + "k" : f, fx(f) - 8, height - 8) });
                    [-24, -12, -6, 0, 6, 12, 24].forEach(function (d) { c.strokeStyle = d === 0 ? "rgba(251,191,36,0.5)" : "rgba(255,255,255,0.08)"; c.beginPath(); c.moveTo(L, fy(d)); c.lineTo(R, fy(d)); c.stroke(); c.fillText((d > 0 ? "+" : "") + d, 6, fy(d) + 3) });
                    if (page.curve.length) {
                        c.beginPath(); c.strokeStyle = "#00f2ff"; c.lineWidth = 2;
                        for (var i = 0; i < page.curve.length; i++) { var p = page.curve[i], x = fx(p.f), y = fy(Math.max(dbBot - 2, Math.min(dbTop + 2, p.db))); if (i === 0) c.moveTo(x, y); else c.lineTo(x, y) }
                        c.stroke();
                        c.lineTo(fx(page.curve[page.curve.length - 1].f), fy(0)); c.lineTo(fx(page.curve[0].f), fy(0)); c.closePath(); c.fillStyle = "rgba(0,242,255,0.08)"; c.fill();
                    }
                    var b = page.bandNow; if (b && b.type > 0) { c.fillStyle = "#fbbf24"; c.beginPath(); c.arc(fx(b.freq), fy(Math.max(dbBot, Math.min(dbTop, b.gainDb))), 5, 0, 6.283); c.fill() }
                }
                MouseArea {   // click to place the selected band
                    anchors.fill: parent
                    onPressed: function (m) { move(m) } onPositionChanged: function (m) { if (pressed) move(m) }
                    function move(m) { var L = 40, R = canvas.width - 10, T = 10, B = canvas.height - 22; var f = 20 * Math.pow(1000, Math.max(0, Math.min(1, (m.x - L) / (R - L)))); var db = 24 - Math.max(0, Math.min(1, (m.y - T) / (B - T))) * 48; var b = page.bandNow; engine.setEq(page.strip, page.band, b.type === 0 ? 1 : b.type, f, b.q, db) }
                }
            }
            // band picker
            RowLayout {
                Layout.fillWidth: true; spacing: 4
                Repeater { model: 10; delegate: Rectangle { implicitWidth: 34; implicitHeight: 22; radius: 4; color: page.band === index ? Theme.amber : Qt.rgba(1, 1, 1, 0.08); border.color: engine.eqBand(page.strip, index).type > 0 ? Theme.cyan : "transparent"; Text { anchors.centerIn: parent; text: "B" + (index + 1); color: page.band === index ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.band = index } } }
            }
            GridLayout {
                columns: 4; columnSpacing: 10; rowSpacing: 4; Layout.fillWidth: true
                Text { text: "TYPE"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Text { text: "FREQUENCY  " + Math.round(page.bandNow.freq) + " Hz"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Text { text: "Q  " + Number(page.bandNow.q).toFixed(2); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Text { text: "GAIN  " + Theme.dbText(page.bandNow.gainDb) + " dB"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                ComboBox { Layout.fillWidth: true; model: page.typeNames; currentIndex: page.bandNow.type; onActivated: engine.setEq(page.strip, page.band, currentIndex, page.bandNow.freq, page.bandNow.q, page.bandNow.gainDb) }
                Slider { Layout.fillWidth: true; from: Math.log(20); to: Math.log(20000); value: Math.log(Math.max(20, page.bandNow.freq)); onMoved: engine.setEq(page.strip, page.band, page.bandNow.type || 1, Math.exp(value), page.bandNow.q, page.bandNow.gainDb) }
                Slider { Layout.fillWidth: true; from: 0.1; to: 12; value: page.bandNow.q; onMoved: engine.setEq(page.strip, page.band, page.bandNow.type || 1, page.bandNow.freq, value, page.bandNow.gainDb) }
                Slider { Layout.fillWidth: true; from: -24; to: 24; value: page.bandNow.gainDb; onMoved: engine.setEq(page.strip, page.band, page.bandNow.type || 1, page.bandNow.freq, page.bandNow.q, value) }
            }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 10
            Panel {
                id: xo
                Layout.fillWidth: true
                title: "CROSSOVER"
                subtitle: "Linkwitz-Riley, Butterworth or Bessel, 6 to 48 dB per octave"
                readonly property var types: ["BUTTERWORTH", "LINKWITZ-RILEY", "BESSEL"]
                readonly property var slopes: ["OFF", "6 dB", "12 dB", "18 dB", "24 dB", "30 dB", "36 dB", "42 dB", "48 dB"]
                ComboBox { Layout.fillWidth: true; model: xo.types; currentIndex: page.ch.xoverType === undefined ? 1 : page.ch.xoverType; onActivated: engine.setCrossover(page.strip, currentIndex, page.ch.hpOrder, page.ch.hpFreq, page.ch.lpOrder, page.ch.lpFreq) }
                Text { text: "HIGH PASS  " + Math.round(page.ch.hpFreq || 80) + " Hz"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                RowLayout { ComboBox { implicitWidth: 90; model: xo.slopes; currentIndex: page.ch.hpOrder || 0; onActivated: engine.setCrossover(page.strip, page.ch.xoverType, currentIndex, page.ch.hpFreq, page.ch.lpOrder, page.ch.lpFreq) }
                    Slider { Layout.fillWidth: true; from: Math.log(20); to: Math.log(20000); value: Math.log(page.ch.hpFreq || 80); onMoved: engine.setCrossover(page.strip, page.ch.xoverType, page.ch.hpOrder, Math.exp(value), page.ch.lpOrder, page.ch.lpFreq) } }
                Text { text: "LOW PASS  " + Math.round(page.ch.lpFreq || 4000) + " Hz"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                RowLayout { ComboBox { implicitWidth: 90; model: xo.slopes; currentIndex: page.ch.lpOrder || 0; onActivated: engine.setCrossover(page.strip, page.ch.xoverType, page.ch.hpOrder, page.ch.hpFreq, currentIndex, page.ch.lpFreq) }
                    Slider { Layout.fillWidth: true; from: Math.log(20); to: Math.log(20000); value: Math.log(page.ch.lpFreq || 4000); onMoved: engine.setCrossover(page.strip, page.ch.xoverType, page.ch.hpOrder, page.ch.hpFreq, page.ch.lpOrder, Math.exp(value)) } }
            }
            Panel {
                Layout.fillWidth: true
                title: "TIME & PROTECTION"
                subtitle: "Delay for time alignment (sub-sample), channel peak limiter"
                Text { text: "DELAY  " + Number(page.ch.delayMs || 0).toFixed(2) + " ms  (" + (Number(page.ch.delayMs || 0) * 34.3).toFixed(1) + " cm)"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Slider { Layout.fillWidth: true; from: 0; to: 50; stepSize: 0.01; value: page.ch.delayMs || 0; onMoved: engine.setDelay(page.strip, value) }
                RowLayout { Layout.fillWidth: true
                    Rectangle { implicitWidth: 70; implicitHeight: 22; radius: 4; color: page.ch.limiterOn ? Theme.emerald : Qt.rgba(1, 1, 1, 0.08); Text { anchors.centerIn: parent; text: "LIMITER"; color: page.ch.limiterOn ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setChannelLimiter(page.strip, !page.ch.limiterOn, page.ch.limiterCeilDb || -1, page.ch.limiterReleaseMs || 80) } }
                    Text { text: "CEILING " + Theme.dbText(page.ch.limiterCeilDb === undefined ? -1 : page.ch.limiterCeilDb) + " dBFS"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } }
                Slider { Layout.fillWidth: true; from: -30; to: 0; value: page.ch.limiterCeilDb === undefined ? -1 : page.ch.limiterCeilDb; onMoved: engine.setChannelLimiter(page.strip, page.ch.limiterOn, value, page.ch.limiterReleaseMs || 80) }
                Text { text: "RELEASE " + Math.round(page.ch.limiterReleaseMs || 80) + " ms"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Slider { Layout.fillWidth: true; from: 1; to: 1000; value: page.ch.limiterReleaseMs || 80; onMoved: engine.setChannelLimiter(page.strip, page.ch.limiterOn, page.ch.limiterCeilDb || -1, value) }
            }
        }
    }
}
