// COMPRESSOR: the channel compressor (threshold, ratio, knee, attack, release, make-up) with the transfer curve and the live gain-reduction meter
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
    function reload() { ch = engine.channelParams(strip); curve = engine.compressorCurve(strip, 120) }
    function apply(k, v) { var p = Object.assign({}, ch); p[k] = v; engine.setCompressor(strip, p.compOn, p.compThresholdDb, p.compRatio, p.compKneeDb, p.compAttackMs, p.compReleaseMs, p.compMakeupDb) }
    Component.onCompleted: reload()
    onStripChanged: reload()
    Connections { target: engine; function onStripsChanged() { page.reload() } }
    readonly property var mt: engine.meters[strip]
    ColumnLayout {
        id: col; width: parent.width - 20; x: 10; y: 10; spacing: 10
        Panel {
            Layout.fillWidth: true
            title: i18n.t("COMPRESSOR • CHANNEL DYNAMICS", i18n.lang)
            subtitle: "Feed-forward peak detector, soft knee, attack and release in the dB domain, make-up gain. The dot is the live input and output of this channel."
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Text { text: i18n.t("CHANNEL", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 }
                Combo {
                    implicitWidth: 150; model: engine.strips; currentIndex: page.strip
                    displayText: engine.stripState[currentIndex] ? engine.stripState[currentIndex].name : ""
                    delegate: ItemDelegate { width: parent.width; text: engine.stripState[index] ? engine.stripState[index].name : index; font.family: Theme.mono }
                    onActivated: page.strip = currentIndex
                }
                Item { Layout.fillWidth: true }
                Rectangle { implicitWidth: 70; implicitHeight: 24; radius: 4; color: page.ch.compOn ? Theme.emerald : Theme.ov(0.08); Text { anchors.centerIn: parent; text: page.ch.compOn ? i18n.t("ON", i18n.lang) : i18n.t("OFF", i18n.lang); color: page.ch.compOn ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.apply("compOn", !page.ch.compOn) } }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 10
                DynCurve { Layout.fillWidth: true; curve: page.curve; thresholdDb: page.ch.compThresholdDb === undefined ? -20 : page.ch.compThresholdDb; liveInDb: page.mt ? page.mt.peakDb : -120; liveGainDb: page.mt ? page.mt.compGrDb : 0; lineColor: Theme.emerald }
                ColumnLayout {   // gain reduction meter
                    Layout.preferredWidth: 56; spacing: 4
                    Text { Layout.alignment: Qt.AlignHCenter; text: i18n.t("GR", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                    Rectangle { Layout.alignment: Qt.AlignHCenter; implicitWidth: 16; Layout.fillHeight: true; implicitHeight: 200; radius: 3; color: Theme.deep; border.color: Theme.lineSoft
                        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 2; height: Math.min(1, Math.max(0, -(page.mt ? page.mt.compGrDb : 0) / 24)) * (parent.height - 4); radius: 2; color: Theme.amber; Behavior on height { NumberAnimation { duration: 40 } } } }
                    Text { Layout.alignment: Qt.AlignHCenter; text: (page.mt ? page.mt.compGrDb : 0).toFixed(1) + i18n.t(" dB", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8 }
                }
            }
            GridLayout {
                columns: width > 820 ? 6 : 3; columnSpacing: 10; rowSpacing: 2; Layout.fillWidth: true
                Repeater {
                    model: [
                        { k: "compThresholdDb", t: "THRESHOLD", u: " dB", from: -60, to: 0, step: 0.5 }, { k: "compRatio", t: "RATIO", u: ":1", from: 1, to: 20, step: 0.1 }, { k: "compKneeDb", t: "KNEE", u: " dB", from: 0, to: 24, step: 0.5 },
                        { k: "compAttackMs", t: "ATTACK", u: " ms", from: 0.1, to: 200, step: 0.1 }, { k: "compReleaseMs", t: "RELEASE", u: " ms", from: 5, to: 2000, step: 1 }, { k: "compMakeupDb", t: "MAKE-UP", u: " dB", from: 0, to: 24, step: 0.5 } ]
                    delegate: ColumnLayout { Layout.fillWidth: true; spacing: 0
                        Text { text: modelData.t + "  " + Number(page.ch[modelData.k] === undefined ? modelData.from : page.ch[modelData.k]).toFixed(modelData.step < 1 ? 1 : 0) + modelData.u; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                        Slider { Layout.fillWidth: true; from: modelData.from; to: modelData.to; stepSize: modelData.step; value: page.ch[modelData.k] === undefined ? modelData.from : page.ch[modelData.k]; onMoved: page.apply(modelData.k, value) } }
                }
            }
            Flow { Layout.fillWidth: true; spacing: 4
                Repeater {
                    model: [ { n: "VOCAL", p: [-18, 3, 6, 5, 80, 3] }, { n: "DRUM BUS", p: [-12, 4, 2, 1, 120, 2] }, { n: "GENTLE GLUE", p: [-24, 2, 12, 20, 300, 2] }, { n: "LIMIT", p: [-6, 20, 0, 0.1, 60, 0] }, { n: "BASS", p: [-20, 5, 4, 10, 150, 4] } ]
                    delegate: Rectangle { implicitWidth: pl.implicitWidth + 14; implicitHeight: 20; radius: 4; color: Theme.ov(0.08); Text { id: pl; anchors.centerIn: parent; text: modelData.n; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setCompressor(page.strip, true, modelData.p[0], modelData.p[1], modelData.p[2], modelData.p[3], modelData.p[4], modelData.p[5]) } }
                }
            }
        }
    }
}
