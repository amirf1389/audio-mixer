// A transfer-curve display for dynamics: input level (x) against output level (y), the unity line, the threshold, the live input / output dot
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)

Canvas {
    id: cv
    property var curve: []            // [{in, out}] in dB
    property real minDb: -60
    property real thresholdDb: -20
    property real liveInDb: -120
    property real liveGainDb: 0       // gain applied right now (<= 0)
    property color lineColor: Theme.cyan
    implicitHeight: 240
    onCurveChanged: requestPaint()
    onLiveInDbChanged: requestPaint()
    onThresholdDbChanged: requestPaint()
    onPaint: {
        var c = getContext("2d"); c.reset(); c.fillStyle = Theme.deep; c.fillRect(0, 0, width, height);
        var L = 34, R = width - 10, T = 10, B = height - 22;
        function fx(db) { return L + (db - minDb) / (0 - minDb) * (R - L) }
        function fy(db) { return B - (db - minDb) / (0 - minDb) * (B - T) }
        c.strokeStyle = "rgba(255,255,255,0.08)"; c.lineWidth = 1; c.fillStyle = "#64748b"; c.font = "8px monospace";
        for (var d = minDb; d <= 0; d += 10) { c.beginPath(); c.moveTo(fx(d), T); c.lineTo(fx(d), B); c.stroke(); c.beginPath(); c.moveTo(L, fy(d)); c.lineTo(R, fy(d)); c.stroke(); c.fillText(d, fx(d) - 8, height - 8); c.fillText(d, 4, fy(d) + 3) }
        c.strokeStyle = "rgba(255,255,255,0.18)"; c.setLineDash([3, 3]); c.beginPath(); c.moveTo(fx(minDb), fy(minDb)); c.lineTo(fx(0), fy(0)); c.stroke(); c.setLineDash([]);
        c.strokeStyle = "rgba(251,191,36,0.6)"; c.beginPath(); c.moveTo(fx(thresholdDb), T); c.lineTo(fx(thresholdDb), B); c.stroke();
        if (curve.length) {
            c.beginPath(); c.strokeStyle = lineColor; c.lineWidth = 2;
            for (var i = 0; i < curve.length; i++) { var p = curve[i], x = fx(Math.max(minDb, p.in)), y = fy(Math.max(minDb, Math.min(0, p.out))); if (i === 0) c.moveTo(x, y); else c.lineTo(x, y) }
            c.stroke();
        }
        if (liveInDb > minDb) { var ox = fx(Math.min(0, liveInDb)), oy = fy(Math.max(minDb, Math.min(0, liveInDb + liveGainDb))); c.fillStyle = "#fbbf24"; c.beginPath(); c.arc(ox, oy, 5, 0, 6.283); c.fill() }
        c.fillStyle = "#94a3b8"; c.fillText("IN dB", R - 34, height - 8); c.fillText("OUT", 4, T + 8);
    }
}
