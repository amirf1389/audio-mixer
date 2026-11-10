// SETTINGS: general, audio (auto-start, defaults, clip point, hold), mixer defaults, language, files (export / import / factory reset), about
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Flickable {
    id: page
    contentHeight: col.implicitHeight + 24; clip: true
    component Opt: RowLayout { property string label: ""; property string hint: ""; Layout.fillWidth: true; spacing: 10
        ColumnLayout { Layout.preferredWidth: 260; spacing: 1; Text { text: label; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } Text { visible: hint.length > 0; text: hint; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap; Layout.preferredWidth: 260 } } }
    component Toggle: Rectangle { property bool on: false; signal toggled(bool v); implicitWidth: 44; implicitHeight: 20; radius: 10; color: on ? Theme.emerald : Qt.rgba(1, 1, 1, 0.1); border.color: on ? Theme.emerald : Theme.lineSoft
        Rectangle { width: 16; height: 16; radius: 8; y: 2; x: parent.on ? parent.width - 18 : 2; color: parent.on ? "#000" : Theme.muted; Behavior on x { NumberAnimation { duration: 90 } } }
        MouseArea { anchors.fill: parent; onClicked: parent.toggled(!parent.on) } }
    component Choice: RowLayout { property var options: []; property var value; signal picked(var v); spacing: 3
        Repeater { model: parent.options; delegate: Rectangle { implicitWidth: ct.implicitWidth + 12; implicitHeight: 20; radius: 4; color: parent.parent.value === modelData.v ? Theme.amber : Qt.rgba(1, 1, 1, 0.08); Text { id: ct; anchors.centerIn: parent; text: modelData.t; color: parent.parent.parent.value === modelData.v ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.parent.picked(modelData.v) } } } }
    ColumnLayout {
        id: col; width: parent.width - 20; x: 10; y: 10; spacing: 10
        Panel {
            Layout.fillWidth: true
            title: "GENERAL"
            subtitle: "A mix OS " + appVersion + " • every change here is saved at once • file: " + settings.settingsFile()
            Opt { label: "Start page"; hint: "The page shown when the console opens"; Choice { options: [{t: "HOME", v: "home"}, {t: "MIXER", v: "mixer"}, {t: "CONNECTIONS", v: "io"}, {t: "ARRANGE", v: "arrange"}]; value: settings.startPage; onPicked: function (v) { settings.startPage = v } } }
            Opt { label: "Confirm destructive actions"; hint: "RESET FADERS, FLATTEN DSP, factory reset ask first"; Toggle { on: settings.confirmDestructive; onToggled: function (v) { settings.confirmDestructive = v } } }
            Opt { label: "Tooltips"; hint: "Short help when the pointer rests on a control"; Toggle { on: settings.tooltips; onToggled: function (v) { settings.tooltips = v } } }
        }
        Panel {
            Layout.fillWidth: true
            title: "AUDIO"
            subtitle: "The sound card and how it starts. The device itself is chosen on CONNECTIONS."
            Opt { label: "Start the last device on launch"; hint: settings.lastDevice.backend ? ("Last: " + settings.lastDevice.backend.toUpperCase() + " • " + settings.lastDevice.rate + " Hz • " + settings.lastDevice.frames + " frames") : "No device remembered yet: start one on CONNECTIONS once"; Toggle { on: settings.autoStart; onToggled: function (v) { settings.autoStart = v } }
                Rectangle { implicitWidth: 90; implicitHeight: 20; radius: 4; color: Qt.rgba(0, 0.95, 1, 0.18); border.color: Theme.cyan; Text { anchors.centerIn: parent; text: "START NOW"; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: settings.startLastDevice() } } }
            Opt { label: "AUTO DETECT ASIO"; hint: "With no remembered device, start the best one by itself: the interface's own ASIO driver first, then a wrapper, then the system endpoint"; Toggle { on: settings.autoAsio; onToggled: function (v) { settings.autoAsio = v } }
                Text { property var det: engine.autoDetect(); text: det && det.name ? ("Now: " + det.name + " · " + det.backend.toUpperCase()) : "Now: no real sound card"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } }
            Opt { label: "SMART START by default"; hint: "Walk the driver ladder (ASIO → WASAPI exclusive → shared) instead of one fixed driver"; Toggle { on: settings.smartStart; onToggled: function (v) { settings.smartStart = v } } }
            Opt { label: "Default sample rate"; Choice { options: [{t: "44.1k", v: 44100}, {t: "48k", v: 48000}, {t: "88.2k", v: 88200}, {t: "96k", v: 96000}, {t: "192k", v: 192000}]; value: settings.defaultRate; onPicked: function (v) { settings.defaultRate = v } } }
            Opt { label: "Default buffer"; hint: (settings.defaultFrames / settings.defaultRate * 1000).toFixed(1) + " ms per block at the default rate"; Choice { options: [{t: "64", v: 64}, {t: "128", v: 128}, {t: "256", v: 256}, {t: "512", v: 512}, {t: "1024", v: 1024}]; value: settings.defaultFrames; onPicked: function (v) { settings.defaultFrames = v } } }
            Opt { label: "Clip point"; hint: "The meters' clip lamp lights at this level (dBFS)"; Slider { from: -12; to: 0; stepSize: 0.1; value: settings.clipDb; onMoved: settings.clipDb = value; implicitWidth: 200 } Text { text: settings.clipDb.toFixed(1) + " dBFS"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8 } }
            Opt { label: "Peak hold"; hint: "How long the peak line stays before falling"; Choice { options: [{t: "OFF", v: 0}, {t: "0.5 s", v: 500}, {t: "1.4 s", v: 1400}, {t: "3 s", v: 3000}, {t: "∞", v: 999999}]; value: settings.peakHoldMs; onPicked: function (v) { settings.peakHoldMs = v } } }
        }
        Panel {
            Layout.fillWidth: true
            title: "RECORDING"
            subtitle: "● REC on the MIXER writes the master and every armed strip (pre-fader, after trim / DSP / inserts) to WAV files, one per source, named by time."
            Opt { label: "Folder"; TextField { Layout.fillWidth: true; text: engine.recordFolder; font.family: Theme.mono; font.pixelSize: 9; implicitHeight: 26; onEditingFinished: engine.recordFolder = text } }
            Opt { label: "Record the master"; hint: "Off = only the armed strips"; Toggle { on: engine.recordMaster; onToggled: function (v) { engine.recordMaster = v } } }
            Opt { label: "Format"; Choice { options: [{t: "24-bit PCM", v: false}, {t: "32-bit float", v: true}]; value: engine.recordFloat; onPicked: function (v) { engine.recordFloat = v } } }
            Text { visible: engine.recordMessage.length > 0; text: engine.recordMessage; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Repeater { model: engine.recordFiles; delegate: Text { text: modelData; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; elide: Text.ElideMiddle; Layout.fillWidth: true } }
        }
        Panel {
            Layout.fillWidth: true
            title: "MIXER DEFAULTS"
            subtitle: "How the MIXER page opens. Each can still be toggled on the page itself."
            Opt { label: "VU faces"; hint: "Analogue VU meters instead of bars"; Toggle { on: settings.vuDefault; onToggled: function (v) { settings.vuDefault = v } } Text { text: "0 VU = " + engine.vuRefDb + " dBFS"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Choice { options: [{t: "-18", v: -18}, {t: "-20", v: -20}, {t: "-14", v: -14}]; value: engine.vuRefDb; onPicked: function (v) { engine.vuRefDb = v } } }
            Opt { label: "Monitor (control room) panel"; Toggle { on: settings.monitorPanelDefault; onToggled: function (v) { settings.monitorPanelDefault = v } } }
            Opt { label: "Plugin rack"; Toggle { on: settings.rackDefault; onToggled: function (v) { settings.rackDefault = v } } }
            Opt { label: "Channel panel"; Toggle { on: settings.channelPanelDefault; onToggled: function (v) { settings.channelPanelDefault = v } } }
        }
        Panel {
            Layout.fillWidth: true
            title: i18n.t("LANGUAGE", i18n.lang) + " • " + i18n.count + " LANGUAGES"
            subtitle: "22 languages come from the web console and were checked; 58 are machine translations marked \"auto\" until a native speaker reviews them. Right-to-left languages mirror the layout."
            Flow { Layout.fillWidth: true; spacing: 4
                Repeater { model: i18n.languages; delegate: Rectangle { implicitWidth: lt.implicitWidth + 14; implicitHeight: 22; radius: 4; color: i18n.lang === modelData.code ? Theme.amber : Qt.rgba(1, 1, 1, 0.06)
                    Text { id: lt; anchors.centerIn: parent; text: modelData.name + (modelData.auto ? " ·auto" : ""); color: i18n.lang === modelData.code ? "#000" : (modelData.auto ? Theme.muted : Theme.ink); font.family: Theme.mono; font.pixelSize: 8 }
                    MouseArea { anchors.fill: parent; onClicked: i18n.lang = modelData.code } } }
            }
        }
        Panel {
            Layout.fillWidth: true
            title: "VST3 / VST2 PLUG-INS"
            subtitle: "Where A mix OS looks for plug-ins: .vst3 bundles, and VST2 .dll (Windows) / .so (Linux) files. The platform folders are always scanned; add your own below. Plug-ins load into the PLUGIN RACK on the MIXER (generic controls, no plug-in window yet)."
            Repeater { model: settings.vstDefaultDirs; delegate: RowLayout { Layout.fillWidth: true; Rectangle { implicitWidth: 8; implicitHeight: 8; radius: 4; color: Theme.emerald } Text { text: modelData; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; Layout.fillWidth: true; elide: Text.ElideMiddle } Text { text: "platform"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } } }
            Repeater { model: settings.vstDirs; delegate: RowLayout { Layout.fillWidth: true; Rectangle { implicitWidth: 8; implicitHeight: 8; radius: 4; color: Theme.cyan } Text { text: modelData; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; Layout.fillWidth: true; elide: Text.ElideMiddle } Rectangle { implicitWidth: 60; implicitHeight: 18; radius: 3; color: Qt.rgba(1, 1, 1, 0.08); Text { anchors.centerIn: parent; text: "REMOVE"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } MouseArea { anchors.fill: parent; onClicked: settings.removeVstDir(modelData) } } } }
            RowLayout { Layout.fillWidth: true; spacing: 6
                TextField { id: vstDirIn; Layout.fillWidth: true; placeholderText: "another folder with .vst3 plug-ins"; font.family: Theme.mono; font.pixelSize: 9; implicitHeight: 26; onAccepted: { settings.addVstDir(text); text = "" } }
                Rectangle { implicitWidth: 90; implicitHeight: 26; radius: 5; color: Theme.amber; Text { anchors.centerIn: parent; text: "ADD FOLDER"; color: "#000"; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { settings.addVstDir(vstDirIn.text); vstDirIn.text = "" } } }
                Rectangle { implicitWidth: 80; implicitHeight: 26; radius: 5; color: Qt.rgba(0, 0.95, 1, 0.18); border.color: Theme.cyan; Text { anchors.centerIn: parent; text: "RESCAN"; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: settings.rescanVst() } } }
            Flow { Layout.fillWidth: true; spacing: 4
                Repeater { model: engine.vstCatalog(); delegate: Rectangle { implicitWidth: vt.implicitWidth + 12; implicitHeight: 18; radius: 3; color: Qt.rgba(0.6, 0.4, 1, 0.18); border.color: "#a78bfa"; Text { id: vt; anchors.centerIn: parent; text: modelData.format + " · " + modelData.name + (modelData.vendor ? " · " + modelData.vendor : ""); color: "#a78bfa"; font.family: Theme.mono; font.pixelSize: 7 } } }
                Text { visible: engine.vstCatalog().length === 0; text: "No VST3 plug-ins found yet."; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
            Text { text: "VST is a trademark of Steinberg Media Technologies GmbH. A mix OS hosts VST3 plug-ins through the VST 3 interface headers under their licence (drivers/vst3/sdk/LICENSE.txt) and VST2 plug-ins through the public VST 2.4 binary interface written clean-room (no Steinberg VST2 SDK file is used or shipped). It is not badged as VST and ships no plug-ins."; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
        Panel {
            Layout.fillWidth: true
            title: "FILES & DATA"
            subtitle: "Everything A mix OS keeps lives in " + settings.dataFolder + " and the settings file. Nothing is sent anywhere."
            Repeater { model: settings.dataFiles; delegate: RowLayout { Layout.fillWidth: true; spacing: 8
                Rectangle { implicitWidth: 8; implicitHeight: 8; radius: 4; color: modelData.exists ? Theme.emerald : Theme.lineSoft }
                Text { text: modelData.name; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; Layout.preferredWidth: 130 }
                Text { text: modelData.path; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; Layout.preferredWidth: 300; elide: Text.ElideMiddle }
                Text { text: modelData.exists ? (modelData.bytes / 1024).toFixed(1) + " KB" : "—"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; Layout.preferredWidth: 60 }
                Text { text: modelData.what; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; Layout.fillWidth: true; elide: Text.ElideRight } } }
            RowLayout { Layout.fillWidth: true; spacing: 6
                Rectangle { implicitWidth: 120; implicitHeight: 26; radius: 5; color: Theme.amber; Text { anchors.centerIn: parent; text: "EXPORT CONSOLE"; color: "#000"; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: settings.exportConsole(ioPath.text) } }
                Rectangle { implicitWidth: 120; implicitHeight: 26; radius: 5; color: Qt.rgba(0, 0.95, 1, 0.18); border.color: Theme.cyan; Text { anchors.centerIn: parent; text: "IMPORT CONSOLE"; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: settings.importConsole(ioPath.text) } }
                TextField { id: ioPath; Layout.fillWidth: true; placeholderText: "path of the JSON file (empty = ~/amixos-console.json)"; font.family: Theme.mono; font.pixelSize: 9; implicitHeight: 26 }
                Rectangle { implicitWidth: 100; implicitHeight: 26; radius: 5; color: Qt.rgba(1, 1, 1, 0.08); border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: "OPEN FOLDER"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: settings.openDataFolder() } }
                Rectangle { implicitWidth: 100; implicitHeight: 26; radius: 5; color: Qt.rgba(1, 1, 1, 0.08); border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: "SAVE NOW"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.save() } } }
            Text { visible: settings.message.length > 0; text: settings.message; color: Theme.emerald; font.family: Theme.mono; font.pixelSize: 8; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.lineSoft }
            RowLayout { Layout.fillWidth: true; spacing: 10
                Rectangle { implicitWidth: 120; implicitHeight: 26; radius: 5; color: Qt.rgba(1, 0.165, 0.373, 0.2); border.color: Theme.danger; Text { anchors.centerIn: parent; text: "FACTORY RESET"; color: Theme.danger; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: settings.confirmDestructive ? confirmReset.open() : settings.factoryReset() } }
                Text { text: "Back to a new desk. " + settings.factoryResetPreview().join(" · "); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap; Layout.fillWidth: true } }
            Dialog { id: confirmReset; title: "Factory reset?"; modal: true; standardButtons: Dialog.Ok | Dialog.Cancel; anchors.centerIn: Overlay.overlay; onAccepted: settings.factoryReset()
                Text { text: settings.factoryResetPreview().join("\n"); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; wrapMode: Text.WordWrap; width: 380 } }
        }
        Panel {
            Layout.fillWidth: true
            title: "ABOUT"
            subtitle: "A mix OS " + appVersion + " • native console by AMFB Audio • C++20 engine + Qt Quick interface, no web runtime"
            Text { text: "Engine drivers built into this copy: " + engine.backends.join(", ").toUpperCase() + " • strips: " + engine.strips + " • edition: " + engine.edition; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9 }
            Flow { Layout.fillWidth: true; spacing: 4
                Repeater { model: engine.pages; delegate: Rectangle { implicitWidth: t.implicitWidth + 12; implicitHeight: 18; radius: 3; color: modelData.ready ? Qt.rgba(0.063, 0.725, 0.506, 0.2) : Qt.rgba(1, 1, 1, 0.05); Text { id: t; anchors.centerIn: parent; text: modelData.text; color: modelData.ready ? Theme.emerald : Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } } } }
        }
    }
}
