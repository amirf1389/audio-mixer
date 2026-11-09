// GROUP FADERS: 8 DCA groups (a group fader adds to its members' faders in dB, a group mute silences them) and 8 mute groups (one button
// mutes every member). Membership: click a channel number under a group.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Layouts

Flickable {
    contentHeight: col.implicitHeight + 24; clip: true
    ColumnLayout {
        id: col; width: parent.width - 20; x: 10; y: 10; spacing: 10
        Panel {
            Layout.fillWidth: true
            title: "GROUP FADERS • DCA"
            subtitle: "A DCA fader moves its members together (added in dB to each channel's own fader, the meters show the result); M mutes the group. A channel is in one DCA at most."
            RowLayout {
                Layout.fillWidth: true; spacing: 6
                Repeater {
                    model: engine.dcaState
                    delegate: Rectangle {
                        Layout.fillWidth: true; implicitHeight: 300; radius: 8; color: Qt.rgba(0, 0, 0, 0.25); border.color: modelData.members.length ? Qt.rgba(0.984, 0.749, 0.141, 0.4) : Theme.lineSoft
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 6; spacing: 4
                            TextInput { Layout.fillWidth: true; text: modelData.name; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; selectByMouse: true; onEditingFinished: engine.setDcaName(modelData.index, text) }
                            Fader { Layout.fillHeight: true; Layout.alignment: Qt.AlignHCenter; value: modelData.db; onMoved: function (db) { engine.setDcaLevel(modelData.index, db) } }
                            Text { Layout.alignment: Qt.AlignHCenter; text: Theme.dbText(modelData.db); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8 }
                            Rectangle { Layout.fillWidth: true; implicitHeight: 18; radius: 3; color: modelData.mute ? Theme.danger : Qt.rgba(1, 1, 1, 0.08); Text { anchors.centerIn: parent; text: "M"; color: modelData.mute ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setDcaMute(modelData.index, !modelData.mute) } }
                            Text { Layout.alignment: Qt.AlignHCenter; text: modelData.members.length + " ch"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
                        }
                    }
                }
            }
            Text { text: "MEMBERS: click a channel to put it in the selected DCA (click again to take it out)"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
            RowLayout {
                Layout.fillWidth: true; spacing: 4
                Text { text: "DCA"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                Repeater { model: 8; delegate: Rectangle { implicitWidth: 28; implicitHeight: 20; radius: 4; color: dcaPick.sel === index + 1 ? Theme.amber : Qt.rgba(1, 1, 1, 0.08); Text { anchors.centerIn: parent; text: index + 1; color: dcaPick.sel === index + 1 ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: dcaPick.sel = index + 1 } } }
                Item { id: dcaPick; property int sel: 1 }
            }
            Flow { Layout.fillWidth: true; spacing: 3
                Repeater { model: engine.stripState; delegate: Rectangle { implicitWidth: 46; implicitHeight: 22; radius: 4
                    color: modelData.dca === dcaPick.sel ? Theme.amber : (modelData.dca > 0 ? Qt.rgba(0.984, 0.749, 0.141, 0.15) : Qt.rgba(1, 1, 1, 0.06))
                    Text { anchors.centerIn: parent; text: (index + 1) + (modelData.dca > 0 && modelData.dca !== dcaPick.sel ? " ·" + modelData.dca : ""); color: modelData.dca === dcaPick.sel ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                    MouseArea { anchors.fill: parent; onClicked: engine.setDca(index, modelData.dca === dcaPick.sel ? 0 : dcaPick.sel) } } }
            }
        }
        Panel {
            Layout.fillWidth: true
            title: "MUTE GROUPS"
            subtitle: "One button mutes every member channel; a channel can be in several groups."
            RowLayout {
                Layout.fillWidth: true; spacing: 6
                Repeater {
                    model: engine.muteGroupState
                    delegate: Rectangle {
                        Layout.fillWidth: true; implicitHeight: 44; radius: 8; color: modelData.active ? Theme.danger : Qt.rgba(0, 0, 0, 0.25); border.color: mgPick.sel === modelData.index ? Theme.ink : (modelData.members.length ? Qt.rgba(1, 0.165, 0.373, 0.4) : Theme.lineSoft)
                        ColumnLayout { anchors.centerIn: parent; spacing: 1
                            Text { Layout.alignment: Qt.AlignHCenter; text: modelData.name; color: modelData.active ? "#000" : Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                            Text { Layout.alignment: Qt.AlignHCenter; text: modelData.members.length + " ch"; color: modelData.active ? "#000" : Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
                        MouseArea { anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.RightButton; onClicked: function (m) { if (m.button === Qt.RightButton) mgPick.sel = modelData.index; else engine.setMuteGroupActive(modelData.index, !modelData.active) } }
                    }
                }
                Item { id: mgPick; property int sel: 1 }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 4
                Text { text: "MEMBERS OF MUTE GROUP"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                Repeater { model: 8; delegate: Rectangle { implicitWidth: 28; implicitHeight: 20; radius: 4; color: mgPick.sel === index + 1 ? Theme.pink : Qt.rgba(1, 1, 1, 0.08); Text { anchors.centerIn: parent; text: index + 1; color: mgPick.sel === index + 1 ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: mgPick.sel = index + 1 } } }
            }
            Flow { Layout.fillWidth: true; spacing: 3
                Repeater { model: engine.stripState; delegate: Rectangle { implicitWidth: 46; implicitHeight: 22; radius: 4
                    property bool member: (modelData.muteGroups >> (mgPick.sel - 1)) & 1
                    color: member ? Theme.pink : Qt.rgba(1, 1, 1, 0.06)
                    Text { anchors.centerIn: parent; text: index + 1; color: member ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                    MouseArea { anchors.fill: parent; onClicked: engine.setMuteGroupMember(index, mgPick.sel, !member) } } }
            }
        }
    }
}
