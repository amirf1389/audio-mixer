// HELP: step by step in plain words, each step with a button to the page it talks about; the keyboard shortcuts of the MIXER page; where
// the files live; and what to do when there is no sound.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "helpPage"
    signal go(string id)
    readonly property var steps: [
        { n: "1", t: "Connect your interface", d: "Plug in the sound card or USB microphone. Open CONNECTIONS: SMART START picks the best driver (ASIO first on Windows) and opens it. LIVE SOURCES lists every interface with a READ button.", p: "io", l: "CONNECTIONS" },
        { n: "2", t: "Hear the sound", d: "On the MIXER, bring the master fader up slowly, then the channel fader of the input you plugged into. MONITOR (right side) has DIM, MONO and the headphone level.", p: "mixer", l: "MIXER" },
        { n: "3", t: "Shape each voice", d: "TONE (EQ) is the parametric equaliser, MIC EQ the same bands as 10 faders with mic presets; COMPRESSOR and NOISE GATE are one click away on the channel panel.", p: "miceq", l: "MIC EQ" },
        { n: "4", t: "Add space", d: "EFFECTS puts a reverb, tape delay, chorus or drive on an aux bus; SEND TO sets how much of each channel goes there.", p: "fx", l: "EFFECTS" },
        { n: "5", t: "Set the balance", d: "Move each fader so no voice is louder than the others. Keep the peaks below the red mark on LEVELS; LOUDNESS shows the programme level in LUFS.", p: "meter", l: "LEVELS" },
        { n: "6", t: "Save your mix", d: "SCENES stores every fader, mute, EQ and send under a name; one tap brings it back. PRESETS store one channel's processing.", p: "scenes", l: "SCENES" },
        { n: "7", t: "Record it", d: "Press ARM on the channels you want, then ⏺ on the MIXER action bar: one WAV per armed channel plus the master, into the record folder (SETTINGS).", p: "mixer", l: "RECORD" },
        { n: "8", t: "Plug-ins", d: "The PLUGIN RACK on the MIXER holds 10 slots per channel: built-in stages or VST3 / VST2 plug-ins from the folders on SETTINGS.", p: "sys", l: "SETTINGS" },
        { n: "9", t: "Arrange and play", d: "ARRANGE is a timeline of WAV clips that plays into the master and exports a mixed WAV.", p: "arrange", l: "ARRANGE" },
        { n: "10", t: "Check and fix", d: "CALIBRATE measures latency and noise; DIAGNOSTICS runs a self test, has SAFE MODE and FACTORY RESET.", p: "boot", l: "DIAGNOSTICS" }]
    readonly property var keys: [["← →", "select the previous / next channel"], ["M", "mute the selected channel"], ["S", "solo the selected channel"], ["0", "selected fader to 0 dB"], ["A", "A / B compare"], ["R", "show / hide the plug-in rack"], ["Ctrl+Z / Ctrl+Y", "undo / redo"], ["Esc", "clear every solo"]]
    readonly property var noSound: [
        "Is the engine running? The top bar reads ENGINE ON when a device is open; otherwise press START on CONNECTIONS.",
        "Does the channel read the right input? ROUTING > INPUTS → STRIPS: one dot per channel; AUTO-PATCH on LIVE SOURCES sets input n to CH n.",
        "Do the outputs carry something? ROUTING > SOURCES → OUTPUTS: MONITOR on OUT 1-2 by default; set MASTER L / R on the outputs that feed the PA.",
        "Is anything muted or soloed? Esc clears solos; SAFE MODE on DIAGNOSTICS pulls every fader down on purpose.",
        "Is a plug-in pending? A red slot in the rack means the plug-in file was not found; remove it or add its folder on SETTINGS.",
        "Still silent: DIAGNOSTICS > RUN AUDIT lists what is missing, and LEVELS shows where the signal stops."]
    component Head: Text { color: Theme.ink; font.family: Theme.mono; font.pixelSize: 12; font.bold: true; font.letterSpacing: 1.5 }
    Flickable {
        anchors.fill: parent; anchors.margins: 10; contentHeight: col.implicitHeight; clip: true; boundsBehavior: Flickable.StopAtBounds
        ColumnLayout {
            id: col; width: parent.width; spacing: 8
            Rectangle { Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
                RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10
                    Text { text: i18n.t("HELP:", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                    Text { text: i18n.t("step by step, in plain words", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                    Item { Layout.fillWidth: true }
                    Text { text: i18n.t("A mix OS ", i18n.lang) + appVersion + " · " + engine.edition; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 } } }
            GridLayout { Layout.fillWidth: true; columns: 2; columnSpacing: 8; rowSpacing: 8
                Repeater { model: page.steps; delegate: Rectangle { Layout.fillWidth: true; implicitHeight: 96; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                    RowLayout { anchors.fill: parent; anchors.margins: 12; spacing: 12
                        Rectangle { width: 34; height: 34; radius: 17; color: Theme.orange; Text { anchors.centerIn: parent; text: i18n.t(modelData.n ?? "", i18n.lang); color: "#000"; font.family: Theme.mono; font.pixelSize: 13; font.bold: true } }
                        ColumnLayout { Layout.fillWidth: true; spacing: 3
                            Text { text: i18n.t(modelData.t ?? "", i18n.lang); color: Theme.ink; font.family: Theme.sans; font.pixelSize: 13; font.bold: true }
                            Text { Layout.fillWidth: true; text: i18n.t(modelData.d ?? "", i18n.lang); color: Theme.muted; font.family: Theme.sans; font.pixelSize: 10; wrapMode: Text.WordWrap } }
                        Rectangle { id: gb43; implicitWidth: 92; implicitHeight: 24; radius: 7; color: Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb43.color) } GradientStop { position: 1; color: Theme.btnBottom(gb43.color) } } border.color: Theme.lineSoft; Layout.alignment: Qt.AlignVCenter
                            Text { anchors.centerIn: parent; text: modelData.l + " →"; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.go(modelData.p) } } } } } }
            RowLayout { Layout.fillWidth: true; spacing: 8
                Rectangle { Layout.fillWidth: true; Layout.preferredWidth: 1; implicitHeight: kc.implicitHeight + 24; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                    ColumnLayout { id: kc; anchors.fill: parent; anchors.margins: 12; spacing: 4
                        Head { text: i18n.t("KEYBOARD (MIXER PAGE)", i18n.lang) }
                        Repeater { model: page.keys; delegate: RowLayout { spacing: 10; Rectangle { id: gb44; implicitWidth: 110; implicitHeight: 20; radius: 7; color: Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb44.color) } GradientStop { position: 1; color: Theme.btnBottom(gb44.color) } } Text { anchors.centerIn: parent; text: modelData[0]; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } } Text { text: modelData[1]; color: Theme.muted; font.family: Theme.sans; font.pixelSize: 10 } } } } }
                Rectangle { Layout.fillWidth: true; Layout.preferredWidth: 1; implicitHeight: nc.implicitHeight + 24; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                    ColumnLayout { id: nc; anchors.fill: parent; anchors.margins: 12; spacing: 4
                        Head { text: i18n.t("NO SOUND?", i18n.lang) }
                        Repeater { model: page.noSound; delegate: RowLayout { spacing: 8; Layout.fillWidth: true; Rectangle { width: 6; height: 6; radius: 3; color: Theme.cyan; Layout.alignment: Qt.AlignTop; Layout.topMargin: 5 } Text { Layout.fillWidth: true; text: modelData; color: Theme.muted; font.family: Theme.sans; font.pixelSize: 10; wrapMode: Text.WordWrap } } } } } }
            Rectangle { Layout.fillWidth: true; implicitHeight: fc.implicitHeight + 24; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout { id: fc; anchors.fill: parent; anchors.margins: 12; spacing: 4
                    Head { text: i18n.t("WHERE THINGS LIVE", i18n.lang) }
                    Text { Layout.fillWidth: true; text: i18n.t("Console, scenes, presets, arrangement, autosave and exported CSVs: ", i18n.lang) + settings.dataFolder + i18n.t(".  Recordings: ", i18n.lang) + engine.recordFolder + i18n.t(".  Settings file: ", i18n.lang) + settings.settingsFile() + i18n.t(".  A backup of everything: CALIBRATE > SESSION > BACKUP CONSOLE.", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; wrapMode: Text.WordWrap }
                    MoreText { Layout.fillWidth: true; text: i18n.t("Licence and editions: LICENSE. Languages: the box at the top right. The console never sends anything over the network on its own; the remote app and the daemon are separate programs you start yourself.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; wrapMode: Text.WordWrap } } }
        }
    }
}
