// I/O FRAME: the front panel (four card slots with LEDs, the status display, LOCATE), the cards and their channels, the frame bus and GPIO.
import QtQuick
import QtQuick.Layouts
import AmixOS

Flickable {
    id: page
    objectName: "framePage"
    contentHeight: col.implicitHeight + 40
    clip: true
    property int sel: 0                                // the slot shown below the panel
    readonly property var cards: frame.cards
    readonly property var card: cards[sel] || ({ channels: [] })
    property bool blink: false
    Timer { interval: 300; running: frame.locating; repeat: true; onTriggered: page.blink = !page.blink; onRunningChanged: if (!running) page.blink = false }
    function lvl(strip) { if (strip < 0) return -120; const m = engine.meterAt(strip); return m.peakDb !== undefined ? m.peakDb : -120 }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked()
        implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 24; radius: 4; color: on ? onColor : Theme.ov(0.08); border.color: on ? onColor : Theme.line
        Text { id: bl; anchors.centerIn: parent; text: i18n.t(label, i18n.lang); color: parent.on ? "#000" : Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked() } }
    component Led: Rectangle { property color lit: Theme.emerald; property bool on: false; width: 9; height: 9; radius: 5; color: on ? lit : Theme.ov(0.08); border.color: Theme.ov(0.2) }

    ColumnLayout {
        id: col
        width: parent.width - 32; x: 16; y: 16; spacing: 12

        // ── THE FRONT PANEL ───────────────────────────────────────────────────────────────────────────────────────────────────────────
        Rectangle {
            objectName: "framePanel"
            Layout.fillWidth: true; implicitHeight: 196; radius: 10
            color: "#15181d"; border.width: frame.locating ? 3 : 1; border.color: frame.locating && page.blink ? Theme.cyan : Theme.line
            RowLayout {
                anchors.fill: parent; anchors.margins: 14; spacing: 14
                ColumnLayout { spacing: 4; Layout.preferredWidth: 150
                    Text { text: "AMF X1"; color: Theme.orange; font.family: Theme.mono; font.pixelSize: 18; font.weight: Font.Black; font.letterSpacing: 4 }
                    Text { text: i18n.t("I/O FRAME", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.letterSpacing: 3 }
                    RowLayout { spacing: 6; Layout.topMargin: 10
                        Led { on: engine.running; lit: Theme.emerald } Text { text: i18n.t("POWER / ENGINE", i18n.lang); color: Theme.muted; font.pixelSize: 8; font.family: Theme.mono } }
                    RowLayout { spacing: 6
                        Led { on: engine.telv("xruns", engine.meterTick) > 0; lit: Theme.amber } Text { text: i18n.t("FAULT (DROP-OUTS)", i18n.lang); color: Theme.muted; font.pixelSize: 8; font.family: Theme.mono } }
                    RowLayout { spacing: 6
                        Led { on: frame.locating && page.blink; lit: Theme.cyan } Text { text: i18n.t("LOCATE", i18n.lang); color: Theme.muted; font.pixelSize: 8; font.family: Theme.mono } }
                    Btn { objectName: "frameLocate"; Layout.topMargin: 6; label: frame.locating ? "STOP LOCATE" : "LOCATE"; on: frame.locating; onColor: Theme.cyan; onClicked: frame.locate(!frame.locating) }
                }
                // four card slots: name, a signal / clip / 48 V LED row per channel
                Repeater {
                    model: page.cards
                    delegate: Rectangle {
                        objectName: "frameSlot" + index
                        Layout.fillWidth: true; Layout.fillHeight: true; radius: 6
                        color: page.sel === index ? Theme.ov(0.12) : "#0d0f12"; border.color: page.sel === index ? Theme.orange : Theme.lineSoft
                        MouseArea { anchors.fill: parent; onClicked: page.sel = index }
                        ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 4
                            Text { text: i18n.t("SLOT", i18n.lang) + " " + (index + 1); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.letterSpacing: 2 }
                            Text { text: i18n.t(modelData.name, i18n.lang); color: modelData.type ? Theme.ink : Theme.dim; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                            Repeater { model: modelData.channels
                                delegate: RowLayout { spacing: 5
                                    readonly property real db: modelData.dir === "in" ? (engine.meterTick, page.lvl(modelData.strip)) : -120
                                    Text { text: modelData.label; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; Layout.preferredWidth: 58 }
                                    Led { on: db > -50; lit: Theme.emerald }   // signal
                                    Led { on: db > -1; lit: Theme.danger }     // clip
                                    Led { visible: modelData.phantom !== undefined; on: modelData.phantom; lit: Theme.amber }   // 48 V
                                    Text { text: modelData.dir === "in" ? "IN" : "OUT"; color: Theme.dim; font.pixelSize: 7; font.family: Theme.mono } } }
                            Item { Layout.fillHeight: true } } }
                }
                // the status display
                Rectangle { Layout.preferredWidth: 230; Layout.fillHeight: true; radius: 4; color: "#071a12"; border.color: "#1f5a3f"
                    Column { anchors.fill: parent; anchors.margins: 8; spacing: 3
                        Repeater { model: [
                                (engine.running ? engine.backend.toUpperCase() + "  " + (engine.sampleRate / 1000).toFixed(1) + " kHz" : i18n.t("ENGINE STOPPED", i18n.lang)),
                                i18n.t("BLOCK ", i18n.lang) + engine.blockFrames + "  " + i18n.t("LOAD ", i18n.lang) + (engine.telv("loadPct", engine.meterTick) || 0).toFixed(0) + " %",
                                i18n.t("BUS ", i18n.lang) + frame.busUsed + " / 256  " + i18n.t("CARDS ", i18n.lang) + page.cards.filter(c => c.type).length + " / 4",
                                i18n.t("DROP-OUTS ", i18n.lang) + (engine.telv("xruns", engine.meterTick) || 0),
                                "GPIO IN " + frame.logicIn.map(v => v ? "1" : "·").join(""),
                                "GPIO OUT " + frame.logicOut.map(v => v ? "1" : "·").join(""),
                                frame.locating ? ">> " + i18n.t("LOCATE", i18n.lang) + " <<" : engine.edition ]
                            delegate: Text { text: modelData; color: "#5ef0a8"; font.family: Theme.mono; font.pixelSize: 9 } } } }
            }
        }

        // ── THE SELECTED CARD ─────────────────────────────────────────────────────────────────────────────────────────────────────────
        Panel {
            Layout.fillWidth: true
            title: i18n.t("SLOT", i18n.lang) + " " + (page.sel + 1) + " · " + i18n.t(page.card.name || "EMPTY", i18n.lang)
            subtitle: page.card.note ? i18n.t(page.card.note, i18n.lang) : i18n.t("Pick a card for this slot. Its channels are your sound card's real channels; APPLY routes them.", i18n.lang)
            ColumnLayout { Layout.fillWidth: true; spacing: 8
                Flow { Layout.fillWidth: true; spacing: 6
                    Btn { label: "EMPTY"; on: !page.card.type; onClicked: frame.setCardType(page.sel, "") }
                    Repeater { model: frame.cardTypes; delegate: Btn { objectName: "frameType_" + modelData.type; label: modelData.name; on: page.card.type === modelData.type; onClicked: frame.setCardType(page.sel, modelData.type) } } }
                Repeater {
                    model: page.card.channels
                    delegate: RowLayout { spacing: 8; Layout.fillWidth: true
                        readonly property var ch: modelData
                        Text { text: ch.label; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; Layout.preferredWidth: 80 }
                        Text { text: i18n.t("BUS", i18n.lang) + " " + ch.bus; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9; Layout.preferredWidth: 56 }
                        Text { text: ch.dir === "in" ? i18n.t("DEVICE IN", i18n.lang) : i18n.t("DEVICE OUT", i18n.lang); color: Theme.muted; font.pixelSize: 9 }
                        Combo { Layout.preferredWidth: 90; model: Array.from({ length: 32 }, (_, i) => String(i + 1)); currentIndex: Math.max(0, ch.device); onActivated: frame.setChannelDevice(page.sel, ch.n, currentIndex) }
                        Text { visible: ch.dir === "in"; text: i18n.t("→ STRIP", i18n.lang); color: Theme.muted; font.pixelSize: 9 }
                        Combo { visible: ch.dir === "in"; Layout.preferredWidth: 90; model: Array.from({ length: 32 }, (_, i) => "CH " + (i + 1)); currentIndex: Math.max(0, ch.strip); onActivated: frame.setChannelStrip(page.sel, ch.n, currentIndex) }
                        Text { visible: ch.dir === "out"; text: i18n.t("← SOURCE", i18n.lang); color: Theme.muted; font.pixelSize: 9 }
                        Combo { visible: ch.dir === "out"; Layout.preferredWidth: 150; model: engine.outputSources.map(s => s.name); currentIndex: Math.max(0, ch.source); onActivated: frame.setChannelSource(page.sel, ch.n, currentIndex) }
                        Btn { visible: ch.phantom !== undefined && page.card.phantomCard; label: "48 V"; on: ch.phantom; onColor: Theme.amber; onClicked: frame.setPhantom(page.sel, ch.n, !ch.phantom) }
                        Item { Layout.fillWidth: true } } }
                RowLayout { spacing: 8
                    Btn { objectName: "frameApply"; label: "APPLY"; on: true; onColor: Theme.emerald; onClicked: frame.apply() }
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.dim; font.pixelSize: 9
                        text: i18n.t("48 V: most interfaces switch phantom power on the unit itself (software cannot reach it on class-compliant devices); the switch here is kept with the card and shown on the panel.", i18n.lang) } }
            }
        }

        // ── GPIO ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
        Panel {
            Layout.fillWidth: true
            title: i18n.t("GPIO · 12 CONTROL INPUTS · 6 LOGIC OUTPUTS", i18n.lang)
            subtitle: i18n.t("Inputs: click here, OSC /gpio/in/N 1|0, MIDI notes 36-47, or a USB GPIO box (I3=1). Outputs: set by LOGIC / GPIO objects in the racks, sent as OSC /gpio/out/N and to the box (O2=1). LOCATE: OSC /locate, MIDI note 48, or LOCATE from the box.", i18n.lang)
            ColumnLayout { Layout.fillWidth: true; spacing: 8
                Flow { Layout.fillWidth: true; spacing: 6
                    Repeater { model: frame.logicIn; delegate: Btn { objectName: "gpioIn" + index; label: "IN " + (index + 1); on: modelData; onColor: Theme.emerald; onClicked: frame.setLogicIn(index, !modelData) } } }
                Flow { Layout.fillWidth: true; spacing: 10
                    Repeater { model: frame.logicOut; delegate: Row { spacing: 5; Led { on: modelData; lit: Theme.orange; anchors.verticalCenter: parent.verticalCenter } Text { text: "OUT " + (index + 1); color: modelData ? Theme.ink : Theme.dim; font.family: Theme.mono; font.pixelSize: 10 } } } }
                RowLayout { spacing: 8
                    Text { text: i18n.t("GPIO BOX (USB SERIAL)", i18n.lang); color: Theme.muted; font.pixelSize: 9; font.family: Theme.mono }
                    Combo { id: portPick; Layout.preferredWidth: 180; model: [i18n.t("— none —", i18n.lang)].concat(frame.serialPorts); currentIndex: Math.max(0, frame.serialPorts.indexOf(frame.serialPort) + 1) }
                    Btn { label: "OPEN"; onClicked: frame.openSerial(portPick.currentIndex > 0 ? frame.serialPorts[portPick.currentIndex - 1] : "") }
                    Btn { label: "RESCAN"; onClicked: frame.rescanSerial() }
                    Text { text: frame.serialNote; color: Theme.dim; font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideRight } }
                Rectangle { Layout.fillWidth: true; implicitHeight: 92; radius: 4; color: Theme.deep; border.color: Theme.lineSoft
                    Column { anchors.fill: parent; anchors.margins: 6; spacing: 1; clip: true
                        Repeater { model: frame.log.slice(0, 7); delegate: Text { text: modelData; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 } } } }
            }
        }
    }
}
