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
            title: i18n.t("PRESETS • CHANNEL CHAINS", i18n.lang)
            subtitle: i18n.t("Gate → crossover → EQ → compressor → limiter, plus delay and polarity, as one preset. Factory presets are starting points: adjust them on TONE, COMPRESSOR and NOISE GATE and save your own.", i18n.lang)
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Text { text: i18n.t("APPLY TO", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 }
                Combo {
                    implicitWidth: 150; model: engine.strips; currentIndex: page.strip
                    displayText: engine.stripAt(currentIndex, engine.stripTick) ? engine.stripAt(currentIndex, engine.stripTick).name : ""
                    delegate: ItemDelegate { width: parent.width; text: engine.stripAt(index, engine.stripTick) ? engine.stripAt(index, engine.stripTick).name : index; font.family: Theme.mono }
                    onActivated: page.strip = currentIndex
                }
                Item { Layout.fillWidth: true }
                Field { id: saveName; Layout.preferredWidth: 200; placeholderText: i18n.t("new preset name", i18n.lang); font.family: Theme.mono; font.pixelSize: 10; color: Theme.ink; background: Rectangle { color: Theme.deep; radius: 4; border.color: Theme.line } }
                Rectangle { id: gb121; implicitWidth: Math.max(120, children[0].implicitWidth + 12); implicitHeight: 28; radius: 7; color: saveName.text.trim().length ? Theme.amber : Theme.ov(0.05); gradient: Gradient { GradientStop { position: 0; color: Qt.lighter(gb121.color, 1.25) } GradientStop { position: 1; color: Qt.darker(gb121.color, 1.12) } } Text { anchors.centerIn: parent; text: i18n.t("SAVE CHANNEL AS", i18n.lang); color: saveName.text.trim().length ? "#000" : Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; enabled: saveName.text.trim().length > 0; onClicked: { if (engine.savePreset(saveName.text, page.strip)) saveName.text = "" } } }
            }
            Flow { Layout.fillWidth: true; spacing: 4
                Repeater { model: page.cats; delegate: Rectangle { id: gb122; implicitWidth: cl.implicitWidth + 16; implicitHeight: 20; radius: 7; color: page.category === modelData ? Theme.amber : Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Qt.lighter(gb122.color, 1.25) } GradientStop { position: 1; color: Qt.darker(gb122.color, 1.12) } } Text { id: cl; anchors.centerIn: parent; text: modelData; color: page.category === modelData ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.category = modelData } } }
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
                                Text { text: i18n.t(modelData.name ?? "", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                Text { text: modelData.category + (modelData.factory ? i18n.t(" • factory", i18n.lang) : i18n.t(" • yours", i18n.lang)); color: modelData.factory ? Theme.dim : Theme.cyan; font.family: Theme.mono; font.pixelSize: 7 } }
                            Rectangle { id: gb123; implicitWidth: Math.max(60, children[0].implicitWidth + 12); implicitHeight: 24; radius: 7; color: Theme.emerald; gradient: Gradient { GradientStop { position: 0; color: Qt.lighter(gb123.color, 1.25) } GradientStop { position: 1; color: Qt.darker(gb123.color, 1.12) } } Text { anchors.centerIn: parent; text: i18n.t("APPLY", i18n.lang); color: "#000"; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.applyPreset(modelData.name, page.strip) } }
                            Rectangle { id: gb124; visible: !modelData.factory; implicitWidth: 24; implicitHeight: 24; radius: 7; color: Qt.rgba(1, 0.165, 0.373, 0.2); gradient: Gradient { GradientStop { position: 0; color: Qt.lighter(gb124.color, 1.25) } GradientStop { position: 1; color: Qt.darker(gb124.color, 1.12) } } border.color: Theme.danger; Text { anchors.centerIn: parent; text: "×"; color: Theme.danger; font.pixelSize: 12; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.deletePreset(modelData.name) } }
                        }
                    }
                }
            }
        }
    }
}
