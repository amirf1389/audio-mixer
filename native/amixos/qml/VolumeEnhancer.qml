// AMFB VOLUME ENHANCER: one panel over the master's own processing for making a mix louder without clipping it. Every control drives a stage
// that already runs on the master (engine/cpp master_chain.hpp and the master limiter); the panel adds nothing to the signal path by itself.
//   LOUDNESS   gain into the limiter (0 .. +12 dB)            CEILING   the limiter's ceiling: the output never passes it
//   ANALOG     soft saturation on / off, DRIVE its amount     SOFTEN    the glue compressor's depth, MIX how much of it is blended in
//   LOW / MID / HIGH   100 Hz shelf, 1 kHz bell, 10 kHz shelf  MONO BELOW  bass made mono under that frequency, WIDTH the stereo width
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: pop
    objectName: "volumeEnhancer"
    modal: true; padding: 0; width: 940; height: 520; anchors.centerIn: Overlay.overlay
    property int tick: 0
    readonly property var m: { tick; return engine.mastering() }
    readonly property var lim: { tick; return engine.masterLimiter() }
    readonly property var tel: engine.telemetry
    property int presetIndex: 0
    // our own starting points; INIT is flat with only the limiter guarding the ceiling
    readonly property var presets: [
        { n: "Init",          gain: 0,   ceiling: -0.3, satOn: false, drive: 0,    soften: 0, compMix: 1,   low: 0,   mid: 0,    air: 0,   mono: 0,   width: 1 },
        { n: "Gentle Lift",   gain: 3,   ceiling: -0.5, satOn: false, drive: 0,    soften: 2, compMix: 0.6, low: 0.5, mid: 0,    air: 1,   mono: 0,   width: 1 },
        { n: "Loud and Clear", gain: 6,  ceiling: -0.3, satOn: true,  drive: 0.15, soften: 4, compMix: 0.7, low: 1,   mid: -0.5, air: 2,   mono: 100, width: 1.1 },
        { n: "Warm Tape",     gain: 4,   ceiling: -0.5, satOn: true,  drive: 0.45, soften: 3, compMix: 0.8, low: 1.5, mid: 0,    air: -0.5, mono: 80, width: 1 },
        { n: "Wide Open",     gain: 4,   ceiling: -0.3, satOn: false, drive: 0,    soften: 2, compMix: 0.5, low: 0,   mid: 0,    air: 1.5, mono: 120, width: 1.4 },
        { n: "Club System",   gain: 7,   ceiling: -0.2, satOn: true,  drive: 0.2,  soften: 5, compMix: 0.9, low: 2,   mid: -1,   air: 1,   mono: 140, width: 1 } ]
    readonly property real soften: Math.max(0, -m.thr / 2)          // SOFTEN n dB = threshold at -2n dB, ratio 2:1
    function put(o) { o.on = true; o.preset = ""; engine.setMastering(o); tick++ }
    function setSoften(v) { put({ thr: -2 * v, ratio: 2, makeup: 0, compOn: v > 0.05, attack: 30, release: 200 }) }
    function setMono(hz) { put({ bassMonoOn: hz >= 40, bassMonoHz: Math.max(40, hz) }) }
    function setCeiling(db) { engine.setMasterLimiter(true, db); tick++ }
    function load(i) {
        presetIndex = (i + presets.length) % presets.length; const p = presets[presetIndex]
        engine.setMastering({ on: true, preset: "", gain: p.gain, satOn: p.satOn, drive: p.drive, thr: -2 * p.soften, ratio: 2, makeup: 0, compOn: p.soften > 0.05, attack: 30, release: 200, compMix: p.compMix,
                              toneOn: true, low: p.low, mid: p.mid, air: p.air, bassMonoOn: p.mono >= 40, bassMonoHz: Math.max(40, p.mono) })
        engine.setMasterLimiter(true, p.ceiling); engine.width = p.width; tick++
    }
    function db(v, digits) { return (v > 0.004 ? "+" : "") + Number(v).toFixed(digits === undefined ? 1 : digits) + " dB" }
    property real limRed: 0
    property real compRed: 0
    onOpened: tick++
    Connections { target: engine; function onMasterChainChanged() { pop.tick++ } function onStripsChanged() { pop.tick++ } }
    Timer { interval: 60; running: pop.visible; repeat: true; onTriggered: { pop.compRed = engine.masteringReductionDb(); pop.limRed = engine.masterLimiterReductionDb() } }

    component Head: Text { color: Theme.muted; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; font.letterSpacing: 2 }
    component Hint: Text { color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9; wrapMode: Text.WordWrap }
    component Pill: Rectangle { property string label; property bool on: false; property color onColor: Theme.orange; signal clicked(); implicitWidth: Math.max(60, pl.implicitWidth + 20); implicitHeight: 26; radius: 6; color: on ? onColor : (ph.hovered ? Theme.ov(0.14) : Theme.ov(0.08)); border.color: on ? onColor : Theme.line
        Text { id: pl; anchors.centerIn: parent; text: parent.label; color: parent.on ? "#000" : Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true } HoverHandler { id: ph } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    // a level meter with its scale: green to -9, amber to -1, red above
    component LevelBar: Rectangle { property real level: -120; implicitWidth: 12; radius: 3; color: Theme.deep; border.color: Theme.lineSoft
        Rectangle { anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottomMargin: 1; width: parent.width - 2; height: Math.max(0, Math.min(1, (parent.level + 36) / 36)) * (parent.height - 2); radius: 2; color: parent.level > -1 ? Theme.danger : parent.level > -9 ? Theme.amber : Theme.emerald } }
    // gain reduction, from the top down
    component RedBar: Rectangle { property real red: 0; property real span: 12; implicitWidth: 12; radius: 3; color: Theme.deep; border.color: Theme.lineSoft
        Rectangle { anchors.top: parent.top; anchors.topMargin: 1; anchors.horizontalCenter: parent.horizontalCenter; width: parent.width - 2; height: Math.max(0, Math.min(1, parent.red / parent.span)) * (parent.height - 2); radius: 2; color: Theme.amber } }
    component Card: Rectangle { default property alias content: inner.data; property string title; property string note; Layout.fillHeight: true; radius: 10; color: Theme.ov(0.04); border.color: Theme.lineSoft
        Head { x: 16; y: 12; text: parent.title }
        Hint { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 14; horizontalAlignment: Text.AlignHCenter; text: parent.note; visible: text !== "" }
        Item { id: inner; anchors.fill: parent; anchors.topMargin: 36; anchors.bottomMargin: parent.note !== "" ? 40 : 10; anchors.leftMargin: 10; anchors.rightMargin: 10 } }

    background: Rectangle { radius: 14; color: Theme.card; border.color: pop.m.on ? Theme.orange : Theme.line; border.width: 1 }
    contentItem: ColumnLayout { spacing: 10
        // ---- title, presets, bypass
        Rectangle { Layout.fillWidth: true; implicitHeight: 54; color: Theme.ov(0.05); radius: 14
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 14; color: Theme.ov(0.0) }
            RowLayout { anchors.fill: parent; anchors.leftMargin: 18; anchors.rightMargin: 14; spacing: 10
                Text { text: "AMFB"; color: Theme.orange; font.family: Theme.mono; font.pixelSize: 17; font.weight: Font.Black; font.letterSpacing: 4 }
                Text { text: i18n.t("VOLUME ENHANCER", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 14; font.bold: true; font.letterSpacing: 3 }
                Item { Layout.fillWidth: true }
                Pill { objectName: "vePrev"; label: "◀"; implicitWidth: 32; onClicked: pop.load(pop.presetIndex - 1) }
                Rectangle { implicitWidth: 200; implicitHeight: 26; radius: 6; color: Theme.deep; border.color: Theme.line
                    Text { objectName: "vePresetName"; anchors.centerIn: parent; text: pop.presets[pop.presetIndex].n; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true } }
                Pill { objectName: "veNext"; label: "▶"; implicitWidth: 32; onClicked: pop.load(pop.presetIndex + 1) }
                Pill { objectName: "veInit"; label: i18n.t("INIT", i18n.lang); onClicked: pop.load(0) }
                Item { Layout.fillWidth: true }
                Pill { objectName: "veBypass"; label: pop.m.on ? i18n.t("● ACTIVE", i18n.lang) : i18n.t("○ BYPASSED", i18n.lang); on: pop.m.on; onColor: Theme.emerald; onClicked: { engine.setMastering({ on: !pop.m.on, preset: "" }); pop.tick++ } }
                Pill { objectName: "veClose"; label: "✕"; implicitWidth: 32; onClicked: pop.close() } } }
        // ---- character, loudness, output
        RowLayout { Layout.fillWidth: true; Layout.leftMargin: 14; Layout.rightMargin: 14; Layout.preferredHeight: 250; Layout.maximumHeight: 250; spacing: 10; opacity: pop.m.on ? 1 : 0.5
            Card { title: i18n.t("ANALOG", i18n.lang); note: i18n.t("Soft saturation: harmonics, and rounder peaks into the limiter.", i18n.lang); Layout.preferredWidth: 230; Layout.maximumWidth: 230
                Column { anchors.centerIn: parent; spacing: 12
                    Knob { anchors.horizontalCenter: parent.horizontalCenter; size: 70; label: i18n.t("DRIVE", i18n.lang); value: pop.m.drive; defaultValue: 0; valueText: Math.round(pop.m.drive * 100) + " %"; onMoved: function (v) { pop.put({ satOn: true, drive: v }) } }
                    Pill { objectName: "veAnalog"; anchors.horizontalCenter: parent.horizontalCenter; label: pop.m.satOn && pop.m.drive > 0 ? i18n.t("ANALOG ON", i18n.lang) : i18n.t("ANALOG OFF", i18n.lang); on: pop.m.satOn && pop.m.drive > 0; onClicked: pop.put(on ? { satOn: true, drive: 0 } : { satOn: true, drive: 0.25 }) } } }
            Card { title: i18n.t("LOUDNESS", i18n.lang); Layout.fillWidth: true; border.color: pop.m.on ? Theme.orange : Theme.lineSoft
                Knob { objectName: "veLoudness"; anchors.horizontalCenter: parent.horizontalCenter; y: -6; size: 140; label: ""; value: Math.max(0, pop.m.gain) / 12; defaultValue: 0; valueText: ""; onMoved: function (v) { pop.put({ gain: Math.round(v * 12 * 20) / 20, satOn: true }); if (!pop.lim.on) pop.setCeiling(pop.lim.ceiling === undefined ? -0.3 : pop.lim.ceiling) } }
                Column { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 4; spacing: 2
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: pop.db(Math.max(0, pop.m.gain), 2); color: Theme.orange; font.family: Theme.mono; font.pixelSize: 20; font.bold: true }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: pop.limRed > 0.05 ? i18n.t("limiting ", i18n.lang) + Number(pop.limRed).toFixed(1) + " dB" : i18n.t("not limiting", i18n.lang); color: pop.limRed > 6 ? Theme.danger : pop.limRed > 0.05 ? Theme.amber : Theme.dim; font.family: Theme.mono; font.pixelSize: 10 } } }
            Card { title: i18n.t("OUTPUT", i18n.lang); Layout.preferredWidth: 270; Layout.maximumWidth: 270
                RowLayout { anchors.fill: parent; spacing: 14
                    Knob { Layout.alignment: Qt.AlignVCenter; size: 70; label: i18n.t("CEILING", i18n.lang); value: (Math.max(-6, Math.min(0, pop.lim.ceiling === undefined ? -0.3 : pop.lim.ceiling)) + 6) / 6; defaultValue: 0.95; valueText: Number(pop.lim.ceiling === undefined ? -0.3 : pop.lim.ceiling).toFixed(2) + " dB"; onMoved: function (v) { pop.setCeiling(Math.round((-6 + v * 6) * 20) / 20) } }
                    Item { Layout.fillHeight: true; Layout.fillWidth: true
                        Row { id: bars; anchors.top: parent.top; anchors.bottom: outNum.top; anchors.bottomMargin: 20; anchors.horizontalCenter: parent.horizontalCenter; spacing: 5
                            Item { height: parent.height; width: 22
                                Repeater { model: [0, -9, -18, -27, -36]; delegate: Text { y: (-modelData / 36) * (parent.height - 2) - 4; text: modelData; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; width: 22; horizontalAlignment: Text.AlignRight } } }
                            LevelBar { height: parent.height; level: pop.tel.masterLDb === undefined ? -120 : pop.tel.masterLDb }
                            LevelBar { height: parent.height; level: pop.tel.masterRDb === undefined ? -120 : pop.tel.masterRDb }
                            Item { width: 6; height: 1 }
                            RedBar { height: parent.height; red: pop.limRed } }
                        Row { anchors.top: bars.bottom; anchors.topMargin: 3; anchors.horizontalCenter: parent.horizontalCenter; spacing: 6
                            Text { text: "      L   R   " + i18n.t("LIMIT", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 } }
                        Text { id: outNum; anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; text: (pop.tel.masterPeakDb === undefined ? "—" : Number(Math.max(-99, pop.tel.masterPeakDb)).toFixed(1)) + " dBFS"; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 11; font.bold: true } } } } }
        // ---- compression, tone, stereo
        RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; Layout.leftMargin: 14; Layout.rightMargin: 14; Layout.bottomMargin: 14; spacing: 10; opacity: pop.m.on ? 1 : 0.5
            Card { title: i18n.t("COMPRESSION", i18n.lang); Layout.preferredWidth: 300; Layout.fillWidth: true
                Row { anchors.centerIn: parent; spacing: 22
                    Knob { size: 60; label: i18n.t("SOFTEN", i18n.lang); value: Math.min(1, pop.soften / 12); defaultValue: 0; valueText: pop.m.compOn ? pop.db(pop.soften, 1) : i18n.t("off", i18n.lang); onMoved: function (v) { pop.setSoften(Math.round(v * 12 * 10) / 10) } }
                    Knob { size: 60; label: i18n.t("MIX", i18n.lang); value: pop.m.compMix === undefined ? 1 : pop.m.compMix; defaultValue: 1; valueText: Math.round((pop.m.compMix === undefined ? 1 : pop.m.compMix) * 100) + " %"; onMoved: function (v) { pop.put({ compMix: v }) } }
                    Column { spacing: 3; RedBar { id: grBar; height: 70; red: pop.compRed } Text { text: "GR"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 } } } }
            Card { title: i18n.t("EQ", i18n.lang); Layout.preferredWidth: 330; Layout.fillWidth: true
                Row { anchors.centerIn: parent; spacing: 22
                    Knob { size: 60; label: i18n.t("LOW", i18n.lang); value: (pop.m.low + 12) / 24; defaultValue: 0.5; valueText: pop.db(pop.m.low); onMoved: function (v) { pop.put({ toneOn: true, low: Math.round((v * 24 - 12) * 10) / 10 }) } }
                    Knob { size: 60; label: i18n.t("MID", i18n.lang); value: ((pop.m.mid || 0) + 12) / 24; defaultValue: 0.5; valueText: pop.db(pop.m.mid || 0); onMoved: function (v) { pop.put({ toneOn: true, mid: Math.round((v * 24 - 12) * 10) / 10 }) } }
                    Knob { size: 60; label: i18n.t("HIGH", i18n.lang); value: (pop.m.air + 12) / 24; defaultValue: 0.5; valueText: pop.db(pop.m.air); onMoved: function (v) { pop.put({ toneOn: true, air: Math.round((v * 24 - 12) * 10) / 10 }) } } } }
            Card { title: i18n.t("STEREO IMAGE", i18n.lang); Layout.preferredWidth: 250; Layout.fillWidth: true
                Row { anchors.centerIn: parent; spacing: 22
                    Knob { size: 60; label: i18n.t("MONO BELOW", i18n.lang); value: pop.m.bassMonoOn ? Math.min(1, pop.m.bassMonoHz / 300) : 0; defaultValue: 0; valueText: pop.m.bassMonoOn ? Math.round(pop.m.bassMonoHz) + " Hz" : i18n.t("off", i18n.lang); onMoved: function (v) { pop.setMono(Math.round(v * 300)) } }
                    Knob { size: 60; label: i18n.t("WIDTH", i18n.lang); value: engine.width / 2; defaultValue: 0.5; valueText: Math.round(engine.width * 100) + " %"; onMoved: function (v) { engine.width = Math.round(v * 200) / 100; pop.tick++ } } } } }
    }
}
