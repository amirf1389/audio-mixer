// A SONGKET BAND: the woven gold-on-dark diamond border of Malay songket cloth, drawn as a repeating motif (our own drawing: a row of
// diamonds with a dot in each, small diamonds between them and a fine line above and below). Painted once per size; nothing animates.
import QtQuick
import AmixOS

Item {
    id: sk
    property color gold: Theme.songket
    property color ground: Theme.dark ? "#141018" : "#f6efe0"
    property real unit: height            // the width of one repeat
    implicitHeight: 10
    Rectangle { anchors.fill: parent; color: sk.ground }
    Canvas { anchors.fill: parent
        onPaint: { const c = getContext("2d"), h = height, u = Math.max(6, sk.unit), m = h / 2; c.reset(); c.fillStyle = sk.gold; c.strokeStyle = sk.gold; c.lineWidth = 1
            c.globalAlpha = 0.55; c.fillRect(0, 0, width, 1); c.fillRect(0, h - 1, width, 1); c.globalAlpha = 1
            for (let x = u / 2; x < width + u; x += u) {
                const r = h * 0.36; c.beginPath(); c.moveTo(x, m - r); c.lineTo(x + r, m); c.lineTo(x, m + r); c.lineTo(x - r, m); c.closePath(); c.stroke()      // the diamond
                c.beginPath(); c.arc(x, m, Math.max(0.8, h * 0.07), 0, 2 * Math.PI); c.fill()                                                                         // its heart
                const s = h * 0.13, xx = x + u / 2; c.beginPath(); c.moveTo(xx, m - s); c.lineTo(xx + s, m); c.lineTo(xx, m + s); c.lineTo(xx - s, m); c.closePath(); c.fill() } }   // the small one between
        onWidthChanged: requestPaint(); onHeightChanged: requestPaint(); Component.onCompleted: requestPaint()
        Connections { target: Theme; function onDarkChanged() { parent.requestPaint() } } }
}
