// CONNECTIONS: the sound cards the engine can open (ASIO / WASAPI / Core Audio / ALSA / null), inputs, outputs, rate, buffer; START / STOP
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Flickable {
    id: page
    property string backend: engine.backends.length ? engine.backends[0] : "null"
    property string inId: ""
    property string outId: ""
    property int rate: settings.defaultRate
    property int frames: settings.defaultFrames
    property var planModel: []
    onInIdChanged: planModel = engine.driverPlan(backend, inId, outId)
    onOutIdChanged: planModel = engine.driverPlan(backend, inId, outId)
    contentHeight: col.implicitHeight + 24
    clip: true
    function devs(dir) { var l = []; for (var i = 0; i < engine.devices.length; i++) { var d = engine.devices[i]; if (d.backend === page.backend && (dir === "in" ? d.inChannels > 0 : d.outChannels > 0)) l.push(d) } return l }
    function dev(id) { for (var i = 0; i < engine.devices.length; i++) if (engine.devices[i].id === id && engine.devices[i].backend === page.backend) return engine.devices[i]; return null }
    ColumnLayout {
        id: col
        width: parent.width - 20; x: 10; y: 10; spacing: 10
        Panel {
            Layout.fillWidth: true
            title: i18n.t("CONNECTIONS • SOUND CARD", i18n.lang)
            subtitle: i18n.t("The engine opens the card directly: ASIO (the device's own driver) and WASAPI on Windows, Core Audio on macOS, ALSA on Linux. \"null\" is a timed test device with no hardware.", i18n.lang)
            RowLayout {
                Layout.fillWidth: true; spacing: 6
                Text { text: i18n.t("DRIVER", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 }
                Repeater { model: engine.backends; delegate: Rectangle { id: gb24; implicitWidth: lbl.implicitWidth + 18; implicitHeight: 24; radius: 7; color: page.backend === modelData ? Theme.amber : Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb24.color) } GradientStop { position: 1; color: Theme.btnBottom(gb24.color) } } Text { id: lbl; anchors.centerIn: parent; text: modelData.toUpperCase(); color: page.backend === modelData ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { page.backend = modelData; page.inId = ""; page.outId = "" } } } }
                Item { Layout.fillWidth: true }
                Rectangle { id: gb25; implicitWidth: Math.max(80, children[0].implicitWidth + 12); implicitHeight: 24; radius: 7; color: Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb25.color) } GradientStop { position: 1; color: Theme.btnBottom(gb25.color) } } Text { anchors.centerIn: parent; text: i18n.t("RESCAN", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.refreshDevices() } }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 10
                ColumnLayout { Layout.fillWidth: true; spacing: 4
                    Text { text: i18n.t("INPUT", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                    Repeater { model: page.devs("in"); delegate: Rectangle { Layout.fillWidth: true; implicitHeight: 30; radius: 4; opacity: modelData.enabled === false ? 0.45 : 1; color: page.inId === modelData.id ? Qt.rgba(0.235, 0.784, 0.859, 0.15) : Qt.rgba(0, 0, 0, 0.3); border.color: page.inId === modelData.id ? Theme.cyan : Theme.lineSoft
                        RowLayout { z: 2; anchors.fill: parent; anchors.margins: 6; Rectangle { objectName: "devOnOff"; implicitWidth: 34; implicitHeight: 16; radius: 8; color: modelData.enabled !== false ? Theme.emerald : Theme.ov(0.12); border.color: Theme.lineSoft; z: 5; Text { anchors.centerIn: parent; text: modelData.enabled !== false ? i18n.t("ON", i18n.lang) : i18n.t("OFF", i18n.lang); color: modelData.enabled !== false ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: engine.setDeviceEnabled(page.backend, modelData.id, modelData.enabled === false) } } Text { text: Theme.kindIcon(modelData.kind); color: Theme.cyan; font.pixelSize: 11 } Text { text: i18n.t(modelData.name ?? "", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; elide: Text.ElideRight; Layout.fillWidth: true } Text { text: i18n.t(Theme.kindName(modelData.kind), i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } Text { text: modelData.inChannels + i18n.t(" ch • ", i18n.lang) + modelData.defaultRate + i18n.t(" Hz", i18n.lang) + (modelData.defaultIn ? i18n.t(" • default", i18n.lang) : ""); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } }
                        MouseArea { anchors.fill: parent; onClicked: { page.inId = modelData.id; page.rate = modelData.defaultRate } } } }
                    Text { visible: page.devs("in").length === 0; text: i18n.t("No input of this driver", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9 } }
                ColumnLayout { Layout.fillWidth: true; spacing: 4
                    Text { text: i18n.t("OUTPUT", i18n.lang); color: Theme.emerald; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                    Repeater { model: page.devs("out"); delegate: Rectangle { Layout.fillWidth: true; implicitHeight: 30; radius: 4; opacity: modelData.enabled === false ? 0.45 : 1; color: page.outId === modelData.id ? Qt.rgba(0.063, 0.725, 0.506, 0.18) : Qt.rgba(0, 0, 0, 0.3); border.color: page.outId === modelData.id ? Theme.emerald : Theme.lineSoft
                        RowLayout { z: 2; anchors.fill: parent; anchors.margins: 6; Rectangle { objectName: "devOnOff"; implicitWidth: 34; implicitHeight: 16; radius: 8; color: modelData.enabled !== false ? Theme.emerald : Theme.ov(0.12); border.color: Theme.lineSoft; z: 5; Text { anchors.centerIn: parent; text: modelData.enabled !== false ? i18n.t("ON", i18n.lang) : i18n.t("OFF", i18n.lang); color: modelData.enabled !== false ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: engine.setDeviceEnabled(page.backend, modelData.id, modelData.enabled === false) } } Text { text: Theme.kindIcon(modelData.kind); color: Theme.cyan; font.pixelSize: 11 } Text { text: i18n.t(modelData.name ?? "", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; elide: Text.ElideRight; Layout.fillWidth: true } Text { text: i18n.t(Theme.kindName(modelData.kind), i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } Text { text: modelData.outChannels + i18n.t(" ch • ", i18n.lang) + modelData.defaultRate + i18n.t(" Hz", i18n.lang) + (modelData.defaultOut ? i18n.t(" • default", i18n.lang) : ""); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } }
                        MouseArea { anchors.fill: parent; onClicked: { page.outId = modelData.id; page.rate = modelData.defaultRate } } } }
                    Text { visible: page.devs("out").length === 0; text: i18n.t("No output of this driver", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9 } }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Text { text: i18n.t("RATE", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 }
                Repeater { model: [44100, 48000, 88200, 96000, 192000]; delegate: Rectangle { id: gb26; implicitWidth: 52; implicitHeight: 22; radius: 7; color: page.rate === modelData ? Theme.amber : Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb26.color) } GradientStop { position: 1; color: Theme.btnBottom(gb26.color) } } Text { anchors.centerIn: parent; text: modelData / 1000 + "k"; color: page.rate === modelData ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.rate = modelData } } }
                Text { text: i18n.t("BUFFER", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; Layout.leftMargin: 10 }
                Repeater { model: [64, 128, 256, 512, 1024]; delegate: Rectangle { id: gb27; implicitWidth: 44; implicitHeight: 22; radius: 7; color: page.frames === modelData ? Theme.amber : Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb27.color) } GradientStop { position: 1; color: Theme.btnBottom(gb27.color) } } Text { anchors.centerIn: parent; text: modelData; color: page.frames === modelData ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.frames = modelData } } }
                Text { text: (page.frames / page.rate * 1000).toFixed(1) + i18n.t(" ms per block", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Rectangle { implicitWidth: 110; implicitHeight: 30; radius: 6; color: engine.running ? Theme.ov(0.08) : Theme.emerald
                    Text { anchors.centerIn: parent; text: engine.running ? i18n.t("RESTART", i18n.lang) : i18n.t("START ENGINE", i18n.lang); color: engine.running ? Theme.ink : "#000"; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                    MouseArea { anchors.fill: parent; onClicked: { var i = page.dev(page.inId), o = page.dev(page.outId); var ok = engine.open(page.backend, page.inId, page.outId, page.rate, page.frames, i ? Math.min(32, i.inChannels) : 2, o ? Math.min(2, o.outChannels) : 2); if (ok) settings.rememberDevice(page.backend, page.inId, page.outId, page.rate, page.frames, i ? Math.min(32, i.inChannels) : 2, o ? Math.min(2, o.outChannels) : 2) } } }
                Rectangle { implicitWidth: Math.max(100, children[0].implicitWidth + 12); implicitHeight: 30; radius: 6; color: Qt.rgba(0.984, 0.749, 0.141, 0.2); border.color: Theme.amber
                    Text { anchors.centerIn: parent; text: i18n.t("AUTO DETECT", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                    property var det: engine.autoDetect()
                    MouseArea { anchors.fill: parent; onClicked: engine.autoStart(page.frames) }
                    ToolTip.visible: adHov.hovered; ToolTip.text: det && det.name ? (i18n.t("Would start ", i18n.lang) + det.name + " (" + det.backend.toUpperCase() + "): " + det.why) : i18n.t("No real sound card found", i18n.lang); HoverHandler { id: adHov } }
                Rectangle { visible: page.backend === "asio"; implicitWidth: 110; implicitHeight: 30; radius: 6; color: Theme.cyan
                    Text { anchors.centerIn: parent; text: i18n.t("SMART START", i18n.lang); color: "#000"; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                    MouseArea { anchors.fill: parent; onClicked: { var i = page.dev(page.inId), o = page.dev(page.outId); var ok = engine.openSmart(page.backend, page.inId, page.outId, page.rate, page.frames, i ? Math.min(32, i.inChannels) : 2, o ? Math.min(2, o.outChannels) : 2); if (ok) settings.rememberDevice(page.backend, page.inId, page.outId, page.rate, page.frames, i ? Math.min(32, i.inChannels) : 2, o ? Math.min(2, o.outChannels) : 2); page.planModel = engine.driverPlan(page.backend, page.inId, page.outId) } } }
                Rectangle { implicitWidth: Math.max(80, children[0].implicitWidth + 12); implicitHeight: 30; radius: 6; color: engine.running ? Theme.danger : Theme.ov(0.05); Text { anchors.centerIn: parent; text: i18n.t("STOP", i18n.lang); color: engine.running ? "#000" : Theme.dim; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; enabled: engine.running; onClicked: engine.stop() } }
                Text { text: i18n.t(engine.status, i18n.lang); color: engine.running ? Theme.emerald : Theme.amber; font.family: Theme.mono; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            }
            Text { visible: engine.running; text: i18n.t("Blocks ", i18n.lang) + engine.telv("blocks", engine.meterTick) + i18n.t(" • frames ", i18n.lang) + engine.telv("frames", engine.meterTick) + i18n.t(" • xruns ", i18n.lang) + engine.telv("xruns", engine.meterTick); color: engine.telv("xruns", engine.meterTick) > 0 ? Theme.danger : Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
            Text { visible: engine.lastRoute.length > 0; text: engine.lastRoute; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
        // DRIVER INTELLIGENCE: the ladder SMART START walks for the chosen ASIO device, the Realtek pin, the locks learned
        Panel {
            Layout.fillWidth: true
            title: i18n.t("DRIVER INTELLIGENCE", i18n.lang)
            subtitle: i18n.t("SMART START tries the best path first and falls back by itself: ASIO (the device's own driver; ASIO4ALL only when nothing here has its own) → WASAPI exclusive → WASAPI shared on the same hardware. The step that worked is locked for next time; a lower lock is retried after 6 hours. In-app routing only: no driver, registry or Windows setting is touched.", i18n.lang)
            Text { text: i18n.t("PLAN FOR THE SELECTED DEVICE", i18n.lang) + (page.backend === "asio" ? "" : i18n.t(" (choose the ASIO driver to see a ladder)", i18n.lang)); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
            Repeater { model: page.planModel; delegate: RowLayout { Layout.fillWidth: true; spacing: 8
                Rectangle { implicitWidth: 18; implicitHeight: 18; radius: 9; color: index === 0 ? Theme.cyan : Theme.ov(0.08); Text { anchors.centerIn: parent; text: index + 1; color: index === 0 ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } }
                Text { text: modelData.mode.toUpperCase() + (modelData.pinned ? i18n.t("  PINNED", i18n.lang) : modelData.fromLock ? i18n.t("  LOCKED", i18n.lang) : ""); color: modelData.pinned ? Theme.amber : modelData.fromLock ? Theme.emerald : Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; Layout.preferredWidth: 180 }
                Text { text: (modelData.in !== "-" ? i18n.t("in ", i18n.lang) + modelData.in : "") + (modelData.out !== "-" ? i18n.t("  out ", i18n.lang) + modelData.out : "") + (modelData.why ? "  ·  " + i18n.t(modelData.why ?? "", i18n.lang) : ""); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideRight; Layout.fillWidth: true } } }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.lineSoft }
            Text { text: i18n.t("REALTEK PIN", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
            Text { visible: engine.driverPins().length === 0; text: i18n.t("No Realtek ASIO driver on this computer: the pin does not apply.", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
            Repeater { model: engine.driverPins(); delegate: RowLayout { Layout.fillWidth: true; spacing: 8
                Rectangle { id: gb28; implicitWidth: 60; implicitHeight: 20; radius: 7; color: modelData.on ? Theme.amber : Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb28.color) } GradientStop { position: 1; color: Theme.btnBottom(gb28.color) } } Text { anchors.centerIn: parent; text: modelData.on ? i18n.t("PINNED", i18n.lang) : i18n.t("UNPINNED", i18n.lang); color: modelData.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { engine.setDriverPin(modelData.name, !modelData.on); page.planModel = engine.driverPlan(page.backend, page.inId, page.outId) } } }
                Text { text: i18n.t(modelData.name ?? "", i18n.lang) + "  ·  " + modelData.reason; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; wrapMode: Text.WordWrap; Layout.fillWidth: true } } }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.lineSoft }
            RowLayout { Layout.fillWidth: true
                Text { text: i18n.t("LOCKS LEARNED", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; Layout.fillWidth: true }
                Rectangle { id: gb29; visible: engine.driverLocks().length > 0; implicitWidth: 90; implicitHeight: 20; radius: 7; color: Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb29.color) } GradientStop { position: 1; color: Theme.btnBottom(gb29.color) } } border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: i18n.t("FORGET ALL", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { engine.forgetDriverLocks("*"); page.planModel = engine.driverPlan(page.backend, page.inId, page.outId) } } } }
            Text { visible: engine.driverLocks().length === 0; text: i18n.t("None yet: locks appear after a start had to fall back, or after a clean ASIO run.", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
            Repeater { model: engine.driverLocks(); delegate: RowLayout { Layout.fillWidth: true; spacing: 8
                Text { text: i18n.t(modelData.name ?? "", i18n.lang) + " → " + modelData.mode.toUpperCase() + (modelData.device && modelData.device !== i18n.t(modelData.name ?? "", i18n.lang) ? " (" + modelData.device + ")" : ""); color: Theme.emerald; font.family: Theme.mono; font.pixelSize: 8; Layout.preferredWidth: 320; elide: Text.ElideRight }
                Text { text: modelData.reason + (modelData.retryAt > 0 ? i18n.t("  ·  retries ASIO ", i18n.lang) + new Date(modelData.retryAt).toISOString().slice(0, 16).replace("T", " ") : ""); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; Layout.fillWidth: true; elide: Text.ElideRight }
                Rectangle { id: gb30; implicitWidth: Math.max(60, children[0].implicitWidth + 12); implicitHeight: 18; radius: 7; color: Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb30.color) } GradientStop { position: 1; color: Theme.btnBottom(gb30.color) } } Text { anchors.centerIn: parent; text: i18n.t("FORGET", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { engine.forgetDriverLocks(modelData.name); page.planModel = engine.driverPlan(page.backend, page.inId, page.outId) } } } } }
        }
    }
}
