// TOOLS: tap tempo, metronome click, test-signal generator (sine / pink / white / polarity pulse into the master), delay tower calculator
// (distance + temperature -> ms, APPLY to a strip's delay), scribble strips + colours for the 32 channels, scene snapshots, and a diagnostic
// scan of the engine (device, xruns, clipping, licence). The browser build's DAW LINK and local SQL vault were mock-ups; they have no
// counterpart here.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "toolsPage"
    property var taps: []
    property real tapBpm: 0
    property real tapMs: 0
    property real distanceM: 15
    property real tempC: 20
    readonly property real soundSpeed: 331.3 + 0.606 * tempC
    readonly property real delayMs: distanceM / soundSpeed * 1000
    property int delayStrip: engine.selectedStrip
    property int scribble: engine.selectedStrip
    property var report: []
    readonly property var tel: engine.telemetry
    function tap() {
        const now = Date.now(); let t = taps.slice(); if (t.length && now - t[t.length - 1] > 2500) t = []; t.push(now); if (t.length > 9) t.shift(); taps = t
        if (t.length >= 2) { let sum = 0; for (let i = 1; i < t.length; ++i) sum += t[i] - t[i - 1]; tapMs = sum / (t.length - 1); tapBpm = 60000 / tapMs } else { tapMs = 0; tapBpm = 0 }
    }
    function scan() {
        const r = []; const t = engine.telemetry
        r.push({ ok: engine.running, t: engine.running ? "AUDIO ENGINE: running on " + engine.backend + " (" + t.frames + " frames / block, " + t.blocks + " blocks)" : "AUDIO ENGINE: stopped. Open a device on CONNECTIONS or LIVE SOURCES." })
        r.push({ ok: t.xruns === 0, t: t.xruns === 0 ? "BUFFER: no overruns since start" : "BUFFER: " + t.xruns + " overruns. Raise the buffer size (CONNECTIONS) or close other audio software." })
        let clips = 0; for (let i = 0; i < engine.meters.length; ++i) if (engine.meters[i].clip) ++clips
        r.push({ ok: clips === 0 && !t.masterClip, t: clips === 0 && !t.masterClip ? "CLIPPING: none" : "CLIPPING: " + clips + " strip(s)" + (t.masterClip ? " and the master" : "") + " clipped. Lower the trims or faders (CLEAR CLIPS on LEVELS)." })
        r.push({ ok: engine.devices.length > 0, t: engine.devices.length > 0 ? "INTERFACES: " + engine.devices.length + " found" : "INTERFACES: none found" })
        const lic = engine.license; r.push({ ok: lic.state === "ok" || lic.state === "licensed", t: "LICENCE: " + lic.planName + " · " + engine.licensedChannels + " channels" + (lic.message ? " · " + lic.message : "") })
        let armed = 0, muted = 0; for (let i = 0; i < engine.stripState.length; ++i) { if (engine.stripState[i].armed) ++armed; if (engine.stripState[i].effectiveMute) ++muted }
        r.push({ ok: true, t: "CONSOLE: " + engine.stripState.length + " strips, " + muted + " muted, " + armed + " armed, master " + t.masterPeakDb.toFixed(1) + " dBFS, correlation " + t.correlation.toFixed(2) })
        r.push({ ok: !engine.testOn, t: engine.testOn ? "TEST SIGNAL is ON and goes to the master: switch it off before a show." : "TEST SIGNAL: off" })
        report = r
    }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 22; radius: 4; color: on ? onColor : Theme.ov(0.08); border.color: Theme.lineSoft
        Text { id: bl; anchors.centerIn: parent; text: label; color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    component Head: Text { color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
    component Note: Text { Layout.fillWidth: true; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }
    Flickable {
        anchors.fill: parent; anchors.margins: 10; contentHeight: grid.implicitHeight; clip: true; boundsBehavior: Flickable.StopAtBounds
        GridLayout {
            id: grid; width: parent.width; columns: 2; columnSpacing: 8; rowSpacing: 8
            // TAP TEMPO + METRONOME
            Rectangle { Layout.fillWidth: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; implicitHeight: tapCol.implicitHeight + 24
                ColumnLayout { id: tapCol; anchors.fill: parent; anchors.margins: 12; spacing: 8
                    Head { text: i18n.t("TAP TEMPO & METRONOME", i18n.lang) }
                    RowLayout { spacing: 10
                        Rectangle { implicitWidth: 110; implicitHeight: 54; radius: 8; color: Theme.amber; Text { anchors.centerIn: parent; text: i18n.t("TAP", i18n.lang); color: "#000"; font.family: Theme.mono; font.pixelSize: 16; font.bold: true; font.letterSpacing: 3 } MouseArea { anchors.fill: parent; onClicked: page.tap() } }
                        ColumnLayout { spacing: 2
                            Text { text: page.tapBpm > 0 ? page.tapBpm.toFixed(1) + i18n.t(" BPM", i18n.lang) : i18n.t("-- BPM", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 20; font.bold: true }
                            Text { text: i18n.t("interval ", i18n.lang) + page.tapMs.toFixed(0) + i18n.t(" ms · ", i18n.lang) + page.taps.length + i18n.t(" taps (8-tap smoothing)", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
                        Item { Layout.fillWidth: true }
                        Btn { label: i18n.t("RESET", i18n.lang); onClicked: { page.taps = []; page.tapBpm = 0; page.tapMs = 0 } }
                        Btn { label: i18n.t("ADOPT TAP", i18n.lang); on: page.tapBpm > 0; onColor: Theme.cyan; onClicked: if (page.tapBpm > 0) engine.clickBpm = Math.round(page.tapBpm) }
                    }
                    RowLayout { spacing: 8; Layout.fillWidth: true
                        Text { text: i18n.t("TEMPO", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                        Slider { Layout.fillWidth: true; from: 40; to: 240; stepSize: 1; value: engine.clickBpm; onMoved: engine.clickBpm = value }
                        Text { Layout.preferredWidth: 60; text: engine.clickBpm.toFixed(0) + i18n.t(" BPM", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignRight } }
                    RowLayout { spacing: 6
                        Text { text: i18n.t("SIGNATURE", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                        Repeater { model: [{ t: "4/4", b: 4 }, { t: "3/4", b: 3 }, { t: "6/8", b: 6 }, { t: "2/2", b: 2 }, { t: "5/4", b: 5 }, { t: "7/8", b: 7 }]; delegate: Btn { label: modelData.t; on: engine.clickBeats === modelData.b; onClicked: engine.clickBeats = modelData.b } }
                        Item { Layout.fillWidth: true }
                        Repeater { model: engine.clickBeats; delegate: Rectangle { width: 12; height: 12; radius: 6; color: engine.clickBeat === index + 1 ? (index === 0 ? Theme.amber : Theme.cyan) : Theme.ov(0.1) } } }
                    RowLayout { spacing: 8; Layout.fillWidth: true
                        Text { text: i18n.t("LEVEL", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                        Slider { Layout.fillWidth: true; from: -40; to: 0; stepSize: 1; value: engine.clickDb; onMoved: engine.clickDb = value }
                        Text { Layout.preferredWidth: 60; text: engine.clickDb.toFixed(0) + i18n.t(" dB", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; horizontalAlignment: Text.AlignRight }
                        Btn { label: engine.clickOn ? i18n.t("STOP CLICK", i18n.lang) : i18n.t("START CLICK", i18n.lang); on: engine.clickOn; onColor: Theme.emerald; onClicked: engine.clickOn = !engine.clickOn } }
                    Note { text: i18n.t("The click is summed into the master (before the master fader), like the ARRANGE player. Accent on beat 1.", i18n.lang) }
                }
            }
            // TEST GENERATOR
            Rectangle { Layout.fillWidth: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; implicitHeight: genCol.implicitHeight + 24
                ColumnLayout { id: genCol; anchors.fill: parent; anchors.margins: 12; spacing: 8
                    RowLayout { Head { text: i18n.t("AUDIO SIGNAL & TEST GENERATOR", i18n.lang) } Item { Layout.fillWidth: true } Text { text: engine.testOn ? i18n.t("ON AIR", i18n.lang) : i18n.t("OFFLINE", i18n.lang); color: engine.testOn ? Theme.danger : Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } }
                    Text { text: i18n.t("SIGNAL TYPE", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.letterSpacing: 1 }
                    Flow { Layout.fillWidth: true; spacing: 4
                        Repeater { model: [{ t: "1 kHz SINE", ty: 0, f: 1000 }, { t: "100 Hz SUB", ty: 0, f: 100 }, { t: "10 kHz AIR", ty: 0, f: 10000 }, { t: "PINK NOISE (RTA)", ty: 1, f: 0 }, { t: "WHITE NOISE", ty: 2, f: 0 }, { t: "POLARITY PULSE", ty: 3, f: 0 }]
                            delegate: Btn { label: modelData.t; on: engine.testType === modelData.ty && (modelData.f === 0 || Math.abs(engine.testFreq - modelData.f) < 0.5); onColor: Theme.cyan; onClicked: { engine.testType = modelData.ty; if (modelData.f > 0) engine.testFreq = modelData.f } } } }
                    RowLayout { visible: engine.testType === 0; spacing: 8; Layout.fillWidth: true
                        Text { text: i18n.t("FREQ", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                        Slider { Layout.fillWidth: true; from: Math.log(20); to: Math.log(20000); value: Math.log(Math.max(20, engine.testFreq)); onMoved: engine.testFreq = Math.round(Math.exp(value)) }
                        Text { Layout.preferredWidth: 70; text: engine.testFreq >= 1000 ? (engine.testFreq / 1000).toFixed(2) + i18n.t(" kHz", i18n.lang) : engine.testFreq.toFixed(0) + i18n.t(" Hz", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; horizontalAlignment: Text.AlignRight } }
                    RowLayout { spacing: 8; Layout.fillWidth: true
                        Text { text: i18n.t("LEVEL", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                        Slider { Layout.fillWidth: true; from: -60; to: 0; stepSize: 1; value: engine.testDb; onMoved: engine.testDb = value }
                        Text { Layout.preferredWidth: 70; text: engine.testDb.toFixed(0) + i18n.t(" dBFS", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; horizontalAlignment: Text.AlignRight } }
                    RowLayout { spacing: 6
                        Btn { label: engine.testOn ? i18n.t("STOP TEST SIGNAL", i18n.lang) : i18n.t("ENGAGE TEST SIGNAL", i18n.lang); on: engine.testOn; onColor: Theme.danger; onClicked: engine.testOn = !engine.testOn }
                        Text { text: i18n.t("OUTPUT: MAIN STEREO L/R (and every output that carries MASTER on ROUTING)", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
                    Note { text: i18n.t("Pink noise with the LIVE SOURCES / MIC EQ spectrum makes an RTA. The polarity pulse is a positive 2 ms click once a second: a speaker in polarity moves outward on it.", i18n.lang) }
                }
            }
            // DELAY TOWER
            Rectangle { Layout.fillWidth: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; implicitHeight: dCol.implicitHeight + 24
                ColumnLayout { id: dCol; anchors.fill: parent; anchors.margins: 12; spacing: 8
                    Head { text: i18n.t("DELAY TOWER & TIME ALIGNMENT", i18n.lang) }
                    RowLayout { spacing: 8; Layout.fillWidth: true
                        Text { Layout.preferredWidth: 100; text: i18n.t("DISTANCE (M)", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                        Slider { Layout.fillWidth: true; from: 0; to: 150; stepSize: 0.1; value: page.distanceM; onMoved: page.distanceM = value }
                        Text { Layout.preferredWidth: 70; text: page.distanceM.toFixed(1) + " m"; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; horizontalAlignment: Text.AlignRight } }
                    RowLayout { spacing: 8; Layout.fillWidth: true
                        Text { Layout.preferredWidth: 100; text: i18n.t("TEMPERATURE (°C)", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                        Slider { Layout.fillWidth: true; from: -20; to: 45; stepSize: 0.5; value: page.tempC; onMoved: page.tempC = value }
                        Text { Layout.preferredWidth: 70; text: page.tempC.toFixed(1) + " °C"; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; horizontalAlignment: Text.AlignRight } }
                    RowLayout { spacing: 16
                        ColumnLayout { spacing: 1; Text { text: i18n.t("SPEED OF SOUND", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } Text { text: page.soundSpeed.toFixed(1) + " m/s"; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true } }
                        ColumnLayout { spacing: 1; Text { text: i18n.t("ACOUSTIC DELAY", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } Text { text: page.delayMs.toFixed(2) + i18n.t(" ms", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 11; font.bold: true } }
                        Item { Layout.fillWidth: true }
                        Text { text: i18n.t("CH", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                        SpinBox { palette.base: Theme.deep; palette.text: Theme.ink; palette.button: Theme.cardRaised; palette.buttonText: Theme.ink; palette.highlight: Theme.orange; palette.mid: Theme.line; from: 1; to: engine.stripState.length; value: page.delayStrip + 1; onValueModified: page.delayStrip = value - 1; implicitWidth: 90 }
                        Btn { label: i18n.t("APPLY DELAY", i18n.lang); on: true; onColor: Theme.cyan; onClicked: engine.setDelay(page.delayStrip, Math.min(500, page.delayMs)) } }
                    Note { text: i18n.t("Delay the fill / tower feed by the time the main PA's sound needs to reach it (plus a few ms so the main arrives first). The strip delay (DELAY stage, up to 500 ms) takes the value; the current value for CH ", i18n.lang) + (page.delayStrip + 1) + i18n.t(" is ", i18n.lang) + engine.channelParams(page.delayStrip).delayMs.toFixed(2) + i18n.t(" ms.", i18n.lang) }
                }
            }
            // SCRIBBLE STRIPS
            Rectangle { Layout.fillWidth: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; implicitHeight: sCol.implicitHeight + 24
                ColumnLayout { id: sCol; anchors.fill: parent; anchors.margins: 12; spacing: 8
                    Head { text: i18n.t("32-CHANNEL SCRIBBLE STRIP & COLOUR", i18n.lang) }
                    Grid { columns: 16; spacing: 3
                        Repeater { model: engine.stripState; delegate: Rectangle { width: 30; height: 24; radius: 3; color: page.scribble === index ? Theme.ov(0.18) : Theme.ov(0.05); border.color: page.scribble === index ? Theme.ink : "transparent"
                            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 3; radius: 1; color: Theme.stripColors[modelData.color] }
                            Text { anchors.centerIn: parent; anchors.verticalCenterOffset: 2; text: index + 1; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true }
                            MouseArea { anchors.fill: parent; onClicked: { page.scribble = index; engine.selectedStrip = index } } } } }
                    RowLayout { spacing: 8; Layout.fillWidth: true
                        Text { text: i18n.t("CH ", i18n.lang) + (page.scribble + 1); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                        Field { id: nameField; Layout.fillWidth: true; text: engine.stripState[page.scribble] ? engine.stripState[page.scribble].name : ""; font.family: Theme.mono; font.pixelSize: 9; maximumLength: 12; onEditingFinished: engine.setName(page.scribble, text) }
                        Repeater { model: Theme.stripColors; delegate: Rectangle { width: 20; height: 20; radius: 10; color: modelData; border.width: engine.stripState[page.scribble] && engine.stripState[page.scribble].color === index ? 3 : 0; border.color: Theme.ink; MouseArea { anchors.fill: parent; onClicked: engine.setColor(page.scribble, index) } } } }
                    Note { text: i18n.t("The name shows on the strip and the LED colour on its fader; both go into scenes and presets.", i18n.lang) }
                }
            }
            // SCENES
            Rectangle { Layout.fillWidth: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; implicitHeight: scCol.implicitHeight + 24
                ColumnLayout { id: scCol; anchors.fill: parent; anchors.margins: 12; spacing: 8
                    Head { text: i18n.t("SCENE SNAPSHOTS (TOTAL CONSOLE RECALL)", i18n.lang) }
                    Grid { columns: 4; spacing: 4
                        Repeater { model: 8; delegate: Rectangle { readonly property var sc: engine.scenes[index]; width: 112; height: 40; radius: 6; color: sc && sc.current ? Qt.rgba(0.98, 0.75, 0.14, 0.18) : Theme.ov(0.05); border.color: sc && sc.current ? Theme.amber : Theme.lineSoft
                            Text { anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 6; text: sc ? sc.name : ""; color: sc && !sc.empty ? Theme.ink : Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; elide: Text.ElideRight; width: parent.width - 12 }
                            Row { anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 4; spacing: 3
                                Rectangle { width: 36; height: 14; radius: 3; color: Theme.cyan; Text { anchors.centerIn: parent; text: i18n.t("STORE", i18n.lang); color: "#000"; font.family: Theme.mono; font.pixelSize: 6; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.storeScene(index, sc && !sc.empty ? sc.name : "") } }
                                Rectangle { width: 36; height: 14; radius: 3; color: sc && !sc.empty ? Theme.emerald : Theme.ov(0.08); Text { anchors.centerIn: parent; text: i18n.t("RECALL", i18n.lang); color: sc && !sc.empty ? "#000" : Theme.dim; font.family: Theme.mono; font.pixelSize: 6; font.bold: true } MouseArea { anchors.fill: parent; onClicked: if (sc && !sc.empty) engine.recallScene(index) } } } } } }
                    Note { text: i18n.t("Faders, mutes, solos, pans, DSP, sends, names and colours of all 32 strips. All slots, names and the export / import are on SCENES; the whole console file is on SETTINGS.", i18n.lang) }
                }
            }
            // DIAGNOSTICS
            Rectangle { Layout.fillWidth: true; Layout.columnSpan: 2; radius: Theme.radius; color: Theme.card; border.color: Theme.line; implicitHeight: dgCol.implicitHeight + 24
                ColumnLayout { id: dgCol; anchors.fill: parent; anchors.margins: 12; spacing: 8
                    RowLayout { Head { text: i18n.t("SYSTEM DIAGNOSTIC", i18n.lang) } Item { Layout.fillWidth: true } Btn { label: i18n.t("RUN FULL SCAN", i18n.lang); on: true; onColor: Theme.cyan; onClicked: page.scan() } Btn { label: i18n.t("CLEAR CLIPS", i18n.lang); onClicked: engine.clearClips() } }
                    Text { visible: page.report.length === 0; text: i18n.t("Press RUN FULL SCAN: the engine, the buffer, clipping, the interfaces and the licence are checked and each line says what to do.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                    Repeater { model: page.report; delegate: RowLayout { spacing: 8
                        Rectangle { width: 10; height: 10; radius: 5; color: modelData.ok ? Theme.emerald : Theme.amber }
                        Text { text: modelData.t; color: modelData.ok ? Theme.muted : Theme.ink; font.family: Theme.mono; font.pixelSize: 8; wrapMode: Text.WordWrap; Layout.fillWidth: true } } }
                }
            }
        }
    }
}
