// A rotary knob like the ones on a plug-in or an amplifier: drag up / down (Shift = fine), wheel, double-click = default. value 0..1.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)

Item {
    id: k
    property real value: 0.5
    property real defaultValue: 0.5
    property string label: ""
    property string valueText: ""
    property color tint: "#ea580c"
    property color cap: "#1b1f27"
    property color ink: "#e5e7eb"
    property real size: 64
    signal moved(real v)
    implicitWidth: Math.max(size + 16, lab.implicitWidth + 6); implicitHeight: size + 34
    onValueChanged: face.requestPaint(); onTintChanged: face.requestPaint()
    Canvas { id: face; width: k.size; height: k.size; anchors.horizontalCenter: parent.horizontalCenter
        onPaint: { const c = getContext("2d"), w = width, r = w / 2, a0 = Math.PI * 0.75, a1 = Math.PI * 2.25, a = a0 + (a1 - a0) * k.value; c.clearRect(0, 0, w, w)
            c.lineCap = "round"; c.lineWidth = w * 0.07; c.strokeStyle = "rgba(0,0,0,0.55)"; c.beginPath(); c.arc(r, r, r - c.lineWidth, a0, a1); c.stroke()
            c.strokeStyle = k.tint; c.beginPath(); c.arc(r, r, r - c.lineWidth, a0, a); c.stroke()
            const g = c.createRadialGradient(r - w * 0.12, r - w * 0.16, w * 0.05, r, r, r * 0.74); g.addColorStop(0, Qt.lighter(k.cap, 1.9)); g.addColorStop(1, k.cap)
            c.fillStyle = "rgba(0,0,0,0.45)"; c.beginPath(); c.arc(r, r + w * 0.03, r * 0.72, 0, 2 * Math.PI); c.fill()
            c.fillStyle = g; c.beginPath(); c.arc(r, r, r * 0.70, 0, 2 * Math.PI); c.fill(); c.lineWidth = 1; c.strokeStyle = "rgba(255,255,255,0.14)"; c.stroke()
            c.strokeStyle = k.ink; c.lineWidth = w * 0.055; c.beginPath(); c.moveTo(r + Math.cos(a) * r * 0.28, r + Math.sin(a) * r * 0.28); c.lineTo(r + Math.cos(a) * r * 0.62, r + Math.sin(a) * r * 0.62); c.stroke() }
        MouseArea { anchors.fill: parent; property real y0: 0; property real v0: 0; cursorShape: Qt.SizeVerCursor
            onPressed: function (m) { y0 = m.y; v0 = k.value }
            onPositionChanged: function (m) { if (!pressed) return; const v = Math.max(0, Math.min(1, v0 + (y0 - m.y) / ((m.modifiers & Qt.ShiftModifier) ? 900 : 180))); if (v !== k.value) { k.value = v; k.moved(v) } }
            onDoubleClicked: { k.value = k.defaultValue; k.moved(k.value) }
            onWheel: function (w) { const v = Math.max(0, Math.min(1, k.value + (w.angleDelta.y > 0 ? 0.02 : -0.02))); k.value = v; k.moved(v) } } }
    Text { id: lab; anchors.top: face.bottom; anchors.topMargin: 3; anchors.horizontalCenter: parent.horizontalCenter; text: k.label; color: k.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; font.letterSpacing: 1 }
    Text { anchors.top: lab.bottom; anchors.topMargin: 1; anchors.horizontalCenter: parent.horizontalCenter; text: k.valueText; color: k.tint; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
}
