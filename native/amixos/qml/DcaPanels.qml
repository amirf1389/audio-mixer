// The MIXER's group tools, three panels over the page:
//   DCA ASSIGN   pick a group, then click channels in or out of it (a channel is in one DCA at most)
//   AUTO DCA     listens to every channel for a few seconds and proposes VOICES / MUSIC / INSTRUMENTS groups; channels with no signal are
//                flagged "not needed". The classifier is fixed rules on measured features (audiomixer/classify.hpp): nothing is sent
//                anywhere. Nothing is assigned until APPLY; a row's class can be changed by clicking it first.
//   DUCKER       VOICE OVER MUSIC: while the KEY (a voice group and / or channels) is above THRESHOLD, the TARGET goes down by DEPTH
//                (attack / hold / release), and the voices can be lifted a little. Runs in the engine, sample by sample.
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
}
