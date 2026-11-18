// GAIN and the analog 3-BAND EQ on a mixer strip, as on the phone and the web remote: GAIN (the input trim, ±24 dB), an EQ in/out key,
// HI (10 kHz shelf), MID (bell) with its FREQ (100 Hz .. 8 kHz) and LO (100 Hz shelf), ±15 dB. Drag a knob up / down or turn the wheel;
// double-click centres it. The same settings as the channel panel, so LIVE SYNC carries them to the phones and browsers.
import QtQuick
import QtQuick.Layouts
import AmixOS

Rectangle {
    id: tone
    property int strip: 0
    property var st: null                         // the strip's state (changes when anything on the strip changes)
    readonly property var eq: (st, engine.channelEq(strip))
    readonly property real trimDb: (st, engine.trim(strip))
    readonly property bool eqOn: !!(eq && eq.on)
    // A CARD: the tone section sits on its own rounded panel, with a sliding EQ switch in its header
    implicitWidth: grid.implicitWidth + 12; implicitHeight: grid.implicitHeight + 12; radius: 10
    color: Theme.ov(0.05); border.width: 1; border.color: eqOn ? Qt.rgba(0.29, 0.87, 0.5, 0.45) : Theme.lineSoft
    function setEq(k, v) { const e = Object.assign({ low: 0, mid: 0, midFreq: 1000, high: 0 }, tone.eq); e[k] = v
        engine.setChannelEq(strip, e.low, e.mid, e.midFreq, e.high, true) }

    // A MODERN KNOB: a dim ring all the way round, the value drawn over it as a bright arc in the band's colour (from the middle for a
    // boost / cut, from the left for FREQ), a dark cap with a bright dot for the pointer. Drag up / down, the wheel, double-click = home.
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
        Layout.alignment: Qt.AlignHCenter; implicitWidth: 44; implicitHeight: 50
        readonly property real pos: Math.max(0, Math.min(1, logScale ? Math.log(value / lo) / Math.log(hi / lo) : (value - lo) / (hi - lo)))
        readonly property real homePos: logScale ? 0 : (home - lo) / (hi - lo)
        readonly property color hue: kn.dim ? Theme.dim : kn.tint
        function at(p) { p = Math.max(0, Math.min(1, p)); const v = logScale ? lo * Math.pow(hi / lo, p) : lo + p * (hi - lo); return logScale ? Math.round(v) : Math.round(v * 2) / 2 }
        onPosChanged: ring.requestPaint(); onHueChanged: ring.requestPaint()
        Text { anchors.horizontalCenter: parent.horizontalCenter; y: 0; text: kn.label; color: kn.hue; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
        Canvas { id: ring; anchors.horizontalCenter: parent.horizontalCenter; y: 10; width: 30; height: 30
            onPaint: { const c = getContext("2d"), r = 15, a0 = Math.PI * 0.75, a1 = Math.PI * 2.25, ap = a0 + (a1 - a0) * kn.pos, ah = a0 + (a1 - a0) * kn.homePos; c.reset(); c.lineCap = "round"; c.lineWidth = 3
                c.strokeStyle = Theme.dark ? "rgba(255,255,255,0.14)" : "rgba(0,0,0,0.16)"; c.beginPath(); c.arc(r, r, r - 2.5, a0, a1); c.stroke()
                c.strokeStyle = kn.hue; c.beginPath(); c.arc(r, r, r - 2.5, Math.min(ah, ap), Math.max(ah, ap) + 0.001); c.stroke() }
            Component.onCompleted: requestPaint() }
        Rectangle { id: cap; anchors.centerIn: ring; width: 18; height: 18; radius: 9; border.width: 1; border.color: Theme.line
            gradient: Gradient { GradientStop { position: 0; color: Theme.dark ? "#3a4252" : "#ffffff" } GradientStop { position: 1; color: Theme.dark ? "#141922" : "#d9dee6" } }
            Item { anchors.fill: parent; rotation: -135 + 270 * kn.pos
                Rectangle { width: 4; height: 4; radius: 2; x: parent.width / 2 - 2; y: 2; color: kn.dim ? Theme.muted : kn.tint } } }
        Text { anchors.horizontalCenter: parent.horizontalCenter; y: 40; text: kn.logScale ? (kn.value >= 1000 ? (kn.value / 1000).toFixed(1) + "k" : Math.round(kn.value)) : (kn.value > 0 ? "+" : "") + kn.value.toFixed(1)
            color: kn.hue; font.family: Theme.mono; font.pixelSize: 7; font.bold: true }
        MouseArea { anchors.fill: parent; cursorShape: Qt.SizeVerCursor; property real y0; property real p0
            onPressed: function (m) { y0 = m.y; p0 = kn.pos; engine.selectedStrip = tone.strip }
            onPositionChanged: function (m) { if (pressed) kn.moved(kn.at(p0 + (y0 - m.y) / 120)) }
            onDoubleClicked: kn.moved(kn.home)
            onWheel: function (w) { kn.moved(kn.at(kn.pos + (w.angleDelta.y > 0 ? 0.03 : -0.03))) } } }

    GridLayout { id: grid; anchors.centerIn: parent; columns: 2; columnSpacing: 4; rowSpacing: 3
        Knob { label: "GAIN"; lo: -24; hi: 24; value: tone.trimDb; tint: Theme.ink; onMoved: function (v) { engine.setTrim(tone.strip, v) } }
        // THE EQ SWITCH: a sliding pill, green when the EQ is in
        Item { objectName: "eqSwitch" + tone.strip; Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter; implicitWidth: 44; implicitHeight: 34
        Text { anchors.horizontalCenter: parent.horizontalCenter; y: 0; text: "EQ"; color: tone.eqOn ? "#4ade80" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
        Rectangle { id: track; anchors.horizontalCenter: parent.horizontalCenter; y: 12; width: 34; height: 18; radius: 9; color: tone.eqOn ? "#4ade80" : Theme.ov(0.14); border.width: 1; border.color: tone.eqOn ? "#4ade80" : Theme.line
            Behavior on color { ColorAnimation { duration: 120 } }
            Rectangle { width: 14; height: 14; radius: 7; y: 2; x: tone.eqOn ? parent.width - 16 : 2; color: tone.eqOn ? "#0b0f14" : Theme.ink; Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } } } }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { engine.selectedStrip = tone.strip; const e = Object.assign({ low: 0, mid: 0, midFreq: 1000, high: 0 }, tone.eq); engine.setChannelEq(tone.strip, e.low, e.mid, e.midFreq, e.high, !tone.eqOn) } } }
        Knob { label: "HI"; tint: "#38bdf8"; value: tone.eq ? tone.eq.high || 0 : 0; dim: !tone.eqOn; onMoved: function (v) { tone.setEq("high", v) } }
        Knob { label: "MID"; tint: "#4ade80"; value: tone.eq ? tone.eq.mid || 0 : 0; dim: !tone.eqOn; onMoved: function (v) { tone.setEq("mid", v) } }
        Knob { label: "LO"; tint: "#fb7185"; value: tone.eq ? tone.eq.low || 0 : 0; dim: !tone.eqOn; onMoved: function (v) { tone.setEq("low", v) } }
        Knob { label: "FREQ"; lo: 100; hi: 8000; logScale: true; home: 1000; tint: "#facc15"; value: tone.eq && tone.eq.midFreq ? tone.eq.midFreq : 1000; dim: !tone.eqOn; onMoved: function (v) { tone.setEq("midFreq", v) } }
    }
}
