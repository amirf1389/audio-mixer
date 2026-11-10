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
    property int rate: 48000
    property int frames: 256
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
            title: "CONNECTIONS • SOUND CARD"
            subtitle: "The engine opens the card directly: ASIO (the device's own driver) and WASAPI on Windows, Core Audio on macOS, ALSA on Linux. \"null\" is a timed test device with no hardware."
            RowLayout {
                Layout.fillWidth: true; spacing: 6
                Text { text: "DRIVER"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 }
                Repeater { model: engine.backends; delegate: Rectangle { implicitWidth: lbl.implicitWidth + 18; implicitHeight: 24; radius: 4; color: page.backend === modelData ? Theme.amber : Qt.rgba(1, 1, 1, 0.08); Text { id: lbl; anchors.centerIn: parent; text: modelData.toUpperCase(); color: page.backend === modelData ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { page.backend = modelData; page.inId = ""; page.outId = "" } } } }
                Item { Layout.fillWidth: true }
                Rectangle { implicitWidth: 80; implicitHeight: 24; radius: 4; color: Qt.rgba(1, 1, 1, 0.08); Text { anchors.centerIn: parent; text: "RESCAN"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.refreshDevices() } }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 10
                ColumnLayout { Layout.fillWidth: true; spacing: 4
                    Text { text: "INPUT"; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                    Repeater { model: page.devs("in"); delegate: Rectangle { Layout.fillWidth: true; implicitHeight: 30; radius: 4; color: page.inId === modelData.id ? Qt.rgba(0, 0.95, 1, 0.15) : Qt.rgba(0, 0, 0, 0.3); border.color: page.inId === modelData.id ? Theme.cyan : Theme.lineSoft
                        RowLayout { anchors.fill: parent; anchors.margins: 6; Text { text: modelData.name; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; elide: Text.ElideRight; Layout.fillWidth: true } Text { text: modelData.inChannels + " ch • " + modelData.defaultRate + " Hz" + (modelData.defaultIn ? " • default" : ""); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } }
                        MouseArea { anchors.fill: parent; onClicked: { page.inId = modelData.id; page.rate = modelData.defaultRate } } } }
                    Text { visible: page.devs("in").length === 0; text: "No input of this driver"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9 } }
                ColumnLayout { Layout.fillWidth: true; spacing: 4
                    Text { text: "OUTPUT"; color: Theme.emerald; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                    Repeater { model: page.devs("out"); delegate: Rectangle { Layout.fillWidth: true; implicitHeight: 30; radius: 4; color: page.outId === modelData.id ? Qt.rgba(0.063, 0.725, 0.506, 0.18) : Qt.rgba(0, 0, 0, 0.3); border.color: page.outId === modelData.id ? Theme.emerald : Theme.lineSoft
                        RowLayout { anchors.fill: parent; anchors.margins: 6; Text { text: modelData.name; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; elide: Text.ElideRight; Layout.fillWidth: true } Text { text: modelData.outChannels + " ch • " + modelData.defaultRate + " Hz" + (modelData.defaultOut ? " • default" : ""); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } }
                        MouseArea { anchors.fill: parent; onClicked: { page.outId = modelData.id; page.rate = modelData.defaultRate } } } }
                    Text { visible: page.devs("out").length === 0; text: "No output of this driver"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9 } }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Text { text: "RATE"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 }
                Repeater { model: [44100, 48000, 88200, 96000, 192000]; delegate: Rectangle { implicitWidth: 52; implicitHeight: 22; radius: 4; color: page.rate === modelData ? Theme.amber : Qt.rgba(1, 1, 1, 0.08); Text { anchors.centerIn: parent; text: modelData / 1000 + "k"; color: page.rate === modelData ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.rate = modelData } } }
                Text { text: "BUFFER"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; Layout.leftMargin: 10 }
                Repeater { model: [64, 128, 256, 512, 1024]; delegate: Rectangle { implicitWidth: 44; implicitHeight: 22; radius: 4; color: page.frames === modelData ? Theme.amber : Qt.rgba(1, 1, 1, 0.08); Text { anchors.centerIn: parent; text: modelData; color: page.frames === modelData ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.frames = modelData } } }
                Text { text: (page.frames / page.rate * 1000).toFixed(1) + " ms per block"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Rectangle { implicitWidth: 110; implicitHeight: 30; radius: 6; color: engine.running ? Qt.rgba(1, 1, 1, 0.08) : Theme.emerald
                    Text { anchors.centerIn: parent; text: engine.running ? "RESTART" : "START ENGINE"; color: engine.running ? Theme.ink : "#000"; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                    MouseArea { anchors.fill: parent; onClicked: { var i = page.dev(page.inId), o = page.dev(page.outId); engine.open(page.backend, page.inId, page.outId, page.rate, page.frames, i ? Math.min(32, i.inChannels) : 2, o ? Math.min(2, o.outChannels) : 2) } } }
                Rectangle { visible: page.backend === "asio"; implicitWidth: 110; implicitHeight: 30; radius: 6; color: Theme.cyan
                    Text { anchors.centerIn: parent; text: "SMART START"; color: "#000"; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                    MouseArea { anchors.fill: parent; onClicked: { var i = page.dev(page.inId), o = page.dev(page.outId); engine.openSmart(page.backend, page.inId, page.outId, page.rate, page.frames, i ? Math.min(32, i.inChannels) : 2, o ? Math.min(2, o.outChannels) : 2); page.planModel = engine.driverPlan(page.backend, page.inId, page.outId) } } }
                Rectangle { implicitWidth: 80; implicitHeight: 30; radius: 6; color: engine.running ? Theme.danger : Qt.rgba(1, 1, 1, 0.05); Text { anchors.centerIn: parent; text: "STOP"; color: engine.running ? "#000" : Theme.dim; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; enabled: engine.running; onClicked: engine.stop() } }
                Text { text: engine.status; color: engine.running ? Theme.emerald : Theme.amber; font.family: Theme.mono; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            }
            Text { visible: engine.running; text: "Blocks " + engine.telemetry.blocks + " • frames " + engine.telemetry.frames + " • xruns " + engine.telemetry.xruns; color: engine.telemetry.xruns > 0 ? Theme.danger : Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
            Text { visible: engine.lastRoute.length > 0; text: engine.lastRoute; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
        // DRIVER INTELLIGENCE: the ladder SMART START walks for the chosen ASIO device, the Realtek pin, the locks learned
        Panel {
            Layout.fillWidth: true
            title: "DRIVER INTELLIGENCE"
            subtitle: "SMART START tries the best path first and falls back by itself: ASIO (the device's own driver; ASIO4ALL only when nothing here has its own) → WASAPI exclusive → WASAPI shared on the same hardware. The step that worked is locked for next time; a lower lock is retried after 6 hours. In-app routing only: no driver, registry or Windows setting is touched."
            Text { text: "PLAN FOR THE SELECTED DEVICE" + (page.backend === "asio" ? "" : " (choose the ASIO driver to see a ladder)"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
            Repeater { model: page.planModel; delegate: RowLayout { Layout.fillWidth: true; spacing: 8
                Rectangle { implicitWidth: 18; implicitHeight: 18; radius: 9; color: index === 0 ? Theme.cyan : Qt.rgba(1, 1, 1, 0.08); Text { anchors.centerIn: parent; text: index + 1; color: index === 0 ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } }
                Text { text: modelData.mode.toUpperCase() + (modelData.pinned ? "  PINNED" : modelData.fromLock ? "  LOCKED" : ""); color: modelData.pinned ? Theme.amber : modelData.fromLock ? Theme.emerald : Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; Layout.preferredWidth: 180 }
                Text { text: (modelData.in !== "-" ? "in " + modelData.in : "") + (modelData.out !== "-" ? "  out " + modelData.out : "") + (modelData.why ? "  ·  " + modelData.why : ""); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideRight; Layout.fillWidth: true } } }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.lineSoft }
            Text { text: "REALTEK PIN"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
            Text { visible: engine.driverPins().length === 0; text: "No Realtek ASIO driver on this computer: the pin does not apply."; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
            Repeater { model: engine.driverPins(); delegate: RowLayout { Layout.fillWidth: true; spacing: 8
                Rectangle { implicitWidth: 60; implicitHeight: 20; radius: 4; color: modelData.on ? Theme.amber : Qt.rgba(1, 1, 1, 0.08); Text { anchors.centerIn: parent; text: modelData.on ? "PINNED" : "UNPINNED"; color: modelData.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { engine.setDriverPin(modelData.name, !modelData.on); page.planModel = engine.driverPlan(page.backend, page.inId, page.outId) } } }
                Text { text: modelData.name + "  ·  " + modelData.reason; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; wrapMode: Text.WordWrap; Layout.fillWidth: true } } }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.lineSoft }
            RowLayout { Layout.fillWidth: true
                Text { text: "LOCKS LEARNED"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; Layout.fillWidth: true }
                Rectangle { visible: engine.driverLocks().length > 0; implicitWidth: 90; implicitHeight: 20; radius: 4; color: Qt.rgba(1, 1, 1, 0.08); border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: "FORGET ALL"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { engine.forgetDriverLocks("*"); page.planModel = engine.driverPlan(page.backend, page.inId, page.outId) } } } }
            Text { visible: engine.driverLocks().length === 0; text: "None yet: locks appear after a start had to fall back, or after a clean ASIO run."; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
            Repeater { model: engine.driverLocks(); delegate: RowLayout { Layout.fillWidth: true; spacing: 8
                Text { text: modelData.name + " → " + modelData.mode.toUpperCase() + (modelData.device && modelData.device !== modelData.name ? " (" + modelData.device + ")" : ""); color: Theme.emerald; font.family: Theme.mono; font.pixelSize: 8; Layout.preferredWidth: 320; elide: Text.ElideRight }
                Text { text: modelData.reason + (modelData.retryAt > 0 ? "  ·  retries ASIO " + new Date(modelData.retryAt).toISOString().slice(0, 16).replace("T", " ") : ""); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; Layout.fillWidth: true; elide: Text.ElideRight }
                Rectangle { implicitWidth: 60; implicitHeight: 18; radius: 4; color: Qt.rgba(1, 1, 1, 0.08); Text { anchors.centerIn: parent; text: "FORGET"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { engine.forgetDriverLocks(modelData.name); page.planModel = engine.driverPlan(page.backend, page.inId, page.outId) } } } } }
        }
    }
}
