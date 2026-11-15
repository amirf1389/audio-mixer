// SOUND SCORE: how good the master sounds right now, 0 .. 100, in full. The big gauge and grade, its six parts (each with what to do), a
// two-minute history, and the analyzer detail behind it: loudness (momentary, short-term, integrated, range), true peak, crest factor,
// peak-to-loudness ratio, stereo correlation and width, spectral tilt and roughness, and the 1/3-octave spectrum of the master.
import QtQuick
import QtQuick.Layouts
import AmixOS

Flickable {
    id: page
    objectName: "scorePage"
    signal go(string id)
    clip: true
    contentHeight: col.implicitHeight + 32
    function t(s) { return i18n.t(s, i18n.lang) }
    function tel(k, d) { const v = engine.telv(k, engine.meterTick); return v === undefined ? d : v }
    property var sc: ({ valid: false, total: 0, grade: "", parts: [] })
    property var hist: []          // [total] twice a second, the last two minutes
    property var spec: []
    Timer { interval: 500; running: page.visible; repeat: true; triggeredOnStart: true
        onTriggered: { page.sc = engine.soundScore(); page.spec = engine.spectrum(0); const h = page.hist.slice(); h.push(page.sc.valid ? page.sc.total : -1); if (h.length > 240) h.shift(); page.hist = h; hc.requestPaint() } }
    function gradeColor(v) { return v >= 85 ? Theme.emerald : v >= 70 ? Theme.cyan : v >= 50 ? Theme.amber : Theme.danger }
    function db(v) { return v <= -119 ? t("-inf") : Number(v).toFixed(1) }

    ColumnLayout {
        id: col; width: page.width - 32; x: 16; y: 16; spacing: 12
        RowLayout { Layout.fillWidth: true; spacing: 10
            Text { text: page.t("SOUND SCORE"); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 16; font.bold: true; font.letterSpacing: 3 }
            Text { text: page.t("of the master, live"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9 }
            Item { Layout.fillWidth: true }
            DspModeSwitch {}
            Rectangle { id: gb135; implicitWidth: maT.implicitWidth + 16; implicitHeight: 22; radius: 7; color: Theme.ov(0.06); gradient: Gradient { GradientStop { position: 0; color: Qt.lighter(gb135.color, 1.25) } GradientStop { position: 1; color: Qt.darker(gb135.color, 1.12) } } border.color: Theme.cyan
                Text { id: maT; anchors.centerIn: parent; text: page.t("MASTER ANALYZER"); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { if (typeof root !== "undefined" && root.openAnalyzer) root.openAnalyzer() } } } }

        RowLayout { Layout.fillWidth: true; spacing: 12
            // the gauge: an arc from 0 to the score, the number and its grade in the middle
            Rectangle { Layout.preferredWidth: 260; Layout.preferredHeight: 260; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                Canvas { id: gauge; anchors.fill: parent; anchors.margins: 18
                    property real v: page.sc.valid ? page.sc.total : 0
                    onVChanged: requestPaint()
                    onPaint: { const c = getContext("2d"); c.reset(); const r = Math.min(width, height) / 2 - 10, cx = width / 2, cy = height / 2, a0 = Math.PI * 0.75, span = Math.PI * 1.5;
                        c.lineWidth = 14; c.lineCap = "round"; c.strokeStyle = Theme.ov(0.08); c.beginPath(); c.arc(cx, cy, r, a0, a0 + span); c.stroke();
                        if (page.sc.valid) { c.strokeStyle = page.gradeColor(v); c.beginPath(); c.arc(cx, cy, r, a0, a0 + span * Math.max(0.005, v / 100)); c.stroke() } } }
                Column { anchors.centerIn: parent; spacing: 2
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: page.sc.valid ? Math.round(page.sc.total) : "—"; color: page.sc.valid ? page.gradeColor(page.sc.total) : Theme.dim; font.family: Theme.sans; font.pixelSize: 64; font.weight: Font.Black }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: page.sc.valid ? page.t("GRADE") + " " + page.sc.grade : page.t("play something"); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 11; font.bold: true } } }
            // the six parts, each with its fix
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 260; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout { anchors.fill: parent; anchors.margins: 14; spacing: 6
                    Text { text: page.t("WHAT MAKES THE SCORE"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                    Repeater { model: page.sc.parts || []
                        delegate: ColumnLayout { Layout.fillWidth: true; spacing: 2
                            RowLayout { Layout.fillWidth: true
                                Text { text: page.t(modelData.name); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; Layout.preferredWidth: 130; elide: Text.ElideRight }
                                Rectangle { Layout.fillWidth: true; height: 8; radius: 3; color: Theme.deep; border.color: Theme.lineSoft
                                    Rectangle { height: parent.height; radius: 3; width: parent.width * (modelData.max > 0 ? modelData.points / modelData.max : 0); color: page.gradeColor(modelData.max > 0 ? 100 * modelData.points / modelData.max : 0) } }
                                Text { text: Math.round(modelData.points) + " / " + modelData.max; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; Layout.preferredWidth: 54; horizontalAlignment: Text.AlignRight } }
                            Text { visible: !!modelData.tip && modelData.points < modelData.max; text: "→ " + page.t(modelData.tip || ""); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8; Layout.fillWidth: true; elide: Text.ElideRight } } }
                    Item { Layout.fillHeight: true } } } }

        // two minutes of the score
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 120; radius: Theme.radius; color: Theme.card; border.color: Theme.line
            Text { x: 14; y: 8; text: page.t("HISTORY · LAST 2 MINUTES"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
            Canvas { id: hc; anchors.fill: parent; anchors.margins: 14; anchors.topMargin: 26
                onPaint: { const c = getContext("2d"); c.reset(); const h = page.hist, w = width, ht = height;
                    c.strokeStyle = Theme.ov(0.08); c.lineWidth = 1; for (const g of [50, 70, 85]) { const y = ht - g / 100 * ht; c.beginPath(); c.moveTo(0, y); c.lineTo(w, y); c.stroke() }
                    c.strokeStyle = Theme.cyan; c.lineWidth = 2; c.beginPath(); let started = false;
                    for (let i = 0; i < h.length; ++i) { if (h[i] < 0) { started = false; continue } const x = w - (h.length - 1 - i) * (w / 239), y = ht - h[i] / 100 * ht; if (!started) { c.moveTo(x, y); started = true } else c.lineTo(x, y) }
                    c.stroke() } } }

        // ANALYZER DETAIL: the measurements behind the score
        GridLayout { Layout.fillWidth: true; columns: page.width > 1100 ? 6 : 3; columnSpacing: 10; rowSpacing: 10
            Repeater {
                model: [
                    { k: "MOMENTARY", v: page.db(page.tel("momentary", -120)), u: "LUFS" }, { k: "SHORT-TERM", v: page.db(page.tel("shortTerm", -120)), u: "LUFS" },
                    { k: "INTEGRATED", v: page.db(page.tel("integrated", -120)), u: "LUFS" }, { k: "RANGE (LRA)", v: Number(page.tel("lra", 0)).toFixed(1), u: "LU" },
                    { k: "TRUE PEAK", v: page.db(page.tel("truePeak", -120)), u: "dBTP" }, { k: "PEAK / RMS", v: page.db(page.tel("masterPeakDb", -120)) + " / " + page.db(page.tel("masterRmsDb", -120)), u: "dBFS" },
                    { k: "CREST FACTOR", v: page.tel("masterRmsDb", -120) <= -119 ? "—" : Number(page.tel("masterPeakDb", -120) - page.tel("masterRmsDb", -120)).toFixed(1), u: "dB" },
                    { k: "PEAK TO LOUDNESS", v: page.tel("shortTerm", -120) <= -119 ? "—" : Number(page.tel("truePeak", -120) - page.tel("shortTerm", -120)).toFixed(1), u: "PLR dB" },
                    { k: "CORRELATION", v: Number(page.tel("correlation", 1)).toFixed(2), u: page.tel("correlation", 1) < 0 ? page.t("out of phase") : page.t("+1 mono · 0 wide") },
                    { k: "WIDTH SETTING", v: Number(page.tel("width", 1) * 100).toFixed(0), u: page.t("% of the mixer's stereo width") },
                    { k: "SPECTRAL TILT", v: page.sc.valid ? Number(page.sc.tilt).toFixed(1) : "—", u: "dB / oct" }, { k: "ROUGHNESS", v: page.sc.valid ? Number(page.sc.roughness).toFixed(1) : "—", u: "dB" }
                ]
                delegate: Rectangle { Layout.fillWidth: true; implicitHeight: 62; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                    Column { anchors.fill: parent; anchors.margins: 10; spacing: 3
                        Text { text: page.t(modelData.k); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                        Text { text: modelData.v; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 17; font.bold: true }
                        Text { text: modelData.u; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } } } } }

        // the master's spectrum, 1/3 octave
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 170; radius: Theme.radius; color: Theme.card; border.color: Theme.line
            Text { x: 14; y: 8; text: page.t("SPECTRUM · MASTER L · 1/3 OCTAVE"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
            Row { anchors.fill: parent; anchors.margins: 14; anchors.topMargin: 28; spacing: 2
                Repeater { model: page.spec.length
                    delegate: Item { width: Math.max(2, (parent.width - 2 * (page.spec.length - 1)) / Math.max(1, page.spec.length)); height: parent.height
                        readonly property real lv: Math.max(0, Math.min(1, (page.spec[index] + 72) / 72))
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: parent.height * parent.lv; radius: 1; color: parent.lv > 0.85 ? Theme.danger : parent.lv > 0.6 ? Theme.amber : Theme.cyan } } } } }
    }
}
