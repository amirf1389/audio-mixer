// PRESETS: a channel's whole DSP chain (gate, crossover, EQ, compressor, limiter, delay, polarity) by name. Factory presets are built in; SAVE AS
// keeps the selected channel's chain under your own name; APPLY puts a preset on the selected channel.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Flickable {
    id: page
    property int strip: engine.selectedStrip
    property string category: "ALL"
    contentHeight: col.implicitHeight + 24; clip: true
    readonly property var cats: { var c = ["ALL"]; for (var i = 0; i < engine.presets.length; i++) if (c.indexOf(engine.presets[i].category) < 0) c.push(engine.presets[i].category); return c }
    ColumnLayout {
        id: col; width: parent.width - 20; x: 10; y: 10; spacing: 10
        Panel {
            Layout.fillWidth: true
            title: "PRESETS • CHANNEL CHAINS"
            subtitle: "Gate → crossover → EQ → compressor → limiter, plus delay and polarity, as one preset. Factory presets are starting points: adjust them on TONE, COMPRESSOR and NOISE GATE and save your own."
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Text { text: "APPLY TO"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 }
                ComboBox {
                    implicitWidth: 150; model: engine.strips; currentIndex: page.strip
                    displayText: engine.stripState[currentIndex] ? engine.stripState[currentIndex].name : ""
                    delegate: ItemDelegate { width: parent.width; text: engine.stripState[index] ? engine.stripState[index].name : index; font.family: Theme.mono }
                    onActivated: page.strip = currentIndex
                }
                Item { Layout.fillWidth: true }
                TextField { id: saveName; Layout.preferredWidth: 200; placeholderText: "new preset name"; font.family: Theme.mono; font.pixelSize: 10; color: Theme.ink; background: Rectangle { color: "#05070a"; radius: 4; border.color: Theme.line } }
                Rectangle { implicitWidth: 120; implicitHeight: 28; radius: 6; color: saveName.text.trim().length ? Theme.amber : Qt.rgba(1, 1, 1, 0.05); Text { anchors.centerIn: parent; text: "SAVE CHANNEL AS"; color: saveName.text.trim().length ? "#000" : Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; enabled: saveName.text.trim().length > 0; onClicked: { if (engine.savePreset(saveName.text, page.strip)) saveName.text = "" } } }
            }
            Flow { Layout.fillWidth: true; spacing: 4
                Repeater { model: page.cats; delegate: Rectangle { implicitWidth: cl.implicitWidth + 16; implicitHeight: 20; radius: 4; color: page.category === modelData ? Theme.amber : Qt.rgba(1, 1, 1, 0.08); Text { id: cl; anchors.centerIn: parent; text: modelData; color: page.category === modelData ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.category = modelData } } }
            }
            GridLayout {
                columns: width > 900 ? 3 : (width > 560 ? 2 : 1); columnSpacing: 8; rowSpacing: 8; Layout.fillWidth: true
                Repeater {
                    model: engine.presets
                    delegate: Rectangle {
                        visible: page.category === "ALL" || modelData.category === page.category
                        Layout.fillWidth: true; implicitHeight: visible ? 56 : 0; radius: 8; color: Qt.rgba(0, 0, 0, 0.25); border.color: modelData.factory ? Theme.lineSoft : Qt.rgba(0, 0.95, 1, 0.35)
                        RowLayout { anchors.fill: parent; anchors.margins: 8; spacing: 8
                            ColumnLayout { Layout.fillWidth: true; spacing: 2
                                Text { text: modelData.name; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                Text { text: modelData.category + (modelData.factory ? " • factory" : " • yours"); color: modelData.factory ? Theme.dim : Theme.cyan; font.family: Theme.mono; font.pixelSize: 7 } }
                            Rectangle { implicitWidth: 60; implicitHeight: 24; radius: 5; color: Theme.emerald; Text { anchors.centerIn: parent; text: "APPLY"; color: "#000"; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.applyPreset(modelData.name, page.strip) } }
                            Rectangle { visible: !modelData.factory; implicitWidth: 24; implicitHeight: 24; radius: 5; color: Qt.rgba(1, 0.165, 0.373, 0.2); border.color: Theme.danger; Text { anchors.centerIn: parent; text: "×"; color: Theme.danger; font.pixelSize: 12; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.deletePreset(modelData.name) } }
                        }
                    }
                }
            }
        }
    }
}
