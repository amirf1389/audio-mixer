// NOISE GATE: the channel gate (threshold, range, attack, hold, release) with the transfer curve and the live gate state
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Flickable {
    id: page
    property int strip: 0
    property var ch: ({})
    property var curve: []
    contentHeight: col.implicitHeight + 24
    clip: true
    function reload() { ch = engine.channelParams(strip); curve = engine.gateCurve(strip, 120) }
    function apply(k, v) { var p = Object.assign({}, ch); p[k] = v; engine.setGate(strip, p.gateOn, p.gateThresholdDb, p.gateRangeDb, p.gateAttackMs, p.gateHoldMs, p.gateReleaseMs) }
    Component.onCompleted: reload()
    onStripChanged: reload()
    Connections { target: engine; function onStripsChanged() { page.reload() } }
    readonly property var mt: engine.meters[strip]
    readonly property bool open: !page.ch.gateOn || (page.mt && page.mt.gateDb > -3)
    ColumnLayout {
        id: col; width: parent.width - 20; x: 10; y: 10; spacing: 10
        Panel {
            Layout.fillWidth: true
            title: "NOISE GATE • CHANNEL DYNAMICS"
            subtitle: "Opens above the threshold, closes 3 dB under it after the hold time; closed, the channel is turned down by the range. The lamp shows the live state."
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Text { text: "CHANNEL"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 }
                ComboBox {
                    implicitWidth: 150; model: engine.strips; currentIndex: page.strip
                    displayText: engine.stripState[currentIndex] ? engine.stripState[currentIndex].name : ""
                    delegate: ItemDelegate { width: parent.width; text: engine.stripState[index] ? engine.stripState[index].name : index; font.family: Theme.mono }
                    onActivated: page.strip = currentIndex
                }
                Item { Layout.fillWidth: true }
                Rectangle { implicitWidth: 90; implicitHeight: 24; radius: 12; color: page.ch.gateOn ? (page.open ? Qt.rgba(0.063, 0.725, 0.506, 0.2) : Qt.rgba(1, 0.165, 0.373, 0.2)) : Qt.rgba(1, 1, 1, 0.05); border.color: page.ch.gateOn ? (page.open ? Theme.emerald : Theme.danger) : Theme.lineSoft
                    Text { anchors.centerIn: parent; text: page.ch.gateOn ? (page.open ? "OPEN" : "CLOSED") : "BYPASS"; color: page.ch.gateOn ? (page.open ? Theme.emerald : Theme.danger) : Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } }
                Rectangle { implicitWidth: 70; implicitHeight: 24; radius: 4; color: page.ch.gateOn ? Theme.pink : Qt.rgba(1, 1, 1, 0.08); Text { anchors.centerIn: parent; text: page.ch.gateOn ? "ON" : "OFF"; color: page.ch.gateOn ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.apply("gateOn", !page.ch.gateOn) } }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 10
                DynCurve { Layout.fillWidth: true; curve: page.curve; minDb: -90; thresholdDb: page.ch.gateThresholdDb === undefined ? -50 : page.ch.gateThresholdDb; liveInDb: page.mt ? page.mt.peakDb : -120; liveGainDb: page.mt ? page.mt.gateDb : 0; lineColor: Theme.pink }
                ColumnLayout {
                    Layout.preferredWidth: 56; spacing: 4
                    Text { Layout.alignment: Qt.AlignHCenter; text: "GAIN"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                    Rectangle { Layout.alignment: Qt.AlignHCenter; implicitWidth: 16; Layout.fillHeight: true; implicitHeight: 200; radius: 3; color: "#05070a"; border.color: Theme.lineSoft
                        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 2; height: Math.min(1, Math.max(0, -(page.mt ? page.mt.gateDb : 0) / 90)) * (parent.height - 4); radius: 2; color: Theme.pink; Behavior on height { NumberAnimation { duration: 40 } } } }
                    Text { Layout.alignment: Qt.AlignHCenter; text: (page.mt ? page.mt.gateDb : 0).toFixed(1) + " dB"; color: Theme.pink; font.family: Theme.mono; font.pixelSize: 8 }
                }
            }
            GridLayout {
                columns: width > 820 ? 5 : 3; columnSpacing: 10; rowSpacing: 2; Layout.fillWidth: true
                Repeater {
                    model: [
                        { k: "gateThresholdDb", t: "THRESHOLD", u: " dB", from: -90, to: 0, step: 0.5 }, { k: "gateRangeDb", t: "RANGE", u: " dB", from: -90, to: 0, step: 1 },
                        { k: "gateAttackMs", t: "ATTACK", u: " ms", from: 0.1, to: 100, step: 0.1 }, { k: "gateHoldMs", t: "HOLD", u: " ms", from: 0, to: 2000, step: 1 }, { k: "gateReleaseMs", t: "RELEASE", u: " ms", from: 5, to: 3000, step: 1 } ]
                    delegate: ColumnLayout { Layout.fillWidth: true; spacing: 0
                        Text { text: modelData.t + "  " + Number(page.ch[modelData.k] === undefined ? modelData.from : page.ch[modelData.k]).toFixed(modelData.step < 1 ? 1 : 0) + modelData.u; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                        Slider { Layout.fillWidth: true; from: modelData.from; to: modelData.to; stepSize: modelData.step; value: page.ch[modelData.k] === undefined ? modelData.from : page.ch[modelData.k]; onMoved: page.apply(modelData.k, value) } }
                }
            }
            Flow { Layout.fillWidth: true; spacing: 4
                Repeater {
                    model: [ { n: "VOCAL MIC", p: [-45, -60, 1, 80, 150] }, { n: "DRUM CLOSE MIC", p: [-30, -80, 0.3, 30, 60] }, { n: "GUITAR AMP HUM", p: [-50, -40, 2, 120, 250] }, { n: "GENTLE EXPAND", p: [-55, -12, 5, 200, 400] } ]
                    delegate: Rectangle { implicitWidth: pl.implicitWidth + 14; implicitHeight: 20; radius: 4; color: Qt.rgba(1, 1, 1, 0.08); Text { id: pl; anchors.centerIn: parent; text: modelData.n; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setGate(page.strip, true, modelData.p[0], modelData.p[1], modelData.p[2], modelData.p[3], modelData.p[4]) } }
                }
            }
        }
    }
}
