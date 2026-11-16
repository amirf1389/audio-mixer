// GAIN and the analog 3-BAND EQ on a mixer strip, as on the phone and the web remote: GAIN (the input trim, ±24 dB), an EQ in/out key,
// HI (10 kHz shelf), MID (bell) with its FREQ (100 Hz .. 8 kHz) and LO (100 Hz shelf), ±15 dB. Drag a knob up / down or turn the wheel;
// double-click centres it. The same settings as the channel panel, so LIVE SYNC carries them to the phones and browsers.
import QtQuick
import QtQuick.Layouts
import AmixOS

ColumnLayout {
    id: tone
    property int strip: 0
    property var st: null                         // the strip's state (changes when anything on the strip changes)
    readonly property var eq: (st, engine.channelEq(strip))
    readonly property real trimDb: (st, engine.trim(strip))
    spacing: 1
    function setEq(k, v) { const e = Object.assign({ low: 0, mid: 0, midFreq: 1000, high: 0 }, tone.eq); e[k] = v
        engine.setChannelEq(strip, e.low, e.mid, e.midFreq, e.high, true) }

    component Knob: Item {
        id: kn
        property string label
        property real lo: -15
        property real hi: 15
        property real value: 0
        property real home: 0
        property bool logScale: false
        property color tint: Theme.accent
        property bool dim: false
        signal moved(real v)
        Layout.alignment: Qt.AlignHCenter; implicitWidth: 34; implicitHeight: 34
        readonly property real pos: logScale ? Math.log(value / lo) / Math.log(hi / lo) : (value - lo) / (hi - lo)
        function at(p) { p = Math.max(0, Math.min(1, p)); const v = logScale ? lo * Math.pow(hi / lo, p) : lo + p * (hi - lo); return logScale ? Math.round(v) : Math.round(v * 2) / 2 }
        Text { anchors.horizontalCenter: parent.horizontalCenter; y: 0; text: kn.label; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 6; font.bold: true }
        Rectangle { id: cap; anchors.horizontalCenter: parent.horizontalCenter; y: 8; width: 18; height: 18; radius: 9; color: Theme.deep; border.color: Theme.lineSoft
            Rectangle { width: 2; height: 7; radius: 1; x: parent.width / 2 - 1; y: 2; color: kn.dim ? Theme.dim : kn.tint; transformOrigin: Item.Bottom
                transform: Rotation { origin.x: 1; origin.y: 7; angle: -135 + 270 * Math.max(0, Math.min(1, kn.pos)) } } }
        Text { anchors.horizontalCenter: parent.horizontalCenter; y: 27; text: kn.logScale ? (kn.value >= 1000 ? (kn.value / 1000).toFixed(1) + "k" : Math.round(kn.value)) : (kn.value > 0 ? "+" : "") + kn.value.toFixed(1)
            color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 6 }
        MouseArea { anchors.fill: parent; cursorShape: Qt.SizeVerCursor; property real y0; property real p0
            onPressed: function (m) { y0 = m.y; p0 = kn.pos; engine.selectedStrip = tone.strip }
            onPositionChanged: function (m) { if (pressed) kn.moved(kn.at(p0 + (y0 - m.y) / 120)) }
            onDoubleClicked: kn.moved(kn.home)
            onWheel: function (w) { kn.moved(kn.at(kn.pos + (w.angleDelta.y > 0 ? 0.03 : -0.03))) } } }

    Knob { label: "GAIN"; lo: -24; hi: 24; value: tone.trimDb; tint: Theme.amber; onMoved: function (v) { engine.setTrim(tone.strip, v) } }
    Rectangle { Layout.alignment: Qt.AlignHCenter; implicitWidth: 30; implicitHeight: 12; radius: 3
        readonly property bool on: !!(tone.eq && tone.eq.on)
        color: on ? Theme.amber : Theme.ov(0.06); border.color: Theme.lineSoft
        Text { anchors.centerIn: parent; text: "EQ"; color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true }
        MouseArea { anchors.fill: parent; onClicked: { const e = Object.assign({ low: 0, mid: 0, midFreq: 1000, high: 0 }, tone.eq); engine.setChannelEq(tone.strip, e.low, e.mid, e.midFreq, e.high, !parent.on) } } }
    Knob { label: "HI"; value: tone.eq ? tone.eq.high || 0 : 0; dim: !(tone.eq && tone.eq.on); onMoved: function (v) { tone.setEq("high", v) } }
    Knob { label: "MID"; value: tone.eq ? tone.eq.mid || 0 : 0; dim: !(tone.eq && tone.eq.on); onMoved: function (v) { tone.setEq("mid", v) } }
    Knob { label: "FREQ"; lo: 100; hi: 8000; logScale: true; home: 1000; tint: Theme.cyan; value: tone.eq && tone.eq.midFreq ? tone.eq.midFreq : 1000; dim: !(tone.eq && tone.eq.on); onMoved: function (v) { tone.setEq("midFreq", v) } }
    Knob { label: "LO"; value: tone.eq ? tone.eq.low || 0 : 0; dim: !(tone.eq && tone.eq.on); onMoved: function (v) { tone.setEq("low", v) } }
}
