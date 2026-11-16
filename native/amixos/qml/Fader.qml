// A console fader: -60 .. +10 dB with a 0 dB detent, drag or wheel, touch friendly. The track is also the strip's INPUT / OUTPUT indicator:
// the left column fills cyan with the input level as the driver delivers it (after the converter, before the fader; inDb from the engine's
// snapshot of the device block), the right column fills orange with the output level after the fader (peakDb, what goes to the bus / driver).
// Both read the same dBFS scale as the fader (-60 .. +10); red at the top means the converter or the output stage clipped.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)

Item {
    id: fader
    property real value: 0            // dB
    property real from: -60
    property real to: 10
    property real inDb: -120          // the input level (dBFS) shown in the track; -120 = no meter
    property real outDb: -120         // the output level (dBFS)
    property bool clip: false
    property bool showLevels: true    // false where the strip has its own IN / OUT meter column: the track is not drawn over twice
    signal moved(real db)
    function lvl(db) { return Math.max(0, Math.min(1, (db - from) / (to - from))) }
    implicitWidth: 34
    implicitHeight: 180
    function pos(db) { return (1 - (db - from) / (to - from)) * (height - knob.height) }
    function dbAt(y) { var t = 1 - Math.max(0, Math.min(1, (y - knob.height / 2) / (height - knob.height))); return from + t * (to - from) }
    Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 10; height: parent.height; radius: 3; color: Theme.deep; border.color: Theme.lineSoft
        // IN (cyan, left) and OUT (orange, right) level columns, bottom up; the band above 0 dB turns red
        Rectangle { x: 1; width: 4; anchors.bottom: parent.bottom; anchors.bottomMargin: 1; radius: 2; height: fader.lvl(fader.inDb) * (parent.height - 2); color: fader.inDb > 0 ? Theme.danger : Theme.cyan; opacity: fader.showLevels && fader.inDb > -119 ? 0.9 : 0 }
        Rectangle { x: 5; width: 4; anchors.bottom: parent.bottom; anchors.bottomMargin: 1; radius: 2; height: fader.lvl(fader.outDb) * (parent.height - 2); color: fader.outDb > 0 || fader.clip ? Theme.danger : Theme.orange; opacity: fader.showLevels && fader.outDb > -119 ? 0.9 : 0 }
        Rectangle { anchors.horizontalCenter: parent.horizontalCenter; y: fader.pos(0) + knob.height / 2; width: parent.width; height: 1; color: Theme.ov(0.35) }   // the 0 dB line across the track
    }
    Repeater {   // scale marks: +10, 0, -10, -20, -40, -60
        model: [10, 0, -10, -20, -40, -60]
        Rectangle { x: parent.width / 2 + 6; y: fader.pos(modelData) + knob.height / 2; width: modelData === 0 ? 10 : 5; height: 1; color: modelData === 0 ? Theme.orange : Theme.dim }
    }
    Rectangle {
        id: knob
        anchors.horizontalCenter: parent.horizontalCenter
        y: fader.pos(fader.value)
        width: 26; height: 40; radius: 4
        gradient: Gradient { GradientStop { position: 0; color: "#3a4150" } GradientStop { position: 0.5; color: "#1c222d" } GradientStop { position: 1; color: "#2a3140" } }
        border.color: drag.pressed ? Theme.orange : Theme.line
        Rectangle { anchors.centerIn: parent; width: parent.width - 6; height: 2; color: Theme.orange }
    }
    MouseArea {
        id: drag
        enabled: !access.surfaceLocked   // ACCOUNT > LOCK CONSOLE: the surface is read-only until the PIN
        anchors.fill: parent
        onPressed: function (m) { var v = fader.dbAt(m.y); fader.value = v; fader.moved(v) }
        onPositionChanged: function (m) { if (pressed) { var v = fader.dbAt(m.y); if (Math.abs(v) < 0.4) v = 0; fader.value = v; fader.moved(v) } }
        onDoubleClicked: { fader.value = 0; fader.moved(0) }
        onWheel: function (w) { var v = Math.max(fader.from, Math.min(fader.to, fader.value + (w.angleDelta.y > 0 ? 0.5 : -0.5))); fader.value = v; fader.moved(v) }
    }
}
