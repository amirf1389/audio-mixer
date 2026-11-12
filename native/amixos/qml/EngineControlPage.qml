// ENGINE CONTROL: the working state of the audio engine on one page, with the switch that belongs to each reading.
//   HEAT · FAN   how hard the processor works for the audio (the load of each block); the PC's own fan follows that load
//   AUDIO        what runs, start / stop, DSP mode        UPDATES   version, check, the update server link
//   UPGRADES     the edition and a product key            MODULATION / NOISE CONTROL / PITCH CONTROL   one press puts the tool on the chosen channel
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AmixOS

Flickable {
    id: page
    objectName: "engineControlPage"
    contentHeight: grid.implicitHeight + 24; clip: true; boundsBehavior: Flickable.StopAtBounds
    readonly property var tel: engine.telemetry
    readonly property real load: engine.running && tel.loadPct !== undefined ? tel.loadPct : 0
    readonly property int ch: engine.selectedStrip
    property string note: ""
    property var heat: []            // the last two minutes of load, one value a second
    Timer { interval: 1000; running: true; repeat: true; onTriggered: { const h = page.heat.slice(-119); h.push(page.load); page.heat = h } }
    function insert(kind, name) { const slots = engine.plugins(ch); for (let k = 0; k < slots.length; ++k) if (slots[k].kind === kind) { note = name + i18n.t(" is already on CH ", i18n.lang) + (ch + 1); return }
        for (let k = 0; k < slots.length; ++k) if (slots[k].kind === "") { engine.setPlugin(ch, k, kind); note = name + i18n.t(" is now on CH ", i18n.lang) + (ch + 1) + i18n.t(": open the PLUGIN RACK on the MIXER to set it", i18n.lang); return }
        note = i18n.t("The rack of this channel is full.", i18n.lang) }
    component Btn: Rectangle { id: bt; property string label; property bool on: false; property color tone: Theme.cyan; signal clicked(); implicitWidth: btT.implicitWidth + 22; implicitHeight: 26; radius: 5
        color: bt.on ? Qt.rgba(bt.tone.r, bt.tone.g, bt.tone.b, 0.2) : Theme.ov(0.06); border.color: bt.on ? bt.tone : Theme.line
        Text { id: btT; anchors.centerIn: parent; text: bt.label; color: bt.on ? bt.tone : Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: bt.clicked() } }
    component Card: Rectangle { id: cd; property string title; property color tone: Theme.cyan; default property alias body: cdCol.data
        Layout.fillWidth: true; Layout.preferredWidth: 100; Layout.alignment: Qt.AlignTop; implicitHeight: cdCol.implicitHeight + 28; radius: Theme.radius; color: Theme.card; border.color: Theme.line
        Rectangle { width: 3; height: parent.height - 16; y: 8; x: 0; radius: 2; color: cd.tone }
        ColumnLayout { id: cdCol; anchors.fill: parent; anchors.margins: 14; spacing: 8
            Text { text: cd.title; color: cd.tone; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 2 } } }
    component Small: Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
    component Big: Text { color: Theme.ink; font.family: Theme.mono; font.pixelSize: 22; font.bold: true }

    GridLayout { id: grid; x: 10; y: 10; width: parent.width - 20; columns: page.width > 1250 ? 3 : page.width > 820 ? 2 : 1; columnSpacing: 10; rowSpacing: 10
        Rectangle { Layout.columnSpan: grid.columns; Layout.fillWidth: true; implicitHeight: 40; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 10; spacing: 10
                Text { text: i18n.t("ENGINE CONTROL", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 2 }
                Text { Layout.fillWidth: true; text: page.note !== "" ? page.note : i18n.t("The tools below go on the channel selected on the MIXER.", i18n.lang); color: page.note !== "" ? Theme.amber : Theme.muted; font.family: Theme.mono; font.pixelSize: 9; elide: Text.ElideRight }
                Text { text: i18n.t("CHANNEL", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 }
                Btn { label: "‹"; onClicked: engine.selectedStrip = Math.max(0, page.ch - 1) }
                Text { objectName: "ecChannel"; text: "CH " + (page.ch + 1); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 12; font.bold: true; Layout.preferredWidth: 48; horizontalAlignment: Text.AlignHCenter }
                Btn { label: "›"; onClicked: engine.selectedStrip = Math.min(engine.licensedChannels - 1, page.ch + 1) } } }

        Card { title: i18n.t("HEAT · FAN", i18n.lang); tone: page.load > 70 ? Theme.danger : page.load > 40 ? Theme.amber : Theme.emerald
            RowLayout { spacing: 12
                Big { objectName: "ecLoad"; text: engine.running ? page.load.toFixed(1) + " %" : "—"; color: page.load > 70 ? Theme.danger : page.load > 40 ? Theme.amber : Theme.emerald }
                Text { text: page.load > 70 ? i18n.t("HOT", i18n.lang) : page.load > 40 ? i18n.t("WARM", i18n.lang) : i18n.t("COOL", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; font.letterSpacing: 2 } }
            Rectangle { Layout.fillWidth: true; height: 46; radius: 4; color: Theme.deep; border.color: Theme.lineSoft; clip: true
                Row { anchors.fill: parent; anchors.margins: 3; spacing: 1
                    Repeater { model: page.heat; delegate: Item { width: Math.max(1, (parent.width - 119) / 120); height: parent.height
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Math.max(1, parent.height * Math.min(100, modelData) / 100); color: modelData > 70 ? Theme.danger : modelData > 40 ? Theme.amber : Theme.emerald } } } } }
            Text { text: engine.running ? i18n.t("block ", i18n.lang) + (page.tel.blockUs || 0).toFixed(0) + " µs · " + i18n.t("worst ", i18n.lang) + (page.tel.maxBlockUs || 0).toFixed(0) + " µs · " + (page.tel.xruns || 0) + i18n.t(" dropouts", i18n.lang) : i18n.t("the engine is stopped", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 }
            RowLayout { spacing: 6
                Btn { objectName: "ecCool"; label: i18n.t("COOL DOWN: BIGGER BUFFER", i18n.lang); on: page.load > 40; tone: Theme.amber; onClicked: page.Window.window.openAudioSettings() }
                Btn { label: i18n.t("SOUND CARD", i18n.lang); onClicked: page.Window.window.current = "asio" } }
            Small { text: i18n.t("Heat here is the share of each block the processor needs for the audio; the last two minutes are drawn. The PC's fan follows that load: AmF X1 does not drive the fan itself. A bigger buffer or fewer plug-ins cools it down.", i18n.lang) } }

        Card { title: i18n.t("AUDIO", i18n.lang); tone: engine.running ? Theme.emerald : Theme.muted
            Big { text: engine.running ? engine.backend.toUpperCase() + " · " + engine.deviceInputs + " / " + engine.deviceOutputs : i18n.t("STOPPED", i18n.lang) }
            Text { text: engine.running ? (engine.sampleRate / 1000) + " kHz · " + engine.blockFrames + i18n.t(" frames", i18n.lang) + " · " + (engine.blockFrames * 1000 / engine.sampleRate).toFixed(1) + " ms" : i18n.t("no device is running", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 }
            RowLayout { spacing: 6
                Btn { label: engine.running ? i18n.t("STOP", i18n.lang) : i18n.t("START", i18n.lang); on: true; tone: engine.running ? Theme.danger : Theme.emerald; onClicked: engine.running ? engine.stop() : settings.startLastDevice() }
                Btn { label: i18n.t("AUDIO / MIDI SETTINGS", i18n.lang); onClicked: page.Window.window.openAudioSettings() } }
            RowLayout { spacing: 6
                Text { text: i18n.t("DSP MODE", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 }
                Btn { label: i18n.t("DIGITAL", i18n.lang); on: engine.dspMode === "digital"; onClicked: engine.dspMode = "digital" }
                Btn { label: i18n.t("ANALOG", i18n.lang); on: engine.dspMode === "analog"; tone: Theme.amber; onClicked: engine.dspMode = "analog" } } }

        Card { title: i18n.t("UPDATES", i18n.lang); tone: updater.state === "available" || updater.state === "downloaded" ? Theme.amber : Theme.cyan
            Big { text: updater.current }
            Text { text: updater.state.toUpperCase() + (updater.latest ? " · " + i18n.t("newest ", i18n.lang) + updater.latest : ""); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 }
            RowLayout { spacing: 6
                Btn { objectName: "ecCheck"; label: i18n.t("CHECK NOW", i18n.lang); on: true; onClicked: updater.check() }
                Btn { label: i18n.t("ADMIN", i18n.lang); onClicked: page.Window.window.current = "admin" } }
            Text { text: i18n.t("UPDATE SERVER LINK", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 }
            RowLayout { spacing: 6
                Field { id: linkIn; objectName: "ecLink"; Layout.fillWidth: true; implicitHeight: 26; text: updater.url; font.family: Theme.mono; font.pixelSize: 8; selectByMouse: true }
                Btn { objectName: "ecLinkSave"; label: i18n.t("USE THIS LINK", i18n.lang); on: true; tone: Theme.amber; onClicked: page.note = updater.setServerLink(linkIn.text) ? i18n.t("Update link saved.", i18n.lang) : i18n.t("That link was not taken: it must be an https address.", i18n.lang) } }
            Small { text: i18n.t("The link is open: any https server may hold the update list. What it offers is installed only if the list carries the vendor's signature and the file matches its checksum.", i18n.lang) } }

        Card { title: i18n.t("UPGRADES", i18n.lang); tone: Theme.violet
            Big { text: (engine.license.planName || "BASIC") }
            Text { text: engine.licensedChannels + i18n.t(" channels", i18n.lang) + (engine.license.expires ? " · " + i18n.t("until ", i18n.lang) + engine.license.expires : ""); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 }
            RowLayout { spacing: 6
                Btn { label: i18n.t("ENTER A PRODUCT KEY", i18n.lang); on: true; tone: Theme.violet; onClicked: page.Window.window.current = "renew" } }
            Small { text: i18n.t("BASIC runs channels 1 to 8, PRO 1 to 16, STUDIO and MASTER all 32. A product key (five groups of five characters) or a full licence key is entered on LICENCE.", i18n.lang) } }

        Card { title: i18n.t("MODULATION", i18n.lang); tone: Theme.pink
            Flow { Layout.fillWidth: true; spacing: 6
                Repeater { model: [["fx:chorus", "CHORUS"], ["fx:flanger", "FLANGER"], ["fx:phaser", "PHASER"], ["fx:tremolo", "TREMOLO"], ["fx:wah", "AUTO WAH"]]
                    delegate: Btn { objectName: "ecMod_" + index; label: i18n.t(modelData[1], i18n.lang); tone: Theme.pink; onClicked: page.insert(modelData[0], modelData[1]) } } }
            Small { text: i18n.t("One press puts the effect on the selected channel's rack.", i18n.lang) } }

        Card { title: i18n.t("NOISE CONTROL", i18n.lang); tone: Theme.emerald
            Flow { Layout.fillWidth: true; spacing: 6
                Btn { objectName: "ecNoise"; label: i18n.t("CLOSE GROUND NOISE", i18n.lang); on: true; tone: Theme.emerald; onClicked: { const db = engine.closeNoise(page.ch); page.note = i18n.t("Gate of CH ", i18n.lang) + (page.ch + 1) + i18n.t(" set just above its noise: ", i18n.lang) + db.toFixed(1) + " dB" } }
                Btn { label: i18n.t("FEEDBACK KILLER", i18n.lang); tone: Theme.emerald; onClicked: page.insert("fx:antifeedback", "FEEDBACK KILLER") }
                Btn { label: i18n.t("NOISE GATE PAGE", i18n.lang); onClicked: page.Window.window.current = "gate" } }
            Small { text: i18n.t("CLOSE GROUND NOISE listens to the channel with nobody playing and sets its gate 6 dB above the hum and hiss it hears.", i18n.lang) } }

        Card { title: i18n.t("LOUDNESS · SOUND BARRIER", i18n.lang); tone: Theme.orange
            Flow { Layout.fillWidth: true; spacing: 6
                Btn { objectName: "ecBarrier"; label: i18n.t("SOUND BARRIER", i18n.lang); on: true; tone: Theme.orange; onClicked: page.insert("fx:barrier", "SOUND BARRIER") } }
            Small { text: i18n.t("Loud, with impact, and never past the ceiling: PUSH raises the level, IMPACT keeps the punch of each hit, SMOOTH holds the treble back so it does not turn harsh, CEILING is the level nothing passes. AUTO is the run-up: the push follows the music by itself until the barrier is working. It delays the channel by 3 ms.", i18n.lang) } }

        Card { title: i18n.t("LISTENING EQ", i18n.lang); tone: Theme.cyan
            Flow { Layout.fillWidth: true; spacing: 6
                Btn { objectName: "ecDualEq"; label: i18n.t("DUAL LISTEN EQ", i18n.lang); on: true; tone: Theme.cyan; onClicked: page.insert("fx:dualeq", "DUAL LISTEN EQ") } }
            Small { text: i18n.t("Two ranges, bass and treble, that listen to the music on the channel and set themselves: a bass-heavy song is eased, a thin one filled, a dull one opened, a harsh one calmed. Put it on the channel the music plays through.", i18n.lang) } }

        Card { title: i18n.t("PITCH CONTROL", i18n.lang); tone: Theme.amber
            Flow { Layout.fillWidth: true; spacing: 6
                Btn { objectName: "ecPitch"; label: i18n.t("PITCH SHIFTER", i18n.lang); on: true; tone: Theme.amber; onClicked: page.insert("fx:pitch", "PITCH SHIFTER") } }
            Small { text: i18n.t("Moves the channel up or down by up to 12 semitones, with a fine control of 50 cents either way, without changing its speed. Set it in the plug-in's own panel.", i18n.lang) } }
    }
}
