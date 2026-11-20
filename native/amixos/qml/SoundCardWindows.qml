// SOUND CARD · WINDOWS SOUND and ONE INTERFACE FOR INPUT AND OUTPUT.
// WINDOWS SOUND: Windows' default playback / recording devices and the communications ones, each with the format Windows runs it at in
// shared mode, and a hint when that rate is not the console's. Windows' defaults are changed in Windows' own Sound panel (a button opens it).
// ONE INTERFACE: a box with both an input and an output (one Windows container, or one ALSA card) opens for both in one click.
import QtQuick
import AmixOS   // this module: brings the Theme singleton
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: box
    objectName: "soundCardWindows"
    spacing: 8
    property var win: ({})
    property var pairs: []
    readonly property int consoleRate: engine.running ? engine.sampleRate : 0
    function refresh() { win = apps.windowsDefaults(); pairs = apps.interfacePairs() }
    function fmt(e) { return !e || !e.rate ? "—" : (e.rate / 1000).toFixed(1) + " kHz · " + e.bits + (e.isFloat ? i18n.t("-bit float", i18n.lang) : "-bit") + " · " + e.channels + " ch" }
    function usePair(p) {
        const rate = engine.running ? engine.sampleRate : settings.defaultRate, frames = engine.blockFrames || settings.defaultFrames
        const inCh = Math.min(32, p.inChannels), outCh = Math.min(32, Math.max(2, p.outChannels))
        if (engine.openSmart(p.backend, p.in, p.out, rate, frames, inCh, outCh)) settings.rememberDevice(p.backend, p.in, p.out, rate, frames, inCh, outCh)
    }
    Component.onCompleted: refresh()
    Connections { target: engine; function onDevicesChanged() { box.refresh() } function onStateChanged() { box.refresh() } }
    component Key: Rectangle { id: key; property string label; property color hue: Theme.cyan; signal hit()
        implicitWidth: kt.implicitWidth + 16; implicitHeight: 22; radius: 7; color: Theme.ov(0.06); border.color: hue
        Text { id: kt; anchors.centerIn: parent; text: key.label; color: key.hue; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: key.hit() } }
    component Row4: ColumnLayout { property string title; property var e; Layout.fillWidth: true; spacing: 1
        RowLayout { Layout.fillWidth: true; spacing: 8
            Text { Layout.preferredWidth: 150; text: title; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
            Text { Layout.fillWidth: true; text: e && e.name ? e.name : i18n.t("none", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; elide: Text.ElideRight }
            Text { text: box.fmt(e); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8 } }
        Text { Layout.fillWidth: true; Layout.leftMargin: 158; wrapMode: Text.WordWrap; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 7
            visible: box.consoleRate > 0 && e && e.rate > 0 && e.rate !== box.consoleRate
            text: i18n.t("Windows runs this device at a different rate than the console: programs that play through Windows are converted. Set the same rate in Windows (Sound, the device's Properties, Advanced) for no conversion.", i18n.lang) + "  (" + (box.consoleRate / 1000).toFixed(1) + " kHz)" } }

    // ---- WINDOWS SOUND
    RowLayout { Layout.fillWidth: true; spacing: 6
        Text { text: i18n.t("WINDOWS SOUND", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
        Item { Layout.fillWidth: true }
        Key { objectName: "winDefRefresh"; label: i18n.t("REFRESH", i18n.lang); hue: Theme.muted; onHit: box.refresh() }
        Key { objectName: "winDefPlayback"; label: i18n.t("SET DEFAULT PLAYBACK…", i18n.lang); hue: Theme.ink; onHit: apps.openWindowsSound("playback") }
        Key { objectName: "winDefRecording"; label: i18n.t("SET DEFAULT RECORDING…", i18n.lang); hue: Theme.ink; onHit: apps.openWindowsSound("recording") }
        Key { objectName: "winDefSettings"; label: i18n.t("SOUND SETTINGS…", i18n.lang); hue: Theme.ink; onHit: apps.openWindowsSound("settings") } }
    Text { visible: !box.win.available; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: i18n.t("Windows only: Windows' default devices and formats are read on Windows.", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8 }
    Row4 { visible: !!box.win.available; title: i18n.t("DEFAULT PLAYBACK", i18n.lang); e: box.win.playback }
    Row4 { visible: !!box.win.available; title: i18n.t("DEFAULT RECORDING", i18n.lang); e: box.win.recording }
    Row4 { visible: !!box.win.available; title: i18n.t("COMMUNICATIONS PLAYBACK", i18n.lang); e: box.win.commPlayback }
    Row4 { visible: !!box.win.available; title: i18n.t("COMMUNICATIONS RECORDING", i18n.lang); e: box.win.commRecording }
    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7
        text: i18n.t("To make the console's device the Windows default: SET DEFAULT PLAYBACK / RECORDING opens Windows' Sound panel; select the device there and press Set Default (Default Communication Device for calls). The format shown is the one Windows uses in shared mode (Default Format in the device's Advanced tab).", i18n.lang) }

    // ---- ONE INTERFACE FOR INPUT AND OUTPUT
    Text { Layout.topMargin: 6; text: i18n.t("ONE INTERFACE FOR INPUT AND OUTPUT", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
    Text { visible: box.pairs.length === 0; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: i18n.t("No interface with both an input and an output is plugged in.", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
    Repeater { model: box.pairs
        delegate: RowLayout { Layout.fillWidth: true; spacing: 8
            readonly property bool current: engine.running && settings.lastDevice && settings.lastDevice.in === modelData.in && settings.lastDevice.out === modelData.out
            Rectangle { width: 6; height: 6; radius: 3; color: parent.current ? Theme.emerald : Theme.dim }
            Text { Layout.fillWidth: true; text: modelData.name + (modelData.usb ? "  · USB" : ""); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; elide: Text.ElideRight }
            Text { text: modelData.backend.toUpperCase() + " · " + modelData.inChannels + " IN / " + modelData.outChannels + " OUT"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
            Key { objectName: "usePair" + index; label: parent.current ? i18n.t("IN USE", i18n.lang) : i18n.t("USE FOR INPUT AND OUTPUT", i18n.lang); hue: parent.current ? Theme.emerald : Theme.cyan; onHit: if (!parent.current) box.usePair(modelData) } } }
    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7
        text: i18n.t("An interface shows up in Windows as two devices, one for recording and one for playback. One click opens both together at the console's rate: its inputs on the strips, the mix on its outputs.", i18n.lang) }
}
