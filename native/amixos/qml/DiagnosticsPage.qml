// DIAGNOSTICS: a power-on self test of what this build really has (engine, device, clock, buffer, interfaces, plug-ins, data files,
// licence, recording folder), SAFE MODE (every fader at -inf and all strips muted, to protect the speakers), SOFT RESTART of the engine,
// FAST BOOT (auto-start the last device) and FACTORY RESET with a preview of what goes.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "diagnosticsPage"
    property var rows: []
    property bool confirmReset: false
    readonly property var tel: engine.telemetry
    function audit() {
        const r = []; const t = engine.telemetry; const lic = engine.license
        const add = (name, what, ok, result) => r.push({ name: name, what: what, ok: ok, result: result })
        add("AUDIO ENGINE", "32 strips · 16 buses (8 AUX + 8 FX) · 300 FX presets · 100 samples · master limiter · loudness", true, "v" + appVersion + " · " + engine.pages.length + " pages")
        add("DEVICE", "the open interface", engine.running, engine.running ? engine.backend.toUpperCase() + " · " + engine.deviceInputs + " in / " + engine.deviceOutputs + " out" : "NOT OPEN")
        add("CLOCK", "sample rate and block size", engine.running, engine.running ? engine.sampleRate + " Hz · " + engine.blockFrames + " frames (" + (engine.blockFrames / engine.sampleRate * 1000).toFixed(2) + " ms)" : "—")
        add("BUFFER", "overruns since start", t.xruns === 0, t.xruns === 0 ? "0 overruns · " + t.blocks + " blocks OK" : t.xruns + " OVERRUNS")
        add("INTERFACES", "found on this machine", engine.devices.length > 0, engine.devices.length + " device(s)")
        let asio = 0; for (let i = 0; i < engine.devices.length; ++i) if (engine.devices[i].backend === "asio") ++asio
        add("ASIO", "native drivers", true, asio > 0 ? asio + " ASIO driver(s)" : (Qt.platform.os === "windows" ? "none installed" : "not on " + Qt.platform.os))
        add("PLUG-INS", "VST3 / VST2 found by the scan", true, engine.vstCatalog().length + " plug-in(s)")
        let clips = 0; for (let i = 0; i < engine.meters.length; ++i) if (engine.meters[i].clip) ++clips
        add("CLIPPING", "strips and master", clips === 0 && !t.masterClip, clips === 0 && !t.masterClip ? "none" : clips + " strip(s)" + (t.masterClip ? " + master" : ""))
        const files = settings.dataFiles; let missing = []; for (let i = 0; i < files.length; ++i) if (!files[i].exists) missing.push(files[i].name)
        add("FOLDERS", settings.folders.os.toUpperCase() + " · program " + settings.folders.program, true, "data " + settings.folders.data + " · cache " + settings.folders.cache + " · temp " + settings.folders.temp)
        add("DATA FILES", settings.dataFolder, true, files.length + " known · " + (missing.length ? missing.length + " not written yet (" + missing.join(", ") + ")" : "all present"))
        add("AUTOSAVE", "console every 60 s", settings.autosave.exists, settings.autosave.exists ? settings.autosave.at : "none yet")
        add("LICENCE", lic.planName, lic.state === "ok" || lic.state === "licensed", engine.licensedChannels + " channels · " + lic.state + (lic.expires ? " · until " + lic.expires : ""))
        add("RECORDING", engine.recordFolder, true, engine.recording ? "RECORDING NOW" : "idle · " + (engine.recordFloat ? "32-bit float" : "24-bit") + " WAV")
        add("LAST SESSION", "clean close", !settings.lastSessionCrashed, settings.lastSessionCrashed ? "ended unexpectedly (CALIBRATE > SESSION)" : "closed cleanly")
        rows = r
    }
    Component.onCompleted: audit()
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 24; radius: 4; color: on ? onColor : Theme.ov(0.08); border.color: Theme.lineSoft
        Text { id: bl; anchors.centerIn: parent; text: label; color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Text { text: i18n.t("DIAGNOSTICS:", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Text { text: i18n.t("power-on self test · safe mode · restart · factory reset", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Item { Layout.fillWidth: true }
                Text { readonly property int bad: { let n = 0; for (let i = 0; i < page.rows.length; ++i) if (!page.rows[i].ok) ++n; return n } text: page.rows.length ? (bad === 0 ? i18n.t("ALL CHECKS OK", i18n.lang) : bad + i18n.t(" CHECK(S) NEED ATTENTION", i18n.lang)) : ""; color: bad === 0 ? Theme.emerald : Theme.amber; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                Btn { label: i18n.t("RUN AUDIT", i18n.lang); on: true; onColor: Theme.cyan; onClicked: page.audit() } }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; clip: true
            ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 4
                Text { text: i18n.t("POWER-ON SELF TEST", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
                Repeater { model: page.rows; delegate: Rectangle { Layout.fillWidth: true; implicitHeight: 30; radius: 4; color: Theme.ov(0.03)
                    RowLayout { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 10
                        Rectangle { width: 8; height: 8; radius: 4; color: modelData.ok ? Theme.emerald : Theme.amber }
                        Text { Layout.preferredWidth: 110; text: modelData.name; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                        Text { Layout.fillWidth: true; text: modelData.what; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; elide: Text.ElideRight }
                        Text { text: modelData.result; color: modelData.ok ? Theme.cyan : Theme.amber; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } } } }
                Item { Layout.fillHeight: true }
            }
        }
        GridLayout {
            Layout.fillWidth: true; columns: 5; columnSpacing: 8
            Repeater {
                model: [
                    { t: "SAFE MODE", d: "Every fader to -inf and every strip muted: nothing reaches the speakers until you bring faders up.", b: "ENGAGE SAFE MODE", c: Theme.amber, a: 0 },
                    { t: "SOFT RESTART", d: "Stops the engine and opens the remembered device again (SMART START). Plug-ins and effects are rebuilt.", b: "RESTART ENGINE", c: Theme.cyan, a: 1 },
                    { t: "FACTORY SETUP", d: "Runs the first-start wizard again: language, terms, sound card, console defaults, operator PIN, updates.", b: "RERUN SETUP", c: Theme.cyan, a: 4 },
                    { t: "FAST BOOT", d: settings.autoStart ? "On: the last device opens when the console starts." : "Off: the console starts silent and waits for START.", b: settings.autoStart ? "SWITCH OFF" : "SWITCH ON", c: Theme.emerald, a: 2 },
                    { t: "FACTORY RESET", d: page.confirmReset ? "Removes: " + settings.factoryResetPreview().join(", ") + ". The licence stays. Press again to confirm." : "Settings, console, scenes, presets, arrangement and driver locks back to zero. The licence stays.", b: page.confirmReset ? "YES, RESET NOW" : "FACTORY RESET", c: Theme.danger, a: 3 }]
                delegate: Rectangle { Layout.fillWidth: true; implicitHeight: 110; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                    ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 6
                        Text { text: modelData.t; color: modelData.c; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; font.letterSpacing: 1.5 }
                        Text { Layout.fillWidth: true; Layout.fillHeight: true; text: modelData.d; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }
                        Btn { label: modelData.b; on: true; onColor: modelData.c; onClicked: {
                            if (modelData.a === 0) { for (let s = 0; s < engine.stripState.length; ++s) engine.setGain(s, -120); engine.muteAll(true); engine.setMaster(-120) }
                            else if (modelData.a === 1) { engine.stop(); settings.startLastDevice() }
                            else if (modelData.a === 2) settings.autoStart = !settings.autoStart
                            else if (modelData.a === 4) settings.rerunSetup()
                            else if (!page.confirmReset) page.confirmReset = true
                            else { page.confirmReset = false; settings.factoryReset() }
                            page.audit() } } } }
            }
        }
    }
}
