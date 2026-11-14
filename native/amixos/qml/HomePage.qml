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
                    Layout.fillWidth: true; implicitHeight: 92; radius: Theme.radius; color: Theme.card; border.color: tileArea.containsMouse ? Theme.orange : Theme.line
                    ColumnLayout { anchors.fill: parent; anchors.margins: 14; spacing: 4
                        Text { text: i18n.t(modelData.t ?? "", i18n.lang); color: Theme.ink; font.family: Theme.sans; font.pixelSize: 15; font.bold: true }
                        Text { text: i18n.t(modelData.s ?? "", i18n.lang); color: Theme.muted; font.family: Theme.sans; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true } }
                    MouseArea { id: tileArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: page.go(modelData.id) }
                }
            }
        }
        Panel {
            Layout.fillWidth: true
            title: i18n.t("WHAT THIS BUILD CARRIES OVER", i18n.lang)
            subtitle: i18n.t("The web console (AmF X1 1.22.1.0 Beta) is retired; its pages return here one by one as native pages. Dimmed pages on the rail are not ported yet.", i18n.lang)
        }
    }
}
