// NOISE GATE: the channel gate (threshold, range, attack, hold, release) with the transfer curve and the live gate state
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Flickable {
    id: page
    property int strip: engine.selectedStrip
    property var ch: ({})
    property var curve: []
    contentHeight: col.implicitHeight + 24
    clip: true
    function reload() { ch = engine.channelParams(strip); curve = engine.gateCurve(strip, 120) }
    function apply(k, v) { var p = Object.assign({}, ch); p[k] = v; engine.setGate(strip, p.gateOn, p.gateThresholdDb, p.gateRangeDb, p.gateAttackMs, p.gateHoldMs, p.gateReleaseMs) }
    Component.onCompleted: reload()
    onStripChanged: reload()
    Connections { target: engine; function onStripsChanged() { page.reload() } }
    readonly property var mt: (engine.meterTick, engine.meterAt(strip))
    readonly property bool open: !page.ch.gateOn || (page.mt && page.mt.gateDb > -3)
    ColumnLayout {
        id: col; width: parent.width - 20; x: 10; y: 10; spacing: 10
        Panel {
            Layout.fillWidth: true
            title: i18n.t("NOISE GATE • CHANNEL DYNAMICS", i18n.lang)
            subtitle: i18n.t("Opens above the threshold, closes 3 dB under it after the hold time; closed, the channel is turned down by the range. The lamp shows the live state.", i18n.lang)
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Text { text: i18n.t("CHANNEL", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 }
                Combo {
                    implicitWidth: 150; model: engine.strips; currentIndex: page.strip
                    displayText: engine.stripAt(currentIndex, engine.stripTick) ? engine.stripAt(currentIndex, engine.stripTick).name : ""
                    delegate: ItemDelegate { width: parent.width; text: engine.stripAt(index, engine.stripTick) ? engine.stripAt(index, engine.stripTick).name : index; font.family: Theme.mono }
                    onActivated: page.strip = currentIndex
                }
                Rectangle { id: gb36; objectName: "closeNoiseBtn"; implicitWidth: cnT.implicitWidth + 20; implicitHeight: 24; radius: 7; color: Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb36.color) } GradientStop { position: 1; color: Theme.btnBottom(gb36.color) } } border.color: Theme.cyan   // CLOSE NOISE: gate set from the noise that is there now
                    property string said: ""
                    Text { id: cnT; anchors.centerIn: parent; text: parent.said !== "" ? parent.said : i18n.t("CLOSE NOISE", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                    MouseArea { anchors.fill: parent; onClicked: { const t = engine.closeNoise(page.strip); parent.said = t < 0 ? i18n.t("GATE AT ", i18n.lang) + t.toFixed(0) + " dB" : i18n.t("NO SIGNAL TO READ", i18n.lang); cnReset.restart() } }
                    Timer { id: cnReset; interval: 2500; onTriggered: parent.said = "" }
                    ToolTip.visible: cnH.hovered; ToolTip.text: i18n.t("With nobody playing, press this: the gate is set 6 dB above the hum, hiss and room noise on this channel, so it closes when the channel is not in use.", i18n.lang); HoverHandler { id: cnH } }
                Item { Layout.fillWidth: true }
                Rectangle { implicitWidth: 90; implicitHeight: 24; radius: 12; color: page.ch.gateOn ? (page.open ? Qt.rgba(0.063, 0.725, 0.506, 0.2) : Qt.rgba(1, 0.165, 0.373, 0.2)) : Theme.ov(0.05); border.color: page.ch.gateOn ? (page.open ? Theme.emerald : Theme.danger) : Theme.lineSoft
                    Text { anchors.centerIn: parent; text: page.ch.gateOn ? (page.open ? i18n.t("OPEN", i18n.lang) : i18n.t("CLOSED", i18n.lang)) : i18n.t("BYPASS", i18n.lang); color: page.ch.gateOn ? (page.open ? Theme.emerald : Theme.danger) : Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } }
                Rectangle { id: gb37; implicitWidth: 70; implicitHeight: 24; radius: 7; color: page.ch.gateOn ? Theme.pink : Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb37.color) } GradientStop { position: 1; color: Theme.btnBottom(gb37.color) } } Text { anchors.centerIn: parent; text: page.ch.gateOn ? i18n.t("ON", i18n.lang) : i18n.t("OFF", i18n.lang); color: page.ch.gateOn ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.apply("gateOn", !page.ch.gateOn) } }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 10
                DynCurve { Layout.fillWidth: true; curve: page.curve; minDb: -90; thresholdDb: page.ch.gateThresholdDb === undefined ? -50 : page.ch.gateThresholdDb; liveInDb: page.mt ? page.mt.peakDb : -120; liveGainDb: page.mt ? page.mt.gateDb : 0; lineColor: Theme.pink }
                ColumnLayout {
                    Layout.preferredWidth: 56; spacing: 4
                    Text { Layout.alignment: Qt.AlignHCenter; text: i18n.t("GAIN", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                    Rectangle { Layout.alignment: Qt.AlignHCenter; implicitWidth: 16; Layout.fillHeight: true; implicitHeight: 200; radius: 3; color: Theme.deep; border.color: Theme.lineSoft
                        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 2; height: Math.min(1, Math.max(0, -(page.mt ? page.mt.gateDb : 0) / 90)) * (parent.height - 4); radius: 2; color: Theme.pink; Behavior on height { NumberAnimation { duration: 40 } } } }
                    Text { Layout.alignment: Qt.AlignHCenter; text: (page.mt ? page.mt.gateDb : 0).toFixed(1) + i18n.t(" dB", i18n.lang); color: Theme.pink; font.family: Theme.mono; font.pixelSize: 8 }
                }
            }
            GridLayout {
                columns: width > 820 ? 5 : 3; columnSpacing: 10; rowSpacing: 2; Layout.fillWidth: true
                Repeater {
                    model: [
                        { k: "gateThresholdDb", t: "THRESHOLD", u: " dB", from: -90, to: 0, step: 0.5 }, { k: "gateRangeDb", t: "RANGE", u: " dB", from: -90, to: 0, step: 1 },
                        { k: "gateAttackMs", t: "ATTACK", u: " ms", from: 0.1, to: 100, step: 0.1 }, { k: "gateHoldMs", t: "HOLD", u: " ms", from: 0, to: 2000, step: 1 }, { k: "gateReleaseMs", t: "RELEASE", u: " ms", from: 5, to: 3000, step: 1 } ]
                    delegate: ColumnLayout { Layout.fillWidth: true; spacing: 0
                        Text { text: i18n.t(modelData.t ?? "", i18n.lang) + "  " + Number(page.ch[modelData.k] === undefined ? modelData.from : page.ch[modelData.k]).toFixed(modelData.step < 1 ? 1 : 0) + modelData.u; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                        Slider { Layout.fillWidth: true; from: modelData.from; to: modelData.to; stepSize: modelData.step; value: page.ch[modelData.k] === undefined ? modelData.from : page.ch[modelData.k]; onMoved: page.apply(modelData.k, value) } }
                }
            }
            Flow { Layout.fillWidth: true; spacing: 4
                Repeater {
                    model: [ { n: "VOCAL MIC", p: [-45, -60, 1, 80, 150] }, { n: "DRUM CLOSE MIC", p: [-30, -80, 0.3, 30, 60] }, { n: "GUITAR AMP HUM", p: [-50, -40, 2, 120, 250] }, { n: "GENTLE EXPAND", p: [-55, -12, 5, 200, 400] } ]
                    delegate: Rectangle { id: gb38; implicitWidth: pl.implicitWidth + 14; implicitHeight: 20; radius: 7; color: Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb38.color) } GradientStop { position: 1; color: Theme.btnBottom(gb38.color) } } Text { id: pl; anchors.centerIn: parent; text: i18n.t(modelData.n ?? "", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setGate(page.strip, true, modelData.p[0], modelData.p[1], modelData.p[2], modelData.p[3], modelData.p[4]) } }
                }
            }
        }
    }
}
