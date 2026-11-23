// THE TRAY POPUP: a click on the tray icon opens this small window by it: the master's quick slider with its dB, MUTE, and OPEN CONSOLE.
// It closes when it loses the focus (a click elsewhere), as the system's own volume popup does.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import AmixOS

Window {
    id: tp
    signal openConsole()
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    width: 280; height: 116; color: Theme.card
    title: i18n.t("Master volume", i18n.lang)
    onActiveChanged: if (!active && visible) hide()
    // near the tray icon (x, y = its centre), kept on the screen
    function popAt(x, y) {
        const s = tp.screen; const ax = s ? s.virtualX : 0, ay = s ? s.virtualY : 0, aw = s ? s.width : 1920, ah = s ? s.height : 1080;
        tp.x = Math.max(ax + 8, Math.min(x - width / 2, ax + aw - width - 8)); tp.y = y > ay + ah / 2 ? Math.max(ay + 8, y - height - 16) : y + 16;
        tp.show(); tp.raise(); tp.requestActivate() }
    property real pendingDb: NaN
    Timer { id: sendT; interval: 16; onTriggered: if (!isNaN(tp.pendingDb)) { engine.masterDb = tp.pendingDb; tp.pendingDb = NaN; sendT.start() } }
    Rectangle { anchors.fill: parent; color: "transparent"; border.color: Theme.line; radius: 8 }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 12; spacing: 8
        RowLayout { Layout.fillWidth: true
            Text { text: "AmF X1  ·  " + i18n.t("MASTER", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
            Item { Layout.fillWidth: true }
            Text { objectName: "trayDb"; text: engine.masterMuted ? i18n.t("MUTED", i18n.lang) : volume.dbText(sl.value) + " dB"; color: engine.masterMuted ? Theme.danger : Theme.cyan; font.family: Theme.mono; font.pixelSize: 12; font.bold: true } }
        Slider { id: sl; objectName: "traySlider"; Layout.fillWidth: true; from: -60; to: 10; stepSize: 0.5; value: engine.masterDb
            Accessible.name: i18n.t("Master volume", i18n.lang)
            onMoved: { if (sendT.running) tp.pendingDb = value; else { engine.masterDb = value; sendT.start() } }
            WheelHandler { onWheel: (e) => volume.stepMaster(e.angleDelta.y > 0 ? 1 : -1) } }
        RowLayout { Layout.fillWidth: true; spacing: 8
            Button { text: engine.masterMuted ? i18n.t("UNMUTE", i18n.lang) : i18n.t("MUTE", i18n.lang); font.family: Theme.mono; font.pixelSize: 9; onClicked: engine.masterMuted = !engine.masterMuted }
            Item { Layout.fillWidth: true }
            Button { text: i18n.t("OPEN CONSOLE", i18n.lang); font.family: Theme.mono; font.pixelSize: 9; onClicked: { tp.hide(); tp.openConsole() } } }
    }
}
