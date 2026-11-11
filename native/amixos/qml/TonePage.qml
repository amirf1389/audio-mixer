// TONE (EQ): the channel DSP of one strip: 10-band parametric EQ with the live response curve (RTA under it), crossover, delay, polarity, channel
// limiter, and EQ PRESETS: 10 locked factory curves (AmiX W Shape, AmiX Golden EQ, ...) and 10 custom slots saved from the strip. Scrolls.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Flickable {
    id: page
    objectName: "tonePage"
    property bool rta: true       // real-time analyser: the strip's 1/3-octave spectrum after its DSP, under the EQ curve
    property var spec: []
    Timer { interval: 60; running: page.visible && page.rta; repeat: true; onTriggered: { page.spec = engine.spectrum(2); canvas.requestPaint() } }
    onRtaChanged: { if (rta) engine.setScopeStrip(strip); canvas.requestPaint() }
    property int strip: engine.selectedStrip
    property int band: 0
    property var curve: []
    property var bandNow: ({ type: 0, freq: 1000, q: 1, gainDb: 0 })
    property var ch: ({})
    contentHeight: col.implicitHeight + 24
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    property int presetTick: 0
    property int presetSel: 0
    property string presetName: ""
    Connections { target: engine; function onStripsChanged() { page.presetTick++ } }
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
            title: i18n.t("TONE (EQ) • MIC / CHANNEL EQ", i18n.lang)
            subtitle: "Per channel: polarity → delay → crossover → 10-band parametric EQ → limiter. Double precision inside the engine; changes land at the next audio block."
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Text { text: i18n.t("CHANNEL", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 }
                Combo {
                    implicitWidth: 150; model: engine.strips; currentIndex: page.strip
                    displayText: engine.stripState[currentIndex] ? engine.stripState[currentIndex].name : ""
                    delegate: ItemDelegate { width: parent.width; text: engine.stripState[index] ? engine.stripState[index].name : index; font.family: Theme.mono }
                    onActivated: page.strip = currentIndex
                }
                Item { Layout.fillWidth: true }
                Rectangle { implicitWidth: 44; implicitHeight: 22; radius: 4; color: page.rta ? Theme.cyan : Theme.ov(0.08); border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: i18n.t("RTA", i18n.lang); color: page.rta ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.rta = !page.rta } ToolTip.visible: rtaH.hovered; ToolTip.text: i18n.t("Real-time analyser under the curve: this strip after its DSP", i18n.lang); HoverHandler { id: rtaH } }
                Rectangle { implicitWidth: 60; implicitHeight: 22; radius: 4; color: page.ch.polarityInvert ? Theme.violet : Theme.ov(0.08); Text { anchors.centerIn: parent; text: "Ø 180°"; color: page.ch.polarityInvert ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setPolarity(page.strip, !page.ch.polarityInvert) } }
            }
            // the curve
            Canvas {
                id: canvas
                Layout.fillWidth: true
                implicitHeight: 220
                onPaint: {
                    var c = getContext("2d"); c.reset(); c.fillStyle = Theme.deep; c.fillRect(0, 0, width, height);
                    var L = 40, R = width - 10, T = 10, B = height - 22, dbTop = 24, dbBot = -24;
                    if (page.rta && page.spec && page.spec.length === 31) {   // RTA: 31 bands 20 Hz .. 20 kHz, 0 .. -72 dBFS over the full height
                        var bw = (R - L) / 31; for (var k = 0; k < 31; k++) { var sdb = Math.max(-72, Math.min(0, page.spec[k])); var bh = (sdb + 72) / 72 * (B - T); c.fillStyle = sdb > -6 ? "rgba(251,191,36,0.5)" : "rgba(0,242,255,0.35)"; c.fillRect(L + k * bw + 1, B - bh, bw - 2, bh) }
                        c.fillStyle = Theme.ov(0.5); c.font = "7px monospace"; c.fillText("RTA 1/3 oct  0 .. -72 dBFS", L + 4, T + 10) }
                    function fx(f) { return L + (Math.log(f / 20) / Math.log(1000)) * (R - L) }
                    function fy(db) { return T + (dbTop - db) / (dbTop - dbBot) * (B - T) }
                    c.strokeStyle = Theme.ov(0.08); c.lineWidth = 1; c.fillStyle = "#64748b"; c.font = "8px monospace";
                    [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000].forEach(function (f) { c.beginPath(); c.moveTo(fx(f), T); c.lineTo(fx(f), B); c.stroke(); c.fillText(f >= 1000 ? (f / 1000) + "k" : f, fx(f) - 8, height - 8) });
                    [-24, -12, -6, 0, 6, 12, 24].forEach(function (d) { c.strokeStyle = d === 0 ? "rgba(251,191,36,0.5)" : Theme.ov(0.08); c.beginPath(); c.moveTo(L, fy(d)); c.lineTo(R, fy(d)); c.stroke(); c.fillText((d > 0 ? "+" : "") + d, 6, fy(d) + 3) });
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
                Repeater { model: 10; delegate: Rectangle { implicitWidth: 34; implicitHeight: 22; radius: 4; color: page.band === index ? Theme.amber : Theme.ov(0.08); border.color: engine.eqBand(page.strip, index).type > 0 ? Theme.cyan : "transparent"; Text { anchors.centerIn: parent; text: "B" + (index + 1); color: page.band === index ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.band = index } } }
            }
            GridLayout {
                columns: 4; columnSpacing: 10; rowSpacing: 4; Layout.fillWidth: true
                Text { text: i18n.t("TYPE", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Text { text: i18n.t("FREQUENCY  ", i18n.lang) + Math.round(page.bandNow.freq) + i18n.t(" Hz", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Text { text: "Q  " + Number(page.bandNow.q).toFixed(2); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Text { text: i18n.t("GAIN  ", i18n.lang) + Theme.dbText(page.bandNow.gainDb) + i18n.t(" dB", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Combo { Layout.fillWidth: true; model: page.typeNames; currentIndex: page.bandNow.type; onActivated: engine.setEq(page.strip, page.band, currentIndex, page.bandNow.freq, page.bandNow.q, page.bandNow.gainDb) }
                Slider { Layout.fillWidth: true; from: Math.log(20); to: Math.log(20000); value: Math.log(Math.max(20, page.bandNow.freq)); onMoved: engine.setEq(page.strip, page.band, page.bandNow.type || 1, Math.exp(value), page.bandNow.q, page.bandNow.gainDb) }
                Slider { Layout.fillWidth: true; from: 0.1; to: 12; value: page.bandNow.q; onMoved: engine.setEq(page.strip, page.band, page.bandNow.type || 1, page.bandNow.freq, value, page.bandNow.gainDb) }
                Slider { Layout.fillWidth: true; from: -24; to: 24; value: page.bandNow.gainDb; onMoved: engine.setEq(page.strip, page.band, page.bandNow.type || 1, page.bandNow.freq, page.bandNow.q, value) }
            }
        }
        Panel {
            Layout.fillWidth: true
            title: i18n.t("EQ PRESETS  ·  10 LOCKED + 10 CUSTOM", i18n.lang)
            subtitle: "Click a preset to load its bands and high-pass / low-pass onto CH " + (page.strip + 1) + ". The first ten are the factory set and cannot be changed (AmiX W Shape, AmiX Golden EQ...); the ten CUSTOM slots take SAVE from this channel. Editing a band clears the preset mark."
            Flow { Layout.fillWidth: true; spacing: 4
                Repeater { model: (page.presetTick, engine.eqPresets())
                    delegate: Rectangle { property bool cur: (page.presetTick, engine.stripEqPreset(page.strip) === modelData.slot); property bool picked: page.presetSel === modelData.slot
                        implicitWidth: 128; implicitHeight: 46; radius: 6; color: cur ? (modelData.locked ? Theme.amber : Theme.cyan) : Theme.ov(0.05); border.color: picked ? Theme.ink : cur ? "transparent" : (modelData.locked ? Qt.rgba(0.984, 0.749, 0.141, 0.35) : Qt.rgba(0, 0.95, 1, 0.3)); border.width: picked ? 1.5 : 1
                        Canvas { anchors.fill: parent; anchors.margins: 3; opacity: 0.55; property var cv: (page.presetTick, modelData.empty ? [] : engine.eqPresetCurve(modelData.slot, 40)); onCvChanged: requestPaint()
                            onPaint: { const c = getContext("2d"); c.reset(); const v = cv; if (!v || v.length < 2) return; c.strokeStyle = cur ? "#000" : (modelData.locked ? "#fbbf24" : "#00f2ff"); c.lineWidth = 1.2; c.beginPath(); for (let i = 0; i < v.length; i++) { const x = i / (v.length - 1) * width, y = height / 2 - Math.max(-12, Math.min(12, v[i].db)) / 12 * (height / 2 - 2); if (i === 0) c.moveTo(x, y); else c.lineTo(x, y) } c.stroke() } }
                        ColumnLayout { anchors.fill: parent; anchors.margins: 6; spacing: 1
                            RowLayout { Layout.fillWidth: true; spacing: 3
                                Text { text: modelData.locked ? "🔒" : (modelData.empty ? "＋" : "✎"); color: cur ? "#000" : Theme.dim; font.pixelSize: 7 }
                                Text { Layout.fillWidth: true; elide: Text.ElideRight; text: modelData.name; color: cur ? "#000" : (modelData.empty ? Theme.dim : Theme.ink); font.family: Theme.mono; font.pixelSize: 8; font.bold: true } }
                            Item { Layout.fillHeight: true }
                            Text { text: (modelData.locked ? i18n.t("FACTORY ", i18n.lang) : i18n.t("CUSTOM ", i18n.lang)) + modelData.slot + (modelData.empty ? i18n.t(" · empty", i18n.lang) : " · " + modelData.bands + i18n.t(" bands", i18n.lang)); color: cur ? "#222" : Theme.dim; font.family: Theme.mono; font.pixelSize: 6 } }
                        MouseArea { anchors.fill: parent; onClicked: { page.presetSel = modelData.slot; if (!modelData.empty) engine.applyEqPreset(modelData.slot, page.strip) }
                            onDoubleClicked: { if (!modelData.locked) { page.presetSel = modelData.slot; nameIn.forceActiveFocus() } } } } } }
            RowLayout { Layout.fillWidth: true; spacing: 6
                Text { text: i18n.t("SAVE INTO", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Text { text: page.presetSel > 10 ? i18n.t("CUSTOM ", i18n.lang) + (page.presetSel - 10) : page.presetSel > 0 ? i18n.t("a locked preset (pick a CUSTOM slot)", i18n.lang) : i18n.t("pick a CUSTOM slot above", i18n.lang); color: page.presetSel > 10 ? Theme.cyan : Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                Rectangle { Layout.preferredWidth: 200; implicitHeight: 22; radius: 4; color: Theme.deep; border.color: nameIn.activeFocus ? Theme.cyan : Theme.lineSoft
                    TextInput { id: nameIn; anchors.fill: parent; anchors.margins: 5; text: page.presetName; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; selectByMouse: true; maximumLength: 24; onTextChanged: page.presetName = text
                        Text { visible: !parent.text.length; text: i18n.t("preset name", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9 } } }
                Rectangle { implicitWidth: 110; implicitHeight: 22; radius: 4; color: page.presetSel > 10 ? Theme.cyan : Theme.ov(0.06); Text { anchors.centerIn: parent; text: i18n.t("SAVE CH ", i18n.lang) + (page.strip + 1) + i18n.t(" EQ", i18n.lang); color: page.presetSel > 10 ? "#000" : Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                    MouseArea { anchors.fill: parent; enabled: page.presetSel > 10; onClicked: { engine.saveEqPreset(page.presetSel, page.presetName, page.strip); page.presetName = "" } } }
                Rectangle { implicitWidth: 60; implicitHeight: 22; radius: 4; color: Theme.ov(0.06); border.color: Theme.lineSoft; visible: page.presetSel > 10; Text { anchors.centerIn: parent; text: i18n.t("CLEAR", i18n.lang); color: Theme.danger; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                    MouseArea { anchors.fill: parent; onClicked: engine.clearEqPreset(page.presetSel) } }
                Item { Layout.fillWidth: true }
                Rectangle { implicitWidth: 90; implicitHeight: 22; radius: 4; color: Theme.ov(0.06); border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: i18n.t("FLAT EQ", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                    MouseArea { anchors.fill: parent; onClicked: { for (let b = 0; b < 10; ++b) engine.setEq(page.strip, b, 0, engine.eqBand(page.strip, b).freq, 1, 0) } } } }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 10
            Panel {
                id: xo
                Layout.fillWidth: true
                title: i18n.t("CROSSOVER", i18n.lang)
                subtitle: "Linkwitz-Riley, Butterworth or Bessel, 6 to 48 dB per octave"
                readonly property var types: ["BUTTERWORTH", "LINKWITZ-RILEY", "BESSEL"]
                readonly property var slopes: ["OFF", "6 dB", "12 dB", "18 dB", "24 dB", "30 dB", "36 dB", "42 dB", "48 dB"]
                Combo { Layout.fillWidth: true; model: xo.types; currentIndex: page.ch.xoverType === undefined ? 1 : page.ch.xoverType; onActivated: engine.setCrossover(page.strip, currentIndex, page.ch.hpOrder, page.ch.hpFreq, page.ch.lpOrder, page.ch.lpFreq) }
                Text { text: i18n.t("HIGH PASS  ", i18n.lang) + Math.round(page.ch.hpFreq || 80) + i18n.t(" Hz", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                RowLayout { Combo { implicitWidth: 90; model: xo.slopes; currentIndex: page.ch.hpOrder || 0; onActivated: engine.setCrossover(page.strip, page.ch.xoverType, currentIndex, page.ch.hpFreq, page.ch.lpOrder, page.ch.lpFreq) }
                    Slider { Layout.fillWidth: true; from: Math.log(20); to: Math.log(20000); value: Math.log(page.ch.hpFreq || 80); onMoved: engine.setCrossover(page.strip, page.ch.xoverType, page.ch.hpOrder, Math.exp(value), page.ch.lpOrder, page.ch.lpFreq) } }
                Text { text: i18n.t("LOW PASS  ", i18n.lang) + Math.round(page.ch.lpFreq || 4000) + i18n.t(" Hz", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                RowLayout { Combo { implicitWidth: 90; model: xo.slopes; currentIndex: page.ch.lpOrder || 0; onActivated: engine.setCrossover(page.strip, page.ch.xoverType, page.ch.hpOrder, page.ch.hpFreq, currentIndex, page.ch.lpFreq) }
                    Slider { Layout.fillWidth: true; from: Math.log(20); to: Math.log(20000); value: Math.log(page.ch.lpFreq || 4000); onMoved: engine.setCrossover(page.strip, page.ch.xoverType, page.ch.hpOrder, page.ch.hpFreq, page.ch.lpOrder, Math.exp(value)) } }
            }
            Panel {
                Layout.fillWidth: true
                title: i18n.t("TIME & PROTECTION", i18n.lang)
                subtitle: "Delay for time alignment (sub-sample), channel peak limiter"
                Text { text: i18n.t("DELAY  ", i18n.lang) + Number(page.ch.delayMs || 0).toFixed(2) + i18n.t(" ms  (", i18n.lang) + (Number(page.ch.delayMs || 0) * 34.3).toFixed(1) + i18n.t(" cm)", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Slider { Layout.fillWidth: true; from: 0; to: 50; stepSize: 0.01; value: page.ch.delayMs || 0; onMoved: engine.setDelay(page.strip, value) }
                RowLayout { Layout.fillWidth: true
                    Rectangle { implicitWidth: 70; implicitHeight: 22; radius: 4; color: page.ch.limiterOn ? Theme.emerald : Theme.ov(0.08); Text { anchors.centerIn: parent; text: i18n.t("LIMITER", i18n.lang); color: page.ch.limiterOn ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setChannelLimiter(page.strip, !page.ch.limiterOn, page.ch.limiterCeilDb || -1, page.ch.limiterReleaseMs || 80) } }
                    Text { text: i18n.t("CEILING ", i18n.lang) + Theme.dbText(page.ch.limiterCeilDb === undefined ? -1 : page.ch.limiterCeilDb) + i18n.t(" dBFS", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } }
                Slider { Layout.fillWidth: true; from: -30; to: 0; value: page.ch.limiterCeilDb === undefined ? -1 : page.ch.limiterCeilDb; onMoved: engine.setChannelLimiter(page.strip, page.ch.limiterOn, value, page.ch.limiterReleaseMs || 80) }
                Text { text: i18n.t("RELEASE ", i18n.lang) + Math.round(page.ch.limiterReleaseMs || 80) + i18n.t(" ms", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Slider { Layout.fillWidth: true; from: 1; to: 1000; value: page.ch.limiterReleaseMs || 80; onMoved: engine.setChannelLimiter(page.strip, page.ch.limiterOn, page.ch.limiterCeilDb || -1, value) }
            }
        }
    }
}
