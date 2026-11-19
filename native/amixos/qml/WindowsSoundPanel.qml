// WINDOWS SOUND: every recording device of Windows (the Recording tab of its Sound properties) on its own strip beside the sound card,
// what Windows says about each device (format, level, mute, enhancements), and buttons that open Windows' own sound panels.
import QtQuick
import AmixOS   // this module: brings the Theme singleton
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: panel
    objectName: "windowsSoundPanel"
    property var devs: []
    property var props: ({})        // id -> what Windows reported
    readonly property int strip: engine.selectedStrip
    function refresh() { devs = apps.recordingDevices() }
    implicitHeight: col.implicitHeight + 24; radius: Theme.radius; color: Theme.card; border.color: Theme.line
    Component.onCompleted: refresh()
    Connections { target: engine; function onDevicesChanged() { panel.refresh() } }
    Connections { target: apps; function onChanged() { panel.refresh() }
        function onPropertiesReady(id, p) { const m = Object.assign({}, panel.props); m[id] = p; panel.props = m } }
    component Key: Rectangle { id: key; property string label; property color hue: Theme.cyan; signal hit()
        implicitWidth: kt.implicitWidth + 16; implicitHeight: 20; radius: 7; color: Theme.ov(0.06); border.color: hue
        gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(key.color) } GradientStop { position: 1; color: Theme.btnBottom(key.color) } }
        Text { id: kt; anchors.centerIn: parent; text: key.label; color: key.hue; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: key.hit() } }
    ColumnLayout { id: col; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12; spacing: 6
        RowLayout { Layout.fillWidth: true; spacing: 6
            Text { text: i18n.t("WINDOWS SOUND  ·  RECORDING DEVICES ON THEIR OWN STRIPS", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
            Item { Layout.fillWidth: true }
            Key { objectName: "winSoundSettings"; label: i18n.t("SOUND SETTINGS…", i18n.lang); hue: Theme.ink; onHit: apps.openWindowsSound("settings") }
            Key { objectName: "winSoundRecording"; label: i18n.t("RECORDING DEVICES…", i18n.lang); hue: Theme.ink; onHit: apps.openWindowsSound("recording") }
            Key { objectName: "winSoundPlayback"; label: i18n.t("PLAYBACK DEVICES…", i18n.lang); hue: Theme.ink; onHit: apps.openWindowsSound("playback") }
            Key { objectName: "winSoundMixer"; label: i18n.t("APP VOLUMES…", i18n.lang); hue: Theme.ink; onHit: apps.openWindowsSound("mixer") } }
        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7
            text: i18n.t("Any device of Windows' Recording tab can join the mix beside the sound card: a second microphone, a webcam's microphone, line in, Stereo Mix. MONO puts it on the selected strip; STEREO on that strip and the next. Windows converts it to the console's sample rate (shared mode, a few tens of milliseconds later than the sound card's own inputs). PROPERTIES shows what Windows reports for the device; the buttons above open Windows' own panels to change it.", i18n.lang) }
        Text { visible: !apps.devicesAvailable; text: i18n.t("Windows' recording devices are read on Windows.", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8 }
        Text { visible: apps.devicesAvailable && panel.devs.length === 0; text: i18n.t("Windows lists no recording device (none plugged in, or all disabled in RECORDING DEVICES).", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
        Repeater { model: panel.devs
            delegate: ColumnLayout { Layout.fillWidth: true; spacing: 2
                readonly property var p: panel.props[modelData.id]
                RowLayout { Layout.fillWidth: true; spacing: 6
                    Rectangle { width: 6; height: 6; radius: 3; color: modelData.slot >= 0 ? Theme.emerald : Theme.dim }
                    Text { Layout.fillWidth: true; text: modelData.name + (modelData.isDefault ? i18n.t("  · Windows default", i18n.lang) : ""); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; elide: Text.ElideRight }
                    Text { text: modelData.channels + " ch · " + (modelData.rate / 1000).toFixed(1) + " kHz"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                    Key { objectName: "winDevProps" + index; label: i18n.t("PROPERTIES", i18n.lang); hue: Theme.muted; onHit: apps.readProperties(modelData.id) }
                    Key { objectName: "winDevMono" + index; label: i18n.t("MONO → CH ", i18n.lang) + (panel.strip + 1); hue: Theme.emerald; onHit: { apps.captureDevice(modelData.id, modelData.name, panel.strip, false); panel.refresh() } }
                    Key { objectName: "winDevStereo" + index; label: i18n.t("STEREO → CH ", i18n.lang) + (panel.strip + 1) + "-" + (panel.strip + 2); hue: Theme.cyan; onHit: { apps.captureDevice(modelData.id, modelData.name, panel.strip, true); panel.refresh() } } }
                Text { objectName: "winDevPropsText" + index; visible: p !== undefined; Layout.fillWidth: true; Layout.leftMargin: 12; wrapMode: Text.WordWrap; color: p && p.ok ? Theme.cyan : Theme.amber; font.family: Theme.mono; font.pixelSize: 8
                    text: !p ? "" : !p.ok ? p.error : i18n.t("Windows: ", i18n.lang) + p.channels + " ch · " + (p.rate / 1000).toFixed(1) + " kHz · " + p.bits + "-bit" + (p.levelPct >= 0 ? i18n.t(" · level ", i18n.lang) + p.levelPct + " %" : "") + (p.muted ? i18n.t(" · MUTED in Windows", i18n.lang) : "") + i18n.t(" · enhancements ", i18n.lang) + i18n.t(p.enhancements, i18n.lang) } } }
        // what is captured (devices and programs share the 8 slots)
        Repeater { model: apps.captures.filter(c => c.device)
            delegate: RowLayout { Layout.fillWidth: true; spacing: 8
                Rectangle { implicitWidth: 54; implicitHeight: 16; radius: 3; color: modelData.state === "live" ? Theme.emerald : modelData.state === "failed" ? Theme.danger : Theme.amber
                    Text { anchors.centerIn: parent; text: i18n.t(String(modelData.state).toUpperCase(), i18n.lang); color: "#000"; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } }
                Text { text: modelData.name; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; elide: Text.ElideRight; Layout.preferredWidth: 220 }
                Text { text: modelData.strips.length ? "CH " + modelData.strips.map(s => s + 1).join(", ") : "—"; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                Rectangle { Layout.preferredWidth: 90; implicitHeight: 6; radius: 3; color: Theme.ov(0.08)
                    Rectangle { height: parent.height; radius: 3; color: Theme.emerald; width: parent.width * Math.max(0, Math.min(1, (modelData.peakDb + 60) / 60)) } }
                Text { Layout.fillWidth: true; text: modelData.message; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; elide: Text.ElideRight }
                Key { label: i18n.t("RELEASE", i18n.lang); hue: Theme.muted; onHit: apps.release(modelData.slot) } } }
        Text { visible: apps.note !== ""; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: apps.note; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8 }
    }
}
