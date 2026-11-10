// PHASE: a goniometer (Lissajous of L against R, rotated 45° so mono is a vertical line and out-of-phase a horizontal one), the correlation
// meter (-1 .. +1) from the loudness block, and the master STEREO WIDTH (0 = mono, 1 = as mixed, 2 = wide; mid/side before the master fader).
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "phasePage"
    property var pairs: []
    property bool frozen: false
    readonly property real corr: engine.telemetry.correlation
    function refresh() { if (frozen) return; pairs = engine.lissajous(1024); gonio.requestPaint() }
    Timer { interval: 50; running: page.visible; repeat: true; onTriggered: page.refresh() }
    Component.onCompleted: refresh()
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Text { text: i18n.t("PHASE:", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Text { text: i18n.t("goniometer · correlation · stereo width", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Item { Layout.fillWidth: true }
                Text { text: engine.running ? i18n.t("LIVE", i18n.lang) : i18n.t("NO DEVICE", i18n.lang); color: engine.running ? Theme.emerald : Theme.amber; font.family: Theme.mono; font.pixelSize: 7; font.bold: true }
                Rectangle { implicitWidth: 60; implicitHeight: 20; radius: 4; color: page.frozen ? Theme.danger : Theme.ov(0.06); Text { anchors.centerIn: parent; text: i18n.t("FREEZE", i18n.lang); color: page.frozen ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.frozen = !page.frozen } }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            Rectangle {
                Layout.fillHeight: true; Layout.preferredWidth: height; radius: Theme.radius; color: Theme.chrome; border.color: Theme.line; clip: true
                Canvas {
                    id: gonio
                    anchors.fill: parent; anchors.margins: 8
                    onPaint: {
                        const ctx = getContext("2d"); const w = width, h = height, cx = w / 2, cy = h / 2, r = Math.min(w, h) / 2 - 4; ctx.clearRect(0, 0, w, h)
                        ctx.strokeStyle = "rgba(255,255,255,0.10)"; ctx.lineWidth = 1
                        ctx.beginPath(); ctx.arc(cx, cy, r, 0, 2 * Math.PI); ctx.stroke(); ctx.beginPath(); ctx.arc(cx, cy, r / 2, 0, 2 * Math.PI); ctx.stroke()
                        ctx.beginPath(); ctx.moveTo(cx, cy - r); ctx.lineTo(cx, cy + r); ctx.moveTo(cx - r, cy); ctx.lineTo(cx + r, cy); ctx.stroke()
                        ctx.strokeStyle = "rgba(255,255,255,0.18)"; ctx.beginPath(); ctx.moveTo(cx - r * 0.707, cy - r * 0.707); ctx.lineTo(cx + r * 0.707, cy + r * 0.707); ctx.moveTo(cx + r * 0.707, cy - r * 0.707); ctx.lineTo(cx - r * 0.707, cy + r * 0.707); ctx.stroke()
                        const p = page.pairs; if (!p || p.length < 4) return
                        ctx.fillStyle = "rgba(0,242,255,0.55)"
                        for (let i = 0; i + 1 < p.length; i += 2) { const l = p[i], rr = p[i + 1]; const x = (rr - l) * 0.7071, y = (l + rr) * 0.7071; ctx.fillRect(cx + x * r - 1, cy - y * r - 1, 2, 2) }
                    }
                }
                Text { anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 10; text: "L"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                Text { anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 10; text: "R"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                Text { anchors.horizontalCenter: parent.horizontalCenter; anchors.top: parent.top; anchors.margins: 10; text: "M"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                Text { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.margins: 10; text: "S"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 12; spacing: 12
                    Text { text: i18n.t("CORRELATION", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 12; font.bold: true; font.letterSpacing: 1.5 }
                    Rectangle {
                        Layout.fillWidth: true; implicitHeight: 18; radius: 4; color: Theme.ov(0.05); border.color: Theme.lineSoft
                        Rectangle { x: parent.width / 2 - 1; width: 2; height: parent.height; color: Theme.line }
                        Rectangle { readonly property real c: Math.max(-1, Math.min(1, page.corr)); x: c >= 0 ? parent.width / 2 : parent.width / 2 * (1 + c); width: Math.abs(c) * parent.width / 2; height: parent.height; radius: 4; color: c >= 0.3 ? Theme.emerald : c >= 0 ? Theme.amber : Theme.danger }
                    }
                    RowLayout { Layout.fillWidth: true
                        Text { text: i18n.t("-1  out of phase", i18n.lang); color: Theme.danger; font.family: Theme.mono; font.pixelSize: 7 } Item { Layout.fillWidth: true }
                        Text { text: page.corr.toFixed(2); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 14; font.bold: true } Item { Layout.fillWidth: true }
                        Text { text: i18n.t("mono  +1", i18n.lang); color: Theme.emerald; font.family: Theme.mono; font.pixelSize: 7 } }
                    Text { Layout.fillWidth: true; text: i18n.t("Above 0 the mix sums to mono without loss. Below 0 something cancels in mono (a flipped cable, a wide effect): check with the POLARITY buttons on the strips and with WIDTH below.", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; wrapMode: Text.WordWrap }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.lineSoft }
                    Text { text: i18n.t("STEREO WIDTH", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 12; font.bold: true; font.letterSpacing: 1.5 }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 10
                        Text { text: i18n.t("MONO", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
                        Slider { id: widthSlider; Layout.fillWidth: true; from: 0; to: 2; stepSize: 0.01; value: engine.width; onMoved: engine.width = value }
                        Text { text: i18n.t("WIDE", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
                        Text { Layout.preferredWidth: 50; text: Math.round(engine.width * 100) + " %"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; horizontalAlignment: Text.AlignRight }
                    }
                    RowLayout { spacing: 6
                        Repeater { model: [{ t: "MONO", v: 0 }, { t: "NARROW", v: 0.5 }, { t: "NORMAL", v: 1 }, { t: "WIDE", v: 1.5 }, { t: "MAX", v: 2 }]
                            delegate: Rectangle { implicitWidth: 60; implicitHeight: 22; radius: 4; color: Math.abs(engine.width - modelData.v) < 0.005 ? Theme.amber : Theme.ov(0.06); border.color: Theme.lineSoft
                                Text { anchors.centerIn: parent; text: modelData.t; color: Math.abs(engine.width - modelData.v) < 0.005 ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.width = modelData.v } } } }
                    Text { Layout.fillWidth: true; text: i18n.t("Mid/side on the master before the fader: 100 % leaves the mix as it is, below narrows it toward mono, above lifts the side signal (watch the correlation).", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; wrapMode: Text.WordWrap }
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
