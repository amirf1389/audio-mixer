// A page of the web console that is not native yet: says so plainly, and what it will carry
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Layouts

Item {
    property var page: ({ text: "", group: "" })
    readonly property var notes: ({
        arrange: "Timeline with clips, tracks, automation, markers, offline WAV export (no recording).",
        dyn: "Per-channel compressor with the curve display.", gate: "Noise gate with threshold, attack, hold, release.",
        fx: "The 300-preset FX library, VST3 / VST2 / DLL inserts (Windows).", bus: "Aux sends pre / post fader, 16 buses.",
        dca: "Group faders (DCA) and mute groups.", stereopan: "Surround master pages (stereo / quad / 7.1.2), 360 binaural, crossfeed.",
        routing: "Input / output routing matrix and the virtual patchbay.", osc: "Oscilloscope and spectrum (RTA).", phase: "Goniometer and correlation history.",
        media: "Media player with the now-playing readout.", live: "Live sources and network audio inputs.", miceq: "Microphone EQ presets.",
        ai: "AI MIX: notches, auto-fader, AI driver lock.", tools: "Calculators, colour picker, scene slots.", plugins: "Plugin host pages.",
        asio: "Sound-card diagnostics (DIAGNOSTICS log, simulated devices, driver lock, Realtek pin).", bluetooth: "Bluetooth devices as inputs and outputs.",
        network: "Remote control, telemetry and streaming over TCP / UDP.", cloud: "Cloud presets and backups.", cascade: "Several consoles in cascade.",
        presets: "Preset library.", acct: "Account.", access: "Access and roles.", renew: "Licence, editions (Lite / Pro), hardware-bound keys.",
        admin: "Administrator page.", calib: "System calibration.", help: "Help and the local support assistant.", scenes: "Scene memory.", boot: "Boot / reset, factory reset."
    })
    ColumnLayout {
        anchors.centerIn: parent; spacing: 10; width: Math.min(parent.width - 40, 560)
        Text { Layout.alignment: Qt.AlignHCenter; text: page.text; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 20; font.bold: true; font.letterSpacing: 2 }
        Text { Layout.alignment: Qt.AlignHCenter; text: "NOT NATIVE YET"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.letterSpacing: 3 }
        Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: notes[page.id] || "Coming to A mix OS."; color: Theme.muted; font.family: Theme.sans; font.pixelSize: 13; wrapMode: Text.WordWrap }
        Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: "This page existed in the web console (AmF X1 1.22.1.0 Beta, git tag html-app-final) and is being rebuilt as a native page."; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; wrapMode: Text.WordWrap }
    }
}
