// SETTINGS: engine facts, saved state, about
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Layouts

Flickable {
    contentHeight: col.implicitHeight + 24; clip: true
    ColumnLayout {
        id: col; width: parent.width - 20; x: 10; y: 10; spacing: 10
        Panel {
            Layout.fillWidth: true
            title: "SETTINGS"
            subtitle: "A mix OS " + appVersion + " • native console by AMFB Audio • C++20 engine + Qt Quick interface, no web runtime"
            Text { text: "Engine drivers built into this copy: " + engine.backends.join(", ").toUpperCase(); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9 }
            Text { text: "Strips: " + engine.strips + " • edition: " + engine.edition; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9 }
            Text { text: "Console state (names, levels, channel DSP, master) is saved on exit and restored at start."; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                Rectangle { implicitWidth: 100; implicitHeight: 26; radius: 5; color: Theme.amber; Text { anchors.centerIn: parent; text: "SAVE NOW"; color: "#000"; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.save() } }
            }
        }
        Panel {
            Layout.fillWidth: true
            title: "PORTING STATUS"
            subtitle: "Pages of the retired web console and whether they are native yet"
            Flow { Layout.fillWidth: true; spacing: 4
                Repeater { model: engine.pages; delegate: Rectangle { implicitWidth: t.implicitWidth + 12; implicitHeight: 18; radius: 3; color: modelData.ready ? Qt.rgba(0.063, 0.725, 0.506, 0.2) : Qt.rgba(1, 1, 1, 0.05); Text { id: t; anchors.centerIn: parent; text: modelData.text; color: modelData.ready ? Theme.emerald : Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } } }
            }
        }
    }
}
