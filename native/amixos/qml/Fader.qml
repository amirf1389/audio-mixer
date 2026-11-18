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
    property bool remoteHot: false    // LIVE SYNC: a phone or browser is moving this fader now: the cap lights cyan and glides to each new value
    property bool numbers: false      // the dB figures printed beside the scale marks (the mixer strips)
    property color tint: Theme.orange // the cap's line and the bubble: the channel's colour on a strip
    readonly property bool unity: Math.abs(value) < 0.05   // exactly 0 dB: the cap's line turns green
    signal moved(real db)
    function lvl(db) { return Math.max(0, Math.min(1, (db - from) / (to - from))) }
    implicitWidth: numbers ? 44 : 34
    readonly property real cx: numbers ? width - 15 : width / 2   // the middle of the track: to the right when the figures are printed on the left
    implicitHeight: 180
    function pos(db) { return (1 - (db - from) / (to - from)) * (height - knob.height) }
    function dbAt(y) { var t = 1 - Math.max(0, Math.min(1, (y - knob.height / 2) / (height - knob.height))); return from + t * (to - from) }
    Rectangle { x: fader.cx - width / 2; width: 10; height: parent.height; radius: 3; color: Theme.deep; border.color: Theme.lineSoft
        // IN (cyan, left) and OUT (orange, right) level columns, bottom up; the band above 0 dB turns red
        Rectangle { x: 1; width: 4; anchors.bottom: parent.bottom; anchors.bottomMargin: 1; radius: 2; height: fader.lvl(fader.inDb) * (parent.height - 2); color: fader.inDb > 0 ? Theme.danger : Theme.cyan; opacity: fader.showLevels && fader.inDb > -119 ? 0.9 : 0 }
        Rectangle { x: 5; width: 4; anchors.bottom: parent.bottom; anchors.bottomMargin: 1; radius: 2; height: fader.lvl(fader.outDb) * (parent.height - 2); color: fader.outDb > 0 || fader.clip ? Theme.danger : Theme.orange; opacity: fader.showLevels && fader.outDb > -119 ? 0.9 : 0 }
        Rectangle { x: 0; y: fader.pos(0) + knob.height / 2; width: parent.width; height: 1; color: Theme.ov(0.35) }   // the 0 dB line across the track
    }
    Repeater {   // the scale: marks, and the dB figures where there is room for them
        model: fader.numbers ? [10, 5, 0, -5, -10, -20, -30, -40, -60] : [10, 0, -10, -20, -40, -60]
        Item { y: fader.pos(modelData) + knob.height / 2; width: fader.width; height: 1
            Rectangle { x: fader.numbers ? fader.cx - 15 : fader.cx + 6; width: modelData === 0 ? 9 : 5; height: modelData === 0 ? 2 : 1; y: modelData === 0 ? -0.5 : 0; color: modelData === 0 ? fader.tint : Theme.dim }
            Text { visible: fader.numbers; x: 0; width: fader.cx - 17; horizontalAlignment: Text.AlignRight; anchors.verticalCenter: parent.verticalCenter
                text: modelData === -60 ? "∞" : (modelData > 0 ? "+" : "") + modelData; color: modelData === 0 ? Theme.ink : Theme.muted; font.family: Theme.mono; font.pixelSize: modelData === -60 ? 9 : 7; font.bold: modelData === 0 } }
    }
    Rectangle {
        id: knob
        x: fader.cx - width / 2
        y: fader.pos(fader.value)
        Behavior on y { enabled: fader.remoteHot && !drag.pressed; NumberAnimation { duration: 70; easing.type: Easing.OutQuad } }   // smooth between remote updates
        width: 26; height: 40; radius: 4
        gradient: Gradient { GradientStop { position: 0; color: "#3a4150" } GradientStop { position: 0.5; color: "#1c222d" } GradientStop { position: 1; color: "#2a3140" } }
        border.color: drag.pressed ? Theme.orange : fader.remoteHot ? Theme.cyan : Theme.line; border.width: fader.remoteHot ? 2 : 1
        Rectangle { visible: fader.remoteHot; anchors.centerIn: parent; width: parent.width + 10; height: parent.height + 10; radius: 7; color: "transparent"; border.color: Theme.cyan; border.width: 2; opacity: 0.45; z: -1 }   // the glow of a remote hand
        Rectangle { anchors.centerIn: parent; width: parent.width - 6; height: fader.unity ? 3 : 2; radius: 1; color: fader.unity ? Theme.emerald : fader.tint }   // green at exactly 0 dB
        // THE LEVEL BUBBLE: the figure beside the cap while the fader is held or the wheel turns it
        Rectangle { id: bubble; objectName: "faderBubble"; visible: drag.pressed || bubbleT.running; z: 5; anchors.verticalCenter: parent.verticalCenter; x: fader.numbers ? -width - 3 : parent.width + 3
            width: bubT.implicitWidth + 10; height: 18; radius: 6; color: fader.tint; border.color: Qt.rgba(0, 0, 0, 0.4)
            Text { id: bubT; anchors.centerIn: parent; text: fader.value <= fader.from + 0.05 ? "-∞" : (fader.value > 0 ? "+" : "") + fader.value.toFixed(1) + " dB"; color: "#0b0f14"; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } }
    }
    Timer { id: bubbleT; interval: 900 }
    MouseArea {
        id: drag
        enabled: !access.surfaceLocked   // ACCOUNT > LOCK CONSOLE: the surface is read-only until the PIN
        anchors.fill: parent
        property real y0: 0; property real v0: 0
        // a plain drag follows the pointer (and catches at 0 dB); with SHIFT held the fader moves finely: 1 dB for 50 pixels, from where it was
        onPressed: function (m) { y0 = m.y; v0 = fader.value; if (m.modifiers & Qt.ShiftModifier) return; var v = fader.dbAt(m.y); fader.value = v; fader.moved(v) }
        onPositionChanged: function (m) { if (!pressed) return; var v
            if (m.modifiers & Qt.ShiftModifier) v = Math.max(fader.from, Math.min(fader.to, Math.round((v0 + (y0 - m.y) / 50) * 10) / 10))
            else { v = fader.dbAt(m.y); if (Math.abs(v) < 0.4) v = 0; y0 = m.y; v0 = v }
            fader.value = v; fader.moved(v) }
        onDoubleClicked: { fader.value = 0; fader.moved(0) }
        // the wheel: half a dB a notch, a tenth with SHIFT
        onWheel: function (w) { var st = (w.modifiers & Qt.ShiftModifier) ? 0.1 : 0.5; var v = Math.max(fader.from, Math.min(fader.to, Math.round((fader.value + (w.angleDelta.y > 0 ? st : -st)) * 10) / 10)); fader.value = v; fader.moved(v); bubbleT.restart() }
    }
}
