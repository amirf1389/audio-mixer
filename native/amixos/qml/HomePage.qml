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
        // LIVE: the SOUND SCORE of the master now, its weakest part with the fix, the DSP MODE, and the analyzer, one click each
        Rectangle { objectName: "homeLive"; Layout.fillWidth: true; implicitHeight: liveRow.implicitHeight + 24; radius: Theme.radius; color: Theme.card; border.color: Theme.line
            property var sc: ({ valid: false, total: 0, grade: "", parts: [] })
            readonly property var weak: { let w = null; for (const p of (sc.parts || [])) if (p.max > 0 && p.points < p.max && (!w || p.points / p.max < w.points / w.max)) w = p; return w }
            Timer { interval: 500; running: page.visible; repeat: true; triggeredOnStart: true; onTriggered: parent.sc = engine.soundScore() }
            function col(v) { return v >= 85 ? Theme.emerald : v >= 70 ? Theme.cyan : v >= 50 ? Theme.amber : Theme.danger }
            RowLayout { id: liveRow; anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.margins: 14; spacing: 16
                Column { spacing: 0
                    Text { text: i18n.t("SOUND SCORE", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                    Row { spacing: 8
                        Text { objectName: "homeScore"; text: liveRow.parent.sc.valid ? Math.round(liveRow.parent.sc.total) : "—"; color: liveRow.parent.sc.valid ? liveRow.parent.col(liveRow.parent.sc.total) : Theme.dim; font.family: Theme.sans; font.pixelSize: 40; font.weight: Font.Black }
                        Text { anchors.baseline: parent.children[0].baseline; text: liveRow.parent.sc.valid ? i18n.t("GRADE", i18n.lang) + " " + liveRow.parent.sc.grade : i18n.t("play something", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 11; font.bold: true } } }
                ColumnLayout { Layout.fillWidth: true; spacing: 4
                    Repeater { model: liveRow.parent.sc.parts || []
                        delegate: RowLayout { Layout.fillWidth: true; spacing: 6
                            Text { text: i18n.t(modelData.name, i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; Layout.preferredWidth: 80; elide: Text.ElideRight }
                            Rectangle { Layout.fillWidth: true; height: 5; radius: 2; color: Theme.deep
                                Rectangle { height: parent.height; radius: 2; width: parent.width * (modelData.max > 0 ? modelData.points / modelData.max : 0); color: liveRow.parent.col(modelData.max > 0 ? 100 * modelData.points / modelData.max : 0) } } } }
                    Text { visible: !!liveRow.parent.weak && !!liveRow.parent.weak.tip; Layout.fillWidth: true; text: "→ " + i18n.t(liveRow.parent.weak ? liveRow.parent.weak.tip || "" : "", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; elide: Text.ElideRight } }
                ColumnLayout { spacing: 6
                    DspModeSwitch {}
                    RowLayout { spacing: 6
                        Rectangle { id: gb45; implicitWidth: fsT.implicitWidth + 16; implicitHeight: 24; radius: 7; color: Theme.ov(0.06); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb45.color) } GradientStop { position: 1; color: Theme.btnBottom(gb45.color) } } border.color: Theme.orange
                            Text { id: fsT; anchors.centerIn: parent; text: i18n.t("FULL SCORE", i18n.lang); color: Theme.orange; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                            MouseArea { objectName: "homeFullScore"; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: page.go("score") } }
                        Rectangle { id: gb46; implicitWidth: anT.implicitWidth + 16; implicitHeight: 24; radius: 7; color: Theme.ov(0.06); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb46.color) } GradientStop { position: 1; color: Theme.btnBottom(gb46.color) } } border.color: Theme.cyan
                            Text { id: anT; anchors.centerIn: parent; text: i18n.t("MASTER ANALYZER", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { if (typeof root !== "undefined" && root.openAnalyzer) root.openAnalyzer() } } } } } } }
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
                    { id: "score", t: "Sound score", s: "How good the master sounds, 0 to 100: six parts, each with its fix, history and the analyzer detail." },
                    { id: "master", t: "Mastering", s: "DIGITAL or ANALOG, enhancer, limiter and the master analyzer." },
                    { id: "sys", t: "Settings", s: "Engine, saved console state, about." }
                ]
                delegate: Rectangle {
                    id: tile
                    readonly property color hue: Theme.stripColors[index % Theme.stripColors.length]   // every tile its own colour
                    Layout.fillWidth: true; implicitHeight: 92; radius: Theme.radius; color: tileArea.pressed ? Theme.ov(0.08) : Theme.card; border.color: tileArea.containsMouse ? hue : Theme.line
                    Rectangle { anchors.fill: parent; radius: parent.radius
                        gradient: Gradient { orientation: Gradient.Horizontal; GradientStop { position: 0; color: Qt.rgba(tile.hue.r, tile.hue.g, tile.hue.b, tileArea.containsMouse ? 0.28 : 0.16) } GradientStop { position: 0.7; color: "transparent" } } }
                    Rectangle { width: 3; radius: 2; height: parent.height - 24; anchors.verticalCenter: parent.verticalCenter; x: 0; color: tile.hue }
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
                model: { const want = ["mixer", "assistant", "score", "amp", "store", "asio", "scenes", "fx", "master", "media", "plugins", "help"]; const all = engine.pages; const out = [];
                         for (const id of want) for (const p of all) if (p.id === id) out.push(p); return out }
                delegate: Rectangle {
                    implicitWidth: qRow.implicitWidth + 24; implicitHeight: 36; radius: Theme.radius
                    color: qa.pressed ? Theme.ov(0.12) : (qa.containsMouse ? Theme.ov(0.07) : Theme.card); border.color: qa.containsMouse ? Theme.orange : Theme.line
                    Row { id: qRow; anchors.centerIn: parent; spacing: 8
                        Icon { width: 15; height: 15; anchors.verticalCenter: parent.verticalCenter; name: modelData.id; color: Theme.harm(Theme.groupColor(modelData.group)); visible: has }
                        Text { visible: !parent.children[0].has; text: modelData.icon; color: Theme.orange; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: i18n.t(modelData.text ?? "", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; anchors.verticalCenter: parent.verticalCenter } }
                    MouseArea { id: qa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: page.go(modelData.id) }
                }
            }
        }
    }
}
