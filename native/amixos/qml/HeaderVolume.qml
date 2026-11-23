// THE MASTER VOLUME KNOB in the header, always in view: drag up / down (Shift = fine), the wheel steps it by the volume-key step
// (1/2/3 dB), a click mutes / un-mutes, the dB readout beside it. Like a Fader, one engine write per frame while turning (the newest value).
// APPS opens the per-app volume (the Windows volume mixer's programs).
import QtQuick
import QtQuick.Controls
import AmixOS

Row {
    id: hv
    spacing: 6
    signal appsClicked()
    readonly property bool muted: engine.masterMuted
    readonly property real db: engine.masterDb
    property real shownDb: db
    onDbChanged: if (!drag.active) shownDb = db
    function pos(d) { return Math.max(0, Math.min(1, (d + 60) / 70)) }   // -60 (off) .. +10 dB along the arc
    function fromPos(p) { return Math.round((Math.max(0, Math.min(1, p)) * 70 - 60) * 10) / 10 }
    // coalesced writes, as Fader.qml: the knob follows every report, the engine gets one write a frame
    property real pendingDb: NaN
    function send(v) { shownDb = v; if (sendT.running) { pendingDb = v; return } pendingDb = NaN; engine.masterDb = v; sendT.start() }
    Timer { id: sendT; interval: 16; onTriggered: if (!isNaN(hv.pendingDb)) { const v = hv.pendingDb; hv.pendingDb = NaN; engine.masterDb = v; sendT.start() } }

    Item {
        id: knob; objectName: "headerKnob"; width: 40; height: 40; anchors.verticalCenter: parent.verticalCenter
        Accessible.role: Accessible.Dial; Accessible.name: i18n.t("Master volume", i18n.lang); Accessible.description: volume.dbText(hv.shownDb) + " dB"
        Canvas {
            id: face; anchors.fill: parent
            property real p: hv.pos(hv.shownDb); property bool m: hv.muted
            onPChanged: requestPaint(); onMChanged: requestPaint()
            Connections { target: Theme; function onDarkChanged() { face.requestPaint() } }
            onPaint: {
                const c = getContext("2d"); c.reset(); const r = width / 2, a0 = Math.PI * 0.75, span = Math.PI * 1.5;
                c.lineWidth = 4; c.lineCap = "round";
                c.strokeStyle = Theme.ov(0.14); c.beginPath(); c.arc(r, r, r - 3, a0, a0 + span); c.stroke();
                c.strokeStyle = m ? Theme.danger : Theme.cyan; c.beginPath(); c.arc(r, r, r - 3, a0, a0 + span * Math.max(0.001, p)); c.stroke();
                c.fillStyle = Theme.cardRaised; c.beginPath(); c.arc(r, r, r - 8, 0, Math.PI * 2); c.fill();
                const a = a0 + span * p; c.strokeStyle = m ? Theme.danger : Theme.ink; c.lineWidth = 2.5;
                c.beginPath(); c.moveTo(r + Math.cos(a) * (r - 18), r + Math.sin(a) * (r - 18)); c.lineTo(r + Math.cos(a) * (r - 10), r + Math.sin(a) * (r - 10)); c.stroke();
            }
        }
        Text { anchors.centerIn: parent; visible: hv.muted; text: "✕"; color: Theme.danger; font.pixelSize: 11; font.bold: true }
        DragHandler {
            id: drag; target: null; xAxis.enabled: false
            property real startP: 0
            onActiveChanged: { if (active) startP = hv.pos(hv.shownDb); else { sendT.stop(); if (!isNaN(hv.pendingDb)) { engine.masterDb = hv.pendingDb; hv.pendingDb = NaN } } }
            onTranslationChanged: if (active) hv.send(hv.fromPos(startP - translation.y / ((centroid.modifiers & Qt.ShiftModifier) ? 600 : 150)))
        }
        TapHandler { onTapped: engine.masterMuted = !engine.masterMuted }
        WheelHandler { onWheel: (e) => { const d = e.angleDelta.y > 0 ? 1 : e.angleDelta.y < 0 ? -1 : 0; if (d !== 0) volume.stepMaster(d) } }
        HoverHandler { id: kH }
        ToolTip.visible: kH.hovered; ToolTip.text: i18n.t("MASTER VOLUME: drag or wheel to set, click to mute", i18n.lang)
    }
    Column {
        anchors.verticalCenter: parent.verticalCenter; spacing: 1
        Text { text: i18n.t("MASTER", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.bold: true; font.letterSpacing: 1 }
        Text { objectName: "headerVolumeDb"; text: hv.muted ? i18n.t("MUTED", i18n.lang) : volume.dbText(hv.shownDb) + " dB"; color: hv.muted ? Theme.danger : Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; width: 64 }
    }
    Rectangle {
        anchors.verticalCenter: parent.verticalCenter; implicitWidth: apT.implicitWidth + 14; width: implicitWidth; height: 22; radius: 6
        color: apH.hovered ? Theme.ov(0.12) : Theme.ov(0.06); border.color: Theme.lineSoft
        Text { id: apT; anchors.centerIn: parent; text: "≡ " + i18n.t("APPS", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
        HoverHandler { id: apH } MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: hv.appsClicked() }
        ToolTip.visible: apH.hovered; ToolTip.text: i18n.t("Per-app volume: each program's level, as in the Windows volume mixer", i18n.lang)
    }
}
