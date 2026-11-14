// A mix OS main window: header, page rail, the current page. Pages come from engine.pages (the same groups and names as the web console had).
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: root
    visible: true
    width: 1380
    height: 860
    minimumWidth: 720
    minimumHeight: 480
    title: Theme.product + " " + appVersion + " · " + Theme.vendor
    property bool forceSetup: false   // the smoke / shots show the wizard on demand
    color: Theme.obsidian
    property string current: settings.startPage
    LayoutMirroring.enabled: i18n.rtl
    Binding { target: Theme; property: "dark"; value: settings.darkTheme }
    LayoutMirroring.childrenInherit: true
    // SOFT RESTART: the console starts over without closing: engine off, sound cards scanned again, the remembered device (or the best one) started, the page rebuilt, the boot screen shown
    function softRestart() { engine.stop(); engine.refreshDevices(); if (!settings.startLastDevice()) engine.autoStart(settings.defaultFrames); const c = root.current; root.current = "home"; root.current = c; pageLoader.active = false; pageLoader.active = true; root.bootOn = settings.bootAnimation && !noBoot }
    Shortcut { sequence: "Ctrl+Shift+R"; context: Qt.ApplicationShortcut; onActivated: root.softRestart() }
    function pageOf(id) { for (var i = 0; i < engine.pages.length; i++) if (engine.pages[i].id === id) return engine.pages[i]; return engine.pages[0] }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {   // header
            Layout.fillWidth: true; implicitHeight: 48; color: Theme.chrome
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.lineSoft }
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14; spacing: 12
                Text { text: Theme.product.toUpperCase(); color: Theme.orange; font.family: Theme.mono; font.pixelSize: 15; font.weight: Font.Black; font.letterSpacing: 4 }
                Text { text: i18n.t("BY AMFB AUDIO", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.letterSpacing: 2 }
                Row {   // FILE · VIEW · INFO: the menus of a desktop program, in the console's own look
                    objectName: "menuBar"; spacing: 2
                    component MenuBtn: Rectangle { property string label; property var menu; implicitWidth: mbT.implicitWidth + 18; height: 24; width: implicitWidth; radius: 4; color: (menu && menu.visible) || mbH.hovered ? Theme.ov(0.10) : "transparent"
                        Text { id: mbT; anchors.centerIn: parent; text: i18n.t(parent.label, i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
                        HoverHandler { id: mbH } MouseArea { anchors.fill: parent; onClicked: parent.menu.popup(parent, 0, parent.height + 2) } }
                    component MItem: MenuItem { font.family: Theme.mono; font.pixelSize: 10; implicitHeight: 26 }
                    MenuBtn { objectName: "menuFile"; label: i18n.t("FILE", i18n.lang); menu: fileMenu }
                    MenuBtn { objectName: "menuEdit"; label: i18n.t("EDIT", i18n.lang); menu: editMenu }
                    MenuBtn { objectName: "menuView"; label: i18n.t("VIEW", i18n.lang); menu: viewMenu }
                    MenuBtn { objectName: "menuTools"; label: i18n.t("TOOLS", i18n.lang); menu: toolsMenu }
                    MenuBtn { objectName: "menuInfo"; label: i18n.t("HELP", i18n.lang); menu: infoMenu }
                    Rectangle { objectName: "menuAbout"; implicitWidth: abT.implicitWidth + 18; height: 24; width: implicitWidth; radius: 4; color: abH.hovered || aboutPop.visible ? Theme.ov(0.10) : "transparent"   // ABOUT: straight to the About box
                        Text { id: abT; anchors.centerIn: parent; text: i18n.t("ABOUT", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
                        HoverHandler { id: abH } MouseArea { anchors.fill: parent; onClicked: aboutPop.open() } }
                    Menu { id: editMenu; objectName: "editMenu"; width: 260
                        MItem { text: i18n.t("Undo", i18n.lang); onTriggered: engine.undo() }
                        MItem { text: i18n.t("Redo", i18n.lang); onTriggered: engine.redo() }
                        MItem { text: i18n.t("Compare A / B", i18n.lang); onTriggered: engine.toggleAB() }
                        MenuSeparator {}
                        MItem { text: i18n.t("Mute all channels", i18n.lang); onTriggered: engine.muteAll(true) }
                        MItem { text: i18n.t("Unmute all channels", i18n.lang); onTriggered: engine.muteAll(false) }
                        MItem { text: i18n.t("Clear solo", i18n.lang); onTriggered: engine.clearSolo() }
                        MItem { text: i18n.t("Clear clip lamps", i18n.lang); onTriggered: engine.clearClips() } }
                    Menu { id: toolsMenu; objectName: "toolsMenu"; width: 260
                        MItem { text: i18n.t("Plug-ins", i18n.lang); onTriggered: root.current = "plugins" }
                        MItem { text: i18n.t("Plug-in Builder (separate app)…", i18n.lang); onTriggered: settings.openPluginBuilder() }
                        MItem { text: i18n.t("Sound card", i18n.lang); onTriggered: root.current = "asio" }
                        MItem { text: i18n.t("Effects", i18n.lang); onTriggered: root.current = "fx" }
                        MItem { text: i18n.t("Mastering", i18n.lang); onTriggered: root.current = "master" }
                        MItem { text: i18n.t("Samples", i18n.lang); onTriggered: root.current = "samples" }
                        MenuSeparator {}
                        MItem { text: i18n.t("Volume Enhancer", i18n.lang); onTriggered: enhancer.open() }
                        MItem { text: i18n.t("Master Analyzer", i18n.lang); onTriggered: analyzer.open() }
                        MenuSeparator {}
                        MItem { text: i18n.t("Settings", i18n.lang); onTriggered: root.current = "sys" } }
                    Menu { id: fileMenu; objectName: "fileMenu"; width: 300
                        MItem { text: i18n.t("Save the console to a file", i18n.lang); onTriggered: { const f = settings.exportConsole(""); root.say(f !== "" ? i18n.t("Saved: ", i18n.lang) + f : i18n.t("The console could not be saved.", i18n.lang)) } }
                        MItem { text: i18n.t("Load the console from that file", i18n.lang); onTriggered: root.say(settings.importConsole("") ? i18n.t("The console was loaded from its file.", i18n.lang) : i18n.t("No saved console file was found.", i18n.lang)) }
                        MenuSeparator {}
                        MItem { text: i18n.t("Scenes", i18n.lang); onTriggered: root.current = "scenes" }
                        MItem { text: i18n.t("Presets", i18n.lang); onTriggered: root.current = "presets" }
                        MItem { text: i18n.t("Media files", i18n.lang); onTriggered: root.current = "media" }
                        MenuSeparator {}
                        MItem { text: i18n.t("Soft restart", i18n.lang) + "   Ctrl+Shift+R"; onTriggered: root.softRestart() }
                        }
                    Menu { id: viewMenu; objectName: "viewMenu"; width: 260
                        MItem { text: i18n.t("Mixer", i18n.lang); onTriggered: root.current = "mixer" }
                        MItem { text: i18n.t("Meters", i18n.lang); onTriggered: root.current = "meter" }
                        MItem { text: i18n.t("Routing", i18n.lang); onTriggered: root.current = "routing" }
                        MItem { text: i18n.t("Connections", i18n.lang); onTriggered: root.current = "io" }
                        MItem { text: i18n.t("Network", i18n.lang); onTriggered: root.current = "network" }
                        MItem { text: i18n.t("Acoustics", i18n.lang); onTriggered: root.current = "acoustics" }
                        MenuSeparator {}
                        MItem { text: settings.darkTheme ? i18n.t("Light look", i18n.lang) : i18n.t("Dark look", i18n.lang); onTriggered: settings.darkTheme = !settings.darkTheme }
                        MItem { text: (settings.tooltips ? "✓ " : "") + i18n.t("Tooltips", i18n.lang); onTriggered: settings.tooltips = !settings.tooltips }
                        MItem { text: root.visibility === Window.FullScreen ? i18n.t("Leave full screen", i18n.lang) : i18n.t("Full screen", i18n.lang); onTriggered: root.visibility = root.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen } }
                    Menu { id: infoMenu; objectName: "infoMenu"; width: 260
                        MItem { text: i18n.t("About this console", i18n.lang); onTriggered: aboutPop.open() }
                        MItem { text: i18n.t("Sound card in use", i18n.lang); onTriggered: root.say(engine.running ? engine.status + "\n\n" + engine.lastRoute : i18n.t("No sound card is running. Open one on CONNECTIONS, or use SOFT RESTART.", i18n.lang)) }
                        MItem { text: i18n.t("What's new", i18n.lang); onTriggered: whatsNew.open() }
                        MItem { text: i18n.t("Help", i18n.lang); onTriggered: root.current = "help" }
                        MItem { text: i18n.t("Licence and account", i18n.lang); onTriggered: root.current = "acct" } }
                }
                Item { Layout.fillWidth: true }
                Rectangle { implicitWidth: statusText.implicitWidth + 20; implicitHeight: 24; radius: 12; color: engine.running ? Qt.rgba(0.063, 0.725, 0.506, 0.15) : Qt.rgba(0.984, 0.749, 0.141, 0.12); border.color: engine.running ? Theme.emerald : Theme.amber
                    Text { id: statusText; anchors.centerIn: parent; text: engine.running ? i18n.t("ENGINE ON • ", i18n.lang) + engine.backend.toUpperCase() : i18n.t("ENGINE OFF", i18n.lang); color: engine.running ? Theme.emerald : Theme.amber; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                    objectName: "enginePill"
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; enabled: !engine.busy; onClicked: { if (engine.running) engine.stop(); else settings.startLastDevice() } }   // ENGINE ON / OFF: one click
                    ToolTip.visible: epH.hovered; ToolTip.text: engine.running ? i18n.t("Click to stop the engine (the sound card is released)", i18n.lang) : i18n.t("Click to start the engine on the remembered sound card (or AUTO DETECT)", i18n.lang); HoverHandler { id: epH } }
                Rectangle { visible: engine.busy; implicitWidth: busyT.implicitWidth + 20; implicitHeight: 24; radius: 12; color: Qt.rgba(0.984, 0.749, 0.141, 0.14); border.color: Theme.amber   // a scan or a driver start is running: the console stays usable
                    Text { id: busyT; anchors.centerIn: parent; text: "⟳ " + i18n.t(engine.busyText(), i18n.lang) + " …"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } }
                Rectangle { implicitWidth: 26; implicitHeight: 26; radius: 13; color: Theme.cardRaised; border.color: Theme.line   // SOFT RESTART
                    Text { anchors.centerIn: parent; text: "↻"; color: Theme.cyan; font.pixelSize: 14; font.bold: true }
                    MouseArea { anchors.fill: parent; onClicked: root.softRestart() }
                    ToolTip.visible: srH.hovered; ToolTip.text: i18n.t("SOFT RESTART (Ctrl+Shift+R): scan the sound cards and start the engine again without closing the console", i18n.lang); HoverHandler { id: srH } }
                Rectangle {   // WHAT IS CONNECTED: the output's kind (headphones, speakers, interface ...) and the input's, with the channel counts
                    readonly property var io: { engine.running; engine.devices; return engine.ioInfo() }
                    visible: engine.running; implicitWidth: ioT.implicitWidth + 18; implicitHeight: 24; radius: 12; color: Theme.ov(0.06); border.color: Theme.lineSoft
                    Text { id: ioT; anchors.centerIn: parent; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true
                        text: Theme.kindIcon(parent.io.outKind) + " " + i18n.t(Theme.kindName(parent.io.outKind), i18n.lang) + " · " + (parent.io.outputs || 0) + " " + i18n.t("OUT", i18n.lang) + (parent.io.inputs > 0 ? "   " + Theme.kindIcon(parent.io.inKind) + " " + i18n.t(Theme.kindName(parent.io.inKind), i18n.lang) + " · " + parent.io.inputs + " " + i18n.t("IN", i18n.lang) : "   ⚠ " + i18n.t("NO INPUT", i18n.lang)) }
                    ToolTip.visible: ioH.hovered; ToolTip.text: (parent ? "" : "") + i18n.t("OUTPUT", i18n.lang) + ": " + (io.outName || "—") + "\n" + i18n.t("INPUT", i18n.lang) + ": " + (io.inName || "—") + "\n" + (io.blockMs ? io.blockMs.toFixed(1) + " ms / block" : ""); HoverHandler { id: ioH } }
                Text { text: i18n.t("MASTER ", i18n.lang) + Theme.dbText(engine.telv("masterPeakDb", engine.meterTick)) + i18n.t(" dBFS", i18n.lang); color: engine.telv("masterClip", engine.meterTick) ? Theme.danger : Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Rectangle {   // UPDATES: a NEW badge when the signed feature feed has entries newer than what was last looked at; click = what's new
                    visible: updater.newFeatures > 0 || updater.state === "available"; implicitWidth: newTxt.implicitWidth + 16; implicitHeight: 22; radius: 11; color: Theme.orange
                    Text { id: newTxt; anchors.centerIn: parent; text: (updater.state === "available" ? i18n.t("UPDATE ", i18n.lang) + updater.latest : i18n.t("NEW", i18n.lang)) + (updater.newFeatures > 0 ? " · " + updater.newFeatures : ""); color: "#000"; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                    MouseArea { anchors.fill: parent; onClicked: whatsNew.open() } }
                Text { text: engine.edition.toUpperCase(); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                Rectangle { implicitWidth: 26; implicitHeight: 26; radius: 13; color: Theme.cardRaised; border.color: Theme.line   // THEME: dark / light
                    Text { anchors.centerIn: parent; text: settings.darkTheme ? "☾" : "☀"; color: settings.darkTheme ? Theme.amber : Theme.orange; font.pixelSize: 13 }
                    MouseArea { anchors.fill: parent; onClicked: settings.darkTheme = !settings.darkTheme }
                    ToolTip.visible: thH.hovered; ToolTip.text: settings.darkTheme ? i18n.t("Dark look (stage). Click for the light look.", i18n.lang) : i18n.t("Light look (daylight). Click for the dark look.", i18n.lang); HoverHandler { id: thH } }
                ComboBox {   // LANGUAGE: 80 languages; "auto" marks a machine translation
                    id: langBox; implicitWidth: 150; implicitHeight: 26; font.family: Theme.mono; font.pixelSize: 9
                    background: Rectangle { radius: 5; color: Theme.cardRaised; border.color: langBox.activeFocus ? Theme.amber : Theme.line }   // explicit: never the style's own light box
                    contentItem: Text { leftPadding: 8; rightPadding: 24; text: langBox.displayText; color: Theme.ink; font: langBox.font; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                    model: i18n.languages; textRole: "name"
                    displayText: (i18n.languages.find(function (l) { return l.code === i18n.lang }) || { name: "English" }).name + (i18n.autoTranslated ? " (auto)" : "")
                    delegate: ItemDelegate { width: parent.width; text: i18n.t(modelData.name ?? "", i18n.lang) + (modelData.auto ? i18n.t("  · auto", i18n.lang) : ""); font.family: Theme.mono; font.pixelSize: 10 }
                    Component.onCompleted: currentIndex = i18n.languages.findIndex(function (l) { return l.code === i18n.lang })
                    onActivated: i18n.lang = i18n.languages[currentIndex].code
                }
            }
        }
        // ALERT: no sound card remembered and the engine idle (the AUDIO step of the factory setup was skipped): say so, lead to SETUP / CONNECTIONS
        Rectangle { Layout.fillWidth: true; implicitHeight: 30; visible: settings.setupDone && !engine.running && !(settings.lastDevice && settings.lastDevice.backend); color: Qt.rgba(0.984, 0.749, 0.141, 0.14); border.color: Theme.amber
            RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 10
                Text { text: "⚠"; color: Theme.amber; font.pixelSize: 13 }
                Text { Layout.fillWidth: true; text: i18n.t("No sound card is set up yet: the faders show no input or output until one is. Run the FACTORY SETUP (AUDIO step) or pick a device on CONNECTIONS.", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideRight }
                Rectangle { implicitWidth: Math.max(110, children[0].implicitWidth + 12); implicitHeight: 20; radius: 4; color: Theme.amber; Text { anchors.centerIn: parent; text: i18n.t("FACTORY SETUP", i18n.lang); color: "#000"; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: root.forceSetup = true } }
                Rectangle { implicitWidth: Math.max(100, children[0].implicitWidth + 12); implicitHeight: 20; radius: 4; color: Theme.ov(0.1); border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: i18n.t("CONNECTIONS", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: root.current = "io" } } } }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
            NavRail { Layout.fillHeight: true; pages: engine.pages; current: root.current; onPicked: function (id) { root.current = id } }
            Item {
                Layout.fillWidth: true; Layout.fillHeight: true
                Loader {
                    id: pageLoader
                    anchors.fill: parent
                    sourceComponent: {
                        switch (root.current) {
                        case "home": return homeC; case "frame": return frameC; case "delay": return delayC; case "cloud": return cloudC; case "cascade": return cascadeC; case "mixer": return mixerC; case "dsp": return toneC; case "rtaeq": return rtaEqC; case "bluetooth": return bluetoothC; case "io": return connC;
                        case "meter": return levelsC; case "lufs": return loudC; case "sys": return settingsC; case "dyn": return compC; case "gate": return gateC; case "bus": return sendC; case "dca": return dcaC; case "scenes": return scenesC; case "presets": return presetsC; case "routing": return routingC; case "renew": return licenseC; case "arrange": return arrangeC; case "fx": return fxC; case "osc": return oscC; case "phase": return phaseC; case "miceq": return miceqC; case "live": return liveC; case "tools": return toolsC; case "calib": return calibC; case "boot": return bootC; case "help": return helpC; case "acct": return acctC; case "access": return accessC; case "admin": return adminC; case "ai": return aiC; case "engctl": return engctlC; case "plugins": return pluginsC; case "network": return networkC; case "media": return mediaC; case "samples": return samplesC; case "asio": return soundC; case "acoustics": return acousticsC; case "master": return masteringC; case "surround": return spatialC; default: return comingC
                        }
                    }
                }
                Component { id: frameC; FramePage {} }
                Component { id: homeC; HomePage { onGo: function (id) { root.current = id } } }
                Component { id: mixerC; MixerPage { onGo: function (id) { root.current = id } } }
                Component { id: toneC; TonePage {} }
                Component { id: rtaEqC; RtaEqPage {} }
                Component { id: bluetoothC; BluetoothPage {} }
                Component { id: connC; ConnectionsPage {} }
                Component { id: levelsC; LevelsPage {} }
                Component { id: loudC; LoudnessPage {} }
                Component { id: settingsC; SettingsPage {} }
                Component { id: compC; CompressorPage {} }
                Component { id: gateC; GatePage {} }
                Component { id: sendC; SendToPage {} }
                Component { id: dcaC; GroupFadersPage {} }
                Component { id: scenesC; ScenesPage {} }
                Component { id: presetsC; PresetsPage {} }
                Component { id: routingC; RoutingPage {} }
                Component { id: licenseC; LicensePage {} }
                Component { id: arrangeC; ArrangePage {} }
                Component { id: fxC; EffectsPage {} }
                Component { id: oscC; WaveformPage {} }
                Component { id: phaseC; PhasePage {} }
                Component { id: miceqC; MicEqPage {} }
                Component { id: liveC; LiveSourcesPage {} }
                Component { id: toolsC; ToolsPage {} }
                Component { id: calibC; CalibratePage {} }
                Component { id: bootC; DiagnosticsPage {} }
                Component { id: helpC; HelpPage { onGo: function (id) { root.current = id } } }
                Component { id: acctC; AccountPage {} }
                Component { id: accessC; AccessPage {} }
                Component { id: adminC; AdminPage {} }
                Component { id: aiC; AiMixPage {} }
                Component { id: engctlC; EngineControlPage {} }
                Component { id: delayC; DelayPage {} }
                Component { id: cloudC; CloudPage {} }
                Component { id: cascadeC; CascadePage {} }
                Component { id: pluginsC; PluginsPage {} }
                Component { id: networkC; NetworkPage {} }
                Component { id: mediaC; MediaPage {} }
                Component { id: samplesC; SamplesPage {} }
                Component { id: soundC; SoundCardPage {} }
                Component { id: acousticsC; AcousticsPage {} }
                Component { id: masteringC; MasteringPage {} }
                Component { id: spatialC; SpatialPage {} }
                Component { id: comingC; ComingPage { page: root.pageOf(root.current) } }
            }
        }
    }
    function say(text) { sayText.text = text; sayPop.open() }
    function openEnhancer() { enhancer.open() }
    function openAudioSettings() { audioSettings.open() }
    // Presses every entry of FILE, VIEW and INFO (not Quit, Soft restart or Full screen, which would end or reshape the test) and says which did not do its job.
    function menuSelfTest() {
        const bad = [], keepPage = root.current, pages = { "Plug-ins": "plugins", "Sound card": "asio", "Effects": "fx", "Mastering": "master", "Samples": "samples", "Settings": "sys", "Scenes": "scenes", "Presets": "presets", "Media files": "media", "Mixer": "mixer", "Meters": "meter", "Routing": "routing", "Connections": "io", "Network": "network", "Acoustics": "acoustics", "Help": "help", "Licence and account": "acct" }
        let pressed = 0
        const pops = [sayPop, aboutPop, whatsNew, enhancer, analyzer], shut = function () { for (const p of pops) p.close() }
        for (const m of [fileMenu, editMenu, viewMenu, toolsMenu, infoMenu]) for (let i = 0; i < m.count; ++i) {
            const it = m.itemAt(i); if (!it || !it.text) continue
            const t = it.text; if (t.indexOf("Soft restart") === 0 || t.indexOf("ull screen") > 0) continue
            ++pressed; shut()
            if (pages[t] !== undefined) { root.current = "home"; it.triggered(); if (root.current !== pages[t]) bad.push(t + " -> " + root.current) }
            else if (t === "Light look" || t === "Dark look") { const d = settings.darkTheme; it.triggered(); if (settings.darkTheme === d) bad.push(t); settings.darkTheme = d }
            else if (t.indexOf("Tooltips") >= 0) { const d = settings.tooltips; it.triggered(); if (settings.tooltips === d) bad.push(t); settings.tooltips = d }
            else if (t === "Volume Enhancer") { it.triggered(); if (!enhancer.opened && !enhancer.visible) bad.push(t) }
            else if (t === "Master Analyzer") { it.triggered(); if (!analyzer.opened && !analyzer.visible) bad.push(t) }
            else if (t === "Mute all channels") { it.triggered(); if (!engine.stripAt(3, engine.stripTick).mute) bad.push(t) }
            else if (t === "Unmute all channels") { it.triggered(); if (engine.stripAt(3, engine.stripTick).mute) bad.push(t) }
            else if (t === "Undo" || t === "Redo" || t === "Compare A / B" || t === "Clear solo" || t === "Clear clip lamps") { it.triggered() }   // must run without an error; their effect is checked in the engine's own tests
            else if (t === "About this console") { it.triggered(); if (!aboutPop.opened && !aboutPop.visible) bad.push(t) }
            else if (t === "What's new") { it.triggered(); if (!whatsNew.opened && !whatsNew.visible) bad.push(t) }
            else if (t === "Save the console to a file") { sayText.text = ""; it.triggered(); if (sayText.text.indexOf("Saved: ") !== 0) bad.push(t + ": " + sayText.text) }
            else if (t === "Load the console from that file") { sayText.text = ""; it.triggered(); if (sayText.text.indexOf("was loaded") < 0) bad.push(t + ": " + sayText.text) }
            else if (t === "Sound card in use") { sayText.text = ""; it.triggered(); if (sayText.text === "") bad.push(t) }
            else bad.push("not covered: " + t)
        }
        shut(); root.current = keepPage
        if (pressed < 33) bad.push("only " + pressed + " menu entries found")
        return bad
    }
    function openAnalyzer() { analyzer.open() }
    MasterAnalyzer { id: analyzer }   // AMFB MASTER ANALYZER: opened from the master strip's ANALYZER button
    AudioSettings { id: audioSettings }   // AUDIO / MIDI SETTINGS: opened from the SOUND CARD page
    VolumeEnhancer { id: enhancer }   // AMFB VOLUME ENHANCER: opened from the master strip's ENHANCER button
    Popup { id: sayPop; objectName: "sayPop"; modal: true; anchors.centerIn: parent; width: Math.min(560, root.width - 80); padding: 18
        background: Rectangle { radius: 10; color: Theme.card; border.color: Theme.line }
        contentItem: Column { spacing: 12
            Text { id: sayText; width: parent.width; wrapMode: Text.WordWrap; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10 }
            Rectangle { width: 70; height: 24; radius: 4; color: Theme.amber; Text { anchors.centerIn: parent; text: i18n.t("OK", i18n.lang); color: "#000"; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: sayPop.close() } } } }
    Popup { id: aboutPop; objectName: "aboutPop"; modal: true; anchors.centerIn: parent; width: Math.min(520, root.width - 80); padding: 22
        readonly property var io: { engine.running; return engine.ioInfo() }
        background: Rectangle { radius: 10; color: Theme.card; border.color: Theme.line }
        contentItem: Column { spacing: 8
            Text { text: Theme.product.toUpperCase(); color: Theme.orange; font.family: Theme.mono; font.pixelSize: 20; font.weight: Font.Black; font.letterSpacing: 5 }
            Text { text: i18n.t("BY AMFB AUDIO", i18n.lang) + "  ·  " + i18n.t("version ", i18n.lang) + appVersion; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 10 }
            Rectangle { width: parent.width; height: 1; color: Theme.lineSoft }
            Text { width: parent.width; wrapMode: Text.WordWrap; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9
                text: i18n.t("Engine: ", i18n.lang) + (engine.running ? engine.status : i18n.t("off", i18n.lang)) }
            Text { visible: engine.running; width: parent.width; wrapMode: Text.WordWrap; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9
                text: i18n.t("Output: ", i18n.lang) + (aboutPop.io.outName || "—") + "\n" + i18n.t("Input: ", i18n.lang) + (aboutPop.io.inName || "—") }
            Text { width: parent.width; wrapMode: Text.WordWrap; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8
                text: i18n.t("ASIO is a trademark and software of Steinberg Media Technologies GmbH. VST is a trademark of Steinberg Media Technologies GmbH. Third-party licences are in the licenses folder next to the program.", i18n.lang) }
            Rectangle { width: 70; height: 24; radius: 4; color: Theme.amber; Text { anchors.centerIn: parent; text: i18n.t("OK", i18n.lang); color: "#000"; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: aboutPop.close() } } } }
    Popup {   // WHAT'S NEW: the feature feed from the signed manifest, newest first
        id: whatsNew; modal: true; anchors.centerIn: parent; width: Math.min(640, root.width - 80); height: Math.min(520, root.height - 80); padding: 18
        background: Rectangle { radius: 12; color: Theme.card; border.color: Theme.line }
        onOpened: updater.markFeaturesSeen()
        ColumnLayout { anchors.fill: parent; spacing: 10
            RowLayout { Text { text: i18n.t("WHAT'S NEW", i18n.lang); color: Theme.orange; font.family: Theme.mono; font.pixelSize: 12; font.bold: true; font.letterSpacing: 2 } Item { Layout.fillWidth: true }
                Text { text: i18n.t("THIS VERSION ", i18n.lang) + updater.current; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 } }
            Text { visible: updater.state === "available"; text: updater.message; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Flickable { Layout.fillWidth: true; Layout.fillHeight: true; contentHeight: feedCol.implicitHeight; clip: true
                ColumnLayout { id: feedCol; width: parent.width; spacing: 8
                    Text { visible: updater.features.length === 0; text: i18n.t("No feature feed yet: CHECK NOW on SETTINGS reads the signed manifest.", i18n.lang); color: Theme.muted; font.family: Theme.sans; font.pixelSize: 11 }
                    Repeater { model: updater.features; delegate: ColumnLayout { Layout.fillWidth: true; spacing: 3
                        RowLayout { Text { text: modelData.version; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true } Text { text: modelData.date + "  " + i18n.t(modelData.title ?? "", i18n.lang); color: Theme.muted; font.family: Theme.sans; font.pixelSize: 11 } }
                        Repeater { model: modelData.items; delegate: Text { Layout.fillWidth: true; text: "• " + modelData; color: Theme.ink; font.family: Theme.sans; font.pixelSize: 11; wrapMode: Text.WordWrap } } } } } }
            RowLayout { Item { Layout.fillWidth: true }
                Text { visible: updater.state === "downloading"; text: i18n.t("DOWNLOADING ", i18n.lang) + updater.progress + " %"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                Rectangle { visible: updater.canInstall; implicitWidth: instT.implicitWidth + 24; implicitHeight: 24; radius: 5; color: Theme.emerald; Text { id: instT; anchors.centerIn: parent; text: i18n.t("INSTALL AND RESTART", i18n.lang); color: "#000"; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: updater.installAndRestart() } }
                Rectangle { visible: updater.state === "downloaded" && !updater.canInstall; implicitWidth: 150; implicitHeight: 24; radius: 5; color: Theme.ov(0.1); border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: i18n.t("OPEN DOWNLOAD FOLDER", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: updater.openDownloadFolder() } }
                Rectangle { visible: updater.state === "available" && updater.file.url !== undefined; implicitWidth: 120; implicitHeight: 24; radius: 5; color: Theme.amber; Text { anchors.centerIn: parent; text: i18n.t("DOWNLOAD", i18n.lang); color: "#000"; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: updater.download() } }
                Rectangle { implicitWidth: Math.max(80, children[0].implicitWidth + 12); implicitHeight: 24; radius: 5; color: Theme.ov(0.1); border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: i18n.t("CLOSE", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: whatsNew.close() } } } } }
    property bool bootOn: settings.bootAnimation && !noBoot   // noBoot: the smoke / shots run without it
    Loader { anchors.fill: parent; z: 200; active: root.bootOn; sourceComponent: BootSplash { onDone: root.bootOn = false } }
    Rectangle {   // UPDATING: the console is about to close; the setup installs and starts it again (like a mixer's firmware update)
        anchors.fill: parent; z: 300; visible: updater.state === "installing"; color: Theme.obsidian
        MouseArea { anchors.fill: parent }
        Column { anchors.centerIn: parent; spacing: 16
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: Theme.product.toUpperCase(); color: Theme.orange; font.family: Theme.mono; font.pixelSize: 40; font.weight: Font.Black; font.letterSpacing: 6 }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: i18n.t("UPDATING TO ", i18n.lang) + updater.latest; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 16; font.bold: true; font.letterSpacing: 3 }
            Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 320; height: 4; radius: 2; color: Theme.ov(0.12); clip: true
                Rectangle { width: 90; height: 4; radius: 2; color: Theme.orange; NumberAnimation on x { from: -90; to: 320; duration: 1100; loops: Animation.Infinite; running: updater.state === "installing" } } }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: i18n.t("The console closes, installs and starts again by itself. Do not switch the computer off.", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 10 } } }
    Rectangle {   // back from an update: one line at the bottom until it is clicked
        z: 150; visible: updater.justUpdated !== "" || updater.failedUpdate !== ""; anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottomMargin: 18; width: updT.implicitWidth + 60; height: 34; radius: 17
        color: updater.failedUpdate !== "" ? Theme.danger : Theme.emerald
        Text { id: updT; anchors.centerIn: parent; color: "#000"; font.family: Theme.mono; font.pixelSize: 10; font.bold: true
            text: updater.failedUpdate !== "" ? i18n.t("The update to ", i18n.lang) + updater.failedUpdate + i18n.t(" did not complete: this is still ", i18n.lang) + updater.current : i18n.t("UPDATED TO ", i18n.lang) + updater.justUpdated + "  ·  " + i18n.t("click to see what is new", i18n.lang) }
        MouseArea { anchors.fill: parent; onClicked: { const ok = updater.failedUpdate === ""; updater.acknowledgeUpdate(); if (ok) whatsNew.open() } } }
    SetupPage { anchors.fill: parent; z: 100; visible: root.forceSetup || !settings.setupDone; onFinished: { settings.finishSetup(); root.forceSetup = false; root.current = settings.startPage } }
}
