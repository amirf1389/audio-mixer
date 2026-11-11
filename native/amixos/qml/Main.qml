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
                Item { Layout.fillWidth: true }
                Rectangle { implicitWidth: statusText.implicitWidth + 20; implicitHeight: 24; radius: 12; color: engine.running ? Qt.rgba(0.063, 0.725, 0.506, 0.15) : Qt.rgba(0.984, 0.749, 0.141, 0.12); border.color: engine.running ? Theme.emerald : Theme.amber
                    Text { id: statusText; anchors.centerIn: parent; text: engine.running ? i18n.t("ENGINE ON • ", i18n.lang) + engine.backend.toUpperCase() : i18n.t("ENGINE OFF", i18n.lang); color: engine.running ? Theme.emerald : Theme.amber; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } }
                Rectangle {   // WHAT IS CONNECTED: the output's kind (headphones, speakers, interface ...) and the input's, with the channel counts
                    readonly property var io: { engine.running; engine.devices; return engine.ioInfo() }
                    visible: engine.running; implicitWidth: ioT.implicitWidth + 18; implicitHeight: 24; radius: 12; color: Theme.ov(0.06); border.color: Theme.lineSoft
                    Text { id: ioT; anchors.centerIn: parent; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true
                        text: Theme.kindIcon(parent.io.outKind) + " " + i18n.t(Theme.kindName(parent.io.outKind), i18n.lang) + " · " + (parent.io.outputs || 0) + " " + i18n.t("OUT", i18n.lang) + (parent.io.inputs > 0 ? "   " + Theme.kindIcon(parent.io.inKind) + " " + i18n.t(Theme.kindName(parent.io.inKind), i18n.lang) + " · " + parent.io.inputs + " " + i18n.t("IN", i18n.lang) : "   ⚠ " + i18n.t("NO INPUT", i18n.lang)) }
                    ToolTip.visible: ioH.hovered; ToolTip.text: (parent ? "" : "") + i18n.t("OUTPUT", i18n.lang) + ": " + (io.outName || "—") + "\n" + i18n.t("INPUT", i18n.lang) + ": " + (io.inName || "—") + "\n" + (io.blockMs ? io.blockMs.toFixed(1) + " ms / block" : ""); HoverHandler { id: ioH } }
                Text { text: i18n.t("MASTER ", i18n.lang) + Theme.dbText(engine.telemetry.masterPeakDb) + i18n.t(" dBFS", i18n.lang); color: engine.telemetry.masterClip ? Theme.danger : Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
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
                    delegate: ItemDelegate { width: parent.width; text: modelData.name + (modelData.auto ? i18n.t("  · auto", i18n.lang) : ""); font.family: Theme.mono; font.pixelSize: 10 }
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
                Rectangle { implicitWidth: 110; implicitHeight: 20; radius: 4; color: Theme.amber; Text { anchors.centerIn: parent; text: i18n.t("FACTORY SETUP", i18n.lang); color: "#000"; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: root.forceSetup = true } }
                Rectangle { implicitWidth: 100; implicitHeight: 20; radius: 4; color: Theme.ov(0.1); border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: i18n.t("CONNECTIONS", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: root.current = "io" } } } }
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
                        case "home": return homeC; case "mixer": return mixerC; case "dsp": return toneC; case "rtaeq": return rtaEqC; case "bluetooth": return bluetoothC; case "io": return connC;
                        case "meter": return levelsC; case "lufs": return loudC; case "sys": return settingsC; case "dyn": return compC; case "gate": return gateC; case "bus": return sendC; case "dca": return dcaC; case "scenes": return scenesC; case "presets": return presetsC; case "routing": return routingC; case "renew": return licenseC; case "arrange": return arrangeC; case "fx": return fxC; case "osc": return oscC; case "phase": return phaseC; case "miceq": return miceqC; case "live": return liveC; case "tools": return toolsC; case "calib": return calibC; case "boot": return bootC; case "help": return helpC; case "acct": return acctC; case "access": return accessC; case "admin": return adminC; case "ai": return aiC; case "plugins": return pluginsC; case "network": return networkC; case "media": return mediaC; case "samples": return samplesC; case "asio": return soundC; case "acoustics": return acousticsC; case "master": return masteringC; case "surround": return spatialC; default: return comingC
                        }
                    }
                }
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
                        RowLayout { Text { text: modelData.version; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true } Text { text: modelData.date + "  " + modelData.title; color: Theme.muted; font.family: Theme.sans; font.pixelSize: 11 } }
                        Repeater { model: modelData.items; delegate: Text { Layout.fillWidth: true; text: "• " + modelData; color: Theme.ink; font.family: Theme.sans; font.pixelSize: 11; wrapMode: Text.WordWrap } } } } } }
            RowLayout { Item { Layout.fillWidth: true }
                Rectangle { visible: updater.state === "available" && updater.file.url !== undefined; implicitWidth: 120; implicitHeight: 24; radius: 5; color: Theme.amber; Text { anchors.centerIn: parent; text: i18n.t("DOWNLOAD", i18n.lang); color: "#000"; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: updater.download() } }
                Rectangle { implicitWidth: 80; implicitHeight: 24; radius: 5; color: Theme.ov(0.1); border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: i18n.t("CLOSE", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: whatsNew.close() } } } } }
    property bool bootOn: settings.bootAnimation && !noBoot   // noBoot: the smoke / shots run without it
    Loader { anchors.fill: parent; z: 200; active: root.bootOn; sourceComponent: BootSplash { onDone: root.bootOn = false } }
    SetupPage { anchors.fill: parent; z: 100; visible: root.forceSetup || !settings.setupDone; onFinished: { settings.finishSetup(); root.forceSetup = false; root.current = settings.startPage } }
}
