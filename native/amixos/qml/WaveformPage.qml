// WAVEFORM: an oscilloscope on the master (L, R or both) or on one strip (pre-fader). Timebase 1..500 ms, trigger on the rising zero crossing
// so a periodic signal stands still, FREEZE holds the picture, and the figures (Vpp, Vrms, frequency, crest) are read from the same window.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "waveformPage"
    property int source: 0            // 0 master L, 1 master R, 2 both, 3 strip
    property int strip: 0
    property real windowMs: 20
    property bool trigger: true
    property bool frozen: false
    property var frameL: ({ samples: [], vpp: 0, vrms: 0, freq: 0, crest: 0 })
    property var frameR: ({ samples: [] })
    readonly property var stripSt: engine.stripState[strip]
    function refresh() {
        if (frozen) return
        if (source === 3) { engine.setScopeStrip(strip); frameL = engine.scopeFrame(2, windowMs, 512, trigger); frameR = { samples: [] } }
        else { frameL = engine.scopeFrame(source === 1 ? 1 : 0, windowMs, 512, trigger); frameR = source === 2 ? engine.scopeFrame(1, windowMs, 512, trigger) : { samples: [] } }
        canvas.requestPaint()
    }
    Timer { interval: 50; running: page.visible; repeat: true; onTriggered: page.refresh() }
    Component.onCompleted: refresh()
    function dbOf(v) { return v > 1e-6 ? (20 * Math.log(v) / Math.LN10).toFixed(1) + " dBFS" : "-inf" }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Text { text: i18n.t("WAVEFORM:", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Repeater { model: ["MASTER L", "MASTER R", "L + R", "STRIP"]; delegate: Rectangle { implicitWidth: tl.implicitWidth + 16; implicitHeight: 20; radius: 4; color: page.source === index ? Theme.amber : Theme.ov(0.06)
                    Text { id: tl; anchors.centerIn: parent; text: modelData; color: page.source === index ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { page.source = index; page.refresh() } } } }
                Rectangle { visible: page.source === 3; implicitWidth: 22; implicitHeight: 20; radius: 4; color: Theme.ov(0.08); Text { anchors.centerIn: parent; text: "‹"; color: Theme.ink; font.pixelSize: 12 } MouseArea { anchors.fill: parent; onClicked: page.strip = Math.max(0, page.strip - 1) } }
                Text { visible: page.source === 3; text: i18n.t("CH ", i18n.lang) + (page.strip + 1 < 10 ? "0" : "") + (page.strip + 1) + " " + (page.stripSt ? page.stripSt.name : ""); color: Theme.stripColors[page.stripSt ? page.stripSt.color : 0]; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                Rectangle { visible: page.source === 3; implicitWidth: 22; implicitHeight: 20; radius: 4; color: Theme.ov(0.08); Text { anchors.centerIn: parent; text: "›"; color: Theme.ink; font.pixelSize: 12 } MouseArea { anchors.fill: parent; onClicked: page.strip = Math.min(engine.stripState.length - 1, page.strip + 1) } }
                Item { Layout.fillWidth: true }
                Text { text: i18n.t("TIME", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.letterSpacing: 2 }
                Repeater { model: [1, 5, 20, 100, 500]; delegate: Rectangle { implicitWidth: 42; implicitHeight: 20; radius: 4; color: page.windowMs === modelData ? Theme.ov(0.16) : Theme.ov(0.06)
                    Text { anchors.centerIn: parent; text: modelData + i18n.t(" ms", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { page.windowMs = modelData; page.refresh() } } } }
                Rectangle { implicitWidth: 60; implicitHeight: 20; radius: 4; color: page.trigger ? Theme.cyan : Theme.ov(0.06); Text { anchors.centerIn: parent; text: i18n.t("TRIGGER", i18n.lang); color: page.trigger ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.trigger = !page.trigger } }
                Rectangle { implicitWidth: 60; implicitHeight: 20; radius: 4; color: page.frozen ? Theme.danger : Theme.ov(0.06); Text { anchors.centerIn: parent; text: i18n.t("FREEZE", i18n.lang); color: page.frozen ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.frozen = !page.frozen } }
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.chrome; border.color: Theme.line; clip: true
            Canvas {
                id: canvas
                anchors.fill: parent; anchors.margins: 8
                onPaint: {
                    const ctx = getContext("2d"); const w = width, h = height; ctx.clearRect(0, 0, w, h)
                    ctx.strokeStyle = Theme.ov(0.07); ctx.lineWidth = 1
                    for (let i = 0; i <= 10; ++i) { const x = Math.round(w * i / 10) + 0.5; ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke() }
                    for (let j = 0; j <= 8; ++j) { const y = Math.round(h * j / 8) + 0.5; ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke() }
                    ctx.strokeStyle = Theme.ov(0.25); ctx.beginPath(); ctx.moveTo(0, h / 2 + 0.5); ctx.lineTo(w, h / 2 + 0.5); ctx.stroke()
                    function trace(s, col) { if (!s || s.length < 2) return; ctx.strokeStyle = col; ctx.lineWidth = 1.5; ctx.beginPath(); for (let i = 0; i < s.length; ++i) { const x = w * i / (s.length - 1), y = h / 2 - Math.max(-1, Math.min(1, s[i])) * h / 2; if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y) } ctx.stroke() }
                    trace(page.frameR.samples, "#00f2ff"); trace(page.frameL.samples, page.source === 3 ? Theme.stripColors[page.stripSt ? page.stripSt.color : 0] : (page.source === 1 ? "#00f2ff" : "#fbbf24"))
                }
            }
            Text { anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 10; text: (page.source === 3 ? i18n.t("STRIP pre-fader", i18n.lang) : page.source === 2 ? i18n.t("MASTER L (amber) + R (cyan)", i18n.lang) : page.source === 1 ? i18n.t("MASTER R", i18n.lang) : i18n.t("MASTER L", i18n.lang)) + "   " + page.windowMs + i18n.t(" ms / screen   ±1.0 FS", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
            Text { anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 10; text: page.frozen ? i18n.t("FROZEN", i18n.lang) : (engine.running ? i18n.t("LIVE", i18n.lang) : i18n.t("NO DEVICE", i18n.lang)); color: page.frozen ? Theme.danger : (engine.running ? Theme.emerald : Theme.amber); font.family: Theme.mono; font.pixelSize: 7; font.bold: true }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Repeater {
                model: [{ t: "Vpp", v: page.frameL.vpp.toFixed(3) + " FS" }, { t: "Vrms", v: page.frameL.vrms.toFixed(3) + " FS  " + page.dbOf(page.frameL.vrms) }, { t: "FREQ", v: page.frameL.freq > 0 ? page.frameL.freq.toFixed(1) + " Hz" : "—" }, { t: "CREST", v: page.frameL.crest > 0 ? page.frameL.crest.toFixed(1) + " dB" : "—" }]
                delegate: Rectangle { Layout.fillWidth: true; implicitHeight: 44; radius: 8; color: Theme.card; border.color: Theme.line
                    Column { anchors.centerIn: parent; spacing: 2
                        Text { text: i18n.t(modelData.t ?? "", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.letterSpacing: 2; anchors.horizontalCenter: parent.horizontalCenter }
                        Text { text: modelData.v; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 12; font.bold: true; anchors.horizontalCenter: parent.horizontalCenter } } }
            }
        }
    }
}
