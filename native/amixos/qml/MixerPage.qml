// MIXER: 32 strips in two pages of 16 (or all), each with name, pan, mute, solo, fader, meter; the master on the right
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    property int bank: 0      // 0 = 1-16, 1 = 17-32, 2 = all
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Text { text: "PAGES:"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Repeater {
                    model: ["PAGE 1: CH 01-16", "PAGE 2: CH 17-32", "ALL 32 CH"]
                    delegate: Rectangle {
                        implicitWidth: lbl.implicitWidth + 16; implicitHeight: 20; radius: 4
                        color: page.bank === index ? Theme.amber : Qt.rgba(1, 1, 1, 0.06)
                        Text { id: lbl; anchors.centerIn: parent; text: modelData; color: page.bank === index ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                        MouseArea { anchors.fill: parent; onClicked: page.bank = index }
                    }
                }
                Item { Layout.fillWidth: true }
                Text { text: engine.running ? engine.status : "ENGINE IDLE: connect a sound card"; color: engine.running ? Theme.emerald : Theme.amber; font.family: Theme.mono; font.pixelSize: 8 }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; clip: true
                Flickable {
                    anchors.fill: parent; anchors.margins: 8
                    contentWidth: strips.implicitWidth; contentHeight: height
                    flickableDirection: Flickable.HorizontalFlick; boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: strips
                        spacing: 4
                        height: parent.height
                        Repeater {
                            model: engine.strips
                            delegate: Item {
                                visible: page.bank === 2 || Math.floor(index / 16) === page.bank
                                width: visible ? 64 : 0
                                height: strips.height
                                property var st: engine.stripState[index]
                                property var mt: engine.meters[index]
                                Rectangle { anchors.fill: parent; radius: 6; color: index % 2 ? Qt.rgba(1, 1, 1, 0.025) : Qt.rgba(0, 0, 0, 0.25); border.color: st && st.dsp ? Qt.rgba(0, 0.95, 1, 0.35) : Theme.lineSoft }
                                ColumnLayout {
                                    anchors.fill: parent; anchors.margins: 4; spacing: 4
                                    TextInput { Layout.fillWidth: true; text: st ? st.name : ""; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; horizontalAlignment: Text.AlignHCenter; selectByMouse: true; onEditingFinished: engine.setName(index, text) }
                                    Dial { id: pan; Layout.alignment: Qt.AlignHCenter; implicitWidth: 30; implicitHeight: 30; from: -1; to: 1; value: st ? st.pan : 0; onMoved: engine.setPan(index, value)
                                        background: Rectangle { radius: width / 2; color: "#05070a"; border.color: Theme.line }
                                        handle: Rectangle { x: pan.background.x + pan.background.width / 2 - width / 2; y: pan.background.y + 3; width: 4; height: 9; radius: 2; color: Theme.cyan; transformOrigin: Item.Bottom; transform: [ Translate { y: 0 }, Rotation { angle: pan.angle; origin.x: 2; origin.y: pan.background.height / 2 - 3 } ] } }
                                    RowLayout { Layout.alignment: Qt.AlignHCenter; spacing: 3
                                        Rectangle { implicitWidth: 24; implicitHeight: 16; radius: 3; color: st && st.mute ? Theme.danger : Qt.rgba(1, 1, 1, 0.08); Text { anchors.centerIn: parent; text: "M"; color: st && st.mute ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setMute(index, !(st && st.mute)) } }
                                        Rectangle { implicitWidth: 24; implicitHeight: 16; radius: 3; color: st && st.solo ? Theme.amber : Qt.rgba(1, 1, 1, 0.08); Text { anchors.centerIn: parent; text: "S"; color: st && st.solo ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setSolo(index, !(st && st.solo)) } } }
                                    RowLayout { Layout.fillHeight: true; Layout.alignment: Qt.AlignHCenter; spacing: 6
                                        Fader { Layout.fillHeight: true; value: st ? st.db : 0; onMoved: function (db) { engine.setGain(index, db) } }
                                        MeterBar { Layout.fillHeight: true; Layout.topMargin: 8; peakDb: mt ? mt.peakDb : -120; rmsDb: mt ? mt.rmsDb : -120; holdDb: mt ? mt.holdDb : -120; clip: mt ? mt.clip : false } }
                                    Text { Layout.alignment: Qt.AlignHCenter; text: Theme.dbText(st ? st.db : 0); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8 }
                                }
                            }
                        }
                    }
                }
            }
            Rectangle {
                Layout.preferredWidth: 120; Layout.fillHeight: true; radius: Theme.radius; color: Theme.cardRaised; border.color: Theme.orange
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 8; spacing: 6
                    Text { Layout.alignment: Qt.AlignHCenter; text: "MASTER"; color: Theme.orange; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; font.letterSpacing: 2 }
                    RowLayout { Layout.fillHeight: true; Layout.alignment: Qt.AlignHCenter; spacing: 8
                        Fader { Layout.fillHeight: true; value: engine.masterDb; onMoved: function (db) { engine.setMaster(db) } }
                        MeterBar { Layout.fillHeight: true; Layout.topMargin: 8; peakDb: engine.telemetry.masterLDb; rmsDb: engine.telemetry.masterRmsDb; clip: engine.telemetry.masterClip }
                        MeterBar { Layout.fillHeight: true; Layout.topMargin: 8; peakDb: engine.telemetry.masterRDb; rmsDb: engine.telemetry.masterRmsDb; clip: engine.telemetry.masterClip } }
                    Text { Layout.alignment: Qt.AlignHCenter; text: Theme.dbText(engine.masterDb) + " dB"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9 }
                    Text { Layout.alignment: Qt.AlignHCenter; text: "LIMITER -0.5 dBFS"; color: Theme.emerald; font.family: Theme.mono; font.pixelSize: 7 }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 18; radius: 3; color: Qt.rgba(1, 1, 1, 0.08); Text { anchors.centerIn: parent; text: "CLEAR CLIPS"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.clearClips() } }
                }
            }
        }
    }
}
