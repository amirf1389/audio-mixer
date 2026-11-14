// MASTERING: the plug-in rack on the master, after the PROGRAM EQ (the RTA EQ page's rack) and before the master fader. One card per
// module, each with its own switch: TONE (low / air shelves), BASS MONO, DE-ESSER, GLUE COMPRESSOR, EXCITER, SATURATION + OUTPUT, and the
// master LIMITER (ceiling, release, gain-reduction meter). Chain: strips -> master sum -> PROGRAM EQ -> MASTERING -> width -> fader -> LIMITER.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "masteringPage"
    property var m: engine.mastering()
    property var lim: engine.masterLimiter()
    property real gr: 0
    property real limGr: 0
    property real ds: 0
    function t(s) { return i18n.t(s, i18n.lang) }
    function put(k, v) { const o = {}; o[k] = v; engine.setMastering(o) }
    Connections { target: engine; function onMasterChainChanged() { page.m = engine.mastering(); page.lim = engine.masterLimiter() } }
    Timer { interval: 50; running: page.visible; repeat: true; onTriggered: { page.gr = engine.masteringReductionDb(); page.limGr = engine.masterLimiterReductionDb(); page.ds = engine.masteringDeessDb() } }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 22; radius: 4; color: on ? onColor : Theme.ov(0.08); border.color: Theme.lineSoft
        Text { id: bl; anchors.centerIn: parent; text: i18n.t(label, i18n.lang); color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    component Knob: RowLayout { property string label; property real value; property real from; property real to; property string unit; property int digits: 1; signal moved(real v); spacing: 6; Layout.fillWidth: true
        Text { text: i18n.t(label, i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; Layout.preferredWidth: 86; elide: Text.ElideRight }
        Slider { Layout.fillWidth: true; from: parent.from; to: parent.to; value: parent.value; onMoved: parent.moved(value) }
        Text { text: Number(parent.value).toFixed(parent.digits) + " " + unit; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; Layout.preferredWidth: 64; horizontalAlignment: Text.AlignRight } }
    component Meter: RowLayout { property string label; property real db; property real span: 20; property color tint: Theme.amber; spacing: 6; Layout.fillWidth: true
        Text { text: i18n.t(label, i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; Layout.preferredWidth: 86 }
        Rectangle { Layout.fillWidth: true; height: 10; radius: 3; color: Theme.deep; border.color: Theme.lineSoft; Rectangle { height: parent.height; radius: 3; width: parent.width * Math.min(1, parent.parent.db / parent.parent.span); color: parent.parent.tint } }
        Text { text: "-" + db.toFixed(1) + " dB"; color: tint; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; Layout.preferredWidth: 64; horizontalAlignment: Text.AlignRight } }
    component Card: Rectangle { id: card; property string title; property string sub; property bool on: false; property color tint: Theme.emerald; signal toggled(); default property alias content: body.data
        Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 150; radius: Theme.radius; color: Theme.card; border.color: on ? tint : Theme.line; opacity: on ? 1 : 0.72
        Rectangle { width: 4; height: parent.height - 16; y: 8; x: 4; radius: 2; color: card.on ? card.tint : Theme.ov(0.12) }
        ColumnLayout { id: body; anchors.fill: parent; anchors.margins: 12; anchors.leftMargin: 16; spacing: 8
            RowLayout { Layout.fillWidth: true; spacing: 8
                Text { text: i18n.t(card.title, i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
                Text { text: card.sub; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; Layout.fillWidth: true; elide: Text.ElideRight }
                Btn { label: card.on ? page.t("ON") : page.t("OFF"); on: card.on; onColor: card.tint; onClicked: card.toggled() } } } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle { Layout.fillWidth: true; implicitHeight: 38; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Text { text: page.t("MASTERING:"); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Btn { label: page.m.on ? page.t("RACK: ON") : page.t("RACK: BYPASSED"); on: page.m.on; onColor: Theme.emerald; onClicked: page.put("on", !page.m.on) }
                Btn { label: page.m.eqOn ? page.t("PROGRAM EQ: ON") : page.t("PROGRAM EQ: BYPASSED"); on: page.m.eqOn; onColor: Theme.cyan; onClicked: page.put("eqOn", !page.m.eqOn) }
                Rectangle { width: 1; height: 20; color: Theme.lineSoft }
                Text { text: page.t("PRESETS"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.letterSpacing: 2 }
                Repeater { model: engine.masteringPresets(); delegate: Btn { label: page.t(modelData); on: page.m.preset === modelData; onClicked: engine.applyMasteringPreset(modelData) } }
                Item { Layout.fillWidth: true } } }
        Rectangle { Layout.fillWidth: true; implicitHeight: 28; radius: 8; color: Theme.chrome; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Repeater { model: [["MASTER SUM", true, false], ["PROGRAM EQ", page.m.eqOn, true], ["TONE", page.m.on && page.m.toneOn, true], ["BASS MONO", page.m.on && page.m.bassMonoOn, true], ["DE-ESSER", page.m.on && page.m.deessOn, true], ["COMPRESSOR", page.m.on && page.m.compOn, true], ["EXCITER", page.m.on && page.m.exciterOn, true], ["SATURATION", page.m.on && page.m.satOn, true], ["WIDTH", true, false], ["MASTER FADER", true, false], ["LIMITER", page.lim.on, true], ["OUTPUTS", true, false]]
                    delegate: RowLayout { spacing: 6; Text { visible: index > 0; text: "→"; color: Theme.dim; font.pixelSize: 10 }
                        Text { text: page.t(modelData[0]); color: !modelData[2] ? Theme.muted : modelData[1] ? Theme.emerald : Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.bold: modelData[2] } } }
                Item { Layout.fillWidth: true } } }
        GridLayout { Layout.fillWidth: true; Layout.fillHeight: true; columns: width > 1000 ? 3 : 2; columnSpacing: 8; rowSpacing: 8
            Card { title: page.t("TONE"); sub: page.t("shelves"); on: page.m.toneOn; onToggled: page.put("toneOn", !page.m.toneOn)
                Knob { label: page.t("LOW 100 Hz"); value: page.m.low; from: -12; to: 12; unit: "dB"; onMoved: function (v) { page.put("low", v) } }
                Knob { label: page.t("AIR 10 kHz"); value: page.m.air; from: -12; to: 12; unit: "dB"; onMoved: function (v) { page.put("air", v) } }
                Item { Layout.fillHeight: true } }
            Card { title: page.t("GLUE COMPRESSOR"); sub: page.t("stereo linked"); on: page.m.compOn; tint: Theme.amber; onToggled: page.put("compOn", !page.m.compOn)
                Knob { label: page.t("THRESHOLD"); value: page.m.thr; from: -40; to: 0; unit: "dBFS"; onMoved: function (v) { page.put("thr", v) } }
                Knob { label: page.t("RATIO"); value: page.m.ratio; from: 1; to: 10; unit: ": 1"; onMoved: function (v) { page.put("ratio", v) } }
                Knob { label: page.t("ATTACK"); value: page.m.attack; from: 0.5; to: 200; unit: "ms"; onMoved: function (v) { page.put("attack", v) } }
                Knob { label: page.t("RELEASE"); value: page.m.release; from: 20; to: 2000; unit: "ms"; digits: 0; onMoved: function (v) { page.put("release", v) } }
                Knob { label: page.t("MAKE-UP"); value: page.m.makeup; from: 0; to: 18; unit: "dB"; onMoved: function (v) { page.put("makeup", v) } }
                Meter { label: page.t("REDUCTION"); db: page.gr }
                Item { Layout.fillHeight: true } }
            Card { title: page.t("LIMITER"); sub: page.t("brick wall, after the master fader"); on: page.lim.on; tint: Theme.danger; onToggled: engine.setMasterLimiter(!page.lim.on, page.lim.ceiling, page.lim.release)
                Knob { label: page.t("CEILING"); value: page.lim.ceiling; from: -12; to: 0; unit: "dBFS"; onMoved: function (v) { engine.setMasterLimiter(page.lim.on, Math.round(v * 10) / 10, page.lim.release) } }
                Knob { label: page.t("RELEASE"); value: page.lim.release; from: 10; to: 1000; unit: "ms"; digits: 0; onMoved: function (v) { engine.setMasterLimiter(page.lim.on, page.lim.ceiling, v) } }
                Meter { label: page.t("REDUCTION"); db: page.limGr; tint: Theme.danger }
                RowLayout { spacing: 6; Repeater { model: [[-0.1, "CD -0.1"], [-0.5, "SAFE -0.5"], [-1, "STREAM -1.0"], [-2, "BROADCAST -2.0"]]; delegate: Btn { label: modelData[1]; on: Math.abs(page.lim.ceiling - modelData[0]) < 0.01; onClicked: engine.setMasterLimiter(true, modelData[0], page.lim.release) } } }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; text: page.t("Instant attack: nothing passes the ceiling. Steady reduction over 3 - 4 dB means the mix is pushed too hard: lower OUTPUT or the compressor make-up.") }
                Item { Layout.fillHeight: true } }
            Card { title: page.t("DE-ESSER"); sub: page.t("above 5 kHz"); on: page.m.deessOn; tint: Theme.cyan; onToggled: page.put("deessOn", !page.m.deessOn)
                Knob { label: page.t("THRESHOLD"); value: page.m.deessThr; from: -50; to: 0; unit: "dBFS"; onMoved: function (v) { page.put("deessThr", v) } }
                Knob { label: page.t("MAX CUT"); value: page.m.deessMax; from: 0; to: 18; unit: "dB"; onMoved: function (v) { page.put("deessMax", v) } }
                Meter { label: page.t("REDUCTION"); db: page.ds; span: 18; tint: Theme.cyan }
                Item { Layout.fillHeight: true } }
            Card { title: page.t("EXCITER + BASS MONO"); sub: page.t("harmonics above 3 kHz · mono lows"); on: page.m.exciterOn || page.m.bassMonoOn; tint: Theme.violet; onToggled: { const v = !(page.m.exciterOn || page.m.bassMonoOn); engine.setMastering({ exciterOn: v, bassMonoOn: v }) }
                RowLayout { spacing: 6; Btn { label: page.m.exciterOn ? page.t("EXCITER: ON") : page.t("EXCITER: OFF"); on: page.m.exciterOn; onColor: Theme.violet; onClicked: page.put("exciterOn", !page.m.exciterOn) } Btn { label: page.m.bassMonoOn ? page.t("BASS MONO: ON") : page.t("BASS MONO: OFF"); on: page.m.bassMonoOn; onColor: Theme.violet; onClicked: page.put("bassMonoOn", !page.m.bassMonoOn) } }
                Knob { label: page.t("EXCITER"); value: page.m.exciter; from: 0; to: 1; unit: ""; digits: 2; onMoved: function (v) { page.put("exciter", v) } }
                Knob { label: page.t("MONO BELOW"); value: page.m.bassMonoHz; from: 40; to: 400; unit: "Hz"; digits: 0; onMoved: function (v) { page.put("bassMonoHz", v) } }
                Item { Layout.fillHeight: true } }
            Card { title: page.t("SATURATION + OUTPUT"); sub: page.t("into the limiter"); on: page.m.satOn; tint: Theme.orange; onToggled: page.put("satOn", !page.m.satOn)
                Knob { label: page.t("SATURATION"); value: page.m.drive; from: 0; to: 1; unit: ""; digits: 2; onMoved: function (v) { page.put("drive", v) } }
                Knob { label: page.t("OUTPUT"); value: page.m.gain; from: -12; to: 12; unit: "dB"; onMoved: function (v) { page.put("gain", v) } }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; text: page.t("OUTPUT pushes the mix into the limiter: louder, with less dynamics. Watch LOUDNESS for the target (-14 LUFS streaming, -23 broadcast). Saved with the console; the recorder and the monitor hear the result.") }
                Item { Layout.fillHeight: true } }
        }
    }
}
