// LIVE SOURCES: the audio interfaces the machine has right now (auto-scan), READ one into the console (SMART START through the driver ladder:
// ASIO first, then WASAPI / Core Audio / ALSA), the live 1/3-octave spectra of the master (MUSIC) and of one strip (MIC), and the patch of
// device inputs onto strips (AUTO-PATCH = input n -> CH n, UNPATCH = every strip silent). The browser build's NOW PLAYING (Spotify, YouTube,
// a shared tab) has no counterpart here: this build reads sound cards, not web pages.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "liveSourcesPage"
    property bool autoScan: true
    property bool asioOnly: false
    property int micStrip: engine.selectedStrip
    property var specMusic: []
    property var specMic: []
    readonly property var last: settings.lastDevice
    function isOpen(d) { if (!engine.running || engine.backend !== d.backend) return false; if (!page.last || (!page.last.in && !page.last.out)) return true; return page.last.in === d.id || page.last.out === d.id }   // without a remembered device, the open backend's entries count as reading
    function read(d) {
        const inCh = Math.min(32, Math.max(0, d.inChannels)), outCh = Math.min(2, Math.max(0, d.outChannels))
        const outId = d.outChannels > 0 ? d.id : (page.last && page.last.out ? page.last.out : "")
        const ok = engine.openSmart(d.backend, d.inChannels > 0 ? d.id : "", outId, d.defaultRate > 0 ? d.defaultRate : settings.defaultRate, settings.defaultFrames, inCh > 0 ? inCh : 2, outCh > 0 ? outCh : 2)
        if (ok) settings.rememberDevice(d.backend, d.inChannels > 0 ? d.id : "", outId, d.defaultRate > 0 ? d.defaultRate : settings.defaultRate, settings.defaultFrames, inCh > 0 ? inCh : 2, outCh > 0 ? outCh : 2)
    }
    Timer { interval: 5000; running: page.visible && page.autoScan; repeat: true; onTriggered: engine.refreshDevices() }
    Timer { interval: 60; running: page.visible; repeat: true; onTriggered: { engine.setScopeStrip(page.micStrip); page.specMusic = engine.spectrum(0); page.specMic = engine.spectrum(2); musicC.requestPaint(); micC.requestPaint() } }
    component Spectrum: Canvas {
        property var data: []
        property color col: Theme.cyan
        onPaint: {
            const ctx = getContext("2d"); const w = width, h = height; ctx.clearRect(0, 0, w, h)
            ctx.strokeStyle = Theme.ov(0.07); for (let j = 0; j <= 6; ++j) { const y = Math.round(h * j / 6) + 0.5; ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke() }
            const sp = data; if (!sp || sp.length !== 31) return; const bw = w / 31; ctx.fillStyle = col
            for (let b = 0; b < 31; ++b) { const db = Math.max(-72, Math.min(0, sp[b])); const bh = (db + 72) / 72 * h; ctx.globalAlpha = 0.55; ctx.fillRect(b * bw + 1, h - bh, bw - 2, bh); ctx.globalAlpha = 1; ctx.fillRect(b * bw + 1, h - bh, bw - 2, 2) }
        }
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Text { text: i18n.t("LIVE SOURCES:", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Text { text: engine.devices.length + i18n.t(" interfaces", i18n.lang) + (engine.running ? i18n.t(" · reading ", i18n.lang) + engine.backend : i18n.t(" · nothing open", i18n.lang)); color: engine.running ? Theme.emerald : Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Item { Layout.fillWidth: true }
                Rectangle { implicitWidth: Math.max(70, children[0].implicitWidth + 12); implicitHeight: 20; radius: 4; color: Theme.ov(0.08); border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: i18n.t("SCAN NOW", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.refreshDevices() } }
                Rectangle { implicitWidth: Math.max(90, children[0].implicitWidth + 12); implicitHeight: 20; radius: 4; color: page.autoScan ? Theme.cyan : Theme.ov(0.06); Text { anchors.centerIn: parent; text: i18n.t("SCAN EVERY 5 S", i18n.lang); color: page.autoScan ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.autoScan = !page.autoScan } }
                Rectangle { implicitWidth: Math.max(66, children[0].implicitWidth + 12); implicitHeight: 20; radius: 4; color: page.asioOnly ? Theme.amber : Theme.ov(0.06); Text { anchors.centerIn: parent; text: i18n.t("ASIO ONLY", i18n.lang); color: page.asioOnly ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.asioOnly = !page.asioOnly } }
                Rectangle { implicitWidth: Math.max(150, children[0].implicitWidth + 12); implicitHeight: 20; radius: 4; color: settings.autoAsio ? Theme.emerald : Theme.ov(0.06); Text { anchors.centerIn: parent; text: i18n.t("AUTO-START BEST INTERFACE", i18n.lang); color: settings.autoAsio ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } MouseArea { anchors.fill: parent; onClicked: settings.autoAsio = !settings.autoAsio } }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: 600; Layout.minimumWidth: 320; radius: Theme.radius; color: Theme.card; border.color: Theme.line; clip: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 12; spacing: 6
                    Text { text: i18n.t("AUDIO INTERFACES (AUTO-SCAN)", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
                    Flickable { Layout.fillWidth: true; Layout.fillHeight: true; contentHeight: devCol.implicitHeight; clip: true; boundsBehavior: Flickable.StopAtBounds
                        ColumnLayout { id: devCol; width: parent.width; spacing: 4
                            Repeater { model: engine.devices
                                delegate: Rectangle { visible: !page.asioOnly || modelData.backend === "asio"; Layout.fillWidth: true; implicitHeight: visible ? 44 : 0; radius: 6; color: page.isOpen(modelData) ? Qt.rgba(0.063, 0.725, 0.506, 0.14) : Qt.rgba(0, 0, 0, 0.3); border.color: page.isOpen(modelData) ? Theme.emerald : Theme.lineSoft
                                    RowLayout { anchors.fill: parent; anchors.margins: 8; spacing: 8
                                        Rectangle { implicitWidth: 52; implicitHeight: 18; radius: 3; color: modelData.backend === "asio" ? Theme.amber : Theme.ov(0.1); Text { anchors.centerIn: parent; text: modelData.backend.toUpperCase(); color: modelData.backend === "asio" ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } }
                                        ColumnLayout { Layout.fillWidth: true; spacing: 1
                                            Text { text: i18n.t(modelData.name ?? "", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                            Text { text: modelData.inChannels + i18n.t(" in · ", i18n.lang) + modelData.outChannels + i18n.t(" out · ", i18n.lang) + modelData.defaultRate + i18n.t(" Hz", i18n.lang) + (modelData.defaultIn ? i18n.t(" · default input", i18n.lang) : "") + (modelData.defaultOut ? i18n.t(" · default output", i18n.lang) : ""); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7 } }
                                        Text { visible: page.isOpen(modelData); text: i18n.t("READING", i18n.lang); color: Theme.emerald; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                                        Rectangle { implicitWidth: 56; implicitHeight: 22; radius: 4; color: page.isOpen(modelData) ? Theme.ov(0.08) : Theme.cyan; Text { anchors.centerIn: parent; text: page.isOpen(modelData) ? i18n.t("STOP", i18n.lang) : i18n.t("READ", i18n.lang); color: page.isOpen(modelData) ? Theme.muted : "#000"; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.isOpen(modelData) ? engine.stop() : page.read(modelData) } } } } }
                            Text { visible: engine.devices.length === 0; text: i18n.t("No audio interface found. Plug one in: the list refreshes every 5 s.", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8 }
                        } }
                    Text { Layout.fillWidth: true; text: i18n.t("READ opens the interface through the driver ladder (ASIO first; a busy ASIO driver falls back to WASAPI / Core Audio / ALSA) and the master mix plays on its outputs. One ASIO device is open at a time. ", i18n.lang) + (engine.lastRoute.length ? engine.lastRoute : ""); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.lineSoft }
                    Text { text: i18n.t("VIRTUAL MIXER PATCH", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
                    RowLayout { spacing: 6
                        Rectangle { implicitWidth: Math.max(150, children[0].implicitWidth + 12); implicitHeight: 24; radius: 4; color: Theme.cyan; Text { anchors.centerIn: parent; text: i18n.t("AUTO-PATCH LIVE SOURCES", i18n.lang); color: "#000"; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.resetRouting() } }
                        Rectangle { implicitWidth: Math.max(80, children[0].implicitWidth + 12); implicitHeight: 24; radius: 4; color: Theme.ov(0.08); border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: i18n.t("UNPATCH", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { for (let s = 0; s < engine.stripState.length; ++s) engine.setInputRoute(s, -1) } } }
                        Text { text: i18n.t("device input n → CH n; the full matrix is on ROUTING. Use headphones with microphones.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
                }
            }
            ColumnLayout {
                Layout.preferredWidth: 380; Layout.maximumWidth: 420; Layout.fillWidth: false; Layout.fillHeight: true; spacing: 8
                Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.chrome; border.color: Theme.line; clip: true
                    Spectrum { id: musicC; anchors.fill: parent; anchors.margins: 8; data: page.specMusic; col: Theme.orange }
                    Text { anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 10; text: i18n.t("FFT SPECTRUM: MUSIC (master L, 1/3-octave)", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } }
                Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.chrome; border.color: Theme.line; clip: true
                    Spectrum { id: micC; anchors.fill: parent; anchors.margins: 8; data: page.specMic; col: Theme.cyan }
                    RowLayout { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 10; spacing: 6
                        Text { text: i18n.t("FFT SPECTRUM: MIC / INTERFACE  CH ", i18n.lang) + (page.micStrip + 1); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } Item { Layout.fillWidth: true }
                        Rectangle { implicitWidth: 18; implicitHeight: 16; radius: 3; color: Theme.ov(0.08); Text { anchors.centerIn: parent; text: "‹"; color: Theme.ink; font.pixelSize: 10 } MouseArea { anchors.fill: parent; onClicked: page.micStrip = Math.max(0, page.micStrip - 1) } }
                        Rectangle { implicitWidth: 18; implicitHeight: 16; radius: 3; color: Theme.ov(0.08); Text { anchors.centerIn: parent; text: "›"; color: Theme.ink; font.pixelSize: 10 } MouseArea { anchors.fill: parent; onClicked: page.micStrip = Math.min(engine.stripState.length - 1, page.micStrip + 1) } } } }
            }
        }
    }
}
