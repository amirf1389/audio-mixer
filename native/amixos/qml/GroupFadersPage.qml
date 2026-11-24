// GROUP FADERS: 8 DCA groups (a group fader adds to its members' faders in dB, a group mute silences them) and 8 mute groups (one button
// mutes every member). Membership: click a channel number under a group.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Layouts
import QtQuick.Window

Flickable {
    contentHeight: col.implicitHeight + 24; clip: true
    ColumnLayout {
        id: col; width: parent.width - 20; x: 10; y: 10; spacing: 10
        Panel {
            Layout.fillWidth: true
            title: i18n.t("GROUP FADERS • DCA", i18n.lang)
            subtitle: i18n.t("A DCA fader moves its members together (added in dB to each channel's own fader, the meters show the result); M mutes the group. A channel is in one DCA at most.", i18n.lang)
            RowLayout {
                Layout.fillWidth: true; spacing: 6
                Repeater {
                    model: engine.dcaState.filter(function (d) { return !d.locked })   // what this edition has: BASIC 1, PRO 4, STUDIO / MASTER 8
                    delegate: Rectangle {
                        Layout.fillWidth: true; Layout.maximumWidth: 140; implicitHeight: 400; radius: 8; color: Qt.rgba(0, 0, 0, 0.25); border.color: modelData.members.length ? Qt.rgba(0.984, 0.749, 0.141, 0.4) : Theme.lineSoft
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 6; spacing: 4
                            TextInput { Layout.fillWidth: true; text: i18n.t(modelData.name ?? "", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; selectByMouse: true; onEditingFinished: engine.setDcaName(modelData.index, text) }
                            BigVu { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: Math.min(parent.width - 4, 74); Layout.preferredHeight: 90; lDb: { engine.meterTick; let m = -120; const mem = modelData.members || []; for (let k = 0; k < mem.length; ++k) { const x = engine.meterAt(mem[k]); if (x.peakDb > m) m = x.peakDb } return m } }
                            Fader { Layout.fillHeight: true; Layout.alignment: Qt.AlignHCenter; value: modelData.db; onMoved: function (db) { engine.setDcaLevel(modelData.index, db) } }
                            Text { Layout.alignment: Qt.AlignHCenter; text: Theme.dbText(modelData.db); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8 }
                            Rectangle { id: gb39; Layout.fillWidth: true; implicitHeight: 18; radius: 7; color: modelData.mute ? Theme.danger : Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb39.color) } GradientStop { position: 1; color: Theme.btnBottom(gb39.color) } } Text { anchors.centerIn: parent; text: "M"; color: modelData.mute ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setDcaMute(modelData.index, !modelData.mute) } }
                            Text { Layout.alignment: Qt.AlignHCenter; text: modelData.members.length + i18n.t(" ch", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
                            // WINDOWS VOLUME: this fader is the Windows system volume (its members keep their own gain)
                            Rectangle { objectName: "dcaWinVol" + modelData.index; Layout.fillWidth: true; implicitHeight: 18; radius: 7; color: modelData.winVol ? Theme.cyan : Theme.ov(0.08)
                                Text { anchors.centerIn: parent; text: i18n.t("WINDOWS VOLUME", i18n.lang); color: modelData.winVol ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: engine.setDcaWinVol(modelData.index, !modelData.winVol) } }
                        }
                    }
                }
                // the DCAs this edition does not have: locked, one press to the licence page
                Rectangle { objectName: "dcaLockedSlot"; visible: engine.dcaLimit < 8; Layout.fillWidth: true; Layout.maximumWidth: 220; implicitHeight: 400; radius: 8; color: Qt.rgba(0, 0, 0, 0.18); border.color: Theme.lineSoft
                    ColumnLayout { anchors.centerIn: parent; width: parent.width - 16; spacing: 8
                        Text { Layout.alignment: Qt.AlignHCenter; text: i18n.t("LOCKED", i18n.lang); font.family: Theme.mono; font.pixelSize: 12; font.bold: true; color: Theme.muted }
                        Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; text: i18n.t("MORE DCA GROUPS", i18n.lang) + " · " + engine.edition + " " + engine.dcaLimit + " / 8"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                        Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; text: i18n.t("PRO has 4 DCA groups, STUDIO and MASTER have 8.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                        Rectangle { objectName: "dcaUpgradeBtn"; Layout.alignment: Qt.AlignHCenter; implicitWidth: upT.implicitWidth + 22; implicitHeight: 24; radius: 7; color: "transparent"; border.color: Theme.amber
                            Text { id: upT; anchors.centerIn: parent; text: i18n.t("UPGRADE", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Window.window.current = "renew" } } } }
            }
            Text { text: i18n.t("MEMBERS: click a channel to put it in the selected DCA (click again to take it out)", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
            RowLayout {
                Layout.fillWidth: true; spacing: 4
                Text { text: i18n.t("DCA", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                Repeater { model: engine.dcaLimit; delegate: Rectangle { id: gb40; implicitWidth: 28; implicitHeight: 20; radius: 7; color: dcaPick.sel === index + 1 ? Theme.amber : Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb40.color) } GradientStop { position: 1; color: Theme.btnBottom(gb40.color) } } Text { anchors.centerIn: parent; text: index + 1; color: dcaPick.sel === index + 1 ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: dcaPick.sel = index + 1 } } }
                Item { id: dcaPick; property int sel: 1 }
            }
            Flow { Layout.fillWidth: true; spacing: 3
                Repeater { model: engine.stripState; delegate: Rectangle { id: gb41; implicitWidth: 46; implicitHeight: 22; radius: 7
                    color: modelData.dca === dcaPick.sel ? Theme.amber : (modelData.dca > 0 ? Qt.rgba(0.984, 0.749, 0.141, 0.15) : Theme.ov(0.06))
                    gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb41.color) } GradientStop { position: 1; color: Theme.btnBottom(gb41.color) } }
                    Text { anchors.centerIn: parent; text: (index + 1) + (modelData.dca > 0 && modelData.dca !== dcaPick.sel ? " ·" + modelData.dca : ""); color: modelData.dca === dcaPick.sel ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                    MouseArea { anchors.fill: parent; onClicked: engine.setDca(index, modelData.dca === dcaPick.sel ? 0 : dcaPick.sel) } } }
            }
        }
        Panel {
            Layout.fillWidth: true
            title: i18n.t("MUTE GROUPS", i18n.lang)
            subtitle: i18n.t("One button mutes every member channel; a channel can be in several groups.", i18n.lang)
            RowLayout {
                Layout.fillWidth: true; spacing: 6
                Repeater {
                    model: engine.muteGroupState
                    delegate: Rectangle {
                        Layout.fillWidth: true; implicitHeight: 44; radius: 8; color: modelData.active ? Theme.danger : Qt.rgba(0, 0, 0, 0.25); border.color: mgPick.sel === modelData.index ? Theme.ink : (modelData.members.length ? Qt.rgba(1, 0.165, 0.373, 0.4) : Theme.lineSoft)
                        ColumnLayout { anchors.centerIn: parent; spacing: 1
                            Text { Layout.alignment: Qt.AlignHCenter; text: i18n.t(modelData.name ?? "", i18n.lang); color: modelData.active ? "#000" : Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                            Text { Layout.alignment: Qt.AlignHCenter; text: modelData.members.length + i18n.t(" ch", i18n.lang); color: modelData.active ? "#000" : Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
                        MouseArea { anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.RightButton; onClicked: function (m) { if (m.button === Qt.RightButton) mgPick.sel = modelData.index; else engine.setMuteGroupActive(modelData.index, !modelData.active) } }
                    }
                }
                Item { id: mgPick; property int sel: 1 }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 4
                Text { text: i18n.t("MEMBERS OF MUTE GROUP", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                Repeater { model: 8; delegate: Rectangle { id: gb42; implicitWidth: 28; implicitHeight: 20; radius: 7; color: mgPick.sel === index + 1 ? Theme.pink : Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb42.color) } GradientStop { position: 1; color: Theme.btnBottom(gb42.color) } } Text { anchors.centerIn: parent; text: index + 1; color: mgPick.sel === index + 1 ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: mgPick.sel = index + 1 } } }
            }
            Flow { Layout.fillWidth: true; spacing: 3
                Repeater { model: engine.stripState; delegate: Rectangle { implicitWidth: 46; implicitHeight: 22; radius: 4
                    property bool member: (modelData.muteGroups >> (mgPick.sel - 1)) & 1
                    color: member ? Theme.pink : Theme.ov(0.06)
                    Text { anchors.centerIn: parent; text: index + 1; color: member ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                    MouseArea { anchors.fill: parent; onClicked: engine.setMuteGroupMember(index, mgPick.sel, !member) } } }
            }
        }
    }
}
