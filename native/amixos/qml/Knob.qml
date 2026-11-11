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
    property bool vintage: false     // a black skirted knob with a white pointer and scale dots, the value above and the end marks below
    property string minText: ""
    property string maxText: ""
    property color paper: "#1c1c1c"   // lettering on a light faceplate (vintage)
    signal moved(real v)
    implicitWidth: Math.max(size + 16, lab.implicitWidth + 6); implicitHeight: size + (vintage ? 40 : 34)
    onValueChanged: face.requestPaint(); onTintChanged: face.requestPaint()
    Canvas { id: face; width: k.size; height: k.size; anchors.horizontalCenter: parent.horizontalCenter
        y: k.vintage ? k.size * 0.18 : 0   // room for the rendered knob's rim above the dial
        opacity: k.vintage ? 0 : 1
        onPaint: { const c = getContext("2d"), w = width, r = w / 2, a0 = Math.PI * 0.75, a1 = Math.PI * 2.25, a = a0 + (a1 - a0) * k.value; c.clearRect(0, 0, w, w)
            if (k.vintage) { for (let i = 0; i <= 10; i++) { const t = a0 + (a1 - a0) * i / 10; c.fillStyle = k.paper; c.beginPath(); c.arc(r + Math.cos(t) * (r - 2.5), r + Math.sin(t) * (r - 2.5), i % 5 === 0 ? 1.8 : 1.1, 0, 2 * Math.PI); c.fill() }
                c.fillStyle = "rgba(0,0,0,0.35)"; c.beginPath(); c.arc(r + 1.5, r + 3, r * 0.74, 0, 2 * Math.PI); c.fill()
                c.fillStyle = "#101010"; c.beginPath(); c.arc(r, r, r * 0.74, 0, 2 * Math.PI); c.fill()                                   // the skirt
                for (let i = 0; i < 24; i++) { const t = i / 24 * 2 * Math.PI + a; c.strokeStyle = "rgba(255,255,255,0.10)"; c.lineWidth = 1; c.beginPath(); c.moveTo(r + Math.cos(t) * r * 0.60, r + Math.sin(t) * r * 0.60); c.lineTo(r + Math.cos(t) * r * 0.73, r + Math.sin(t) * r * 0.73); c.stroke() }
                const g2 = c.createRadialGradient(r - w * 0.10, r - w * 0.14, w * 0.03, r, r, r * 0.56); g2.addColorStop(0, "#5a5a5a"); g2.addColorStop(0.5, "#242424"); g2.addColorStop(1, "#0c0c0c")
                c.fillStyle = g2; c.beginPath(); c.arc(r, r, r * 0.56, 0, 2 * Math.PI); c.fill()
                c.strokeStyle = "#f5f5f0"; c.lineWidth = w * 0.06; c.lineCap = "round"; c.beginPath(); c.moveTo(r + Math.cos(a) * r * 0.22, r + Math.sin(a) * r * 0.22); c.lineTo(r + Math.cos(a) * r * 0.70, r + Math.sin(a) * r * 0.70); c.stroke(); return }
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
    // vintage: the rendered knob (its light and shadow stay put) with the pointer turning on it, and printed scale marks around it
    Repeater { model: k.vintage ? 11 : 0; delegate: Rectangle { readonly property real t: (135 + 27 * index) * Math.PI / 180; width: index % 5 === 0 ? 3.2 : 2.2; height: width; radius: width / 2; color: k.paper; opacity: 0.9
            x: face.x + k.size / 2 + Math.cos(t) * (k.size * 0.52) - width / 2; y: face.y + k.size / 2 + Math.sin(t) * (k.size * 0.52) - height / 2 } }
    Image { visible: k.vintage; source: "qrc:/qt/qml/AmixOS/skin/knob.png"; width: k.size * 1.32; height: width; x: face.x + k.size / 2 - width / 2; y: face.y + k.size / 2 - height / 2; smooth: true; mipmap: true }
    Image { visible: k.vintage; source: "qrc:/qt/qml/AmixOS/skin/knob-pointer.png"; width: k.size * 1.32; height: width; x: face.x + k.size / 2 - width / 2; y: face.y + k.size / 2 - height / 2; smooth: true; mipmap: true; rotation: -135 + 270 * k.value }
    Text { visible: k.vintage; anchors.top: face.bottom; anchors.topMargin: 0; x: face.x - 6; text: k.minText; color: k.paper; opacity: 0.9; font.family: Theme.sans; font.pixelSize: 8; style: Text.Sunken; styleColor: "#60ffffff" }
    Text { visible: k.vintage; anchors.top: face.bottom; anchors.topMargin: 0; anchors.right: face.right; anchors.rightMargin: -6; text: k.maxText; color: k.paper; opacity: 0.9; font.family: Theme.sans; font.pixelSize: 8; style: Text.Sunken; styleColor: "#60ffffff" }
    Text { id: lab; anchors.top: face.bottom; anchors.topMargin: k.vintage ? 7 : 3; anchors.horizontalCenter: parent.horizontalCenter; text: k.label; color: k.vintage ? k.paper : k.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; font.letterSpacing: 1 }
    Text { anchors.top: lab.bottom; anchors.topMargin: 1; anchors.horizontalCenter: parent.horizontalCenter; text: k.valueText; color: k.vintage ? k.paper : k.tint; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
}
