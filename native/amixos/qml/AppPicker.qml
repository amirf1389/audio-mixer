// APP PICKER: pick a program the way a call program lets you share a window: one tile per program that plays sound, with its icon,
// its name and a picture of its window. MONO / STEREO put its sound on the strip; LIVE PICTURE also keeps the window's picture
// in the console (MEDIA > APP CAPTURE). The pictures come from Windows for that window only and stay in memory.
import QtQuick
import AmixOS   // this module: brings the Theme singleton
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: pick
    objectName: "appPicker"
    property int strip: 0
    property var progs: []
    property bool live: false
    signal picked(var prog, bool stereo)
    function show(forStrip) { strip = forStrip; progs = apps.programs(); for (let i = 0; i < progs.length; ++i) appWindows.snapshot(progs[i].pid); open() }
    modal: true; focus: true; padding: 14; closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    parent: Overlay.overlay; anchors.centerIn: parent
    width: Math.min(900, (parent ? parent.width : 900) - 60); height: Math.min(620, (parent ? parent.height : 620) - 60)
    background: Rectangle { radius: 10; color: Theme.cardSolid; border.color: Theme.cyan }
    component Key: Rectangle { id: key; property string label; property color hue: Theme.cyan; property bool solid: false; signal hit()
        implicitWidth: kt.implicitWidth + 16; implicitHeight: 22; radius: 7; color: solid ? hue : Theme.ov(0.06); border.color: hue
        gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(key.color) } GradientStop { position: 1; color: Theme.btnBottom(key.color) } }
        Text { id: kt; anchors.centerIn: parent; text: key.label; color: key.solid ? "#000" : key.hue; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: key.hit() } }
    contentItem: ColumnLayout { spacing: 8
        RowLayout { Layout.fillWidth: true; spacing: 8
            Text { text: i18n.t("PICK A PROGRAM  →  CH ", i18n.lang) + (pick.strip + 1); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 12; font.bold: true; font.letterSpacing: 2 }
            Item { Layout.fillWidth: true }
            Key { objectName: "pickerLive"; label: pick.live ? i18n.t("LIVE PICTURE: ON", i18n.lang) : i18n.t("LIVE PICTURE: OFF", i18n.lang); hue: Theme.amber; solid: pick.live; visible: appWindows.available; onHit: pick.live = !pick.live }
            Key { objectName: "pickerRefresh"; label: i18n.t("LOOK AGAIN", i18n.lang); onHit: pick.show(pick.strip) }
            Key { objectName: "pickerClose"; label: "✕"; hue: Theme.muted; onHit: pick.close() } }
        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8
            text: i18n.t("Every program that plays sound now. The sound of the program you pick goes onto the strip; nothing else on the computer is heard. LIVE PICTURE also shows its window inside the console (a window that protects its content stays black).", i18n.lang) }
        Text { visible: pick.progs.length === 0; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: apps.available ? i18n.t("No program plays sound now: start the player, then LOOK AGAIN.", i18n.lang) : i18n.t("Capturing a program needs Windows 10 version 2004 or later.", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9 }
        Flickable { Layout.fillWidth: true; Layout.fillHeight: true; clip: true; contentHeight: tiles.implicitHeight; boundsBehavior: Flickable.StopAtBounds
            Flow { id: tiles; width: parent.width; spacing: 10
                Repeater { model: pick.progs
                    delegate: Rectangle { objectName: "pickerTile" + index; width: 270; height: 226; radius: 8; color: Theme.ov(0.04); border.color: modelData.slot >= 0 ? Theme.emerald : Theme.lineSoft
                        ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 6
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 142; radius: 5; color: "#0e1014"; clip: true
                                Image { anchors.fill: parent; fillMode: Image.PreserveAspectFit; cache: false; asynchronous: false; source: "image://appwin/window/" + modelData.pid + "?" + appWindows.tick }
                                Text { anchors.centerIn: parent; visible: !appWindows.available; text: i18n.t("no picture on this system", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
                            RowLayout { Layout.fillWidth: true; spacing: 6
                                Image { Layout.preferredWidth: 20; Layout.preferredHeight: 20; cache: false; source: "image://appwin/icon/" + modelData.pid; sourceSize.width: 20; sourceSize.height: 20 }
                                ColumnLayout { Layout.fillWidth: true; spacing: 0
                                    Text { Layout.fillWidth: true; text: modelData.name; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; elide: Text.ElideRight }
                                    Text { Layout.fillWidth: true; text: { appWindows.tick; return appWindows.titleOf(modelData.pid) } color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; elide: Text.ElideRight } } }
                            RowLayout { Layout.fillWidth: true; spacing: 6
                                Key { objectName: "pickerMono" + index; Layout.fillWidth: true; label: i18n.t("MONO → CH ", i18n.lang) + (pick.strip + 1); hue: Theme.emerald; solid: true; onHit: { pick.picked(modelData, false); pick.close() } }
                                Key { objectName: "pickerStereo" + index; Layout.fillWidth: true; label: i18n.t("STEREO → CH ", i18n.lang) + (pick.strip + 1) + "-" + (pick.strip + 2); hue: Theme.cyan; solid: true; onHit: { pick.picked(modelData, true); pick.close() } } } } } } } }
    }
    // the pick itself: the sound onto the strip, and the live picture when it was asked for
    onPicked: (prog, stereo) => { const r = apps.capture(prog.id, prog.name, pick.strip, stereo); if (r.ok && pick.live) appWindows.watch(prog.pid, true) }
}
