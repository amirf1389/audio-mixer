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
    property int ch3Tick: 0
    property bool graphic: false   // the ten MIC EQ faders shown on this page
    // DUAL LISTEN mode of the channel EQ: the DUAL LISTEN EQ in this channel's rack; its four knobs take the place of the three bands
    readonly property int dualSlot: { ch3Tick; const sl = engine.plugins(strip); for (let k = 0; k < sl.length; ++k) if (sl[k].kind === "fx:dualeq") return k; return -1 }
    readonly property var dualP: { ch3Tick; dualTick; return dualSlot >= 0 ? engine.pluginParams(strip, dualSlot) : [] }
    property int dualTick: 0
    property string eqNote: ""
    readonly property var ch3: { ch3Tick; const c = engine.channelEq(strip); return c.low === undefined ? { low: 0, mid: 0, midFreq: 1000, high: 0, on: false } : c }
    function put3(key, v) { const c = Object.assign({}, ch3); c[key] = v; if (key !== "on") c.on = true; engine.setChannelEq(strip, c.low, c.mid, c.midFreq, c.high, c.on) }
    Connections { target: engine; function onStripsChanged() { page.ch3Tick++ } }
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
        Rectangle {   // CHANNEL EQ: the strip's own three bands. The ten parametric bands below are the MIC EQ rack.
            objectName: "channelEq3"; Layout.fillWidth: true; implicitHeight: 118; radius: Theme.radius; color: Theme.card; border.color: page.ch3.on ? Theme.emerald : Theme.line
            RowLayout { anchors.fill: parent; anchors.margins: 12; spacing: 18
                ColumnLayout { Layout.preferredWidth: 210; spacing: 4
                    Text { text: page.dualSlot >= 0 ? i18n.t("CHANNEL EQ · DUAL LISTEN", i18n.lang) : i18n.t("CHANNEL EQ · 3-BAND", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
                    Row { spacing: 4
                        Repeater { model: [{ t: "3-BAND", dual: false }, { t: "DUAL LISTEN", dual: true }]
                            Rectangle { required property var modelData; readonly property bool on: (page.dualSlot >= 0) === modelData.dual
                                objectName: modelData.dual ? "eqModeDual" : "eqMode3"; width: mT.implicitWidth + 14; height: 18; radius: 3; color: on ? Theme.cyan : Theme.ov(0.08); border.color: Theme.lineSoft
                                Text { id: mT; anchors.centerIn: parent; text: i18n.t(modelData.t, i18n.lang); color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                    onClicked: { if (modelData.dual) { if (page.dualSlot < 0 && engine.addToRack(page.strip, "fx:dualeq") < 0) page.eqNote = i18n.t("The rack of this channel is full.", i18n.lang) } else if (page.dualSlot >= 0) engine.setPlugin(page.strip, page.dualSlot, "") } } } } }
                    Text { text: i18n.t("CH ", i18n.lang) + (page.strip + 1); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: i18n.t("Low, mid and high of this channel. The 10-band MIC EQ is a rack item (PLUG-INS), and RTA EQ is the master's.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
                    Row { spacing: 6
                        Rectangle { objectName: "ch3OnBtn"; width: 52; height: 20; radius: 4; color: page.ch3.on ? Theme.emerald : Theme.ov(0.08); border.color: Theme.lineSoft
                            Text { anchors.centerIn: parent; text: page.ch3.on ? i18n.t("ON", i18n.lang) : i18n.t("OFF", i18n.lang); color: page.ch3.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                            MouseArea { anchors.fill: parent; onClicked: page.put3("on", !page.ch3.on) } }
                        Rectangle { objectName: "ch3FlatBtn"; width: 52; height: 20; radius: 4; color: Theme.ov(0.08); border.color: Theme.lineSoft
                            Text { anchors.centerIn: parent; text: i18n.t("FLAT", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                            MouseArea { anchors.fill: parent; onClicked: engine.setChannelEq(page.strip, 0, 0, 1000, 0, false) } } } }
                Knob { visible: page.dualSlot < 0; label: i18n.t("LOW 100 Hz", i18n.lang); value: (page.ch3.low - (-15)) / 30; defaultValue: 0.5; valueText: (page.ch3.low > 0 ? "+" : "") + Number(page.ch3.low).toFixed(1) + " dB"; onMoved: function (v) { page.put3("low", Math.round((-15 + v * 30) * 2) / 2) } }
                Knob { visible: page.dualSlot < 0; label: i18n.t("MID", i18n.lang); value: (page.ch3.mid - (-15)) / 30; defaultValue: 0.5; valueText: (page.ch3.mid > 0 ? "+" : "") + Number(page.ch3.mid).toFixed(1) + " dB"; onMoved: function (v) { page.put3("mid", Math.round((-15 + v * 30) * 2) / 2) } }
                Knob { visible: page.dualSlot < 0; label: i18n.t("MID FREQ", i18n.lang); value: Math.log(page.ch3.midFreq / 100) / Math.log(80); defaultValue: Math.log(10) / Math.log(80); valueText: Math.round(page.ch3.midFreq) + " Hz"; onMoved: function (v) { page.put3("midFreq", Math.round(100 * Math.pow(80, v))) } }
                Knob { visible: page.dualSlot < 0; label: i18n.t("HIGH 10 kHz", i18n.lang); value: (page.ch3.high - (-15)) / 30; defaultValue: 0.5; valueText: (page.ch3.high > 0 ? "+" : "") + Number(page.ch3.high).toFixed(1) + " dB"; onMoved: function (v) { page.put3("high", Math.round((-15 + v * 30) * 2) / 2) } }
                Repeater { model: page.dualSlot >= 0 ? page.dualP : []
                    Knob { required property var modelData; required property int index
                           label: i18n.t(modelData.title, i18n.lang); value: modelData.value; defaultValue: 0.5; valueText: modelData.text
                           onMoved: function (v) { engine.setPluginParam(page.strip, page.dualSlot, index, v); page.dualTick++ } } }
                Text { visible: page.eqNote !== ""; text: page.eqNote; color: Theme.amber; font.pixelSize: 9 }
                Item { Layout.fillWidth: true } } }
        RowLayout { spacing: 10
            Text { text: i18n.t("DYNAMIC MIC · 10 BANDS", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1.5 }
            Rectangle { objectName: "dynMicBtn"; implicitWidth: dmT.implicitWidth + 20; implicitHeight: 22; radius: 4; color: "transparent"; border.color: Theme.cyan
                Text { id: dmT; anchors.centerIn: parent; text: i18n.t("MIC PRESETS + HIGH-PASS", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                MouseArea { anchors.fill: parent; onClicked: page.Window.window.current = "miceq" } } }
        Rectangle {   // MIC EQ and TONE in one place: the same ten bands as ten faders (the MIC EQ page's graphic view), next to the parametric controls below
            id: gcard; objectName: "toneGraphic"; Layout.fillWidth: true; implicitHeight: page.graphic ? 150 : 34; radius: Theme.radius; color: Theme.card; border.color: Theme.line; clip: true
            property int tick: 0
            readonly property var gains: { tick; page.ch3Tick; return engine.graphicEq(page.strip) }
            Row { x: 12; y: 7; spacing: 8
                Rectangle { objectName: "toneGraphicBtn"; width: 150; height: 20; radius: 4; color: page.graphic ? Theme.amber : Theme.ov(0.08); border.color: Theme.lineSoft
                    Text { anchors.centerIn: parent; text: page.graphic ? i18n.t("MIC EQ FADERS: SHOWN", i18n.lang) : i18n.t("SHOW MIC EQ FADERS", i18n.lang); color: page.graphic ? "#000" : Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                    MouseArea { anchors.fill: parent; onClicked: page.graphic = !page.graphic } }
                Rectangle { visible: page.graphic; width: 60; height: 20; radius: 4; color: Theme.ov(0.08); border.color: Theme.lineSoft
                    Text { anchors.centerIn: parent; text: i18n.t("FLAT", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                    MouseArea { anchors.fill: parent; onClicked: { engine.setGraphicEq(page.strip, [0, 0, 0, 0, 0, 0, 0, 0, 0, 0], false); gcard.tick++ } } }
                Text { anchors.verticalCenter: parent.verticalCenter; text: i18n.t("The same ten bands as the MIC EQ page and the parametric controls below: move any of them.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
            Row { visible: page.graphic; x: 12; y: 34; spacing: 14
                Repeater { model: ["31", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"]
                    delegate: Column { spacing: 2; width: 44
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: { const g = toneGraphicGain(index); return (g > 0 ? "+" : "") + g.toFixed(1) } color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 7
                            function toneGraphicGain(i) { const l = gcard.gains; return l && l[i] !== undefined ? Number(l[i]) : 0 } }
                        Slider { anchors.horizontalCenter: parent.horizontalCenter; orientation: Qt.Vertical; height: 78; from: -12; to: 12; stepSize: 0.5
                            value: { const l = gcard.gains; return l && l[index] !== undefined ? Number(l[index]) : 0 }
                            onMoved: { const card = gcard; const l = []; for (let b = 0; b < 10; ++b) l.push(b === index ? value : (card.gains[b] !== undefined ? Number(card.gains[b]) : 0)); engine.setGraphicEq(page.strip, l, true); card.tick++ } }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: modelData; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7 } } } } }
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
                Rectangle { readonly property bool on: engine.stripState[page.strip] !== undefined && !!engine.stripState[page.strip].autoPol
                            implicitWidth: 70; implicitHeight: 22; radius: 4; color: on ? Theme.violet : Theme.ov(0.08); border.color: Theme.lineSoft
                            Text { anchors.centerIn: parent; text: i18n.t("Ø AUTO", i18n.lang); color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: engine.setAutoPolarity(page.strip, !parent.on) }
                            ToolTip.visible: hov.hovered; ToolTip.text: i18n.t("AUTO PHASE: compares this channel with the rest of the mix and turns it round when it stays out of phase", i18n.lang)
                            HoverHandler { id: hov } }
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
