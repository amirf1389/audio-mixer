// APP CAPTURE: several programs at once, each on its own strip (apps: Apps.h). The programs that play sound now, with MONO / STEREO
// onto the selected strip, and what is captured: its strips, its level, and RELEASE. The same pick is on every strip's INPUT (MIXER).
import QtQuick
import AmixOS   // this module: brings the Theme singleton
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: panel
    objectName: "appCapturePanel"
    property var progs: []
    readonly property int strip: engine.selectedStrip
    function refresh() { progs = apps.programs() }
    implicitHeight: col.implicitHeight + 24; radius: Theme.radius; color: Theme.card; border.color: Theme.line
    Component.onCompleted: refresh()
    component Key: Rectangle { id: key; property string label; property color hue: Theme.cyan; property bool solid: false; signal hit()
        implicitWidth: kt.implicitWidth + 16; implicitHeight: 20; radius: 7; color: solid ? hue : Theme.ov(0.06); border.color: hue
        gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(key.color) } GradientStop { position: 1; color: Theme.btnBottom(key.color) } }
        Text { id: kt; anchors.centerIn: parent; text: key.label; color: key.solid ? "#000" : key.hue; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
        MouseArea { anchors.fill: parent; onClicked: key.hit() } }
    ColumnLayout { id: col; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12; spacing: 6
        RowLayout { Layout.fillWidth: true; spacing: 8
            Text { text: i18n.t("APP CAPTURE  ·  SEVERAL PROGRAMS, EACH ON ITS OWN STRIP", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
            Item { Layout.fillWidth: true }
            Text { text: apps.count + " / 8"; color: apps.count > 0 ? Theme.emerald : Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
            Key { objectName: "appCaptureRefresh"; label: i18n.t("LOOK AGAIN", i18n.lang); onHit: panel.refresh() } }
        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7
            text: i18n.t("Each program below can go onto its own strip beside the microphones: a player on one, a browser on another, a call on a third. MONO puts it on the selected strip; STEREO on that strip (left) and the next (right). It is the sound Windows plays for that program only; a protected stream may stay silent. Up to 8 programs.", i18n.lang) }
        // the programs that play sound now
        Text { visible: !apps.available; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: i18n.t("Capturing a program needs Windows 10 version 2004 or later.", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8 }
        Text { visible: apps.available && panel.progs.length === 0; text: i18n.t("No program plays sound now: start the player, then LOOK AGAIN.", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
        Repeater { model: panel.progs
            delegate: RowLayout { Layout.fillWidth: true; spacing: 6
                Rectangle { width: 6; height: 6; radius: 3; color: modelData.slot >= 0 ? Theme.emerald : Theme.dim }
                Text { Layout.fillWidth: true; text: modelData.name; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; elide: Text.ElideRight }
                Key { objectName: "appMono" + index; label: i18n.t("MONO → CH ", i18n.lang) + (panel.strip + 1); hue: Theme.emerald; onHit: { apps.capture(modelData.id, modelData.name, panel.strip, false); panel.refresh() } }
                Key { objectName: "appStereo" + index; label: i18n.t("STEREO → CH ", i18n.lang) + (panel.strip + 1) + "-" + (panel.strip + 2); hue: Theme.cyan; onHit: { apps.capture(modelData.id, modelData.name, panel.strip, true); panel.refresh() } } } }
        // what is captured
        Rectangle { visible: apps.captures.length > 0; Layout.fillWidth: true; implicitHeight: 1; color: Theme.lineSoft }
        Repeater { model: apps.captures
            delegate: RowLayout { objectName: "appSlot" + modelData.slot; Layout.fillWidth: true; spacing: 8
                Rectangle { implicitWidth: 54; implicitHeight: 16; radius: 3
                    color: modelData.state === "live" ? Theme.emerald : modelData.state === "failed" ? Theme.danger : Theme.amber
                    Text { anchors.centerIn: parent; text: i18n.t(String(modelData.state).toUpperCase(), i18n.lang); color: "#000"; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } }
                Text { text: modelData.name; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; elide: Text.ElideRight; Layout.preferredWidth: 180 }
                Text { text: modelData.strips.length ? "CH " + modelData.strips.map(s => s + 1).join(", ") : "—"; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                Rectangle { Layout.preferredWidth: 90; implicitHeight: 6; radius: 3; color: Theme.ov(0.08)
                    Rectangle { height: parent.height; radius: 3; color: Theme.emerald; width: parent.width * Math.max(0, Math.min(1, (modelData.peakDb + 60) / 60)) } }
                Text { Layout.fillWidth: true; text: modelData.message + (modelData.gaps > 0 ? (modelData.message ? " · " : "") + modelData.gaps + i18n.t(" gaps", i18n.lang) : ""); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; elide: Text.ElideRight }
                Key { objectName: "appRelease" + modelData.slot; label: i18n.t("RELEASE", i18n.lang); hue: Theme.muted; onHit: { apps.release(modelData.slot); panel.refresh() } } } }
        Text { visible: apps.note !== ""; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: apps.note; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8 }
    }
}
