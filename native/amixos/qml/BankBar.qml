// BANK BAR: the channel pages of 16. Up to 32 channels it is the two buttons the console always had ("CH 01-16", "CH 17-32"). With
// more (CHANNEL EXTEND packs, ULTIMATE: 300 channels) it becomes  ‹  CH 033-048  ›  3/19  with a list of every page behind the label,
// so nineteen pages take the room of two buttons.
import QtQuick
import AmixOS   // this module: brings the Theme singleton
import QtQuick.Controls

Row {
    id: bb
    property int bank: 0
    property int count: engine.strips      // how many channels (or outputs) the pages cover
    property string prefix: "CH"
    property bool pad: true                // "CH 01-16" or "OUT 1-16"
    property int buttonWidth: 66
    property bool accent: false            // the MIXER's own look: the page in use in amber
    readonly property int banks: Math.max(1, Math.ceil(count / 16))
    readonly property bool paged: banks > 2
    signal picked(int b)
    spacing: 6
    onBanksChanged: if (bank >= banks) picked(banks - 1)
    function label(b) {
        const from = b * 16 + 1, to = Math.min(count, (b + 1) * 16), n = !pad ? 0 : count > 99 ? 3 : 2
        const z = v => { let t = String(v); while (t.length < n) t = "0" + t; return t }
        return prefix + " " + z(from) + "-" + z(to)
    }
    component Key: Rectangle {
        id: key
        property string label; property bool on: false; signal hit()
        implicitWidth: bb.buttonWidth; implicitHeight: 20; radius: 7
        color: on ? (bb.accent ? Theme.amber : Theme.ov(0.16)) : Theme.ov(0.06)
        gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(key.color) } GradientStop { position: 1; color: Theme.btnBottom(key.color) } }
        Text { anchors.centerIn: parent; text: key.label; color: key.on && bb.accent ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
        MouseArea { anchors.fill: parent; onClicked: key.hit() }
    }
    // up to 32: one button per page, as ever
    Repeater { model: bb.paged ? 0 : bb.banks; delegate: Key { label: bb.label(index); on: bb.bank === index; onHit: bb.picked(index) } }
    // more: back, the page in use (press: every page), forward, where we are
    Key { visible: bb.paged; objectName: "bankPrev"; implicitWidth: 24; label: "‹"; opacity: bb.bank > 0 ? 1 : 0.4; onHit: if (bb.bank > 0) bb.picked(bb.bank - 1) }
    Key { id: here; visible: bb.paged; objectName: "bankHere"; implicitWidth: 96; label: bb.label(bb.bank) + "  ▾"; on: true; onHit: list.open()
        Popup { id: list; objectName: "bankList"; y: here.height + 4; padding: 8; width: 5 * 80 + 4 * 4 + 16
            background: Rectangle { radius: 8; color: Theme.cardSolid; border.color: Theme.line }
            contentItem: Flow { spacing: 4
                Repeater { model: list.visible ? bb.banks : 0; delegate: Key { implicitWidth: 80; label: bb.label(index); on: bb.bank === index; onHit: { bb.picked(index); list.close() } } } } } }
    Key { visible: bb.paged; objectName: "bankNext"; implicitWidth: 24; label: "›"; opacity: bb.bank < bb.banks - 1 ? 1 : 0.4; onHit: if (bb.bank < bb.banks - 1) bb.picked(bb.bank + 1) }
    Text { visible: bb.paged; anchors.verticalCenter: parent.verticalCenter; text: (bb.bank + 1) + "/" + bb.banks; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
}
