// MASTERING: the plug-in on the master after the PROGRAM EQ (the RTA EQ page's rack) and before the master fader and limiter: low shelf,
// air shelf, a stereo-linked glue compressor, soft saturation and output gain into the limiter. Presets set everything; every value can
// then be moved. Chain: strips -> master sum -> PROGRAM EQ -> MASTERING -> width -> master fader -> limiter -> outputs.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "masteringPage"
    property var m: engine.mastering()
    property real gr: 0
    function t(s) { return i18n.t(s, i18n.lang) }
    function put(k, v) { const o = {}; o[k] = v; engine.setMastering(o) }
    Connections { target: engine; function onMasterChainChanged() { page.m = engine.mastering() } }
    Timer { interval: 50; running: page.visible; repeat: true; onTriggered: page.gr = engine.masteringReductionDb() }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(64, bl.implicitWidth + 18); implicitHeight: 24; radius: 4; color: on ? onColor : Theme.ov(0.08); border.color: Theme.lineSoft
        Text { id: bl; anchors.centerIn: parent; text: label; color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    component Knob: RowLayout { property string label; property string k; property real from; property real to; property string unit; property int digits: 1; spacing: 8; Layout.fillWidth: true
        Text { text: label; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; Layout.preferredWidth: 130 }
        Slider { Layout.fillWidth: true; from: parent.from; to: parent.to; value: page.m[parent.k] || 0; onMoved: page.put(parent.k, value) }
        Text { text: Number(page.m[parent.k] || 0).toFixed(parent.digits) + " " + unit; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; Layout.preferredWidth: 70 } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle { Layout.fillWidth: true; implicitHeight: 38; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Text { text: page.t("MASTERING:"); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Btn { label: page.m.on ? page.t("PLUG-IN: ON") : page.t("PLUG-IN: OFF"); on: page.m.on; onColor: Theme.emerald; onClicked: page.put("on", !page.m.on) }
                Btn { label: page.m.eqOn ? page.t("PROGRAM EQ: ON") : page.t("PROGRAM EQ: BYPASSED"); on: page.m.eqOn; onColor: Theme.cyan; onClicked: page.put("eqOn", !page.m.eqOn) }
                Rectangle { width: 1; height: 20; color: Theme.lineSoft }
                Repeater { model: engine.masteringPresets(); delegate: Btn { label: page.t(modelData); on: page.m.preset === modelData; onClicked: engine.applyMasteringPreset(modelData) } }
                Item { Layout.fillWidth: true } } }
        Rectangle { Layout.fillWidth: true; implicitHeight: 30; radius: 8; color: Theme.chrome; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Repeater { model: ["STRIPS", "MASTER SUM", "PROGRAM EQ", "MASTERING", "WIDTH", "MASTER FADER", "LIMITER", "OUTPUTS"]
                    delegate: RowLayout { spacing: 6; Text { visible: index > 0; text: "→"; color: Theme.dim; font.pixelSize: 11 }
                        Text { text: page.t(modelData); color: (index === 2 && page.m.eqOn) || (index === 3 && page.m.on) ? Theme.emerald : (index === 2 || index === 3) ? Theme.dim : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: index === 2 || index === 3 } } }
                Item { Layout.fillWidth: true } } }
        Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; opacity: page.m.on ? 1 : 0.6
            ColumnLayout { anchors.fill: parent; anchors.margins: 14; spacing: 10
                Text { text: page.t("TONE"); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; font.letterSpacing: 1.5 }
                Knob { label: page.t("LOW SHELF 100 Hz"); k: "low"; from: -12; to: 12; unit: "dB" }
                Knob { label: page.t("AIR SHELF 10 kHz"); k: "air"; from: -12; to: 12; unit: "dB" }
                Text { text: page.t("GLUE COMPRESSOR"); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; font.letterSpacing: 1.5 }
                Knob { label: page.t("THRESHOLD"); k: "thr"; from: -40; to: 0; unit: "dBFS" }
                Knob { label: page.t("RATIO"); k: "ratio"; from: 1; to: 10; unit: ": 1" }
                Knob { label: page.t("ATTACK"); k: "attack"; from: 0.5; to: 200; unit: "ms" }
                Knob { label: page.t("RELEASE"); k: "release"; from: 20; to: 2000; unit: "ms"; digits: 0 }
                Knob { label: page.t("MAKE-UP"); k: "makeup"; from: 0; to: 18; unit: "dB" }
                RowLayout { spacing: 8; Layout.fillWidth: true
                    Text { text: page.t("GAIN REDUCTION"); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; Layout.preferredWidth: 130 }
                    Rectangle { Layout.fillWidth: true; height: 10; radius: 3; color: Theme.deep; border.color: Theme.lineSoft; Rectangle { height: parent.height; radius: 3; width: parent.width * Math.min(1, page.gr / 20); color: Theme.amber } }
                    Text { text: "-" + page.gr.toFixed(1) + " dB"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; Layout.preferredWidth: 70 } }
                Text { text: page.t("COLOUR AND LEVEL"); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; font.letterSpacing: 1.5 }
                Knob { label: page.t("SATURATION"); k: "drive"; from: 0; to: 1; unit: ""; digits: 2 }
                Knob { label: page.t("OUTPUT INTO LIMITER"); k: "gain"; from: -12; to: 12; unit: "dB" }
                Item { Layout.fillHeight: true }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7
                    text: page.t("The mastering plug-in works on the whole mix, after the PROGRAM EQ of the RTA EQ page. Raising OUTPUT pushes the mix into the master limiter (its ceiling is on SOUND CARD): louder, with less dynamics; watch LOUDNESS for the LUFS target (-14 streaming, -23 broadcast). It is saved with the console. The recorder and the monitor hear the result.") } } }
    }
}
