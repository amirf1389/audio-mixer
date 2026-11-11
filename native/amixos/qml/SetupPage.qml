// FACTORY SETUP: the first-run wizard (and RERUN SETUP from SETTINGS / DIAGNOSTICS; FACTORY RESET brings it back too), the way a phone
// sets itself up: WELCOME (language) -> TERMS -> AUDIO (auto detect) -> CONSOLE (rate, block, start page, licence key) -> OPERATOR (PIN,
// profile) -> UPDATES (signed manifest check) -> DONE. The setup is mandatory: there is no SKIP SETUP; the wizard stays until FINISH on the last step (AUDIO alone may be left for CONNECTIONS later).
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: wiz
    objectName: "setupPage"
    property int step: 0
    property bool accepted: false
    property string audioNote: ""
    readonly property var steps: ["WELCOME", "TERMS", "AUDIO", "CONSOLE", "OPERATOR", "UPDATES", "DONE"]
    signal finished()
    color: Theme.obsidian
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(70, bl.implicitWidth + 24); implicitHeight: 30; radius: 6; color: on ? onColor : Theme.ov(0.08); border.color: Theme.lineSoft
        Text { id: bl; anchors.centerIn: parent; text: label; color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    component Head: Text { color: Theme.ink; font.family: Theme.sans; font.pixelSize: 24; font.weight: Font.DemiBold }
    component Body: Text { Layout.fillWidth: true; color: Theme.muted; font.family: Theme.sans; font.pixelSize: 12; wrapMode: Text.WordWrap; lineHeight: 1.25 }
    component Small: Text { Layout.fillWidth: true; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }
    ColumnLayout {
        anchors.centerIn: parent; width: Math.min(820, parent.width - 48); spacing: 18
        RowLayout { spacing: 10
            Text { text: Theme.product.toUpperCase(); color: Theme.orange; font.family: Theme.mono; font.pixelSize: 20; font.weight: Font.Black; font.letterSpacing: 5 }
            Text { text: i18n.t("by ", i18n.lang) + Theme.vendor; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9; font.letterSpacing: 2 }
            Item { Layout.fillWidth: true }
            Text { text: i18n.t("FACTORY SETUP · ", i18n.lang) + (wiz.step + 1) + " / " + wiz.steps.length; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.letterSpacing: 2 } }
        RowLayout { spacing: 4; Repeater { model: wiz.steps; delegate: Rectangle { Layout.fillWidth: true; implicitHeight: 4; radius: 2; color: index <= wiz.step ? Theme.orange : Theme.ov(0.1) } } }
        Rectangle { Layout.fillWidth: true; implicitHeight: 480; radius: 14; color: Theme.card; border.color: Theme.line
            StackLayout { anchors.fill: parent; anchors.margins: 28; currentIndex: wiz.step
                // 0 WELCOME
                ColumnLayout { spacing: 12
                    Head { text: i18n.t("Welcome to ", i18n.lang) + Theme.product }
                    Body { text: i18n.t("A mixing console and DSP workstation: 32 channels, 16 buses (8 AUX + 8 FX), EQ with RTA, dynamics, 300 effect presets, 100 samples, VST plug-ins, recording, remote control. Pick your language; everything else can be changed later on SETTINGS.", i18n.lang) }
                    Text { text: i18n.t("LANGUAGE", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.letterSpacing: 2 }
                    Flickable { Layout.fillWidth: true; Layout.fillHeight: true; contentHeight: langFlow.implicitHeight; clip: true
                        Flow { id: langFlow; width: parent.width; spacing: 6
                            Repeater { model: i18n.languages; delegate: Btn { label: modelData.name; on: i18n.lang === modelData.code; onClicked: i18n.lang = modelData.code } } } }
                    Small { text: i18n.t("22 languages are checked by people; the rest are machine translations and say so in the menu. Right-to-left languages mirror the layout.", i18n.lang) } }
                // 1 TERMS
                ColumnLayout { spacing: 12
                    Head { text: i18n.t("Terms and licences", i18n.lang) }
                    Flickable { Layout.fillWidth: true; Layout.fillHeight: true; contentHeight: terms.implicitHeight; clip: true
                        Body { id: terms; width: parent.width; text: "• " + Theme.product + i18n.t(" is published by ", i18n.lang) + Theme.vendor + i18n.t(" under the repository licence; the BASIC plan (8 channels) is free, PRO / STUDIO / MASTER unlock more with a key bound to this computer.\n\n• The Windows ASIO support uses the Steinberg ASIO SDK under Steinberg's licence terms; VST3 hosting uses the Steinberg VST3 interface headers under their dual licence. ASIO and VST are trademarks of Steinberg Media Technologies GmbH. Plug-ins you load are governed by their own licences.\n\n• Nothing is sent anywhere by itself: no telemetry, no account. The update check reads one signed file from the project's own hosts when you ask (or at start, if you switch it on below). Licence keys are verified offline against the vendor's public key.\n\n• The remote control (phone app, OSC, the daemon) serves this computer only unless you open it to your network; those paths are not encrypted without your own TLS proxy, and the pages say so.", i18n.lang) } }
                    RowLayout { spacing: 10
                        Rectangle { width: 22; height: 22; radius: 4; color: wiz.accepted ? Theme.emerald : Theme.ov(0.08); border.color: Theme.line; Text { anchors.centerIn: parent; text: wiz.accepted ? "✓" : ""; color: "#000"; font.bold: true } MouseArea { anchors.fill: parent; onClicked: wiz.accepted = !wiz.accepted } }
                        Text { text: i18n.t("I have read the terms", i18n.lang); color: Theme.ink; font.family: Theme.sans; font.pixelSize: 12 } } }
                // 2 AUDIO
                ColumnLayout { spacing: 12
                    Head { text: i18n.t("Your sound card", i18n.lang) }
                    Body { text: i18n.t("AUTO DETECT picks the best driver on this machine (a native ASIO driver first on Windows, then WASAPI / Core Audio / ALSA) and starts it. You can also skip this and choose on CONNECTIONS later.", i18n.lang) }
                    RowLayout { spacing: 8
                        Btn { label: i18n.t("AUTO DETECT AND START", i18n.lang); on: true; onColor: Theme.cyan; onClicked: { const ok = engine.autoStart(settings.defaultFrames); wiz.audioNote = ok ? "Started: " + engine.lastRoute : "Nothing started: " + engine.status } }
                        Btn { label: engine.running ? i18n.t("STOP", i18n.lang) : i18n.t("SKIP", i18n.lang); onClicked: if (engine.running) engine.stop(); else wiz.step = 3 }
                        Btn { label: settings.autoAsio ? i18n.t("AUTO DETECT AT START: ON", i18n.lang) : i18n.t("AUTO DETECT AT START: OFF", i18n.lang); on: settings.autoAsio; onColor: Theme.emerald; onClicked: settings.autoAsio = !settings.autoAsio } }
                    Text { text: engine.running ? i18n.t("ENGINE RUNNING on ", i18n.lang) + engine.backend.toUpperCase() + " · " + engine.sampleRate + i18n.t(" Hz · ", i18n.lang) + engine.blockFrames + i18n.t(" frames · ", i18n.lang) + engine.deviceInputs + i18n.t(" in / ", i18n.lang) + engine.deviceOutputs + i18n.t(" out", i18n.lang) : i18n.t("engine stopped", i18n.lang); color: engine.running ? Theme.emerald : Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                    Body { text: wiz.audioNote }
                    Text { text: i18n.t("INTERFACES FOUND: ", i18n.lang) + engine.devices.length; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.letterSpacing: 2 }
                    Flickable { Layout.fillWidth: true; Layout.fillHeight: true; contentHeight: devCol.implicitHeight; clip: true
                        ColumnLayout { id: devCol; width: parent.width; spacing: 2
                            Repeater { model: engine.devices; delegate: Text { text: Theme.kindIcon(modelData.kind) + " " + modelData.backend.toUpperCase() + "  " + i18n.t(Theme.kindName(modelData.kind), i18n.lang) + "  " + modelData.name + "  (" + modelData.inChannels + i18n.t(" in / ", i18n.lang) + modelData.outChannels + i18n.t(" out)", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideRight; Layout.fillWidth: true } } } } }
                // 3 CONSOLE
                ColumnLayout { spacing: 12
                    Head { text: i18n.t("Console defaults", i18n.lang) }
                    RowLayout { spacing: 6; Text { text: i18n.t("SAMPLE RATE", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; Layout.preferredWidth: 110 } Repeater { model: [44100, 48000, 96000]; delegate: Btn { label: modelData / 1000 + i18n.t(" kHz", i18n.lang); on: settings.defaultRate === modelData; onClicked: settings.defaultRate = modelData } } }
                    RowLayout { spacing: 6; Text { text: i18n.t("BLOCK", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; Layout.preferredWidth: 110 } Repeater { model: [64, 128, 256, 512]; delegate: Btn { label: modelData + ""; on: settings.defaultFrames === modelData; onClicked: settings.defaultFrames = modelData } } Text { text: "(" + (settings.defaultFrames / settings.defaultRate * 1000).toFixed(1) + i18n.t(" ms; smaller = less latency, more load)", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
                    RowLayout { spacing: 6; Text { text: i18n.t("START PAGE", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; Layout.preferredWidth: 110 } Repeater { model: [["home", "HOME"], ["mixer", "MIXER"], ["io", "CONNECTIONS"]]; delegate: Btn { label: modelData[1]; on: settings.startPage === modelData[0]; onClicked: settings.startPage = modelData[0] } } }
                    RowLayout { spacing: 6; Text { text: i18n.t("VU METERS", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; Layout.preferredWidth: 110 } Btn { label: settings.vuDefault ? i18n.t("ANALOGUE VU", i18n.lang) : i18n.t("BAR METERS", i18n.lang); on: settings.vuDefault; onClicked: settings.vuDefault = !settings.vuDefault } }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.lineSoft }
                    Text { text: i18n.t("LICENCE: ", i18n.lang) + engine.license.planName + " · " + engine.licensedChannels + i18n.t(" channels", i18n.lang) + (engine.license.state === "basic" ? i18n.t(" (free). Have a key? Paste it:", i18n.lang) : ""); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                    RowLayout { spacing: 6; Layout.fillWidth: true
                        TextField { id: keyIn; Layout.fillWidth: true; placeholderText: i18n.t("AMIX1-... licence key (optional)", i18n.lang); font.family: Theme.mono; font.pixelSize: 9 }
                        Btn { label: i18n.t("ACTIVATE", i18n.lang); on: keyIn.text.length > 8; onClicked: { const r = engine.activateKey(keyIn.text); wiz.audioNote = r.message; if (r.ok) keyIn.text = "" } } }
                    Small { text: i18n.t("The machine code for a key bound to this computer is on LICENSE. Keys are checked offline; nothing is sent.", i18n.lang) } }
                // 4 OPERATOR
                ColumnLayout { spacing: 12
                    Head { text: i18n.t("Operator and PIN", i18n.lang) }
                    Body { text: i18n.t("The PIN locks the fader surface and guards the admin pages. The default is 1234 until you set one; set it now, or leave it for later on ACCOUNT.", i18n.lang) }
                    RowLayout { spacing: 6; Layout.fillWidth: true
                        TextField { id: pin1; Layout.preferredWidth: 160; placeholderText: i18n.t("new PIN (4-8 digits)", i18n.lang); echoMode: TextInput.Password; inputMethodHints: Qt.ImhDigitsOnly; font.family: Theme.mono; font.pixelSize: 9 }
                        TextField { id: pin2; Layout.preferredWidth: 160; placeholderText: i18n.t("again", i18n.lang); echoMode: TextInput.Password; inputMethodHints: Qt.ImhDigitsOnly; font.family: Theme.mono; font.pixelSize: 9 }
                        Btn { label: i18n.t("SET PIN", i18n.lang); on: pin1.text.length >= 4 && pin1.text === pin2.text; onColor: Theme.cyan; onClicked: { if (access.setPin(access.pinIsDefault ? "1234" : pin1.text, pin1.text)) { pin1.text = ""; pin2.text = "" } } }
                        Text { text: access.pinIsDefault ? i18n.t("default 1234 still active", i18n.lang) : i18n.t("PIN set", i18n.lang); color: access.pinIsDefault ? Theme.amber : Theme.emerald; font.family: Theme.mono; font.pixelSize: 8 } }
                    Text { text: i18n.t("PROFILE", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.letterSpacing: 2 }
                    Repeater { model: access.roles; delegate: RowLayout { spacing: 8; Btn { label: modelData.name; on: access.role === modelData.id; onClicked: access.setRole(modelData.id, access.pinIsDefault ? "1234" : "") } Text { text: modelData.blurb; color: Theme.muted; font.family: Theme.sans; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight } } }
                    Small { text: access.message } }
                // 5 UPDATES
                ColumnLayout { spacing: 12
                    Head { text: i18n.t("Updates", i18n.lang) }
                    Body { text: i18n.t("The console checks one signed file (update.json) on the project's own hosts. The file is verified against the vendor key before it is read; a download is checked against its sha256 and saved for you to open. Nothing is installed by itself.", i18n.lang) }
                    RowLayout { spacing: 8
                        Btn { label: i18n.t("CHECK NOW", i18n.lang); on: updater.state !== "checking"; onColor: Theme.cyan; onClicked: updater.check() }
                        Btn { label: updater.checkAtStart ? i18n.t("CHECK AT START: ON", i18n.lang) : i18n.t("CHECK AT START: OFF", i18n.lang); on: updater.checkAtStart; onColor: Theme.emerald; onClicked: updater.checkAtStart = !updater.checkAtStart }
                        Btn { visible: updater.state === "available" && updater.file.url !== undefined; label: i18n.t("DOWNLOAD ", i18n.lang) + (updater.file.name || ""); onColor: Theme.amber; on: true; onClicked: updater.download() } }
                    Text { text: i18n.t("THIS VERSION ", i18n.lang) + updater.current + " · " + updater.state.toUpperCase() + (updater.lastCheck ? i18n.t(" · last check ", i18n.lang) + updater.lastCheck : ""); color: updater.state === "available" ? Theme.amber : updater.state === "error" ? Theme.danger : Theme.emerald; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                    Body { text: updater.message }
                    Repeater { model: updater.notes; delegate: Text { text: "• " + modelData; color: Theme.muted; font.family: Theme.sans; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap } }
                    Small { text: i18n.t("Manifest: ", i18n.lang) + updater.url } }
                // 6 DONE
                ColumnLayout { spacing: 12
                    Head { text: i18n.t("Ready", i18n.lang) }
                    Body { text: Theme.product + i18n.t(" is set up. The console starts on ", i18n.lang) + settings.startPage.toUpperCase() + i18n.t("; the engine ", i18n.lang) + (engine.running ? i18n.t("is running on ", i18n.lang) + engine.backend.toUpperCase() : i18n.t("waits for START on CONNECTIONS", i18n.lang)) + i18n.t("; profile ", i18n.lang) + access.role.toUpperCase() + (access.pinIsDefault ? i18n.t(" with the default PIN", i18n.lang) : i18n.t(" with your PIN", i18n.lang)) + i18n.t("; licence ", i18n.lang) + engine.license.planName + i18n.t(".\n\nYou can run this setup again from SETTINGS or DIAGNOSTICS; FACTORY RESET wipes the console and brings it back at the next start.", i18n.lang) }
                    Item { Layout.fillHeight: true } } } }
        RowLayout { spacing: 8
            Btn { label: i18n.t("BACK", i18n.lang); visible: wiz.step > 0; onClicked: wiz.step-- }
            Item { Layout.fillWidth: true }
            Btn { label: wiz.step === 6 ? i18n.t("FINISH", i18n.lang) : i18n.t("NEXT", i18n.lang); on: wiz.step !== 1 || wiz.accepted; onColor: wiz.step === 6 ? Theme.emerald : Theme.amber; onClicked: { if (wiz.step === 1 && !wiz.accepted) return; if (wiz.step === 6) wiz.finished(); else wiz.step++ } } }
    }
}
