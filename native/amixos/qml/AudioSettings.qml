// AUDIO / MIDI SETTINGS: everything about the sound card in one window, top to bottom in the order a studio program asks for it:
// mute the input (against a feedback loop), the audio system, the output and input device, which channel pairs are in use, the sample
// rate, the block size and the MIDI inputs. Every choice here is made by hand; nothing is detected or opened until APPLY.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AmixOS

Popup {
    id: pop
    objectName: "audioSettings"
    modal: true; padding: 0; width: 600; anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape
    property string type: ""
    property string outId: ""
    property string inId: ""
    property int outCh: 2
    property int inCh: 2
    property real rate: 48000
    property int frames: 256
    property bool muteIn: false
    property string note: ""
    readonly property var types: engine.backends
    function devs(out) { const l = []; for (const d of engine.devices) if (d.backend === type && (out ? d.outChannels > 0 : d.inChannels > 0)) l.push(d); return l }
    function dev(id) { for (const d of engine.devices) if (d.backend === type && d.id === id) return d; return null }
    function pickDefaults() { const o = devs(true), i = devs(false); let od = o[0], idv = i[0]; for (const d of o) if (d.defaultOut) od = d; for (const d of i) if (d.defaultIn) idv = d
        outId = od ? od.id : ""; inId = idv ? idv.id : ""; outCh = Math.max(2, Math.min(outCh, od ? od.outChannels : 2)); inCh = Math.min(Math.max(inCh, 0), idv ? idv.inChannels : 0); if (idv && inCh === 0) inCh = Math.min(2, idv.inChannels) }
    function load() { type = engine.running && types.indexOf(engine.backend) >= 0 ? engine.backend : (types.length ? types[0] : ""); rate = engine.sampleRate > 0 ? engine.sampleRate : 48000; frames = engine.blockFrames > 0 ? engine.blockFrames : settings.defaultFrames; note = ""; pickDefaults() }
    function apply() { const ok = engine.open(type, muteIn ? "" : inId, outId, rate, frames, muteIn ? 0 : inCh, outCh); note = ok ? i18n.t("Running.", i18n.lang) : i18n.t("This device did not open with these settings.", i18n.lang) }
    onAboutToShow: load()
    background: Rectangle { radius: 8; color: Theme.card; border.color: Theme.line }

    component Btn: Rectangle { id: bt; property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: btT.implicitWidth + 22; implicitHeight: 24; radius: 5
        color: bt.on ? Qt.rgba(bt.onColor.r, bt.onColor.g, bt.onColor.b, 0.22) : Theme.ov(0.06); border.color: bt.on ? bt.onColor : Theme.line
        Text { id: btT; anchors.centerIn: parent; text: bt.label; color: bt.on ? bt.onColor : Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: bt.clicked() } }
    component Line: RowLayout { property string label; Layout.fillWidth: true; spacing: 10
        Text { Layout.preferredWidth: 170; Layout.alignment: Qt.AlignTop; topPadding: 5; horizontalAlignment: Text.AlignRight; text: i18n.t(parent.label, i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 10 } }
    component Check: RowLayout { id: ck; property bool on; property string label; signal toggled(); Layout.fillWidth: true; spacing: 8
        Rectangle { width: 16; height: 16; radius: 3; color: ck.on ? Theme.amber : Theme.ov(0.06); border.color: ck.on ? Theme.amber : Theme.line
            Text { anchors.centerIn: parent; visible: ck.on; text: "✓"; color: "#000"; font.pixelSize: 11; font.bold: true } }
        Text { Layout.fillWidth: true; text: ck.label; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; elide: Text.ElideRight }
        TapHandler { onTapped: ck.toggled() } }
    component Box: Rectangle { default property alias body: boxCol.data; Layout.fillWidth: true; implicitHeight: Math.max(30, boxCol.implicitHeight + 12); radius: 5; color: Theme.ov(0.03); border.color: Theme.lineSoft
        ColumnLayout { id: boxCol; anchors.fill: parent; anchors.margins: 6; spacing: 6 } }

    contentItem: ColumnLayout { spacing: 0
        Rectangle { Layout.fillWidth: true; height: 34; color: Theme.cardRaised; radius: 8
            Text { x: 14; anchors.verticalCenter: parent.verticalCenter; text: i18n.t("AUDIO / MIDI SETTINGS", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
            Text { anchors.right: parent.right; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter; text: "✕"; color: Theme.muted; font.pixelSize: 13; MouseArea { anchors.fill: parent; anchors.margins: -6; onClicked: pop.close() } } }
        ColumnLayout { Layout.fillWidth: true; Layout.margins: 16; spacing: 10
            Line { label: i18n.t("Feedback loop:", i18n.lang); Box { Check { on: pop.muteIn; label: i18n.t("Mute audio input", i18n.lang); onToggled: pop.muteIn = !pop.muteIn } } }
            Line { label: i18n.t("Audio device type:", i18n.lang)
                Combo { objectName: "asType"; Layout.fillWidth: true; model: pop.types.map(function (t) { return t.toUpperCase() }); currentIndex: Math.max(0, pop.types.indexOf(pop.type)); onActivated: function (i) { pop.type = pop.types[i]; pop.pickDefaults() } } }
            Item { height: 2 }
            Line { label: i18n.t("Output:", i18n.lang)
                Combo { Layout.fillWidth: true; readonly property var l: pop.devs(true); model: l.map(function (d) { return d.name }); currentIndex: { for (let k = 0; k < l.length; k++) if (l[k].id === pop.outId) return k; return l.length ? 0 : -1 }
                    onActivated: function (i) { pop.outId = l[i].id; pop.outCh = Math.max(2, Math.min(pop.outCh, l[i].outChannels)) } }
                Btn { objectName: "asTest"; label: i18n.t("Test", i18n.lang); onClicked: { if (!engine.testTone()) pop.note = i18n.t("Start the device with APPLY first, then press Test.", i18n.lang); else pop.note = i18n.t("Test tone: 1 kHz for one second on outputs 1 + 2.", i18n.lang) } } }
            Line { label: i18n.t("Input:", i18n.lang)
                Combo { Layout.fillWidth: true; enabled: !pop.muteIn; opacity: enabled ? 1 : 0.4; readonly property var l: pop.devs(false); model: l.map(function (d) { return d.name }); currentIndex: { for (let k = 0; k < l.length; k++) if (l[k].id === pop.inId) return k; return l.length ? 0 : -1 }
                    onActivated: function (i) { pop.inId = l[i].id; pop.inCh = Math.max(1, Math.min(Math.max(pop.inCh, 2), l[i].inChannels)) } }
                Row { spacing: 1; readonly property real db: (engine.meterTick, engine.meterAt(0).vuDb !== undefined ? engine.meterAt(0).vuDb : -120)
                    Repeater { model: 7; delegate: Rectangle { width: 8; height: 24; color: parent.db > -48 + index * 7 ? (index >= 6 ? Theme.danger : Theme.emerald) : Theme.ov(0.12) } } } }
            Line { label: i18n.t("Active output channels:", i18n.lang)
                Box { Repeater { model: { const d = pop.dev(pop.outId); return d ? Math.min(16, Math.ceil(d.outChannels / 2)) : 0 }
                        delegate: Check { on: index * 2 < pop.outCh; label: i18n.t("Output channel ", i18n.lang) + (index * 2 + 1) + " + " + (index * 2 + 2); onToggled: pop.outCh = on && index > 0 ? index * 2 : (index + 1) * 2 } }
                    Text { visible: !pop.dev(pop.outId); text: i18n.t("(No output device)", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 10 } } }
            Line { label: i18n.t("Active input channels:", i18n.lang)
                Box { opacity: pop.muteIn ? 0.4 : 1
                    Repeater { model: { const d = pop.dev(pop.inId); return d ? Math.min(16, Math.ceil(d.inChannels / 2)) : 0 }
                        delegate: Check { on: index * 2 < pop.inCh; label: i18n.t("Input channel ", i18n.lang) + (index * 2 + 1) + " + " + (index * 2 + 2); onToggled: { if (!pop.muteIn) pop.inCh = on ? index * 2 : (index + 1) * 2 } } }
                    Text { visible: !pop.dev(pop.inId); text: i18n.t("(No input device)", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 10 } } }
            Item { height: 2 }
            Line { label: i18n.t("Sample rate:", i18n.lang)
                Combo { Layout.fillWidth: true; readonly property var l: { const d = pop.dev(pop.outId); return d && d.sampleRates && d.sampleRates.length ? d.sampleRates : [44100, 48000, 88200, 96000] }
                    model: l.map(function (r) { return r + " Hz" }); currentIndex: Math.max(0, l.indexOf(pop.rate)); onActivated: function (i) { pop.rate = l[i] } } }
            Line { label: i18n.t("Audio buffer size:", i18n.lang)
                Combo { Layout.fillWidth: true; readonly property var l: [32, 64, 128, 256, 512, 1024, 2048]
                    model: l.map(function (f) { return f + i18n.t(" samples (", i18n.lang) + (f * 1000 / pop.rate).toFixed(1) + " ms)" }); currentIndex: Math.max(0, l.indexOf(pop.frames)); onActivated: function (i) { pop.frames = l[i] } } }
            Item { height: 2 }
            Line { label: i18n.t("Active MIDI inputs:", i18n.lang)
                Box { Repeater { model: net.midiInputs
                        delegate: Check { on: net.midiPort === modelData.id; label: i18n.t(modelData.name ?? "", i18n.lang); onToggled: net.openMidi(on ? "" : modelData.id) } }
                    Text { visible: net.midiInputs.length === 0; text: i18n.t("(No MIDI inputs available)", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 10 } } }
            RowLayout { Layout.fillWidth: true; Layout.topMargin: 4; spacing: 8
                Text { Layout.fillWidth: true; text: pop.note !== "" ? pop.note : i18n.t("Channel pairs are used in order from 1: ticking a pair turns on every pair up to it.", i18n.lang); color: pop.note !== "" ? Theme.amber : Theme.dim; font.family: Theme.mono; font.pixelSize: 8; wrapMode: Text.WordWrap }
                Btn { label: i18n.t("STOP", i18n.lang); onClicked: { engine.stop(); pop.note = i18n.t("Stopped.", i18n.lang) } }
                Btn { objectName: "asApply"; label: i18n.t("APPLY", i18n.lang); on: true; onColor: Theme.emerald; onClicked: pop.apply() }
                Btn { label: i18n.t("CLOSE", i18n.lang); onClicked: pop.close() } } } }
}
