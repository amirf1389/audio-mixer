// BANK COLUMN (1.27): the mixer's layer keys, one column at the left of the faders: channel pages (1-16, 17-32, 33-40 when the console
// has them), ALL CH, the AUX sends on the faders, the buses, MAIN / MATRIX, the DCA groups and ALL; below, the strip modes EDIT (the
// layout keys) and MOVE (tap a strip, then the place it goes: the two swap name, colour, fader, pan, mute and input).
// 96 px wide, keys 44 px high, 8 px apart (the console's 8 px grid).
import QtQuick
import AmixOS
import QtQuick.Layouts

Rectangle {
    id: bc
    objectName: "bankColumn"
    property string current: "ch1"    // ch1 ch2 ch3 allch aux bus main dca all
    property bool editing: false
    property bool moving: false
    signal pick(string layer)
    signal toggleEdit()
    signal toggleMove()
    // 44 px keys where the column has the room; on a short window (720 px) they shrink to fit, never under 28 px
    readonly property int keyH: Math.max(28, Math.min(44, Math.floor((height - 16 - 40 - 8 * (keys.length + 4)) / (keys.length + 2))))
    implicitWidth: 96; radius: Theme.radius; color: Theme.card; border.color: Theme.line
    readonly property var keys: {
        const l = [["ch1", "CH 1-16"]]
        if (engine.strips > 16) l.push(["ch2", "CH 17-32"])
        if (engine.strips > 32) l.push(["ch3", engine.strips > 40 ? "CH 33-48" : "CH 33-40"])
        return l.concat([["allch", "ALL CH"], ["aux", "AUX"], ["bus", "BUSES"], ["main", "MAIN / MATRIX"], ["dca", "DCA"], ["all", "ALL"]])
    }
    component Key: Rectangle {
        id: k
        property string label; property bool lit: false; property color hue: Theme.accent; signal hit()
        Layout.fillWidth: true; implicitHeight: bc.keyH; radius: Theme.radiusS
        readonly property color face: lit ? Theme.harm(hue) : Theme.ov(0.06)
        gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(k.face) } GradientStop { position: 1; color: Theme.btnBottom(k.face) } }
        border.color: lit ? Qt.lighter(hue, 1.2) : Theme.lineSoft
        Rectangle { visible: Theme.cockpit; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; anchors.leftMargin: 4; width: 3; height: parent.height - 16; radius: 1; color: k.lit ? k.hue : Theme.ov(0.12) }
        Text { anchors.fill: parent; anchors.margins: 4; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; wrapMode: Text.WordWrap; text: i18n.t(k.label, i18n.lang)
            color: k.lit && !Theme.cockpit ? "#000" : Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: k.hit() }
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 8; spacing: 8
        Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: i18n.t("LAYERS", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
        Repeater { model: bc.keys
            delegate: Key { objectName: "layer_" + modelData[0]; label: modelData[1]; lit: bc.current === modelData[0]; onHit: bc.pick(modelData[0]) } }
        Item { Layout.fillHeight: true }
        Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: i18n.t("STRIPS", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
        Key { objectName: "stripEdit"; label: "EDIT"; lit: bc.editing; hue: Theme.amber; onHit: bc.toggleEdit() }
        Key { objectName: "stripMove"; label: "MOVE"; lit: bc.moving; hue: Theme.cyan; onHit: bc.toggleMove() }
    }
}
