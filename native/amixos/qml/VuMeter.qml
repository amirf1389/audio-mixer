// An analogue VU meter: a needle over the classic -20 .. +3 VU scale, 0 VU = refDb dBFS (-18 EBU / -20 SMPTE / -14 loud). The ballistics
// (300 ms to 99 % of a step, the IEC 60268-17 definition) are computed in the Engine (meters[].vuDb, telemetry.masterVuDb); this is the face.
import QtQuick
import AmixOS

Item {
    id: vu
    property real vuDb: -120          // the ballistically smoothed level in dBFS
    property real refDb: -18          // 0 VU
    property bool peakLamp: false     // lit from the peak meter's clip flag
    property string label: i18n.t("VU", i18n.lang)
    implicitWidth: 64
    implicitHeight: 44
    readonly property real valueVu: Math.max(-20, Math.min(3, vuDb - refDb))
    // the scale is not linear: -20 at the far left, -10 at ~27 %, -7, -5, -3, -2, -1, 0 at 72 %, +1, +2, +3 at the right (the classic face)
    function pos(v) { return v <= -10 ? 0.27 * (v + 20) / 10 : v <= 0 ? 0.27 + 0.45 * (v + 10) / 10 : 0.72 + 0.28 * v / 3 }
    // the needle angle over the arc; the pivot is the cap at the bottom of the face, in the middle
    readonly property real span: 44   // degrees each side of the middle
    property real angle: -span + 2 * span * pos(valueVu)
    // the needle ballistics are the engine's VU (300 ms one-pole): no extra animation here, it kept the window redrawing
    Rectangle { anchors.fill: parent; radius: 4; color: "#f4ead8"; border.color: "#3a3326"; border.width: 1
        gradient: Gradient { GradientStop { position: 0; color: "#fbf4e4" } GradientStop { position: 1; color: "#e9dcc2" } } }
    Canvas {
        id: face
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d"); ctx.reset();
            const w = width, h = height, cx = w / 2, cy = h * 1.0, r = h * 0.86;
            ctx.lineWidth = 1.5;
            // the arc: black to 0 VU, red above
            const a = v => (-vu.span + 2 * vu.span * vu.pos(v)) * Math.PI / 180 - Math.PI / 2;
            ctx.strokeStyle = "#1e1a12"; ctx.beginPath(); ctx.arc(cx, cy, r, a(-20), a(0)); ctx.stroke();
            ctx.strokeStyle = "#c8102e"; ctx.lineWidth = 3; ctx.beginPath(); ctx.arc(cx, cy, r, a(0), a(3)); ctx.stroke();
            ctx.lineWidth = 1; ctx.font = "bold " + Math.max(5, Math.round(h * 0.13)) + "px sans-serif"; ctx.textAlign = "center";
            const marks = [-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3];
            for (const v of marks) {
                const t = a(v), big = v === -20 || v === -10 || v === -5 || v === 0 || v === 3;
                ctx.strokeStyle = ctx.fillStyle = v > 0 ? "#c8102e" : "#1e1a12";
                ctx.beginPath(); ctx.moveTo(cx + Math.cos(t) * (r - (big ? 6 : 3)), cy + Math.sin(t) * (r - (big ? 6 : 3))); ctx.lineTo(cx + Math.cos(t) * r, cy + Math.sin(t) * r); ctx.stroke();
                // labels: the whole face only when there is room; a strip-sized face shows -20, -10, 0 and +3
                const show = h >= 56 ? (big || v === -3 || v === -7) : (v === -20 || v === -10 || v === 0 || v === 3);
                if (show) ctx.fillText(v === 0 ? "0" : (v > 0 ? "+" + v : String(v)), cx + Math.cos(t) * (r - 11), cy + Math.sin(t) * (r - 11) + 3);
            }
            ctx.fillStyle = "#1e1a12"; ctx.font = "bold " + Math.max(6, Math.round(h * 0.15)) + "px sans-serif"; ctx.fillText(vu.label, cx, h * 0.84);
        }
        Component.onCompleted: requestPaint()
        onWidthChanged: requestPaint(); onHeightChanged: requestPaint()
    }
    Connections { target: vu; function onLabelChanged() { face.requestPaint() } }
    // the needle, pivoting below the face
    Item { anchors.fill: parent; clip: true   // the pivot is below the face: only the part of the needle over the face is drawn
        Item { x: parent.width / 2; y: parent.height * 1.0; rotation: vu.angle
            Rectangle { x: -0.75; y: -vu.height * 0.90; width: 1.5; height: vu.height * 0.90; color: "#1e1a12"; antialiasing: true } } }
    Rectangle { anchors.horizontalCenter: parent.horizontalCenter; anchors.verticalCenter: parent.bottom; width: 10; height: 10; radius: 5; color: "#3a3326" }
    Rectangle { anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 3; width: 6; height: 6; radius: 3; color: vu.peakLamp ? "#ff2a2a" : "#6b2b2b"; border.color: "#3a3326" }
}
