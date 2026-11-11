// The editor window of a rack plug-in, in the layout studio plug-ins share: a header bar (name, preset browser with previous / next,
// A / B compare, bypass), a display that draws what the plug-in is doing (the amp's transfer curve, a reverb's decay, a delay's echoes, a
// modulation's sweep), one knob per parameter with its value under it, and an input level meter. Flat and dark, one accent colour per
// effect. Hosted VST plug-ins get the same window with one knob per parameter. Drag the header to move it.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: ed
    objectName: "plugEd"
    property int strip: 0
    property int slot: 0
    property string kind: ""
    property string title: ""
    property var params: []
    property bool bypass: false
    property int rev: 0
    property var presets: []
    property int presetAt: -1
    property var stateA: null     // A / B: the knob values kept on the other side
    property var stateB: null
    property string side: "A"
    property real level: -120
    readonly property string fx: kind.indexOf("fx:") === 0 ? kind.substring(3) : ""
    readonly property var accents: ({ tubeamp: "#f59e0b", preamp: "#f87171", postamp: "#38bdf8", hall: "#60a5fa", room: "#2dd4bf", reverb: "#818cf8", delay: "#fbbf24", chorus: "#a78bfa", flanger: "#e879f9", phaser: "#fb923c", tremolo: "#4ade80", wah: "#facc15", crusher: "#22d3ee", exciter: "#fb7185", drive: "#f97316" })
    readonly property color accent: accents[fx] || "#a78bfa"
    readonly property color panel: "#15171c"
    readonly property color panelHi: "#1d2027"
    readonly property color edge: "#2a2e37"
    readonly property color text: "#e6e8ec"
    readonly property color sub: "#8b93a1"
    function openFor(strip_, slot_, kind_, title_) { strip = strip_; slot = slot_; kind = kind_; title = title_; presets = engine.rackPresets(kind_); presetAt = -1; stateA = null; stateB = null; side = "A"; reload(); x = Math.max(8, (parent.width - width) / 2); y = Math.max(8, (parent.height - height) / 2); open() }
    function reload() { params = engine.pluginParams(strip, slot); const p = engine.plugins(strip)[slot]; bypass = p ? p.bypass : false; rev++; screen.requestPaint() }
    function setParam(i, v) { engine.setPluginParam(strip, slot, params[i].id, v); const t = engine.pluginParams(strip, slot); if (t.length === params.length) params = t; presetAt = -1; screen.requestPaint() }
    function val(i) { return params[i] ? params[i].value : 0.5 }
    function snapshot() { const a = []; for (let i = 0; i < params.length; i++) a.push(params[i].value); return a }
    function restore(a) { if (!a) return; for (let i = 0; i < params.length && i < a.length; i++) engine.setPluginParam(strip, slot, params[i].id, a[i]); reload() }
    function flip() { if (side === "A") { stateA = snapshot(); side = "B"; if (stateB) restore(stateB) } else { stateB = snapshot(); side = "A"; if (stateA) restore(stateA) } }
    function preset(step) { if (!presets.length) return; presetAt = (presetAt + step + presets.length) % presets.length; engine.applyRackPreset(strip, slot, presets[presetAt].id); const at = presetAt; reload(); presetAt = at }
    readonly property int cols: Math.min(7, Math.max(3, params.length <= 7 ? params.length : Math.ceil(params.length / Math.ceil(params.length / 7))))
    width: Math.max(560, cols * 92 + 96); height: Math.min(parent.height - 16, 300 + Math.ceil(Math.max(1, params.length) / cols) * 104)
    modal: false; closePolicy: Popup.CloseOnEscape; padding: 0
    Timer { interval: 60; running: ed.visible; repeat: true; onTriggered: { const m = engine.meters[ed.strip]; ed.level = m ? (m.inDb !== undefined ? m.inDb : m.peakDb) : -120; if (["chorus", "flanger", "phaser", "tremolo"].indexOf(ed.fx) >= 0) screen.requestPaint() } }
    background: Rectangle { radius: 10; color: ed.panel; border.color: ed.edge; border.width: 1
        Rectangle { width: parent.width; height: 3; radius: 2; color: ed.accent; opacity: ed.bypass ? 0.25 : 1 } }
    component Chip: Rectangle { property string label; property bool on: false; signal clicked(); implicitWidth: Math.max(30, ct.implicitWidth + 16); implicitHeight: 24; radius: 5; color: on ? ed.accent : ed.panelHi; border.color: on ? ed.accent : ed.edge
        Text { id: ct; anchors.centerIn: parent; text: label; color: parent.on ? "#0b0c0f" : ed.text; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    contentItem: Item {
        // header bar
        Rectangle { id: bar; x: 1; y: 4; width: parent.width - 2; height: 44; color: "transparent"
            MouseArea { anchors.fill: parent; property point p0; cursorShape: Qt.SizeAllCursor; onPressed: function (m) { p0 = Qt.point(m.x, m.y) } onPositionChanged: function (m) { if (pressed) { ed.x += m.x - p0.x; ed.y += m.y - p0.y } } }
            RowLayout { anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 12; spacing: 8
                Column { spacing: 1
                    Text { text: ed.title.toUpperCase(); color: ed.text; font.family: Theme.sans; font.pixelSize: 15; font.weight: Font.DemiBold; font.letterSpacing: 2 }
                    Text { text: (ed.fx ? "AMFB AUDIO" : i18n.t("HOSTED PLUG-IN", i18n.lang)) + "  ·  CH " + (ed.strip + 1 < 10 ? "0" : "") + (ed.strip + 1) + "  ·  " + i18n.t("SLOT ", i18n.lang) + (ed.slot + 1); color: ed.sub; font.family: Theme.mono; font.pixelSize: 7; font.letterSpacing: 1.5 } }
                Item { Layout.fillWidth: true }
                RowLayout { visible: ed.presets.length > 0; spacing: 0   // the preset browser
                    Chip { label: "‹"; onClicked: ed.preset(-1) }
                    Rectangle { implicitWidth: 190; implicitHeight: 24; color: "#0d0e12"; border.color: ed.edge
                        Text { anchors.centerIn: parent; width: parent.width - 12; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; text: ed.presetAt >= 0 ? ed.presets[ed.presetAt].name : i18n.t("presets", i18n.lang) + "  (" + ed.presets.length + ")"; color: ed.presetAt >= 0 ? ed.accent : ed.sub; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                        MouseArea { anchors.fill: parent; onClicked: presetMenu.open() }
                        Menu { id: presetMenu; y: parent.height; width: 240; height: Math.min(360, implicitHeight)
                            Repeater { model: ed.presets; delegate: MenuItem { text: modelData.name; font.pixelSize: 11; onTriggered: { ed.presetAt = index - 1; ed.preset(1) } } } } }
                    Chip { label: "›"; onClicked: ed.preset(1) } }
                Chip { label: "A"; on: ed.side === "A"; onClicked: if (ed.side !== "A") ed.flip() }
                Chip { label: "B"; on: ed.side === "B"; onClicked: if (ed.side !== "B") ed.flip() }
                Chip { label: ed.bypass ? i18n.t("BYPASSED", i18n.lang) : i18n.t("ON", i18n.lang); on: !ed.bypass; onClicked: { engine.setPluginBypass(ed.strip, ed.slot, !ed.bypass); ed.bypass = !ed.bypass } }
                Chip { label: "✕"; onClicked: ed.close() } } }
        Rectangle { x: 0; y: 50; width: parent.width; height: 1; color: ed.edge }
        // the display + the level meter
        Rectangle { id: scr; x: 16; y: 62; width: parent.width - 60; height: 150; radius: 6; color: "#0b0c10"; border.color: ed.edge; clip: true
            Canvas { id: screen; anchors.fill: parent; anchors.margins: 1; opacity: ed.bypass ? 0.35 : 1
                property real t: 0
                onPaint: { const c = getContext("2d"), w = width, h = height; c.clearRect(0, 0, w, h); c.lineWidth = 1; c.strokeStyle = "rgba(255,255,255,0.05)"
                    for (let i = 1; i < 8; i++) { c.beginPath(); c.moveTo(w * i / 8, 0); c.lineTo(w * i / 8, h); c.stroke() } for (let i = 1; i < 4; i++) { c.beginPath(); c.moveTo(0, h * i / 4); c.lineTo(w, h * i / 4); c.stroke() }
                    const f = ed.fx, v = ed.val, col = ed.accent; t += 0.06; c.strokeStyle = col; c.fillStyle = Qt.rgba(col.r, col.g, col.b, 0.14); c.lineWidth = 2; c.lineJoin = "round"
                    const line = function (fn, fill) { c.beginPath(); for (let x = 0; x <= w; x += 2) { const y = fn(x / w); if (x === 0) c.moveTo(x, y); else c.lineTo(x, y) } c.stroke(); if (fill) { c.lineTo(w, h); c.lineTo(0, h); c.closePath(); c.fill() } }
                    if (f === "tubeamp" || f === "drive" || f === "preamp" || f === "postamp" || f === "exciter" || f === "crusher") {   // the transfer curve: input left to right, output bottom to top
                        const g = f === "tubeamp" ? Math.pow(10, v(0) * 46 / 20) * 0.08 : f === "drive" ? Math.pow(10, v(0) * 36 / 20) * 0.25 : f === "preamp" ? 1 + 2 * v(2) : f === "postamp" ? 1 / Math.max(0.25, Math.pow(10, -v(2) * 12 / 20)) : f === "exciter" ? 1 + 6 * v(0) : 1
                        const steps = f === "crusher" ? Math.pow(2, Math.floor(4 + v(0) * 12) - 1) : 0
                        c.strokeStyle = "rgba(255,255,255,0.18)"; c.lineWidth = 1; c.beginPath(); c.moveTo(0, h); c.lineTo(w, 0); c.stroke(); c.strokeStyle = col; c.lineWidth = 2
                        line(function (u) { const x = u * 2 - 1; let y = f === "tubeamp" ? Math.tanh(x * g * 4 + 0.18) - Math.tanh(0.18) : Math.tanh(x * g) / Math.tanh(Math.max(1, g)); if (steps) y = Math.round(x * Math.min(steps, 24)) / Math.min(steps, 24); return h / 2 - Math.max(-1, Math.min(1, y)) * (h / 2 - 8) }, false) }
                    else if (f === "reverb" || f === "hall" || f === "room") {   // the decay: level against time
                        const rt = f === "hall" ? 0.5 + v(1) * v(1) * 19.5 : f === "room" ? 0.1 + v(1) * 2.9 : 0.3 + v(0) * 11.7, span = f === "room" ? 3 : f === "hall" ? 12 : 8, pre = f === "hall" ? v(3) * 0.25 / span : f === "reverb" ? v(2) * 0.1 / span : 0
                        line(function (u) { if (u < pre) return h - 4; const s = (u - pre) * span; return 10 + (h - 20) * Math.min(1, s / rt * 0.9 + 0.0) * 1 }, false)
                        c.beginPath(); c.moveTo(pre * w, h); c.lineTo(pre * w, 10); for (let x = pre * w; x <= w; x += 2) { const s = (x / w - pre) * span; c.lineTo(x, 10 + (h - 12) * (1 - Math.exp(-6.9 * s / rt))) } c.lineTo(w, h); c.closePath(); c.fill()
                        c.fillStyle = ed.sub; c.font = "9px monospace"; c.fillText("RT60 " + rt.toFixed(1) + " s", 8, 14) }
                    else if (f === "delay") {   // the echoes
                        const fb = v(1), n = 9; for (let k = 0; k < n; k++) { const a = Math.pow(0.25 + fb * 0.7, k), x = 20 + k * (w - 40) / n * (0.5 + v(0)); if (x > w - 6) break; c.fillStyle = Qt.rgba(col.r, col.g, col.b, 0.25 + 0.75 * a); c.fillRect(x, h - 8 - a * (h - 24), 7, a * (h - 24)) } }
                    else if (f === "chorus" || f === "flanger" || f === "phaser" || f === "tremolo") {   // the sweep, moving
                        const rate = 1 + v(0) * 5, depth = 0.15 + v(1) * 0.8; line(function (u) { return h / 2 - Math.sin(u * rate * 6.283 + t) * depth * (h / 2 - 10) }, true) }
                    else if (f === "wah") { const fc = 0.15 + v(1) * 0.6, q = 4 + v(2) * 30; line(function (u) { const d = (u - fc) * q; return h - 10 - (h - 24) / (1 + d * d) }, true) }
                    else { const n = ed.params.length; for (let i = 0; i < n; i++) { const bw = (w - 16) / Math.max(1, n); c.fillStyle = Qt.rgba(col.r, col.g, col.b, 0.35 + 0.5 * v(i)); c.fillRect(8 + i * bw + 2, h - 8 - v(i) * (h - 20), bw - 4, v(i) * (h - 20)) } } } }
            Text { anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8; text: ed.bypass ? i18n.t("BYPASSED", i18n.lang) : ""; color: ed.sub; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } }
        Rectangle { x: parent.width - 36; y: 62; width: 20; height: 150; radius: 4; color: "#0b0c10"; border.color: ed.edge; clip: true   // input level, 0 .. -60 dBFS
            Rectangle { anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; width: 12; height: Math.max(0, Math.min(1, (ed.level + 60) / 60)) * (parent.height - 4); y: 2; radius: 2
                gradient: Gradient { GradientStop { position: 0; color: ed.level > -3 ? "#ef4444" : "#facc15" } GradientStop { position: 0.35; color: "#22c55e" } GradientStop { position: 1; color: "#15803d" } } } }
        Text { x: parent.width - 40; y: 216; width: 28; horizontalAlignment: Text.AlignHCenter; text: i18n.t("IN", i18n.lang); color: ed.sub; font.family: Theme.mono; font.pixelSize: 7; font.bold: true }
        // the knobs
        Flickable { x: 16; y: 232; width: parent.width - 32; height: parent.height - y - 30; contentHeight: grid.implicitHeight; clip: true
            Grid { id: grid; columns: ed.cols; columnSpacing: Math.max(4, (parent.width - ed.cols * 84) / Math.max(1, ed.cols - 1) - 0.01); rowSpacing: 6
                Repeater { model: ed.params.length
                    delegate: Knob { width: 84; size: 56; tint: ed.accent; cap: "#262a33"; ink: ed.text
                        label: ed.params[index] ? ed.params[index].title : ""; valueText: ed.params[index] ? ed.params[index].text : ""; defaultValue: ed.params[index] ? ed.params[index]["default"] : 0.5
                        readonly property int rev: ed.rev; onRevChanged: sync(); Component.onCompleted: sync()
                        function sync() { if (ed.params[index]) value = ed.params[index].value }
                        onMoved: function (v) { ed.setParam(index, v) } } } } }
        Rectangle { x: 0; y: parent.height - 26; width: parent.width; height: 1; color: ed.edge }
        Text { x: 16; y: parent.height - 19; text: i18n.t("drag a knob up or down · Shift = fine · double-click = default · wheel", i18n.lang); color: ed.sub; font.family: Theme.mono; font.pixelSize: 7 }
        Text { anchors.right: parent.right; anchors.rightMargin: 16; y: parent.height - 19; text: ed.fx ? "AMFB AUDIO" : (ed.kind.indexOf("vst2:") === 0 ? "VST2" : "VST3") + "  ·  " + i18n.t("generic controls", i18n.lang); color: ed.sub; font.family: Theme.mono; font.pixelSize: 7; font.letterSpacing: 2 }
    }
}
