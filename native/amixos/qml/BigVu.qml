// THE BIG STRIP VU: one shared meter for every fader page (MIXER, DAW look, AUX / FX sends, DCA, MONITOR, MASTERING, bus returns). Two bars
// (L | R) over a -60 .. +6 dBFS scale, a peak-hold line, a CLIP lamp that latches above 0 dBFS (and when the engine flags a clip), and a
// dB readout that flashes red while the clip is latched. Amber from -3 dBFS, red above 0 dBFS. A narrow strip (< 90 px) shows one merged
// bar (the louder side). A click on the meter clears the hold and the clip lamp. It reads the meter values it is given (the engine's
// existing snapshot, 30 a second); its own hold / flash timers run only while it is visible.
import QtQuick
import AmixOS

Item {
    id: vu
    objectName: "bigVu"
    property real lDb: -120             // peak dBFS, left (or mono)
    property real rDb: NaN              // peak dBFS, right; NaN = mono source: both bars show lDb
    property real rmsDb: -120           // optional body level (drawn under the peak)
    property bool clip: false           // the engine's own clip flag
    property real floorDb: -60
    property real topDb: 6
    property real warnDb: -3            // amber from here
    property bool merged: width < 90     // a strip passes its own width (< 90 px: one merged bar)
    readonly property real rVal: isNaN(rDb) ? lDb : rDb
    readonly property real maxDb: Math.max(lDb, rVal)
    property real holdDb: -120
    property bool latched: false
    implicitWidth: 60
    implicitHeight: 120
    // the strip height rule: ~28 % of the strip, at least 90 px, at most 220 px
    function sizeFor(stripH) { return Math.max(90, Math.min(220, stripH * 0.28)) }
    function norm(db) { return Math.max(0, Math.min(1, (db - floorDb) / (topDb - floorDb))) }
    function zone(db) { return db > 0 ? Theme.danger : db >= warnDb ? Theme.amber : Theme.emerald }
    function clear() { latched = false; holdDb = -120 }
    onMaxDbChanged: { if (maxDb > holdDb) { holdDb = maxDb; holdT.restart() } if (maxDb > 0) latched = true }
    onClipChanged: if (clip) latched = true
    Timer { id: holdT; interval: 1500; running: false; onTriggered: vu.holdDb = vu.maxDb }
    Timer { id: flashT; interval: 400; repeat: true; running: vu.latched && vu.visible; property bool on: false; onTriggered: on = !on; onRunningChanged: if (!running) on = false }
    Rectangle { anchors.fill: parent; radius: 4; color: Theme.deep; border.color: vu.latched ? Theme.danger : Theme.line }
    // the clip lamp, top middle
    Rectangle { objectName: "bigVuClip"; anchors.top: parent.top; anchors.topMargin: 3; anchors.horizontalCenter: parent.horizontalCenter; width: Math.min(28, parent.width - 8); height: 6; radius: 2
        color: vu.latched ? Theme.danger : Theme.ov(0.10); border.color: Theme.lineSoft }
    readonly property real top0: 12
    readonly property real bot0: 14
    readonly property real barH: Math.max(10, height - top0 - bot0)
    readonly property real scaleW: vu.merged ? 0 : 16
    // the scale on the left
    Repeater { model: vu.merged ? [] : [6, 0, -3, -10, -20, -40, -60]
        Text { x: 2; width: vu.scaleW - 3; horizontalAlignment: Text.AlignRight; y: vu.top0 + vu.barH * (1 - vu.norm(modelData)) - 4
            text: modelData > 0 ? "+" + modelData : String(modelData); color: modelData === 0 ? Theme.ink : Theme.muted; font.family: Theme.mono; font.pixelSize: 6 } }
    Row {
        id: bars
        x: vu.scaleW + 3; y: vu.top0; width: vu.width - x - 4; height: vu.barH; spacing: 2
        Repeater { model: vu.merged ? 1 : 2
            Item { width: vu.merged ? bars.width : (bars.width - 2) / 2; height: bars.height
                readonly property real db: vu.merged ? vu.maxDb : (index === 0 ? vu.lDb : vu.rVal)
                Rectangle { anchors.fill: parent; radius: 2; color: Theme.ov(0.05); border.color: Theme.lineSoft }
                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 1; radius: 1
                    height: (parent.height - 2) * vu.norm(Math.min(parent.db, vu.rmsDb > -119 ? Math.max(vu.rmsDb, parent.db - 12) : parent.db)); color: vu.zone(parent.db); opacity: 0.55 }
                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 1; height: 2; y: 1 + (parent.height - 2) * (1 - vu.norm(parent.db)) - 1
                    color: vu.zone(parent.db); visible: parent.db > vu.floorDb }
                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 1; radius: 1
                    height: (parent.height - 2) * vu.norm(parent.db); color: vu.zone(parent.db); opacity: 0.85 } } }
        }
    // the 0 dBFS and -3 dBFS lines across the bars
    Rectangle { x: bars.x; width: bars.width; height: 1; y: vu.top0 + vu.barH * (1 - vu.norm(0)); color: Theme.danger; opacity: 0.7 }
    Rectangle { x: bars.x; width: bars.width; height: 1; y: vu.top0 + vu.barH * (1 - vu.norm(vu.warnDb)); color: Theme.amber; opacity: 0.5 }
    // the peak-hold line
    Rectangle { objectName: "bigVuHold"; x: bars.x; width: bars.width; height: 2; y: vu.top0 + vu.barH * (1 - vu.norm(vu.holdDb)) - 1; visible: vu.holdDb > vu.floorDb; color: vu.zone(vu.holdDb) }
    Text { objectName: "bigVuReadout"; anchors.bottom: parent.bottom; anchors.bottomMargin: 1; anchors.horizontalCenter: parent.horizontalCenter
        text: vu.holdDb <= -119 ? "-inf" : (vu.holdDb > 0 ? "+" : "") + vu.holdDb.toFixed(1)
        color: vu.latched ? (flashT.on ? Theme.ink : Theme.danger) : vu.holdDb >= vu.warnDb ? Theme.amber : Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: vu.latched }
    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: vu.clear() }
}
