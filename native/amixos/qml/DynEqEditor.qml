// The editor of the AmF DYNAMIC EQ (fx:dyneq): a curve display over the live spectrum (before the EQ dim, after it in the accent colour)
// with one node per band. Drag a node to set its frequency and gain, turn the wheel over it for Q, double-click the empty display to add
// a band there, right-click a node to remove it. The panel below sets the chosen band's shape, slope, channel (stereo / mid / side / left /
// right) and its dynamics. Our own design in the AmF X1 graphite look.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AmixOS

Popup {
    id: dq
    objectName: "dynEqEd"
    property int strip: 0
    property int slot: 0
    property string title: "AmF DYNAMIC EQ"
    property var info: ({})
    property int sel: -1
    property var presets: []
    property int presetAt: -1
    readonly property int points: 160
    readonly property var typeNames: ["BELL", "LOW SHELF", "HIGH SHELF", "LOW CUT", "HIGH CUT", "NOTCH", "BAND PASS"]
    readonly property var chanNames: ["STEREO", "MID", "SIDE", "LEFT", "RIGHT"]
    readonly property var slopes: [6, 12, 18, 24, 30, 36, 48, 72, 96]
    readonly property var bandColors: ["#e8a93a", "#3cc8db", "#a78bfa", "#34d399", "#f472b6", "#fb923c", "#60a5fa", "#f0304f"]
    width: Math.min(900, (parent ? parent.width : 900) - 16); height: Math.min(560, (parent ? parent.height : 560) - 16)
    padding: 0; modal: false; closePolicy: Popup.CloseOnEscape
    background: Rectangle { color: Theme.cardSolid; radius: Theme.radius; border.color: Theme.line; border.width: 1 }

    function openFor(strip_, slot_, title_) { strip = strip_; slot = slot_; title = title_ || "AmF DYNAMIC EQ"; sel = -1; presets = engine.rackPresets("fx:dyneq"); presetAt = -1; refresh(); x = Math.max(4, (parent.width - width) / 2); y = Math.max(4, (parent.height - height) / 2); open() }
    function refresh() { info = engine.dynEqInfo(strip, slot, points); disp.requestPaint() }
    function b(i, k) { return info.bands && info.bands[i] ? info.bands[i][k] : 0 }
    function isOn(i) { return b(i, 0) > 0.5 }
    function typeOf(i) { return Math.max(0, Math.min(6, Math.round(b(i, 1) * 6))) }
    function hz(v) { return 20 * Math.pow(1000, v) }
    function db(v) { return (v - 0.5) * 60 }
    function qv(v) { return 0.1 * Math.pow(300, v) }
    function hzText(f) { return f >= 1000 ? (f / 1000).toFixed(f >= 10000 ? 1 : 2) + " kHz" : f.toFixed(0) + " Hz" }
    function set(i, k, v) { engine.setDynEqBand(strip, slot, i, k, Math.max(0, Math.min(1, v))); presetAt = -1; refresh() }
    function hasGain(i) { const t = typeOf(i); return t <= 2 }
    // the display's scales: 20 Hz .. 20 kHz across, +-30 dB for the curve, -90 .. 0 dBFS for the spectrum
    function xOf(v) { return v * disp.width }
    function yOfDb(d) { return disp.height * (0.5 - d / 60) }
    function yOfSpec(d) { return disp.height * (1 - (Math.max(-90, Math.min(0, d)) + 90) / 90) }
    function nodeAt(px, py) { let best = -1, bd = 14 * 14; for (let i = 0; i < 24; i++) { if (!isOn(i)) continue; const nx = xOf(b(i, 2)), ny = hasGain(i) ? yOfDb(db(b(i, 3))) : disp.height / 2, d = (nx - px) * (nx - px) + (ny - py) * (ny - py); if (d < bd) { bd = d; best = i } } return best }
    function addAt(px, py) { for (let i = 0; i < 24; i++) if (!isOn(i)) { set(i, 1, 0); set(i, 2, px / disp.width); set(i, 3, 0.5 - (py / disp.height - 0.5)); set(i, 0, 1); sel = i; return } }

    Timer { interval: 40; running: dq.visible; repeat: true; onTriggered: dq.refresh() }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        RowLayout { Layout.fillWidth: true; spacing: 8
            Text { text: "AmF DYNAMIC EQ"; color: Theme.accent; font.family: Theme.mono; font.pixelSize: 13; font.bold: true }
            Text { text: i18n.t("CH", i18n.lang) + " " + (dq.strip + 1) + "  ·  " + i18n.t("zero latency", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9; Layout.fillWidth: true }
            ComboBox { id: presetBox; objectName: "dynEqPresets"; implicitWidth: 230; implicitHeight: 24; font.pixelSize: 10
                model: [i18n.t("-- MANUAL --", i18n.lang)].concat(dq.presets.map(p => p.name)); currentIndex: dq.presetAt + 1
                onActivated: function (k) { if (k > 0) { engine.applyRackPreset(dq.strip, dq.slot, dq.presets[k - 1].id); dq.presetAt = k - 1; dq.sel = -1; dq.refresh() } } }
            Button { objectName: "dynEqBypass"; text: dq.info.bypass ? i18n.t("BYPASSED", i18n.lang) : i18n.t("ON", i18n.lang); implicitHeight: 24; font.pixelSize: 10; onClicked: { engine.setPluginBypass(dq.strip, dq.slot, !dq.info.bypass); dq.refresh() } }
            Button { objectName: "dynEqClose"; text: "✕"; implicitWidth: 28; implicitHeight: 24; onClicked: dq.close() } }

        Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 200; color: Theme.deep; radius: 6; border.color: Theme.lineSoft; clip: true
            Canvas { id: disp; objectName: "dynEqDisplay"; anchors.fill: parent; anchors.margins: 1
                onPaint: { const c = getContext("2d"), W = width, H = height; c.reset(); if (W <= 0 || H <= 0) return
                    c.strokeStyle = Theme.ov(0.07); c.lineWidth = 1; c.fillStyle = Theme.dim; c.font = "9px " + Theme.mono
                    for (const f of [50, 100, 200, 500, 1000, 2000, 5000, 10000]) { const x = Math.log(f / 20) / Math.log(1000) * W; c.beginPath(); c.moveTo(x, 0); c.lineTo(x, H); c.stroke(); c.fillText(f >= 1000 ? (f / 1000) + "k" : "" + f, x + 2, H - 4) }
                    for (const d of [-24, -12, 0, 12, 24]) { const y = dq.yOfDb(d); c.strokeStyle = d === 0 ? Theme.ov(0.18) : Theme.ov(0.07); c.beginPath(); c.moveTo(0, y); c.lineTo(W, y); c.stroke(); c.fillText((d > 0 ? "+" : "") + d, 3, y - 2) }
                    const n = dq.points, pre = dq.info.pre || [], post = dq.info.post || [], cur = dq.info.curve || [], still = dq.info.still || []
                    if (pre.length === n) { c.beginPath(); c.moveTo(0, H); for (let k = 0; k < n; k++) c.lineTo((k + 0.5) / n * W, dq.yOfSpec(pre[k])); c.lineTo(W, H); c.closePath(); c.fillStyle = Theme.ov(0.10); c.fill() }
                    if (post.length === n) { c.beginPath(); for (let k = 0; k < n; k++) { const x = (k + 0.5) / n * W, y = dq.yOfSpec(post[k]); if (k) c.lineTo(x, y); else c.moveTo(x, y) } c.strokeStyle = Qt.rgba(0.24, 0.78, 0.86, 0.55); c.lineWidth = 1; c.stroke() }
                    if (still.length === n) { c.beginPath(); for (let k = 0; k < n; k++) { const x = (k + 0.5) / n * W, y = dq.yOfDb(still[k]); if (k) c.lineTo(x, y); else c.moveTo(x, y) } c.strokeStyle = Theme.ov(0.35); c.lineWidth = 1; c.setLineDash([4, 3]); c.stroke(); c.setLineDash([]) }
                    if (cur.length === n) { c.beginPath(); for (let k = 0; k < n; k++) { const x = (k + 0.5) / n * W, y = dq.yOfDb(Math.max(-40, Math.min(40, cur[k]))); if (k) c.lineTo(x, y); else c.moveTo(x, y) } c.strokeStyle = Theme.accent; c.lineWidth = 2; c.stroke() }
                    for (let i = 0; i < 24; i++) { if (!dq.isOn(i)) continue; const x = dq.xOf(dq.b(i, 2)), g = dq.hasGain(i) ? dq.db(dq.b(i, 3)) : 0, y = dq.yOfDb(g), col = dq.bandColors[i % dq.bandColors.length]
                        if (dq.b(i, 7) > 0.5 && dq.hasGain(i) && dq.info.dyn) { const yd = dq.yOfDb(g + dq.info.dyn[i]); c.strokeStyle = col; c.lineWidth = 3; c.beginPath(); c.moveTo(x, y); c.lineTo(x, yd); c.stroke() }
                        c.beginPath(); c.arc(x, y, i === dq.sel ? 8 : 6, 0, Math.PI * 2); c.fillStyle = i === dq.sel ? col : Theme.cardSolid; c.fill(); c.strokeStyle = col; c.lineWidth = 2; c.stroke()
                        c.fillStyle = i === dq.sel ? "#000" : col; c.font = "bold 8px " + Theme.mono; c.fillText("" + (i + 1), x - (i >= 9 ? 5 : 2.5), y + 3) } } }
            MouseArea { id: dArea; objectName: "dynEqDrag"; anchors.fill: disp; acceptedButtons: Qt.LeftButton | Qt.RightButton; hoverEnabled: true; property int held: -1
                cursorShape: held >= 0 ? Qt.SizeAllCursor : Qt.CrossCursor
                onPressed: function (m) { const i = dq.nodeAt(m.x, m.y); if (m.button === Qt.RightButton) { if (i >= 0) { dq.set(i, 0, 0); if (dq.sel === i) dq.sel = -1 } return } held = i; if (i >= 0) dq.sel = i }
                onPositionChanged: function (m) { if (held < 0) return; engine.setDynEqBand(dq.strip, dq.slot, held, 2, Math.max(0, Math.min(1, m.x / width))); if (dq.hasGain(held)) engine.setDynEqBand(dq.strip, dq.slot, held, 3, Math.max(0, Math.min(1, 1 - m.y / height))); dq.presetAt = -1; dq.refresh() }
                onReleased: held = -1
                onDoubleClicked: function (m) { if (dq.nodeAt(m.x, m.y) < 0) dq.addAt(m.x, m.y) }
                onWheel: function (w) { let i = dq.nodeAt(w.x, w.y); if (i < 0) i = dq.sel; if (i < 0) return; dq.set(i, 4, dq.b(i, 4) + (w.angleDelta.y > 0 ? 0.02 : -0.02)) } }
            Text { anchors.centerIn: parent; visible: !(dq.info.bands || []).some((x, i) => dq.isOn(i)); text: i18n.t("Double-click the display to add a band · drag a node for frequency / gain · wheel for Q · right-click removes", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 10 } }

        // THE CHOSEN BAND
        Rectangle { Layout.fillWidth: true; implicitHeight: bandCol.implicitHeight + 12; color: Theme.cardRaised; radius: 6; border.color: Theme.lineSoft
            ColumnLayout { id: bandCol; anchors.fill: parent; anchors.margins: 6; spacing: 4
                Text { visible: dq.sel < 0; text: i18n.t("Choose a band on the display", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 10 }
                RowLayout { visible: dq.sel >= 0; spacing: 8; Layout.fillWidth: true
                    Text { text: i18n.t("BAND", i18n.lang) + " " + (dq.sel + 1); color: dq.bandColors[Math.max(0, dq.sel) % dq.bandColors.length]; font.family: Theme.mono; font.pixelSize: 11; font.bold: true }
                    ComboBox { objectName: "dynEqType"; implicitWidth: 120; implicitHeight: 24; font.pixelSize: 10; model: dq.typeNames.map(t => i18n.t(t, i18n.lang)); currentIndex: dq.sel >= 0 ? dq.typeOf(dq.sel) : 0; onActivated: function (k) { dq.set(dq.sel, 1, k / 6) } }
                    ComboBox { visible: dq.sel >= 0 && (dq.typeOf(dq.sel) === 3 || dq.typeOf(dq.sel) === 4); implicitWidth: 110; implicitHeight: 24; font.pixelSize: 10; model: dq.slopes.map(s => s + " dB/oct"); currentIndex: dq.sel >= 0 ? Math.round(dq.b(dq.sel, 5) * 8) : 3; onActivated: function (k) { dq.set(dq.sel, 5, k / 8) } }
                    ComboBox { objectName: "dynEqChannel"; implicitWidth: 100; implicitHeight: 24; font.pixelSize: 10; model: dq.chanNames.map(t => i18n.t(t, i18n.lang)); currentIndex: dq.sel >= 0 ? Math.round(dq.b(dq.sel, 6) * 4) : 0; onActivated: function (k) { dq.set(dq.sel, 6, k / 4) } }
                    CheckBox { objectName: "dynEqDynamic"; enabled: dq.sel >= 0 && dq.hasGain(dq.sel); text: i18n.t("DYNAMIC", i18n.lang); font.pixelSize: 10; checked: dq.sel >= 0 && dq.b(dq.sel, 7) > 0.5; onToggled: dq.set(dq.sel, 7, checked ? 1 : 0) }
                    Item { Layout.fillWidth: true }
                    Button { text: i18n.t("REMOVE", i18n.lang); implicitHeight: 24; font.pixelSize: 10; onClicked: { dq.set(dq.sel, 0, 0); dq.sel = -1 } } }
                GridLayout { visible: dq.sel >= 0; columns: 4; columnSpacing: 10; rowSpacing: 0; Layout.fillWidth: true
                    Repeater { model: [[2, "FREQ"], [3, "GAIN"], [4, "Q"], [8, "THRESHOLD"], [9, "RATIO"], [10, "RANGE"], [11, "ATTACK"], [12, "RELEASE"]]
                        delegate: RowLayout { required property var modelData; Layout.fillWidth: true; spacing: 4
                            readonly property int key: modelData[0]
                            visible: dq.sel >= 0 && (key <= 4 || (dq.b(dq.sel, 7) > 0.5 && dq.hasGain(dq.sel))) && (key !== 3 || dq.hasGain(dq.sel))
                            Text { text: i18n.t(modelData[1], i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; Layout.preferredWidth: 62 }
                            MSlider { Layout.fillWidth: true; from: 0; to: 1; value: dq.sel >= 0 ? dq.b(dq.sel, key) : 0; onMoved: dq.set(dq.sel, key, value) }
                            Text { Layout.preferredWidth: 64; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8
                                text: { if (dq.sel < 0) return ""; const v = dq.b(dq.sel, key)
                                    switch (key) { case 2: return dq.hzText(dq.hz(v)); case 3: return (dq.db(v) >= 0 ? "+" : "") + dq.db(v).toFixed(1) + " dB"; case 4: return dq.qv(v).toFixed(2)
                                        case 8: return (-60 * (1 - v)).toFixed(1) + " dB"; case 9: return Math.pow(20, v).toFixed(1) + ":1"; case 10: return (dq.db(v) >= 0 ? "+" : "") + dq.db(v).toFixed(1) + " dB"
                                        case 11: return (0.1 * Math.pow(3000, v)).toFixed(1) + " ms"; default: return (5 * Math.pow(600, v)).toFixed(0) + " ms" } } } } } } } }
    }
}
