// A console fader: -60 .. +10 dB with a 0 dB detent, drag or wheel, touch friendly
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)

Item {
    id: fader
    property real value: 0            // dB
    property real from: -60
    property real to: 10
    signal moved(real db)
    implicitWidth: 34
    implicitHeight: 180
    function pos(db) { return (1 - (db - from) / (to - from)) * (height - knob.height) }
    function dbAt(y) { var t = 1 - Math.max(0, Math.min(1, (y - knob.height / 2) / (height - knob.height))); return from + t * (to - from) }
    Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 4; height: parent.height; radius: 2; color: "#05070a"; border.color: Theme.lineSoft }
    Repeater {   // scale marks: +10, 0, -10, -20, -40, -60
        model: [10, 0, -10, -20, -40, -60]
        Rectangle { x: parent.width / 2 + 6; y: fader.pos(modelData) + knob.height / 2; width: modelData === 0 ? 10 : 5; height: 1; color: modelData === 0 ? Theme.amber : Theme.dim }
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
