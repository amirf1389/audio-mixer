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
    signal moved(real db)
    function lvl(db) { return Math.max(0, Math.min(1, (db - from) / (to - from))) }
    property bool printed: false      // the dB numbers printed beside the slot, the way a desk's panel has them (the analog channel strips)
    implicitWidth: printed ? 46 : 34
    implicitHeight: 180
    readonly property real slotX: printed ? width - 15 : width / 2   // the middle of the slot: to the right when the numbers are printed on the left
    function pos(db) { return (1 - (db - from) / (to - from)) * (height - knob.height) }
    function dbAt(y) { var t = 1 - Math.max(0, Math.min(1, (y - knob.height / 2) / (height - knob.height))); return from + t * (to - from) }
    // THE SLOT: a recess milled into the panel, dark inside, a light lip on its right edge
    Rectangle { x: fader.slotX - width / 2; y: knob.height / 2 - 4; width: fader.showLevels ? 10 : 6; height: parent.height - knob.height + 8; radius: 3; color: "#050506"; border.color: "#000"
        Rectangle { anchors.right: parent.right; anchors.rightMargin: -1; width: 1; height: parent.height - 4; y: 2; color: Qt.rgba(1, 1, 1, 0.16) }
        // IN (cyan, left) and OUT (orange, right) level columns, bottom up; the band above 0 dB turns red
        Rectangle { x: 1; width: 4; anchors.bottom: parent.bottom; anchors.bottomMargin: 1; radius: 2; height: fader.lvl(fader.inDb) * (parent.height - 2); color: fader.inDb > 0 ? Theme.danger : Theme.cyan; opacity: fader.showLevels && fader.inDb > -119 ? 0.9 : 0 }
        Rectangle { x: 5; width: 4; anchors.bottom: parent.bottom; anchors.bottomMargin: 1; radius: 2; height: fader.lvl(fader.outDb) * (parent.height - 2); color: fader.outDb > 0 || fader.clip ? Theme.danger : Theme.orange; opacity: fader.showLevels && fader.outDb > -119 ? 0.9 : 0 }
    }
    Repeater {   // the printed scale
        model: fader.printed ? [10, 5, 0, -5, -10, -20, -30, -40, -60] : [10, 0, -10, -20, -40, -60]
        Item { y: fader.pos(modelData) + knob.height / 2; width: fader.width; height: 1
            Rectangle { x: fader.printed ? fader.slotX - 16 : fader.slotX + 6; width: modelData === 0 ? 10 : (fader.printed ? 7 : 5); height: modelData === 0 && fader.printed ? 2 : 1; y: modelData === 0 && fader.printed ? -0.5 : 0
                color: fader.printed ? Theme.engrave : (modelData === 0 ? Theme.orange : Theme.dim); opacity: fader.printed && modelData !== 0 ? 0.75 : 1 }
            Text { visible: fader.printed; anchors.right: parent.left; anchors.rightMargin: -(fader.slotX - 18); anchors.verticalCenter: parent.verticalCenter
                text: modelData === -60 ? "∞" : Math.abs(modelData); color: Theme.engrave; opacity: modelData === 0 ? 1 : 0.8; font.family: Theme.sans; font.pixelSize: modelData === -60 ? 9 : 7; font.bold: true } }
    }
    // THE CAP: black, ridged for the finger, a white line across the middle
    Rectangle {
        id: knob
        x: fader.slotX - width / 2
        y: fader.pos(fader.value)
        Behavior on y { enabled: fader.remoteHot && !drag.pressed; NumberAnimation { duration: 70; easing.type: Easing.OutQuad } }   // smooth between remote updates
        width: 24; height: 42; radius: 3
        gradient: Gradient { GradientStop { position: 0; color: "#6a6d73" } GradientStop { position: 0.12; color: "#33353a" } GradientStop { position: 0.5; color: "#121315" } GradientStop { position: 0.88; color: "#2b2d31" } GradientStop { position: 1; color: "#0a0a0b" } }
        border.color: drag.pressed ? Theme.orange : fader.remoteHot ? Theme.cyan : "#000"; border.width: fader.remoteHot ? 2 : 1
        Rectangle { visible: fader.remoteHot; anchors.centerIn: parent; width: parent.width + 10; height: parent.height + 10; radius: 7; color: "transparent"; border.color: Theme.cyan; border.width: 2; opacity: 0.45; z: -1 }   // the glow of a remote hand
        Rectangle { z: -2; x: 2; y: 4; width: parent.width; height: parent.height; radius: 3; color: Qt.rgba(0, 0, 0, 0.45) }   // its shadow on the panel
        Repeater { model: [6, 10, 14, 27, 31, 35]; Rectangle { x: 3; y: modelData; width: knob.width - 6; height: 1; color: Qt.rgba(1, 1, 1, 0.13) } }
        Rectangle { anchors.centerIn: parent; width: parent.width - 4; height: 2; color: "#f4f1e8" }
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
