// A mix OS main window: header, page rail, the current page. Pages come from engine.pages (the same groups and names as the web console had).
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: root
    visible: true
    width: 1380
    height: 860
    minimumWidth: 720
    minimumHeight: 480
    title: "A mix OS " + appVersion
    color: Theme.obsidian
    property string current: "home"
    function pageOf(id) { for (var i = 0; i < engine.pages.length; i++) if (engine.pages[i].id === id) return engine.pages[i]; return engine.pages[0] }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {   // header
            Layout.fillWidth: true; implicitHeight: 48; color: "#070a10"
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.lineSoft }
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14; spacing: 12
                Text { text: "A MIX OS"; color: Theme.orange; font.family: Theme.mono; font.pixelSize: 15; font.weight: Font.Black; font.letterSpacing: 4 }
                Text { text: "BY AMFB AUDIO"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.letterSpacing: 2 }
                Item { Layout.fillWidth: true }
                Rectangle { implicitWidth: statusText.implicitWidth + 20; implicitHeight: 24; radius: 12; color: engine.running ? Qt.rgba(0.063, 0.725, 0.506, 0.15) : Qt.rgba(0.984, 0.749, 0.141, 0.12); border.color: engine.running ? Theme.emerald : Theme.amber
                    Text { id: statusText; anchors.centerIn: parent; text: engine.running ? "ENGINE ON • " + engine.backend.toUpperCase() : "ENGINE OFF"; color: engine.running ? Theme.emerald : Theme.amber; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } }
                Text { text: "MASTER " + Theme.dbText(engine.telemetry.masterPeakDb) + " dBFS"; color: engine.telemetry.masterClip ? Theme.danger : Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Text { text: engine.edition.toUpperCase(); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
            NavRail { Layout.fillHeight: true; pages: engine.pages; current: root.current; onPicked: function (id) { root.current = id } }
            Item {
                Layout.fillWidth: true; Layout.fillHeight: true
                Loader {
                    id: pageLoader
                    anchors.fill: parent
                    sourceComponent: {
                        switch (root.current) {
                        case "home": return homeC; case "mixer": return mixerC; case "dsp": return toneC; case "io": return connC;
                        case "meter": return levelsC; case "lufs": return loudC; case "sys": return settingsC; case "dyn": return compC; case "gate": return gateC; default: return comingC
                        }
                    }
                }
                Component { id: homeC; HomePage { onGo: function (id) { root.current = id } } }
                Component { id: mixerC; MixerPage {} }
                Component { id: toneC; TonePage {} }
                Component { id: connC; ConnectionsPage {} }
                Component { id: levelsC; LevelsPage {} }
                Component { id: loudC; LoudnessPage {} }
                Component { id: settingsC; SettingsPage {} }
                Component { id: compC; CompressorPage {} }
                Component { id: gateC; GatePage {} }
                Component { id: comingC; ComingPage { page: root.pageOf(root.current) } }
            }
        }
    }
}
