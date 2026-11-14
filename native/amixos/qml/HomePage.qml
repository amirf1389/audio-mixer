import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Layouts

Flickable {
    id: page
    signal go(string id)
    contentHeight: col.implicitHeight + 40
    clip: true
    ColumnLayout {
        id: col
        width: Math.min(parent.width - 40, 1000)
        anchors.horizontalCenter: parent.horizontalCenter
        y: 24
        spacing: 16
        Text { text: Theme.product; color: Theme.ink; font.family: Theme.sans; font.pixelSize: 30; font.weight: Font.Black }
        Text { text: i18n.t("Native console, version ", i18n.lang) + appVersion + " • " + engine.edition + i18n.t(" tier • engine ", i18n.lang) + (engine.running ? engine.backend : i18n.t("idle", i18n.lang)); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 10 }
        Text { text: i18n.t(engine.status, i18n.lang); color: engine.running ? Theme.emerald : Theme.amber; font.family: Theme.mono; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        GridLayout {
            columns: page.width > 900 ? 3 : (page.width > 560 ? 2 : 1)
            columnSpacing: 12; rowSpacing: 12
            Layout.fillWidth: true
            Repeater {
                model: [
                    { id: "io", t: "Connect a sound card", s: "ASIO, WASAPI, Core Audio, ALSA or the test device. Choose inputs, outputs, rate and buffer." },
                    { id: "mixer", t: "Mix", s: "32 strips: fader, pan, mute, solo, meters, master with limiter." },
                    { id: "dsp", t: "Tone", s: "10-band parametric EQ, crossover, delay and polarity per channel, with the live curve." },
                    { id: "meter", t: "Levels", s: "Peak, RMS, hold and clip on every strip and the master." },
                    { id: "lufs", t: "Loudness", s: "EBU R128: momentary, short-term, integrated, range, true peak." },
                    { id: "sys", t: "Settings", s: "Engine, saved console state, about." }
                ]
                delegate: Rectangle {
                    Layout.fillWidth: true; implicitHeight: 92; radius: Theme.radius; color: tileArea.pressed ? Theme.ov(0.08) : Theme.card; border.color: tileArea.containsMouse ? Theme.orange : Theme.line
                    // top-aligned: every card's title sits on the same line, whatever the length of its description
                    ColumnLayout { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14; spacing: 4
                        Text { text: i18n.t(modelData.t ?? "", i18n.lang); color: Theme.ink; font.family: Theme.sans; font.pixelSize: 15; font.bold: true }
                        // the channel count of this licence, not a fixed 32
                        Text { text: { const s = i18n.t(modelData.s ?? "", i18n.lang); return modelData.id === "mixer" ? s.replace("32", engine.licensedChannels) : s }
                               color: Theme.muted; font.family: Theme.sans; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true } }
                    MouseArea { id: tileArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: page.go(modelData.id) }
                }
            }
        }
        // START: one click to the pages used most (the rail's own names, already in every language)
        Text { text: i18n.t("START", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2; Layout.topMargin: 6 }
        Flow {
            Layout.fillWidth: true; spacing: 8
            Repeater {
                model: { const want = ["mixer", "asio", "scenes", "fx", "master", "media", "plugins", "help"]; const all = engine.pages; const out = [];
                         for (const id of want) for (const p of all) if (p.id === id) out.push(p); return out }
                delegate: Rectangle {
                    implicitWidth: qRow.implicitWidth + 24; implicitHeight: 36; radius: Theme.radius
                    color: qa.pressed ? Theme.ov(0.12) : (qa.containsMouse ? Theme.ov(0.07) : Theme.card); border.color: qa.containsMouse ? Theme.orange : Theme.line
                    Row { id: qRow; anchors.centerIn: parent; spacing: 8
                        Text { text: modelData.icon; color: Theme.orange; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: i18n.t(modelData.text ?? "", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; anchors.verticalCenter: parent.verticalCenter } }
                    MouseArea { id: qa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: page.go(modelData.id) }
                }
            }
        }
    }
}
