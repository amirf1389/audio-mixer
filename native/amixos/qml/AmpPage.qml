// AMP / SYSTEM: the processor in front of the power amplifiers. One stage per device output, after ROUTING: name, gain, mute, polarity,
// alignment delay (and the distance it stands for), a high-pass and a low-pass crossover leg, and a peak limiter with its gain reduction.
// SPEAKER PRESETS set a whole output in one click (sub, tops, full range, wedge, fill); the curve shows the crossover of the selected output.
import QtQuick
import QtQuick.Layouts
import AmixOS

Flickable {
    id: page
    objectName: "ampPage"
    signal go(string id)
    clip: true
    contentHeight: col.implicitHeight + 32
    function t(s) { return i18n.t(s, i18n.lang) }
    property int sel: 0
    property int rev: 0                      // bumped on every change so the bindings read the engine again
    property var meters: []
    Connections { target: engine; function onAmpChanged() { page.rev++ } }
    Timer { interval: 100; running: page.visible; repeat: true; onTriggered: page.meters = engine.ampMeters() }
    function st(o) { page.rev; return engine.ampState(o) }
    function setBand(o, b, k, v) { const eq = JSON.parse(JSON.stringify(page.st(o).eq)); eq[b][k] = v; if (k !== "type" && eq[b].type === 0) eq[b].type = 1; engine.setAmp(o, { eq: eq, on: true }) }
    function set(o, k, v) { const m = {}; m[k] = v; if (k !== "on" && k !== "name") m.on = true; engine.setAmp(o, m) }
    readonly property var presets: [
        { n: "SUB", d: "below 100 Hz, LR 24 dB", p: { on: true, hpOrder: 2, hpHz: 30, lpOrder: 4, lpHz: 100, type: 1, limitOn: true, limitDb: -3 } },
        { n: "TOPS", d: "above 100 Hz, LR 24 dB", p: { on: true, hpOrder: 4, hpHz: 100, lpOrder: 0, type: 1, limitOn: true, limitDb: -2 } },
        { n: "FULL RANGE", d: "40 Hz high-pass", p: { on: true, hpOrder: 2, hpHz: 40, lpOrder: 0, type: 1, limitOn: true, limitDb: -1 } },
        { n: "WEDGE", d: "monitor: 120 Hz high-pass, 16 kHz top", p: { on: true, hpOrder: 2, hpHz: 120, lpOrder: 2, lpHz: 16000, type: 0, limitOn: true, limitDb: -4 } },
        { n: "FILL", d: "front / delay fill: 150 Hz high-pass", p: { on: true, hpOrder: 2, hpHz: 150, lpOrder: 0, type: 1, limitOn: true, limitDb: -3 } },
        { n: "BYPASS", d: "the output untouched", p: { on: false } } ]
    component Chip: Rectangle { property string label; property bool on: false; property color onColor: Theme.cyan; signal clicked()
        implicitWidth: Math.max(34, cl.implicitWidth + 12); implicitHeight: 20; radius: 8; gradient: Gradient { GradientStop { position: 0; color: on ? Qt.lighter(onColor, 1.22) : Theme.ov(0.12) } GradientStop { position: 1; color: on ? Qt.darker(onColor, 1.15) : Theme.ov(0.035) } } color: on ? onColor : Theme.ov(0.06); border.color: on ? "transparent" : Theme.lineSoft
        Rectangle { anchors.fill: parent; anchors.margins: 1; radius: Math.max(0, parent.radius - 1); color: "transparent"; border.width: 1; border.color: Qt.rgba(1, 1, 1, parent.on ? 0.28 : 0.07) }
        Text { id: cl; anchors.centerIn: parent; text: page.t(parent.label); color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked() } }
    component Param: RowLayout { property string label; property real value; property real from; property real to; property real step: 1; property string unit; property int decimals: 1; property bool log: false; signal moved(real v)
        function pos(v) { return log ? Math.log(Math.max(from, v) / from) / Math.log(to / from) : (v - from) / (to - from) }
        function val(r) { const v = log ? from * Math.pow(to / from, r) : from + r * (to - from); return Math.round(v / step) * step }
        Layout.fillWidth: true; spacing: 6
        Text { visible: parent.label !== ""; text: page.t(parent.label); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; Layout.preferredWidth: 86 }
        Rectangle { Layout.fillWidth: true; height: 6; radius: 3; color: Theme.deep; border.color: Theme.lineSoft
            Rectangle { height: parent.height; radius: 3; color: Theme.cyan; width: parent.width * Math.max(0, Math.min(1, parent.parent.pos(parent.parent.value))) }
            MouseArea { anchors.fill: parent; anchors.margins: -6; cursorShape: Qt.PointingHandCursor
                function at(x) { return parent.parent.val(Math.max(0, Math.min(1, x / width))) }
                onPressed: function (e) { parent.parent.moved(at(e.x)) } onPositionChanged: function (e) { if (pressed) parent.parent.moved(at(e.x)) } } }
        Text { text: Number(parent.value).toFixed(parent.decimals) + " " + parent.unit; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; Layout.preferredWidth: parent.label !== "" ? 78 : 56; horizontalAlignment: Text.AlignRight } }

    ColumnLayout {
        id: col; width: page.width - 32; x: 16; y: 16; spacing: 12
        RowLayout { Layout.fillWidth: true; spacing: 10
            Text { text: page.t("AMP / SYSTEM"); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 16; font.bold: true; font.letterSpacing: 3 }
            Text { text: page.t("the processor in front of the power amplifiers: one stage per output, after ROUTING"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideRight }
            Chip { label: "ROUTING"; onClicked: page.go("routing") } }

        // SYSTEM SETUP: routes the master to the outputs and sets every crossover in one click
        Rectangle { Layout.fillWidth: true; implicitHeight: sysRow.implicitHeight + 16; radius: Theme.radius; color: Theme.card; border.color: Theme.orange
            property string layout: "2way"; property string sub: "mono"; property real xo: 100; property string note: ""
            ColumnLayout { id: sysRow; anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.margins: 10; spacing: 6
                RowLayout { spacing: 6
                    Text { text: page.t("SYSTEM SETUP"); color: Theme.orange; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; font.letterSpacing: 2; Layout.preferredWidth: 120 }
                    Repeater { model: [["full", "FULL RANGE"], ["2way", "2-WAY"], ["3way", "3-WAY"]]
                        delegate: Chip { label: modelData[1]; on: sysRow.parent.layout === modelData[0]; onColor: Theme.orange; onClicked: sysRow.parent.layout = modelData[0] } }
                    Item { width: 10 }
                    Repeater { model: [["none", "NO SUB"], ["stereo", "STEREO SUB"], ["mono", "MONO SUB"]]
                        delegate: Chip { label: modelData[1]; on: sysRow.parent.sub === modelData[0]; onColor: Theme.orange; onClicked: sysRow.parent.sub = modelData[0] } }
                    Item { width: 10 }
                    Text { text: page.t("SUB CROSSOVER"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                    Repeater { model: [80, 100, 120]
                        delegate: Chip { label: modelData + " Hz"; on: sysRow.parent.xo === modelData; onColor: Theme.orange; onClicked: sysRow.parent.xo = modelData } }
                    Item { Layout.fillWidth: true }
                    Rectangle { objectName: "ampSetupApply"; implicitWidth: apT.implicitWidth + 20; implicitHeight: 24; radius: 4; color: Theme.orange
                        Text { id: apT; anchors.centerIn: parent; text: page.t("SET UP"); color: "#000"; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { sysRow.parent.note = engine.setupSystem(sysRow.parent.layout, sysRow.parent.sub, sysRow.parent.xo); page.sel = 0 } } } }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: sysRow.parent.note ? Theme.emerald : Theme.dim; font.family: Theme.mono; font.pixelSize: 8
                       text: sysRow.parent.note ? sysRow.parent.note : page.t("Outputs are used in order from output 1: each side's ways (low, mid, high), then the sub. Linkwitz-Riley 24 dB crossovers, limiters on; then set each output's gain, delay and speaker EQ below.") } } }

        // every output: name, status, meter with gain reduction, quick mute
        Flow { Layout.fillWidth: true; spacing: 8
            Repeater { model: engine.ampOutputs()
                delegate: Rectangle { objectName: "ampOut_" + index
                    readonly property var s: page.st(index); readonly property var m: page.meters[index] || { pk: -120, gr: 0 }
                    width: 132; height: 92; radius: Theme.radius; color: page.sel === index ? Theme.ov(0.08) : Theme.card; border.color: page.sel === index ? Theme.cyan : Theme.line
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: page.sel = index }
                    Column { anchors.fill: parent; anchors.margins: 8; spacing: 4
                        Row { spacing: 6
                            Text { text: (index + 1); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                            Text { text: s.name; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; width: 90; elide: Text.ElideRight } }
                        Text { text: !s.on ? page.t("BYPASS") : (s.mute ? page.t("MUTED") : (s.hpOrder > 0 ? Math.round(s.hpHz) + " Hz ↑ " : "") + (s.lpOrder > 0 ? "↓ " + (s.lpHz >= 1000 ? (s.lpHz / 1000).toFixed(1) + "k" : Math.round(s.lpHz)) : "") + (s.hpOrder === 0 && s.lpOrder === 0 ? page.t("FULL BAND") : ""))
                               color: !s.on ? Theme.dim : s.mute ? Theme.danger : Theme.cyan; font.family: Theme.mono; font.pixelSize: 8 }
                        Rectangle { width: parent.width; height: 6; radius: 2; color: Theme.deep
                            Rectangle { height: parent.height; radius: 2; width: parent.width * Math.max(0, Math.min(1, (m.pk + 60) / 60)); color: m.pk > -1 ? Theme.danger : m.pk > -9 ? Theme.amber : Theme.emerald } }
                        Text { text: Number(s.gainDb).toFixed(1) + " dB" + (s.delayMs > 0 ? " · " + Number(s.delayMs).toFixed(2) + " ms" : "") + (m.gr > 0.1 ? " · LIM −" + Number(m.gr).toFixed(1) : ""); color: m.gr > 0.1 ? Theme.amber : Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                        Row { spacing: 4
                            Chip { label: "MUTE"; on: s.mute; onColor: Theme.danger; onClicked: page.set(index, "mute", !s.mute) }
                            Chip { label: "Ø"; on: s.invert; onColor: Theme.amber; onClicked: page.set(index, "invert", !s.invert) } } } } } }

        // the selected output in full
        RowLayout { Layout.fillWidth: true; spacing: 12
            readonly property var s: page.st(page.sel)
            Rectangle { Layout.fillWidth: true; Layout.preferredWidth: 3; implicitHeight: edit.implicitHeight + 24; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout { id: edit; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12; spacing: 8
                    readonly property var s: parent.parent.s
                    RowLayout { spacing: 8
                        Text { text: page.t("OUTPUT") + " " + (page.sel + 1); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 11; font.bold: true }
                        Chip { objectName: "ampOn"; label: edit.s.on ? "ON" : "BYPASS"; on: edit.s.on; onColor: Theme.emerald; onClicked: page.set(page.sel, "on", !edit.s.on) }
                        Item { Layout.fillWidth: true }
                        Text { text: page.t("SPEAKER PRESET"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                        Repeater { model: page.presets
                            delegate: Chip { objectName: "ampPreset_" + modelData.n; label: modelData.n; onClicked: { const p = Object.assign({}, modelData.p); if (modelData.n !== "BYPASS") p.name = modelData.n; engine.setAmp(page.sel, p) } } } }
                    Param { label: "GAIN"; value: edit.s.gainDb; from: -40; to: 12; step: 0.5; unit: "dB"; onMoved: function (v) { page.set(page.sel, "gainDb", v) } }
                    Param { label: "DELAY"; value: edit.s.delayMs; from: 0; to: 100; step: 0.05; unit: "ms"; decimals: 2; onMoved: function (v) { page.set(page.sel, "delayMs", v) } }
                    Text { text: page.t("= the sound travelling") + " " + (edit.s.delayMs * 0.343).toFixed(2) + " m (" + (edit.s.delayMs * 1.1253).toFixed(1) + " ft) " + page.t("at 20 °C: delay a fill or a delay tower by its distance to the main speakers"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    RowLayout { spacing: 6
                        Text { text: page.t("HIGH-PASS"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; Layout.preferredWidth: 86 }
                        Repeater { model: [[0, "OFF"], [2, "12 dB"], [4, "24 dB"], [6, "36 dB"], [8, "48 dB"]]
                            delegate: Chip { label: modelData[1]; on: edit.s.hpOrder === modelData[0]; onClicked: page.set(page.sel, "hpOrder", modelData[0]) } } }
                    Param { visible: edit.s.hpOrder > 0; label: "  FREQUENCY"; value: edit.s.hpHz; from: 20; to: 5000; step: 1; log: true; unit: "Hz"; decimals: 0; onMoved: function (v) { page.set(page.sel, "hpHz", v) } }
                    RowLayout { spacing: 6
                        Text { text: page.t("LOW-PASS"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; Layout.preferredWidth: 86 }
                        Repeater { model: [[0, "OFF"], [2, "12 dB"], [4, "24 dB"], [6, "36 dB"], [8, "48 dB"]]
                            delegate: Chip { label: modelData[1]; on: edit.s.lpOrder === modelData[0]; onClicked: page.set(page.sel, "lpOrder", modelData[0]) } } }
                    Param { visible: edit.s.lpOrder > 0; label: "  FREQUENCY"; value: edit.s.lpHz; from: 40; to: 20000; step: 1; log: true; unit: "Hz"; decimals: 0; onMoved: function (v) { page.set(page.sel, "lpHz", v) } }
                    RowLayout { spacing: 6
                        Text { text: page.t("FILTER TYPE"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; Layout.preferredWidth: 86 }
                        Repeater { model: [[1, "LINKWITZ-RILEY"], [0, "BUTTERWORTH"], [2, "BESSEL"]]
                            delegate: Chip { label: modelData[1]; on: edit.s.type === modelData[0]; onClicked: page.set(page.sel, "type", modelData[0]) } } }
                    RowLayout { spacing: 6
                        Text { text: page.t("LIMITER"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; Layout.preferredWidth: 86 }
                        Chip { label: edit.s.limitOn ? "ON" : "OFF"; on: edit.s.limitOn; onColor: Theme.amber; onClicked: page.set(page.sel, "limitOn", !edit.s.limitOn) }
                        Text { text: page.t("protects the amplifier and the drivers: nothing above the ceiling leaves this output"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; Layout.fillWidth: true; elide: Text.ElideRight } }
                    Param { visible: edit.s.limitOn; label: "  CEILING"; value: edit.s.limitDb; from: -30; to: 0; step: 0.5; unit: "dBFS"; onMoved: function (v) { page.set(page.sel, "limitDb", v) } }
                    Param { visible: edit.s.limitOn; label: "  RELEASE"; value: edit.s.releaseMs; from: 10; to: 2000; step: 10; unit: "ms"; decimals: 0; onMoved: function (v) { page.set(page.sel, "releaseMs", v) } }
                    // SPEAKER EQ: 8 parametric bands after the crossover, to tune the cabinet (they show in the curve)
                    RowLayout { spacing: 6
                        Text { text: page.t("SPEAKER EQ"); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; Layout.preferredWidth: 86 }
                        Text { text: page.t("8 bands after the crossover: tune the cabinet, they show in the curve"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; Layout.fillWidth: true; elide: Text.ElideRight }
                        Chip { label: "FLAT"; onClicked: { const eq = JSON.parse(JSON.stringify(edit.s.eq)); for (const b of eq) { b.type = 0; b.gain = 0 } engine.setAmp(page.sel, { eq: eq }) } } }
                    Repeater { model: edit.s.eq ? edit.s.eq.length : 0
                        delegate: RowLayout { Layout.fillWidth: true; spacing: 4
                            readonly property var b: edit.s.eq[index]
                            Text { text: (index + 1); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; Layout.preferredWidth: 10 }
                            Repeater { model: [[0, "OFF"], [1, "PEQ"], [2, "LS"], [3, "HS"], [6, "NOTCH"]]
                                delegate: Chip { label: modelData[1]; on: b.type === modelData[0]; onClicked: page.setBand(page.sel, index, "type", modelData[0]) } }
                            Param { opacity: b.type === 0 ? 0.4 : 1; label: ""; Layout.preferredWidth: 150; value: b.freq; from: 20; to: 20000; step: 1; log: true; unit: "Hz"; decimals: 0; onMoved: function (v) { page.setBand(page.sel, index, "freq", v) } }
                            Param { opacity: b.type === 0 ? 0.4 : 1; label: ""; Layout.preferredWidth: 120; value: b.gain; from: -18; to: 12; step: 0.5; unit: "dB"; onMoved: function (v) { page.setBand(page.sel, index, "gain", v) } }
                            Param { opacity: b.type === 0 ? 0.4 : 1; label: ""; Layout.preferredWidth: 110; value: b.q; from: 0.3; to: 12; step: 0.1; log: true; unit: "Q"; onMoved: function (v) { page.setBand(page.sel, index, "q", v) } } } } } }
            // the crossover curve of the selected output
            Rectangle { Layout.fillWidth: true; Layout.preferredWidth: 2; Layout.alignment: Qt.AlignTop; implicitHeight: 260; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                Text { x: 12; y: 8; text: page.t("RESPONSE (CROSSOVER + EQ) · OUTPUT") + " " + (page.sel + 1); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Canvas { id: curve; anchors.fill: parent; anchors.margins: 14; anchors.topMargin: 28
                    property var pts: { page.rev; return engine.ampResponse(page.sel) }
                    onPtsChanged: requestPaint()
                    onPaint: { const c = getContext("2d"); c.reset(); const w = width, h = height, top = 6, bot = -48;
                        const y = function (db) { return (top + 6 - Math.max(bot, Math.min(top + 6, db))) / (top + 6 - bot) * h }; const x = function (hz) { return Math.log(hz / 20) / Math.log(1000) * w };
                        c.strokeStyle = Theme.ov(0.08); c.lineWidth = 1; for (const d of [0, -12, -24, -36]) { c.beginPath(); c.moveTo(0, y(d)); c.lineTo(w, y(d)); c.stroke() }
                        for (const f of [50, 100, 200, 500, 1000, 2000, 5000, 10000]) { c.beginPath(); c.moveTo(x(f), 0); c.lineTo(x(f), h); c.stroke() }
                        c.fillStyle = Theme.dim; c.font = "8px monospace"; for (const f of [100, 1000, 10000]) c.fillText(f >= 1000 ? f / 1000 + "k" : f, x(f) + 2, h - 2);
                        c.strokeStyle = Theme.cyan; c.lineWidth = 2; c.beginPath(); for (let i = 0; i < pts.length; ++i) { const px = x(pts[i].hz), py = y(pts[i].db); if (i) c.lineTo(px, py); else c.moveTo(px, py) } c.stroke() } } } }
        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8
               text: page.t("Each output is processed after ROUTING and before the sound card. BYPASS leaves it bit-exact. Set a SUB and a TOPS output at the same crossover frequency with LINKWITZ-RILEY: the two add up flat. The settings are saved with the console.") }
    }
}
