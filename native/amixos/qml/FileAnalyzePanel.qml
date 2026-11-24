// FILE ANALYZE + AI SETUP: open an audio file (WAV, FLAC, AIFF, MP3, M4A, OGG ...), measure it offline (integrated loudness, true peak,
// loudness range, crest factor, octave balance against a pink reference) and see the setup the rules suggest for a strip: gain to the
// target, corrective EQ (the Auto EQ on the strip's 10-band graphic EQ), compressor, limiter. Each decision says why. Nothing changes
// until APPLY is confirmed; UNDO puts the strip back exactly as it was.
import QtQuick
import AmixOS
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: fa
    objectName: "fileAnalyzePanel"
    radius: Theme.radiusL; color: Theme.card; border.color: Theme.line
    gradient: Theme.fluid ? glassGrad : null
    Gradient { id: glassGrad; GradientStop { position: 0; color: Theme.glassTop } GradientStop { position: 1; color: Theme.card } }
    implicitHeight: col.implicitHeight + 24
    property var res: ({})
    property var setup: res.setup || ({})
    property string target: "stream"
    property real custom: -16
    property int strip: 0
    property bool confirming: false
    property var undo: null          // {strip, trim, eq, eqOn, ch}: the strip before APPLY
    property string note: ""
    readonly property bool have: res.ok === true
    function f1(v) { return v === undefined || v <= -119 ? "—" : (v > 0 ? "+" : "") + Number(v).toFixed(1) }
    function analyze(p) { if (!p || !p.length) return; note = ""; confirming = false; res = measure.analyzeFile(p, target, custom); if (!res.ok) note = i18n.t("Cannot read the file: ", i18n.lang) + res.error }
    function retarget(t) { target = t; if (have) { const r = res; r.setup = measure.fileSetup(target, custom); res = r; confirming = false } }
    function apply() {
        const s = setup, i = strip, p = engine.channelParams(i), g = engine.graphicEq(i); let on = false; for (let b = 0; b < 10; ++b) if (Number(g[b]) !== 0) on = true
        undo = { strip: i, trim: engine.trim(i), eq: g, eqOn: on, ch: p }
        engine.setTrim(i, s.gain)
        if (s.eqOn) engine.setGraphicEq(i, s.eq, true)
        engine.setCompressor(i, s.compOn, s.compThr, s.compRatio, s.compKnee, s.compAttack, s.compRelease, 0)
        engine.setChannelLimiter(i, s.limOn, s.limCeiling, s.limRelease)
        confirming = false; note = i18n.t("Applied to CH ", i18n.lang) + (i + 1) + i18n.t(". UNDO puts it back.", i18n.lang)
    }
    function revert() {
        if (!undo) return; const u = undo, c = u.ch
        engine.setTrim(u.strip, u.trim); engine.setGraphicEq(u.strip, u.eq, u.eqOn)
        engine.setCompressor(u.strip, c.compOn, c.compThresholdDb, c.compRatio, c.compKneeDb, c.compAttackMs, c.compReleaseMs, c.compMakeupDb)
        engine.setChannelLimiter(u.strip, c.limiterOn, c.limiterCeilDb, c.limiterReleaseMs)
        undo = null; note = i18n.t("CH ", i18n.lang) + (u.strip + 1) + i18n.t(" is back as it was.", i18n.lang)
    }
    component Btn: Rectangle { id: b; property string label; property bool on: false; property color onColor: Theme.amber; signal clicked()
        implicitWidth: Math.max(48, bt.implicitWidth + 16); implicitHeight: 24; radius: Theme.radiusS; color: on ? onColor : Theme.ov(bh.hovered ? 0.12 : 0.07); border.color: on ? onColor : Theme.line
        Behavior on color { enabled: Theme.motion; ColorAnimation { duration: Theme.motionMs } }
        Text { id: bt; anchors.centerIn: parent; text: i18n.t(b.label, i18n.lang); color: b.on ? "#000" : Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
        HoverHandler { id: bh } MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: b.clicked() } }
    component Num: ColumnLayout { property string label; property string before; property string after; spacing: 1; Layout.preferredWidth: 92
        Text { text: i18n.t(parent.label, i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
        Text { text: parent.before; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 13; font.bold: true }
        Text { visible: parent.after.length > 0; text: "→ " + parent.after; color: Theme.emerald; font.family: Theme.mono; font.pixelSize: 10; font.bold: true } }
    ColumnLayout {
        id: col; anchors.fill: parent; anchors.margins: 12; spacing: 8
        RowLayout { Layout.fillWidth: true; spacing: 8
            Text { text: i18n.t("FILE ANALYZE  ·  AI SETUP", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1 }
            Item { Layout.fillWidth: true }
            Text { visible: fa.have; text: fa.have ? fa.res.name + "  ·  " + (fa.res.seconds / 60).toFixed(1) + i18n.t(" min", i18n.lang) + "  ·  " + Math.round(fa.res.rate) + i18n.t(" Hz", i18n.lang) : ""; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; elide: Text.ElideMiddle; Layout.maximumWidth: 360 } }
        RowLayout { Layout.fillWidth: true; spacing: 6
            Field { id: pathIn; objectName: "fileAnalyzePath"; Layout.fillWidth: true; placeholderText: i18n.t("an audio file to measure (WAV, FLAC, MP3, M4A, OGG ...)", i18n.lang); onAccepted: fa.analyze(text) }
            Btn { label: "BROWSE"; onClicked: { if (!fa.dlg) { try { fa.dlg = Qt.createQmlObject('import QtQuick.Dialogs; FileDialog { }', fa, "analyzeDialog"); fa.dlg.nameFilters = measure.audioFilters(); fa.dlg.title = i18n.t("Choose an audio file", i18n.lang); fa.dlg.accepted.connect(function () { pathIn.text = fa.dlg.selectedFile.toString(); fa.analyze(pathIn.text) }) } catch (e) { fa.note = i18n.t("No file dialog here: type the path.", i18n.lang); return } } fa.dlg.open() } }
            Btn { label: "ANALYZE"; on: true; onColor: Theme.cyan; onClicked: fa.analyze(pathIn.text) } }
        RowLayout { Layout.fillWidth: true; spacing: 6
            Text { text: i18n.t("TARGET", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
            Btn { label: "STREAM -14 LUFS"; on: fa.target === "stream"; onClicked: fa.retarget("stream") }
            Btn { label: "BROADCAST -23 LUFS"; on: fa.target === "broadcast"; onClicked: fa.retarget("broadcast") }
            Btn { label: "CUSTOM"; on: fa.target === "custom"; onClicked: fa.retarget("custom") }
            MSlider { visible: fa.target === "custom"; Layout.preferredWidth: 140; from: -36; to: -6; stepSize: 0.5; value: fa.custom; onMoved: { fa.custom = value; fa.retarget("custom") } }
            Text { visible: fa.target === "custom"; text: fa.custom.toFixed(1) + i18n.t(" LUFS", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
            Item { Layout.fillWidth: true } }
        Text { visible: !fa.have; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9
            text: i18n.t("Measured offline on this computer: integrated loudness (BS.1770), true peak, loudness range, crest factor and the octave balance against a pink reference. The suggestion follows fixed rules you can read; nothing is sent anywhere and nothing changes until you APPLY.", i18n.lang) }
        Flow { visible: fa.have; Layout.fillWidth: true; spacing: 10
            Num { label: "LOUDNESS LUFS"; before: fa.f1(fa.res.lufs); after: fa.f1(fa.setup.afterLufs) }
            Num { label: "TRUE PEAK dBTP"; before: fa.f1(fa.res.truePeak); after: fa.f1(fa.setup.afterTruePeak) }
            Num { label: "RANGE LU"; before: fa.have ? Number(fa.res.lra).toFixed(1) : ""; after: fa.have ? Number(fa.setup.afterLra).toFixed(1) : "" }
            Num { label: "CREST dB"; before: fa.have ? Number(fa.res.crest).toFixed(1) : ""; after: "" }
            Num { label: "PEAK dBFS"; before: fa.f1(fa.res.peak); after: "" }
            Num { label: "CLIPPED"; before: fa.have ? String(fa.res.clipped) : ""; after: "" } }
        RowLayout { visible: fa.have; Layout.fillWidth: true; spacing: 3   // the octave balance (bar) and the suggested EQ (number)
            Repeater { model: 10
                delegate: ColumnLayout { spacing: 1; Layout.fillWidth: true
                    readonly property real bal: fa.have ? Number(fa.res.balance[index]) : 0
                    Rectangle { Layout.fillWidth: true; implicitHeight: 40; radius: Theme.radiusS; color: Theme.deep
                        Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: parent.width - 8; height: Math.min(20, Math.abs(parent.parent.bal) * 2); radius: 2
                            y: parent.parent.bal >= 0 ? 20 - height : 20; color: Math.abs(parent.parent.bal) > 3 ? Theme.amber : Theme.cyan }
                        Rectangle { anchors.left: parent.left; anchors.right: parent.right; y: 20; height: 1; color: Theme.line } }
                    Text { Layout.alignment: Qt.AlignHCenter; text: ["31", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"][index]; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                    Text { Layout.alignment: Qt.AlignHCenter; text: fa.setup.eqOn ? fa.f1(fa.setup.eq[index]) : "0"; color: fa.setup.eqOn && Number(fa.setup.eq[index]) !== 0 ? Theme.emerald : Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } } } }
        ColumnLayout { visible: fa.have; Layout.fillWidth: true; spacing: 2
            Repeater { model: fa.setup.reasons || []
                delegate: Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "• " + modelData; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9 } } }
        RowLayout { visible: fa.have; Layout.fillWidth: true; spacing: 6
            Text { text: i18n.t("TO CH", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
            Btn { label: "−"; onClicked: fa.strip = Math.max(0, fa.strip - 1) }
            Text { text: String(fa.strip + 1); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 12; font.bold: true; Layout.preferredWidth: 24; horizontalAlignment: Text.AlignHCenter }
            Btn { label: "+"; onClicked: fa.strip = Math.min(engine.strips - 1, fa.strip + 1) }
            Btn { objectName: "fileAnalyzeApply"; visible: !fa.confirming; label: "APPLY"; on: true; onColor: Theme.emerald; onClicked: fa.confirming = true }
            Btn { visible: fa.confirming; label: "CONFIRM"; on: true; onColor: Theme.emerald; onClicked: fa.apply() }
            Btn { visible: fa.confirming; label: "CANCEL"; onClicked: fa.confirming = false }
            Btn { visible: fa.undo !== null; label: "UNDO"; on: true; onColor: Theme.amber; onClicked: fa.revert() }
            Text { Layout.fillWidth: true; text: fa.confirming ? i18n.t("Sets the trim, graphic EQ, compressor and limiter of this channel.", i18n.lang) : fa.note; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; wrapMode: Text.WordWrap } }
        Text { visible: !fa.have && fa.note.length > 0; Layout.fillWidth: true; text: fa.note; color: Theme.danger; font.family: Theme.mono; font.pixelSize: 9; wrapMode: Text.WordWrap }
    }
    property var dlg: null
}
