// The MIXER's group tools, three panels over the page:
//   DCA ASSIGN   pick a group, then click channels in or out of it (a channel is in one DCA at most)
//   AUTO DCA     listens to every channel for a few seconds and proposes VOICES / MUSIC / INSTRUMENTS groups; channels with no signal are
//                flagged "not needed". The classifier is fixed rules on measured features (audiomixer/classify.hpp): nothing is sent
//                anywhere. Nothing is assigned until APPLY; a row's class can be changed by clicking it first.
//   DUCKER       VOICE OVER MUSIC: while the KEY (a voice group and / or channels) is above THRESHOLD, the TARGET goes down by DEPTH
//                (attack / hold / release), and the voices can be lifted a little. Runs in the engine, sample by sample.
//   LOUDNESS MANAGER  AUTO FADER per channel (rides the fader toward a target within a range, at limited speed; the fader moves and its
//                own position stays as a ghost), AUTO ON/OFF (a silent channel fades out after the hold, back at once), the master target
//                (STREAMING -14, PODCAST -16, BROADCAST -23, CUSTOM). Moving a fader pauses its ride for 5 s. Runs in the engine.
import QtQuick
import AmixOS
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: dp
    property int group: 1
    function openAssign(g, strip) { dp.group = g > 0 ? g : 1; assignPop.open() }
    function openAuto() { autoPop.open() }
    function openDucker() { duckPop.open() }
    function openLoudness() { loudPop.open() }
    readonly property int chans: Math.min(engine.licensedChannels, 64)
    function chName(i) { const s = engine.stripAt(i, engine.stripTick); return s && s.name ? s.name : "CH " + (i + 1) }
    component Chip: Rectangle { id: ch; property string label; property bool on: false; property color tint: Theme.amber; signal hit()
        implicitWidth: Math.max(30, chT.implicitWidth + 12); implicitHeight: 20; radius: 5; color: on ? tint : Theme.ov(0.07); border.color: Theme.lineSoft
        Text { id: chT; anchors.centerIn: parent; text: ch.label; color: ch.on ? "#0b0f14" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: ch.hit() } }
    component Head: Text { color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 2 }
    component Note: Text { Layout.fillWidth: true; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; wrapMode: Text.WordWrap }

    Popup { id: assignPop; objectName: "dcaAssignPop"; parent: Overlay.overlay; x: (parent.width - width) / 2; y: 70; width: 520; padding: 12; modal: true; focus: true; closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { radius: 8; color: Theme.cardSolid; border.color: Theme.amber }
        ColumnLayout { width: parent.width; spacing: 8
            Head { text: i18n.t("DCA ASSIGN", i18n.lang) }
            Note { text: i18n.t("Pick a group, then click channels in or out of it. The group fader moves its channels' level without moving their faders.", i18n.lang) }
            Flow { Layout.fillWidth: true; spacing: 4
                Repeater { model: engine.dcaState; delegate: Chip { objectName: "dcaPick" + modelData.index; label: modelData.index + " · " + modelData.name; on: dp.group === modelData.index; tint: Theme.stripColors[modelData.color || 0]; onHit: dp.group = modelData.index } } }
            Flow { Layout.fillWidth: true; spacing: 4
                Repeater { model: dp.chans; delegate: Chip { objectName: "dcaCh" + index; readonly property var st: engine.stripAt(index, engine.stripTick); label: (index + 1) + (st && st.dca > 0 && st.dca !== dp.group ? " ·" + st.dca : "")
                        on: st && st.dca === dp.group; onHit: engine.setDca(index, st && st.dca === dp.group ? 0 : dp.group) } } }
            RowLayout { Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Chip { label: i18n.t("CLOSE", i18n.lang); onHit: assignPop.close() } } } }

    Popup { id: autoPop; objectName: "autoDcaPop"; parent: Overlay.overlay; x: (parent.width - width) / 2; y: 60; width: 600; padding: 12; modal: true; focus: true; closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        property bool muteSilent: false
        readonly property var cls: ["VOICE", "MUSIC", "INSTRUMENT", "SILENT"]
        background: Rectangle { radius: 8; color: Theme.cardSolid; border.color: Theme.cyan }
        ColumnLayout { width: parent.width; spacing: 8
            Head { text: i18n.t("AUTO DCA", i18n.lang) }
            Note { text: i18n.t("Play your sources as in the show, then LISTEN: every channel is measured for 4 seconds (level, voice band 300-3400 Hz, brightness, zero crossings, pauses, crest, stereo) and sorted by fixed rules into VOICE, MUSIC or INSTRUMENT. Channels with no signal are marked not needed. Nothing leaves this computer and nothing changes until APPLY.", i18n.lang) }
            RowLayout { spacing: 6
                Chip { objectName: "autoDcaListen"; label: engine.autoDcaBusy ? i18n.t("LISTENING…", i18n.lang) : i18n.t("LISTEN 4 s", i18n.lang); on: engine.autoDcaBusy; tint: Theme.cyan; onHit: if (!engine.autoDcaBusy) engine.startAutoDca(4) }
                Text { text: engine.autoDcaProposal.length ? engine.autoDcaProposal.length + " " + i18n.t("channels looked at", i18n.lang) : ""; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } }
            ListView { Layout.fillWidth: true; Layout.preferredHeight: Math.min(300, count * 34); clip: true; model: engine.autoDcaProposal; spacing: 2
                delegate: Rectangle { width: ListView.view.width; height: 32; radius: 4; color: Theme.ov(0.04)
                    readonly property color tc: modelData.cls === "VOICE" ? Theme.stripColors[2] : modelData.cls === "MUSIC" ? Theme.stripColors[5] : modelData.cls === "INSTRUMENT" ? Theme.stripColors[3] : Theme.dim
                    RowLayout { anchors.fill: parent; anchors.margins: 4; spacing: 6
                        Text { Layout.preferredWidth: 110; text: (modelData.strip + 1) + " · " + modelData.name; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; elide: Text.ElideRight }
                        Chip { objectName: "autoDcaCls" + modelData.strip; label: modelData.cls === "SILENT" ? i18n.t("NOT NEEDED", i18n.lang) : i18n.t(modelData.cls, i18n.lang); on: true; tint: parent.parent.tc
                            onHit: { const c = autoPop.cls; engine.setAutoDcaClass(modelData.strip, c[(c.indexOf(modelData.cls) + 1) % c.length]) } }
                        Text { Layout.fillWidth: true; text: modelData.why; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; elide: Text.ElideRight } } } }
            RowLayout { Layout.fillWidth: true; spacing: 6
                Chip { objectName: "autoDcaMuteSilent"; label: i18n.t("MUTE THE CHANNELS NOT NEEDED", i18n.lang); on: autoPop.muteSilent; tint: Theme.danger; onHit: autoPop.muteSilent = !autoPop.muteSilent }
                Item { Layout.fillWidth: true }
                Chip { objectName: "autoDcaCancel"; label: i18n.t("CANCEL", i18n.lang); onHit: { engine.cancelAutoDca(); autoPop.close() } }
                Chip { objectName: "autoDcaApply"; label: i18n.t("APPLY", i18n.lang); on: engine.autoDcaProposal.length > 0; tint: Theme.emerald; onHit: if (engine.autoDcaProposal.length) { engine.applyAutoDca(autoPop.muteSilent); autoPop.close() } } } } }

    Popup { id: duckPop; objectName: "duckerPop"; parent: Overlay.overlay; x: (parent.width - width) / 2; y: 50; width: 560; padding: 12; modal: true; focus: true; closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        readonly property var ds: engine.duckState
        background: Rectangle { radius: 8; color: Theme.cardSolid; border.color: Theme.emerald }
        ColumnLayout { width: parent.width; spacing: 7
            RowLayout { Layout.fillWidth: true
                Head { text: i18n.t("DUCKER · VOICE OVER MUSIC", i18n.lang); Layout.fillWidth: true }
                Chip { objectName: "duckOn"; label: duckPop.ds.on ? i18n.t("ON", i18n.lang) : i18n.t("OFF", i18n.lang); on: duckPop.ds.on; tint: Theme.emerald; onHit: engine.setDuck({ on: !duckPop.ds.on }) } }
            Note { text: i18n.t("While a voice (the KEY) is above the threshold, the music (the TARGET) goes down by the depth and comes back after the hold and release. Smooth, sample by sample in the engine.", i18n.lang) }
            Text { text: i18n.t("KEY · VOICE GROUP", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
            Flow { Layout.fillWidth: true; spacing: 4
                Chip { label: i18n.t("NONE", i18n.lang); on: duckPop.ds.keyDca === 0; onHit: engine.setDuck({ keyDca: 0 }) }
                Repeater { model: engine.dcaState; delegate: Chip { objectName: "duckKey" + modelData.index; label: modelData.index + " · " + modelData.name; on: duckPop.ds.keyDca === modelData.index; tint: Theme.stripColors[modelData.color || 0]; onHit: engine.setDuck({ keyDca: modelData.index }) } } }
            Text { text: i18n.t("TARGET · MUSIC GROUP", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
            Flow { Layout.fillWidth: true; spacing: 4
                Chip { label: i18n.t("NONE", i18n.lang); on: duckPop.ds.targetDca === 0; onHit: engine.setDuck({ targetDca: 0 }) }
                Repeater { model: engine.dcaState; delegate: Chip { objectName: "duckTarget" + modelData.index; label: modelData.index + " · " + modelData.name; on: duckPop.ds.targetDca === modelData.index; tint: Theme.stripColors[modelData.color || 0]; onHit: engine.setDuck({ targetDca: modelData.index }) } } }
            Text { text: i18n.t("CHANNELS · click: key, target, neither", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
            Flow { Layout.fillWidth: true; spacing: 3
                Repeater { model: dp.chans; delegate: Chip { objectName: "duckCh" + index; readonly property int role: (duckPop.ds, engine.duckRole(index)); readonly property bool own: duckPop.ds.keyStrips.indexOf(index) >= 0 || duckPop.ds.targetStrips.indexOf(index) >= 0
                        label: (index + 1) + (role === 1 ? " K" : role === 2 ? " T" : ""); on: role > 0; tint: role === 1 ? Theme.stripColors[2] : Theme.stripColors[5]
                        onHit: { const k = duckPop.ds.keyStrips.slice(), t = duckPop.ds.targetStrips.slice(); const ik = k.indexOf(index), it = t.indexOf(index)
                            if (ik >= 0) { k.splice(ik, 1); t.push(index) } else if (it >= 0) t.splice(it, 1); else k.push(index)
                            engine.setDuck({ keyStrips: k, targetStrips: t }) } } } }
            GridLayout { Layout.fillWidth: true; columns: 3; columnSpacing: 8; rowSpacing: 2
                Repeater { model: [["threshold", "THRESHOLD", -80, 0, " dBFS"], ["depth", "DEPTH", -40, 0, " dB"], ["attack", "ATTACK", 1, 200, " ms"], ["hold", "HOLD", 0, 2000, " ms"], ["release", "RELEASE", 50, 3000, " ms"], ["lift", "VOICE LIFT", 0, 6, " dB"]]
                    delegate: ColumnLayout { Layout.fillWidth: true; spacing: 0
                        Text { text: i18n.t(modelData[1], i18n.lang) + "  " + Number(duckPop.ds[modelData[0]]).toFixed(modelData[0] === "lift" ? 1 : 0) + modelData[4]; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                        MSlider { objectName: "duck_" + modelData[0]; Layout.fillWidth: true; tint: Theme.emerald; from: modelData[2]; to: modelData[3]; value: duckPop.ds[modelData[0]]
                            onMoved: { const o = {}; o[modelData[0]] = value; engine.setDuck(o) } } } } }
            RowLayout { Layout.fillWidth: true
                Text { text: i18n.t("MUSIC NOW", i18n.lang) + "  " + (duckPop.ds.on ? engine.duckGrDb.toFixed(1) : "0.0") + " dB"; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                Item { Layout.fillWidth: true }
                Chip { label: i18n.t("CLOSE", i18n.lang); onHit: duckPop.close() } } } }

    Popup { id: loudPop; objectName: "loudPop"; parent: Overlay.overlay; x: (parent.width - width) / 2; y: 40; width: 640; padding: 12; modal: true; focus: true; closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        readonly property var ls: engine.loudState
        property int tick: 0
        Timer { interval: 150; repeat: true; running: loudPop.visible; onTriggered: loudPop.tick++ }
        readonly property var tl: (loudPop.tick, engine.telemetry)
        function lu(v) { return v === undefined || v <= -119 ? i18n.t("-inf", i18n.lang) : Number(v).toFixed(1) }
        background: Rectangle { radius: 8; color: Theme.cardSolid; border.color: Theme.cyan }
        ColumnLayout { width: parent.width; spacing: 7
            Head { text: i18n.t("LOUDNESS MANAGER", i18n.lang) }
            Note { text: i18n.t("AUTO FADER rides each channel's fader toward its target loudness (short-term, 3 s) within a range around your own position, at a limited speed. AUTO ON/OFF fades a silent channel out after the hold and brings it back at once. Moving a fader pauses its ride for 5 s and makes the new position the centre. Order: fader + DCA, then the ride, auto on/off, automix and the ducker.", i18n.lang) }
            // MASTER
            RowLayout { Layout.fillWidth: true; spacing: 4
                Text { text: i18n.t("MASTER TARGET", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; Layout.preferredWidth: 96 }
                Chip { objectName: "loudMasterOn"; label: loudPop.ls.masterOn ? i18n.t("ON", i18n.lang) : i18n.t("OFF", i18n.lang); on: loudPop.ls.masterOn; tint: Theme.emerald; onHit: engine.setLoud({ masterOn: !loudPop.ls.masterOn }) }
                Repeater { model: [["STREAMING", -14], ["PODCAST", -16], ["BROADCAST", -23]]
                    delegate: Chip { objectName: "loudPreset_" + modelData[0]; label: i18n.t(modelData[0], i18n.lang) + " " + modelData[1]; on: loudPop.ls.preset === modelData[0]; tint: Theme.cyan; onHit: engine.setLoud({ preset: modelData[0] }) } }
                Chip { objectName: "loudPreset_CUSTOM"; label: i18n.t("CUSTOM", i18n.lang); on: loudPop.ls.preset === "CUSTOM"; tint: Theme.cyan; onHit: engine.setLoud({ masterTarget: loudPop.ls.masterTarget }) }
                Text { text: Number(loudPop.ls.masterTarget).toFixed(0) + " LUFS"; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } }
            MSlider { objectName: "loudMasterTarget"; visible: loudPop.ls.preset === "CUSTOM"; Layout.fillWidth: true; tint: Theme.cyan; from: -30; to: -8; value: loudPop.ls.masterTarget; onMoved: engine.setLoud({ masterTarget: Math.round(value) }) }
            RowLayout { Layout.fillWidth: true; spacing: 4
                Repeater { model: [["momentary", "M", "LUFS"], ["shortTerm", "S", "LUFS"], ["integrated", "I", "LUFS"], ["lra", "LRA", "LU"], ["truePeak", "TP", "dBTP"], ["autoLevelDb", "GAIN", "dB"]]
                    delegate: Rectangle { Layout.fillWidth: true; implicitHeight: 34; radius: 5; color: Qt.rgba(0, 0, 0, 0.3); border.color: Theme.lineSoft
                        readonly property real v: loudPop.tl[modelData[0]] === undefined ? -120 : loudPop.tl[modelData[0]]
                        Column { anchors.centerIn: parent
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: i18n.t(modelData[1], i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7 }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: (modelData[0] === "lra" ? Number(parent.parent.v).toFixed(1) : modelData[0] === "autoLevelDb" ? (parent.parent.v > 0 ? "+" : "") + Number(parent.parent.v).toFixed(1) : loudPop.lu(parent.parent.v)) + " " + modelData[2]; color: modelData[0] === "autoLevelDb" ? Theme.emerald : Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } } } } }
            Note { visible: loudPop.ls.masterOn && !(engine.masterLimiter() || {}).on; text: i18n.t("Turn the master limiter on: it is the true-peak safety under the master target.", i18n.lang); color: Theme.amber }
            // CHANNELS
            RowLayout { Layout.fillWidth: true; spacing: 4
                Text { text: i18n.t("CHANNELS", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; Layout.preferredWidth: 96 }
                Chip { objectName: "loudAllRideOn"; label: i18n.t("ALL AUTO ON", i18n.lang); tint: Theme.cyan; onHit: engine.setLoudAll("ride", true) }
                Chip { objectName: "loudAllRideOff"; label: i18n.t("ALL AUTO OFF", i18n.lang); onHit: engine.setLoudAll("ride", false) }
                Chip { objectName: "loudAllGateOn"; label: i18n.t("ALL ON/OFF ON", i18n.lang); tint: Theme.amber; onHit: engine.setLoudAll("gate", true) }
                Chip { objectName: "loudAllGateOff"; label: i18n.t("ALL ON/OFF OFF", i18n.lang); onHit: engine.setLoudAll("gate", false) }
                Item { Layout.fillWidth: true }
                Text { text: loudPop.ls.rides + " " + i18n.t("riding", i18n.lang) + " · " + loudPop.ls.gates + " " + i18n.t("on/off", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 } }
            RowLayout { Layout.fillWidth: true; spacing: 4
                Text { text: i18n.t("SPEED", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; Layout.preferredWidth: 96 }
                Repeater { model: [["slow", "SLOW"], ["medium", "MEDIUM"], ["fast", "FAST"]]
                    delegate: Chip { objectName: "loudSpeed_" + modelData[0]; label: i18n.t(modelData[1], i18n.lang); on: loudPop.ls.speed === modelData[0]; tint: Theme.cyan; onHit: engine.setLoud({ speed: modelData[0] }) } }
                Text { text: "+" + Number(loudPop.ls.up).toFixed(1) + " / -" + Number(loudPop.ls.down).toFixed(1) + " dB/s"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                Item { Layout.fillWidth: true }
                Chip { objectName: "loudGlobal"; label: i18n.t("ONE TARGET FOR ALL", i18n.lang); on: loudPop.ls.globalOn; tint: Theme.cyan; onHit: engine.setLoud({ globalOn: !loudPop.ls.globalOn }) } }
            GridLayout { Layout.fillWidth: true; columns: 4; columnSpacing: 8; rowSpacing: 2
                Repeater { model: [["range", "RANGE ±", 0, 20, " dB", 0], ["gateThr", "ON/OFF BELOW", -80, -20, " LUFS", 0], ["hold", "HOLD", 0.5, 15, " s", 1], ["globalTarget", "TARGET", -40, -6, " LUFS", 0]]
                    delegate: ColumnLayout { Layout.fillWidth: true; spacing: 0; opacity: modelData[0] !== "globalTarget" || loudPop.ls.globalOn ? 1 : 0.4
                        Text { text: i18n.t(modelData[1], i18n.lang) + " " + Number(loudPop.ls[modelData[0]]).toFixed(modelData[5]) + modelData[4]; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                        MSlider { objectName: "loud_" + modelData[0]; Layout.fillWidth: true; tint: Theme.cyan; from: modelData[2]; to: modelData[3]; value: loudPop.ls[modelData[0]]
                            onMoved: { const o = {}; o[modelData[0]] = modelData[5] ? Math.round(value * 10) / 10 : Math.round(value); engine.setLoud(o) } } } } }
            ListView { objectName: "loudList"; Layout.fillWidth: true; Layout.preferredHeight: Math.min(260, count * 24); clip: true; model: Math.min(dp.chans, loudPop.ls.strips.length); spacing: 1
                delegate: Rectangle { width: ListView.view.width; height: 23; radius: 3; color: Theme.ov(0.04)
                    readonly property var u: loudPop.ls.strips[index] || ({})
                    readonly property var mt: (loudPop.tick, engine.meterAt(index))
                    readonly property var clsN: ["AUTO", "VOICE", "MUSIC", "INSTRUMENT"]
                    RowLayout { anchors.fill: parent; anchors.margins: 2; spacing: 5
                        Text { Layout.preferredWidth: 96; text: dp.chName(index); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; elide: Text.ElideRight }
                        Chip { objectName: "loudRide" + index; label: i18n.t("AUTO", i18n.lang); on: !!u.ride; tint: Theme.cyan; onHit: engine.setLoudStrip(index, { ride: !u.ride }) }
                        Chip { objectName: "loudGate" + index; label: i18n.t("ON/OFF", i18n.lang); on: !!u.gate; tint: Theme.amber; onHit: engine.setLoudStrip(index, { gate: !u.gate }) }
                        Chip { objectName: "loudCls" + index; label: i18n.t(clsN[(u.cls === undefined ? -1 : u.cls) + 1], i18n.lang); onHit: engine.setLoudStrip(index, { cls: ((u.cls === undefined ? -1 : u.cls) + 2) % 4 - 1 }) }
                        Text { Layout.preferredWidth: 64; text: Number(u.target || -18).toFixed(0) + " LUFS"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                        Text { Layout.preferredWidth: 72; text: i18n.t("IN", i18n.lang) + " " + loudPop.lu(mt ? mt.rideLufs : -120); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8 }
                        Text { Layout.fillWidth: true; color: mt && mt.rideGateDb < -0.01 ? Theme.dim : Theme.emerald; font.family: Theme.mono; font.pixelSize: 8; font.bold: true
                            text: !mt || (!u.ride && !u.gate) ? "" : mt.rideGateDb < -0.01 ? i18n.t("OFF", i18n.lang) + " " + Number(mt.rideGateDb).toFixed(0) + " dB" : mt.rideSusp ? i18n.t("HOLD", i18n.lang) : (mt.rideDb >= 0 ? "+" : "") + Number(mt.rideDb).toFixed(1) + " dB" } } } }
            RowLayout { Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Chip { label: i18n.t("CLOSE", i18n.lang); onHit: loudPop.close() } } } }
}
