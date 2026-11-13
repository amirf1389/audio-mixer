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
    readonly property var mt: (engine.meterTick, engine.meterAt(strip))
    ColumnLayout {
        id: col; width: parent.width - 20; x: 10; y: 10; spacing: 10
        Rectangle {   // COMPRESSION TARGET: what this channel's input does, and the setting that would bring it to the chosen target; applied only on the click
            id: tgt
            property var sug: ({})
            Timer { interval: 1000; running: page.visible; repeat: true; triggeredOnStart: true; onTriggered: tgt.sug = engine.compSuggestion(page.strip) }
            Layout.fillWidth: true; implicitHeight: tgtCol.implicitHeight + 20; radius: 6; color: Theme.card; border.color: tgt.sug.verdict === "too dynamic" ? Theme.amber : tgt.sug.verdict === "on target" ? Theme.emerald : Theme.line
            ColumnLayout {
                id: tgtCol; anchors.fill: parent; anchors.margins: 10; spacing: 6
                RowLayout { Layout.fillWidth: true; spacing: 8
                    Text { text: i18n.t("COMPRESSION TARGET", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; font.letterSpacing: 1.5 }
                    Item { Layout.fillWidth: true }
                    Repeater { model: [{ t: "VOCAL 6 dB", v: 6 }, { t: "INSTRUMENT 9 dB", v: 9 }, { t: "MUSIC 12 dB", v: 12 }]
                        Rectangle { required property var modelData; readonly property bool on: Math.abs(engine.compTargetDb - modelData.v) < 0.01
                            objectName: "compTarget" + modelData.v; implicitWidth: tT.implicitWidth + 16; implicitHeight: 20; radius: 3; color: on ? Theme.cyan : Theme.ov(0.08); border.color: Theme.lineSoft
                            Text { id: tT; anchors.centerIn: parent; text: i18n.t(modelData.t, i18n.lang); color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { engine.compTargetDb = modelData.v; tgt.sug = engine.compSuggestion(page.strip) } } } }
                }
                RowLayout { Layout.fillWidth: true; spacing: 14
                    Text { color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9
                           text: !tgt.sug.ready ? i18n.t("Listening: play or speak into this channel for a few seconds.", i18n.lang)
                                : i18n.t("Spread ", i18n.lang) + Number(tgt.sug.spreadDb).toFixed(1) + i18n.t(" dB (target ", i18n.lang) + Number(tgt.sug.targetDb).toFixed(0) + " dB) · " + i18n.t(tgt.sug.verdict, i18n.lang) }
                    Item { Layout.fillWidth: true }
                    Text { visible: !!tgt.sug.ready; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true
                           text: tgt.sug.ratio > 1 ? i18n.t("Suggested: threshold ", i18n.lang) + Number(tgt.sug.thresholdDb).toFixed(1) + i18n.t(" dB, ratio ", i18n.lang) + Number(tgt.sug.ratio).toFixed(1) + ":1" : i18n.t("No compression needed", i18n.lang) }
                    Rectangle { objectName: "compTargetApply"; visible: !!tgt.sug.ready && tgt.sug.ratio > 1; implicitWidth: apT.implicitWidth + 20; implicitHeight: 22; radius: 4; color: Qt.rgba(0, 0.95, 1, 0.15); border.color: Theme.cyan
                        Text { id: apT; anchors.centerIn: parent; text: i18n.t("APPLY", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { engine.applyCompSuggestion(page.strip); page.reload() } } }
                }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.dim; font.pixelSize: 8
                       text: i18n.t("Measured on the channel's input over the last 10 seconds: how far the loud and the quiet moments sit apart. Nothing changes until you press APPLY.", i18n.lang) }
            }
        }
        Rectangle {   // the compression level alert: more than 12 dB of reduction for half a second
            visible: page.mt !== undefined && !!page.mt.overComp
            Layout.fillWidth: true; implicitHeight: ocRow.implicitHeight + 16; radius: 6; color: Qt.rgba(1, 0.3, 0.3, 0.14); border.color: Theme.danger
            RowLayout { id: ocRow; anchors.fill: parent; anchors.margins: 8; spacing: 10
                Text { text: i18n.t("OVER-COMPRESSED", i18n.lang); color: Theme.danger; font.family: Theme.mono; font.pixelSize: 10; font.bold: true }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink; font.pixelSize: 10
                       text: i18n.t("This channel loses more than 12 dB to the compressor: raise the threshold or lower the ratio, or add DE-SQUASH to give the attacks back.", i18n.lang) }
                Rectangle { implicitWidth: dsT.implicitWidth + 20; implicitHeight: 24; radius: 4; color: Theme.ov(0.06); border.color: Theme.danger
                    Text { id: dsT; anchors.centerIn: parent; text: i18n.t("ADD DE-SQUASH", i18n.lang); color: Theme.danger; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: engine.addToRack(page.strip, "fx:desquash") } } }
        }
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
                        Text { text: i18n.t(modelData.t ?? "", i18n.lang) + "  " + Number(page.ch[modelData.k] === undefined ? modelData.from : page.ch[modelData.k]).toFixed(modelData.step < 1 ? 1 : 0) + modelData.u; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                        Slider { Layout.fillWidth: true; from: modelData.from; to: modelData.to; stepSize: modelData.step; value: page.ch[modelData.k] === undefined ? modelData.from : page.ch[modelData.k]; onMoved: page.apply(modelData.k, value) } }
                }
            }
            Flow { Layout.fillWidth: true; spacing: 4
                Repeater {
                    model: [ { n: "VOCAL", p: [-18, 3, 6, 5, 80, 3] }, { n: "DRUM BUS", p: [-12, 4, 2, 1, 120, 2] }, { n: "GENTLE GLUE", p: [-24, 2, 12, 20, 300, 2] }, { n: "LIMIT", p: [-6, 20, 0, 0.1, 60, 0] }, { n: "BASS", p: [-20, 5, 4, 10, 150, 4] } ]
                    delegate: Rectangle { implicitWidth: pl.implicitWidth + 14; implicitHeight: 20; radius: 4; color: Theme.ov(0.08); Text { id: pl; anchors.centerIn: parent; text: i18n.t(modelData.n ?? "", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setCompressor(page.strip, true, modelData.p[0], modelData.p[1], modelData.p[2], modelData.p[3], modelData.p[4], modelData.p[5]) } }
                }
            }
        }
    }
}
