// PER-APP VOLUME: every program Windows plays, with its icon, its level and MUTE, two-way with the Windows volume mixer (a move there shows
// here, a move here shows there). A program also captured by APP SOUND shows the strips it plays on (click: APP SOUND). Below, the VOLUME
// KEYS: what they move (the console's master or the Windows volume), the step, the keyboard's own volume keys, the hotkeys, the tray icon.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AmixOS

Popup {
    id: pop
    signal showCapture()
    modal: true; anchors.centerIn: parent; width: Math.min(560, (parent ? parent.width : 600) - 60); padding: 16
    background: Rectangle { color: Theme.card; radius: 10; border.color: Theme.line }
    component Seg: Rectangle { id: sg; property string label; property bool on: false; signal hit(); implicitWidth: sgT.implicitWidth + 16; implicitHeight: 22; radius: 6
        color: on ? Theme.cyan : Theme.ov(0.06); border.color: on ? Theme.cyan : Theme.lineSoft
        Text { id: sgT; anchors.centerIn: parent; text: i18n.t(sg.label, i18n.lang); color: sg.on ? "#000" : Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: sg.hit() } }
    ColumnLayout {
        width: parent.width; spacing: 8
        RowLayout { Layout.fillWidth: true
            Text { text: i18n.t("APPS", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 13; font.weight: Font.Black; font.letterSpacing: 3 }
            Text { Layout.fillWidth: true; text: i18n.t("each program's volume, as in the Windows volume mixer", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideRight }
            Seg { label: "WINDOWS MIXER"; visible: volume.appsAvailable; onHit: apps.openWindowsSound("mixer") } }
        Text { visible: !volume.appsAvailable; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9
            text: i18n.t("Per-app volume needs Windows: there each program that plays sound is listed here with its own level.", i18n.lang) }
        Text { visible: volume.appsAvailable && volume.appIds.length === 0; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9; text: i18n.t("No program is playing sound.", i18n.lang) }
        ScrollView {
            Layout.fillWidth: true; Layout.preferredHeight: Math.min(330, appCol.implicitHeight); clip: true; visible: volume.appIds.length > 0
            Column {
                id: appCol; width: pop.availableWidth; spacing: 4
                Repeater {
                    model: volume.appIds
                    delegate: Rectangle {
                        id: row; required property string modelData
                        readonly property var a: { volume.apps; return volume.app(modelData) }
                        width: appCol.width; height: 40; radius: 6; color: Theme.ov(0.04); border.color: Theme.lineSoft
                        RowLayout { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 8
                            Item { implicitWidth: 24; implicitHeight: 24
                                Rectangle { anchors.fill: parent; radius: 6; visible: ic.status !== Image.Ready; color: row.a.system ? Theme.ov(0.12) : Theme.violet
                                    Text { anchors.centerIn: parent; text: row.a.system ? "♪" : (row.a.name || "?").charAt(0).toUpperCase(); color: "#fff"; font.family: Theme.mono; font.pixelSize: 11; font.bold: true } }
                                Image { id: ic; anchors.fill: parent; source: row.a.icon || ""; sourceSize: Qt.size(32, 32); asynchronous: true; visible: status === Image.Ready } }
                            Text { Layout.preferredWidth: 120; text: row.a.name || ""; color: row.a.mute ? Theme.dim : Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; elide: Text.ElideRight }
                            Slider { id: sl; Layout.fillWidth: true; from: 0; to: 100; stepSize: 1; value: row.a.volume || 0; opacity: row.a.mute ? 0.5 : 1
                                Accessible.name: (row.a.name || "") + " " + i18n.t("volume", i18n.lang)
                                onMoved: volume.setAppVolume(row.modelData, value) }
                            Text { Layout.preferredWidth: 34; horizontalAlignment: Text.AlignRight; text: Math.round(sl.value) + "%"; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9 }
                            Rectangle { implicitWidth: 44; implicitHeight: 20; radius: 5; color: row.a.mute ? Theme.danger : Theme.ov(0.08); border.color: row.a.mute ? Theme.danger : Theme.lineSoft
                                Text { anchors.centerIn: parent; text: i18n.t("MUTE", i18n.lang); color: row.a.mute ? "#fff" : Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: volume.setAppMute(row.modelData, !row.a.mute) }
                                Accessible.role: Accessible.CheckBox; Accessible.name: i18n.t("Mute", i18n.lang) + " " + (row.a.name || ""); Accessible.checked: !!row.a.mute }
                            Rectangle { visible: (row.a.strips || []).length > 0; implicitWidth: cpT.implicitWidth + 12; implicitHeight: 20; radius: 5; color: Theme.ov(0.06); border.color: Theme.emerald
                                Text { id: cpT; anchors.centerIn: parent; text: "◉ CH " + (row.a.strips || []).map(function (s) { return s + 1 }).join(","); color: Theme.emerald; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { pop.close(); pop.showCapture() } }
                                ToolTip.visible: cpH.hovered; ToolTip.text: i18n.t("APP SOUND: this program also plays on these console strips", i18n.lang); HoverHandler { id: cpH } }
                        }
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.lineSoft }
        Text { text: i18n.t("VOLUME KEYS", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; font.letterSpacing: 2 }
        RowLayout { spacing: 6
            Text { Layout.preferredWidth: 110; text: i18n.t("THE KEYS MOVE", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
            Seg { objectName: "volTargetConsole"; label: "CONSOLE MASTER"; on: volume.target === 0; onHit: volume.target = 0 }
            Seg { objectName: "volTargetWindows"; label: "WINDOWS VOLUME"; on: volume.target === 1; onHit: volume.target = 1 } }
        RowLayout { spacing: 6
            Text { Layout.preferredWidth: 110; text: i18n.t("STEP", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
            Repeater { model: [1, 2, 3]; delegate: Seg { required property int modelData; label: modelData + " dB"; on: volume.stepDb === modelData; onHit: volume.stepDb = modelData } } }
        RowLayout { spacing: 6
            Seg { label: "Ctrl+Alt+↑ ↓ M"; on: volume.hotkeys; onHit: volume.hotkeys = !volume.hotkeys }
            Seg { label: "TRAY ICON"; visible: volume.trayAvailable; on: volume.tray; onHit: volume.tray = !volume.tray } }
        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8
            text: (volume.keysNote ? volume.keysNote + "\n" : "") + i18n.t("The keyboard's volume and media keys always control the Windows volume. In the console: Up / Down = master, Ctrl+Up / Down = the selected channel, 1 dB a press (Shift 0.1 dB); not while typing in a text field.", i18n.lang) }
    }
}
