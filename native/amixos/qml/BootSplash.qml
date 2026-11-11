// BOOT: the opening animation, about three seconds: rings open from the centre, the ten EQ bars rise in a wave, the name writes itself in
// and the orange line sweeps under it, while a chord (a major ninth, note by note, then together) plays on the master if the engine is
// already running. A click or any key skips it. Switched off on SETTINGS (animation and sound separately).
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)

Rectangle {
    id: boot
    anchors.fill: parent; z: 200; color: Theme.obsidian
    property real t: 0            // 0 .. 1 over the whole animation
    property bool chimed: false
    signal done()
    function finish() { if (fade.running || !visible) return; fade.start() }
    NumberAnimation on t { id: run; from: 0; to: 1; duration: 3000; running: true; onFinished: boot.finish() }
    NumberAnimation { id: fade; target: boot; property: "opacity"; to: 0; duration: 350; onFinished: { boot.visible = false; boot.done() } }
    Timer { interval: 150; running: boot.visible && !boot.chimed && settings.bootSound; repeat: true; onTriggered: if (engine.running && boot.t < 0.6) { boot.chimed = measure.chime() } }
    MouseArea { anchors.fill: parent; onClicked: boot.finish() }
    focus: true; Keys.onPressed: boot.finish()
    Repeater { model: 4
        delegate: Rectangle { anchors.centerIn: parent; readonly property real k: Math.max(0, Math.min(1, boot.t * 2.2 - index * 0.18))
            width: 120 + k * (260 + index * 150); height: width; radius: width / 2; color: "transparent"; border.width: 1; border.color: index % 2 ? Theme.cyan : Theme.orange; opacity: (1 - k) * 0.55 } }
    Row { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: title.top; anchors.bottomMargin: 26; spacing: 7
        Repeater { model: 10
            delegate: Item { width: 9; height: 90
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; radius: 3
                    readonly property real k: Math.max(0, Math.min(1, boot.t * 3 - index * 0.12))
                    height: 8 + k * (26 + 48 * Math.abs(Math.sin(index * 0.9 + boot.t * 9)))
                    gradient: Gradient { GradientStop { position: 0; color: Theme.amber } GradientStop { position: 1; color: Theme.orange } } opacity: 0.25 + 0.75 * k } } } }
    Row { id: title; anchors.centerIn: parent; spacing: 6
        Repeater { model: Theme.product.toUpperCase().split("")
            delegate: Text { text: modelData === " " ? " " : modelData; color: Theme.orange; font.family: Theme.mono; font.pixelSize: 54; font.weight: Font.Black
                readonly property real k: Math.max(0, Math.min(1, boot.t * 4 - 0.6 - index * 0.22)); opacity: k; scale: 1.6 - 0.6 * k; transform: Translate { y: (1 - k) * 18 } } } }
    Rectangle { anchors.top: title.bottom; anchors.topMargin: 10; anchors.horizontalCenter: parent.horizontalCenter; height: 3; radius: 2; color: Theme.orange
        width: Math.max(0, Math.min(1, boot.t * 2.2 - 0.9)) * (title.width + 40) }
    Text { anchors.top: title.bottom; anchors.topMargin: 26; anchors.horizontalCenter: parent.horizontalCenter; text: updater.justUpdated !== "" ? i18n.t("UPDATED TO ", i18n.lang) + updater.justUpdated : i18n.t("by ", i18n.lang) + Theme.vendor + "  ·  " + appVersion
        color: Theme.muted; font.family: Theme.mono; font.pixelSize: 11; font.letterSpacing: 4; opacity: Math.max(0, Math.min(1, boot.t * 3 - 1.6)) }
    Text { anchors.bottom: parent.bottom; anchors.bottomMargin: 22; anchors.horizontalCenter: parent.horizontalCenter; text: i18n.t("click or press a key to skip", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; opacity: 0.7 }
}
