// BOOT: the start-up screen, shown from the first frame until the console is ready: the mark, a progress bar and the stages as they
// complete (interface, data folder, sound cards scanned, engine auto-started, licence read). Stays at least 1.6 s so the stages can be
// read, never longer than the work itself; fades out, then the HOME (or the start page) is there. --smoke skips the wait.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Layouts

Rectangle {
    id: boot
    objectName: "bootPage"
    color: Theme.obsidian
    property int stage: 0            // 0 interface, 1 folders, 2 sound cards, 3 engine, 4 licence, 5 done
    property bool done: false
    property bool fast: false        // --smoke / --shots: no waiting
    readonly property var stages: ["Interface", "Data folders", "Sound cards and drivers", "Audio engine", "Licence", "Ready"]
    signal finished()
    function advance() { if (stage < 5) stage++; if (stage === 5) { done = true; fadeOut.start() } }
    Component.onCompleted: { if (fast) { stage = 5; done = true; boot.finished() } else tick.start() }
    Timer { id: tick; interval: 320; repeat: true; running: false; onTriggered: { boot.advance(); if (boot.stage >= 5) stop() } }
    NumberAnimation { id: fadeOut; target: boot; property: "opacity"; from: 1; to: 0; duration: 450; onFinished: boot.finished() }
    // the pulse behind the mark
    Rectangle { anchors.centerIn: parent; width: 520; height: 520; radius: 260; color: "transparent"; border.color: Theme.orange; border.width: 1; opacity: 0.12
        SequentialAnimation on scale { loops: Animation.Infinite; running: !boot.done; NumberAnimation { from: 0.9; to: 1.08; duration: 1400; easing.type: Easing.InOutSine } NumberAnimation { from: 1.08; to: 0.9; duration: 1400; easing.type: Easing.InOutSine } } }
    ColumnLayout { anchors.centerIn: parent; spacing: 18; width: 420
        Text { Layout.alignment: Qt.AlignHCenter; text: Theme.product.toUpperCase(); color: Theme.orange; font.family: Theme.mono; font.pixelSize: 54; font.weight: Font.Black; font.letterSpacing: 12 }
        Text { Layout.alignment: Qt.AlignHCenter; text: "BY " + Theme.vendor.toUpperCase() + "  ·  " + appVersion; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 10; font.letterSpacing: 4 }
        // the 32 "strips" light up as the stages pass
        Row { Layout.alignment: Qt.AlignHCenter; spacing: 3
            Repeater { model: 32; delegate: Rectangle { width: 9; height: 26; radius: 2; color: index < boot.stage * 6.4 ? (index % 4 === 0 ? Theme.orange : Theme.amber) : Theme.ov(0.08)
                Behavior on color { ColorAnimation { duration: 180 } } } } }
        Rectangle { Layout.fillWidth: true; implicitHeight: 4; radius: 2; color: Theme.ov(0.08)
            Rectangle { height: parent.height; radius: 2; color: Theme.orange; width: parent.width * boot.stage / 5; Behavior on width { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } } } }
        ColumnLayout { Layout.fillWidth: true; spacing: 3
            Repeater { model: boot.stages.length
                delegate: RowLayout { spacing: 8; opacity: index <= boot.stage ? 1 : 0.35
                    Text { text: index < boot.stage ? "✓" : index === boot.stage ? "▶" : "·"; color: index < boot.stage ? Theme.emerald : index === boot.stage ? Theme.amber : Theme.dim; font.family: Theme.mono; font.pixelSize: 9; Layout.preferredWidth: 12 }
                    Text { text: boot.stages[index]; color: index <= boot.stage ? Theme.ink : Theme.dim; font.family: Theme.mono; font.pixelSize: 9; font.letterSpacing: 1 }
                    Text { visible: index === 3 && boot.stage >= 3; text: engine.running ? engine.status : (settings.autoStart ? "auto start: " + settings.message : "auto start is off"); color: engine.running ? Theme.emerald : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideRight; Layout.fillWidth: true }
                    Text { visible: index === 4 && boot.stage >= 4; text: engine.edition; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8 }
                    Text { visible: index === 2 && boot.stage >= 2; text: engine.devices.length + " devices · " + engine.backends.join(" / ").toUpperCase(); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideRight; Layout.fillWidth: true } } } }
    }
    Text { anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottomMargin: 24; text: "the system is auto on: the last sound card starts by itself (SETTINGS › AUTO START)"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
    MouseArea { anchors.fill: parent }   // nothing underneath is clickable until the boot is done
}
