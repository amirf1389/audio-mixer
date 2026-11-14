// DSP MODE, wherever the sound is shaped (SOUND CARD, MIXER, MASTERING): DIGITAL leaves the mix exactly as computed; ANALOG colours the
// master and every channel the way an analogue console does (see Engine::setDspMode). One switch, the same state on every page.
import QtQuick
import AmixOS

Row {
    id: sw
    spacing: 4
    property bool compact: false
    Text { visible: !sw.compact; text: i18n.t("DSP MODE", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2; anchors.verticalCenter: parent.verticalCenter; rightPadding: 4 }
    Repeater {
        model: [{ v: "digital", t: "DIGITAL" }, { v: "analog", t: "ANALOG" }]
        delegate: Rectangle {
            objectName: "dspSwitch_" + modelData.v
            readonly property bool on: engine.dspMode === modelData.v
            implicitWidth: lt.implicitWidth + 16; implicitHeight: 20; radius: 4
            color: on ? (modelData.v === "analog" ? Theme.amber : Theme.cyan) : Theme.ov(0.06); border.color: on ? "transparent" : Theme.lineSoft
            Text { id: lt; anchors.centerIn: parent; text: i18n.t(modelData.t, i18n.lang); color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: engine.dspMode = modelData.v }
        }
    }
}
