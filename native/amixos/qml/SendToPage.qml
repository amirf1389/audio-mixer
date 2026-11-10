// SEND TO: 16 buses, 8 AUX (monitor mixes, pre-fader) and 8 FX (effects, post-fader). Pick a bus; every strip shows its send level to it (PRE = before the fader, for monitor mixes; POST = after fader,
// mute and solo, for effects). The bus has a level, a mute, a meter, a RETURN into the master, and its own output channels (OUT 3-4 for BUS 1 ...).
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    property int bus: 0
    property int bank: 0
    readonly property var b: engine.busState[bus]
    readonly property var bm: engine.busMeters[bus]
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Text { text: i18n.t("SEND TO:", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Repeater { model: engine.busState; delegate: Rectangle { implicitWidth: bl.implicitWidth + 12; implicitHeight: 20; radius: 4; color: page.bus === index ? (modelData.kind === "fx" ? "#a78bfa" : Theme.emerald) : Theme.ov(0.06); border.color: modelData.kind === "fx" ? Qt.rgba(0.65, 0.55, 1, 0.4) : Qt.rgba(0.2, 0.9, 0.5, 0.35)
                    Text { id: bl; anchors.centerIn: parent; text: modelData.name; color: page.bus === index ? "#000" : (modelData.kind === "fx" ? "#a78bfa" : Theme.emerald); font.family: Theme.mono; font.pixelSize: 7; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.bus = index } } }
                Item { Layout.fillWidth: true }
                Repeater { model: ["CH 01-16", "CH 17-32"]; delegate: Rectangle { implicitWidth: 70; implicitHeight: 20; radius: 4; color: page.bank === index ? Theme.ov(0.16) : Theme.ov(0.06); Text { anchors.centerIn: parent; text: modelData; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.bank = index } } }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            Rectangle {   // the sends of the 16 strips of this bank
                Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; clip: true
                Row {
                    anchors.fill: parent; anchors.margins: 8; spacing: 4
                    Repeater {
                        model: 16
                        delegate: Item {
                            property int strip: page.bank * 16 + index
                            property var st: engine.stripState[strip]
                            property var sd: engine.send(strip, page.bus)
                            width: (parent.width - 15 * 4) / 16; height: parent.height
                            Connections { target: engine; function onStripsChanged() { sd = engine.send(strip, page.bus) } }
                            Rectangle { anchors.fill: parent; radius: 6; color: index % 2 ? Theme.ov(0.025) : Qt.rgba(0, 0, 0, 0.25); border.color: sd && sd.db > -60 ? Qt.rgba(0.984, 0.749, 0.141, 0.35) : Theme.lineSoft }
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 4; spacing: 4
                                Text { Layout.fillWidth: true; text: st ? st.name : ""; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
                                Rectangle { Layout.alignment: Qt.AlignHCenter; implicitWidth: 34; implicitHeight: 16; radius: 3; color: sd && sd.pre ? Theme.cyan : Theme.ov(0.08)
                                    Text { anchors.centerIn: parent; text: sd && sd.pre ? i18n.t("PRE", i18n.lang) : i18n.t("POST", i18n.lang); color: sd && sd.pre ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true }
                                    MouseArea { anchors.fill: parent; onClicked: engine.setSend(strip, page.bus, sd ? sd.db : -60, !(sd && sd.pre)) } }
                                Fader { Layout.fillHeight: true; Layout.alignment: Qt.AlignHCenter; value: sd ? sd.db : -60; onMoved: function (db) { engine.setSend(strip, page.bus, db, sd ? sd.pre : false) } }
                                Text { Layout.alignment: Qt.AlignHCenter; text: sd && sd.db > -60 ? Theme.dbText(sd.db) : i18n.t("OFF", i18n.lang); color: sd && sd.db > -60 ? Theme.amber : Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                            }
                        }
                    }
                }
            }
            Rectangle {   // the bus master
                Layout.preferredWidth: 150; Layout.fillHeight: true; radius: Theme.radius; color: Theme.cardRaised; border.color: Theme.amber
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 8; spacing: 6
                    TextInput { Layout.fillWidth: true; text: page.b ? page.b.name : ""; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; horizontalAlignment: Text.AlignHCenter; selectByMouse: true; onEditingFinished: engine.setBusName(page.bus, text) }
                    Text { Layout.alignment: Qt.AlignHCenter; text: page.b ? page.b.outputs : ""; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
                    RowLayout { Layout.fillHeight: true; Layout.alignment: Qt.AlignHCenter; spacing: 8
                        Fader { Layout.fillHeight: true; value: page.b ? page.b.db : 0; onMoved: function (db) { engine.setBusLevel(page.bus, db) } }
                        MeterBar { Layout.fillHeight: true; Layout.topMargin: 8; peakDb: page.bm ? page.bm.peakDb : -120; rmsDb: page.bm ? page.bm.rmsDb : -120; holdDb: page.bm ? page.bm.holdDb : -120; clip: page.bm ? page.bm.clip : false } }
                    Text { Layout.alignment: Qt.AlignHCenter; text: Theme.dbText(page.b ? page.b.db : 0) + i18n.t(" dB", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9 }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 20; radius: 3; color: page.b && page.b.mute ? Theme.danger : Theme.ov(0.08); Text { anchors.centerIn: parent; text: i18n.t("MUTE", i18n.lang); color: page.b && page.b.mute ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setBusMute(page.bus, !(page.b && page.b.mute)) } }
                    Text { text: i18n.t("RETURN TO MASTER  ", i18n.lang) + (page.b && page.b.returnDb > -60 ? Theme.dbText(page.b.returnDb) + i18n.t(" dB", i18n.lang) : i18n.t("OFF", i18n.lang)); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7 }
                    Slider { Layout.fillWidth: true; from: -60; to: 10; stepSize: 0.5; value: page.b ? page.b.returnDb : -60; onMoved: engine.setBusReturn(page.bus, value) }
                }
            }
        }
    }
}
