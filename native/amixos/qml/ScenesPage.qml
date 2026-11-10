// SCENES: 16 snapshots of the whole console (faders, pans, mutes, channel DSP, sends, groups, buses, master). STORE writes the console into the
// selected slot, RECALL brings it back; channels marked RECALL SAFE keep what they have during a recall.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Flickable {
    id: page
    property int sel: 0
    contentHeight: col.implicitHeight + 24; clip: true
    readonly property var scene: engine.scenes[sel]
    ColumnLayout {
        id: col; width: parent.width - 20; x: 10; y: 10; spacing: 10
        Panel {
            Layout.fillWidth: true
            title: i18n.t("SCENES", i18n.lang)
            subtitle: "A scene is the whole console at one moment. Pick a slot, STORE the console into it, RECALL it later. Stored on this computer."
            GridLayout {
                columns: width > 900 ? 8 : 4; columnSpacing: 6; rowSpacing: 6; Layout.fillWidth: true
                Repeater {
                    model: engine.scenes
                    delegate: Rectangle {
                        Layout.fillWidth: true; implicitHeight: 64; radius: 8
                        color: page.sel === index ? Qt.rgba(0.984, 0.749, 0.141, 0.16) : Qt.rgba(0, 0, 0, 0.25)
                        border.color: page.sel === index ? Theme.amber : (modelData.current ? Theme.emerald : (modelData.empty ? Theme.lineSoft : Theme.line))
                        ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 2
                            RowLayout { Text { text: (index + 1).toString().padStart(2, "0"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 } Text { visible: modelData.current; text: i18n.t("LIVE", i18n.lang); color: Theme.emerald; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } }
                            Text { Layout.fillWidth: true; text: modelData.empty ? i18n.t("empty", i18n.lang) : modelData.name; color: modelData.empty ? Theme.dim : Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: !modelData.empty; elide: Text.ElideRight }
                            Text { text: modelData.at; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
                        MouseArea { anchors.fill: parent; onClicked: page.sel = index; onDoubleClicked: engine.recallScene(index) }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Text { text: i18n.t("SLOT ", i18n.lang) + (page.sel + 1); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                TextField { id: nameField; Layout.preferredWidth: 220; placeholderText: i18n.t("scene name", i18n.lang); text: page.scene && !page.scene.empty ? page.scene.name : ""; font.family: Theme.mono; font.pixelSize: 10; color: Theme.ink; background: Rectangle { color: Theme.deep; radius: 4; border.color: Theme.line } onEditingFinished: if (page.scene && !page.scene.empty) engine.renameScene(page.sel, text) }
                Rectangle { implicitWidth: 90; implicitHeight: 28; radius: 6; color: Theme.amber; Text { anchors.centerIn: parent; text: i18n.t("STORE", i18n.lang); color: "#000"; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.storeScene(page.sel, nameField.text) } }
                Rectangle { implicitWidth: 90; implicitHeight: 28; radius: 6; color: page.scene && !page.scene.empty ? Theme.emerald : Theme.ov(0.05); Text { anchors.centerIn: parent; text: i18n.t("RECALL", i18n.lang); color: page.scene && !page.scene.empty ? "#000" : Theme.dim; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; enabled: page.scene && !page.scene.empty; onClicked: engine.recallScene(page.sel) } }
                Rectangle { implicitWidth: 70; implicitHeight: 28; radius: 6; color: page.scene && !page.scene.empty ? Qt.rgba(1, 0.165, 0.373, 0.2) : Theme.ov(0.05); border.color: page.scene && !page.scene.empty ? Theme.danger : "transparent"; Text { anchors.centerIn: parent; text: i18n.t("CLEAR", i18n.lang); color: page.scene && !page.scene.empty ? Theme.danger : Theme.dim; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; enabled: page.scene && !page.scene.empty; onClicked: engine.clearScene(page.sel) } }
                Item { Layout.fillWidth: true }
                Text { text: i18n.t("double-click a slot = recall", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
            }
        }
        Panel {
            Layout.fillWidth: true
            title: i18n.t("RECALL SAFE", i18n.lang)
            subtitle: "Channels marked here are not changed when a scene is recalled (the lead vocal stays where you set it)."
            Flow { Layout.fillWidth: true; spacing: 3
                Repeater { model: engine.stripState; delegate: Rectangle { implicitWidth: 64; implicitHeight: 24; radius: 4
                    property bool safe: engine.recallSafe(index)
                    Connections { target: engine; function onScenesChanged() { safe = engine.recallSafe(index) } }
                    color: safe ? Theme.cyan : Theme.ov(0.06)
                    Text { anchors.centerIn: parent; text: modelData.name; color: safe ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; elide: Text.ElideRight; width: parent.width - 6; horizontalAlignment: Text.AlignHCenter }
                    MouseArea { anchors.fill: parent; onClicked: engine.setRecallSafe(index, !safe) } } }
            }
        }
    }
}
