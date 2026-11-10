// EFFECTS: one card per aux bus. Pick an effect (REVERB, TAPE DELAY, CHORUS, DRIVE, or none), set its parameters with the plug-in's own display
// text, MIX (dry/wet), BYPASS and the bus RETURN into the master. Feed a bus from SEND TO: the effect runs on the bus sum, after the bus level.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "effectsPage"
    property int bus: 0
    property int tick: 0                                   // bumps on busesChanged so busFx() re-reads
    readonly property var catalog: engine.fxCatalog()
    readonly property var b: engine.busState[bus]
    readonly property var fx: (tick, engine.busFx(bus))
    readonly property var bm: engine.busMeters[bus]
    Connections { target: engine; function onBusesChanged() { page.tick++ } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Text { text: "EFFECTS:"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Repeater { model: engine.busState; delegate: Rectangle { implicitWidth: bl.implicitWidth + 16; implicitHeight: 20; radius: 4; color: page.bus === index ? Theme.amber : Qt.rgba(1, 1, 1, 0.06); border.color: modelData.fx !== "" ? Theme.violet : "transparent"
                    Text { id: bl; anchors.centerIn: parent; text: modelData.name + (modelData.fx !== "" ? " ·" : ""); color: page.bus === index ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.bus = index } } }
                Item { Layout.fillWidth: true }
                Text { text: page.fx.kind !== "" ? page.fx.name + " on " + (page.b ? page.b.name : "") : "no effect on this bus"; color: page.fx.kind !== "" ? Theme.violet : Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            // the effect chooser
            Rectangle {
                Layout.preferredWidth: 190; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 10; spacing: 4
                    Text { text: "EFFECT"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.letterSpacing: 2 }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 30; radius: 6; color: page.fx.kind === "" ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(1, 1, 1, 0.05); border.color: Theme.lineSoft
                        Text { anchors.centerIn: parent; text: "NONE"; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setBusFx(page.bus, "") } }
                    Repeater { model: page.catalog; delegate: Rectangle { Layout.fillWidth: true; implicitHeight: 30; radius: 6; color: page.fx.kind === modelData.kind ? Theme.violet : Qt.rgba(1, 1, 1, 0.05); border.color: Theme.lineSoft
                        Text { anchors.centerIn: parent; text: modelData.name; color: page.fx.kind === modelData.kind ? "#000" : Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setBusFx(page.bus, modelData.kind) } } }
                    Item { Layout.fillHeight: true }
                    Text { Layout.fillWidth: true; text: "Send strips to this bus on SEND TO (POST for effects). The wet signal returns to the master at RETURN and to the bus outputs " + (page.b ? page.b.outputs : "") + "."; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }
                }
            }
            // parameters
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 12; spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: page.fx.kind !== "" ? page.fx.name.toUpperCase() : "NO EFFECT"; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 12; font.bold: true; font.letterSpacing: 1.5 }
                        Item { Layout.fillWidth: true }
                        Rectangle { visible: page.fx.kind !== ""; implicitWidth: 64; implicitHeight: 22; radius: 4; color: page.fx.bypass ? Theme.amber : Qt.rgba(1, 1, 1, 0.08); border.color: Theme.lineSoft
                            Text { anchors.centerIn: parent; text: "BYPASS"; color: page.fx.bypass ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setBusFxBypass(page.bus, !page.fx.bypass) } }
                        Rectangle { visible: page.fx.kind !== ""; implicitWidth: 64; implicitHeight: 22; radius: 4; color: Qt.rgba(1, 1, 1, 0.08); border.color: Theme.lineSoft
                            Text { anchors.centerIn: parent; text: "DEFAULTS"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                            MouseArea { anchors.fill: parent; onClicked: { const ps = page.fx.params; for (let i = 0; i < ps.length; ++i) engine.setBusFxParam(page.bus, ps[i].id, ps[i]["default"]); engine.setBusFxMix(page.bus, 1) } } }
                    }
                    Text { visible: page.fx.kind === ""; text: "Pick an effect on the left. Each aux bus can carry one: a reverb for vocals on BUS 1, a tape delay on BUS 2, a chorus on BUS 3 ..."; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Repeater {
                        model: page.fx.params
                        delegate: RowLayout {
                            Layout.fillWidth: true; spacing: 10
                            Text { Layout.preferredWidth: 90; text: modelData.title.toUpperCase(); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                            Slider { Layout.fillWidth: true; from: 0; to: 1; stepSize: 0.001; value: modelData.value; onMoved: engine.setBusFxParam(page.bus, modelData.id, value) }
                            Text { Layout.preferredWidth: 80; text: modelData.text; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; horizontalAlignment: Text.AlignRight }
                        }
                    }
                    RowLayout {
                        visible: page.fx.kind !== ""
                        Layout.fillWidth: true; spacing: 10
                        Text { Layout.preferredWidth: 90; text: "MIX"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                        Slider { Layout.fillWidth: true; from: 0; to: 1; stepSize: 0.01; value: page.fx.mix; onMoved: engine.setBusFxMix(page.bus, value) }
                        Text { Layout.preferredWidth: 80; text: Math.round(page.fx.mix * 100) + " % wet"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; horizontalAlignment: Text.AlignRight }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 10
                        Text { Layout.preferredWidth: 90; text: "RETURN"; color: Theme.emerald; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                        Slider { Layout.fillWidth: true; from: -60; to: 10; stepSize: 0.5; value: page.b ? page.b.returnDb : -60; onMoved: engine.setBusReturn(page.bus, value) }
                        Text { Layout.preferredWidth: 80; text: page.b ? (page.b.returnDb <= -60 ? "off" : page.b.returnDb.toFixed(1) + " dB") : ""; color: Theme.emerald; font.family: Theme.mono; font.pixelSize: 9; horizontalAlignment: Text.AlignRight }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 10
                        Text { Layout.preferredWidth: 90; text: "BUS LEVEL"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                        Slider { Layout.fillWidth: true; from: -60; to: 10; stepSize: 0.5; value: page.b ? page.b.db : 0; onMoved: engine.setBusLevel(page.bus, value) }
                        Text { Layout.preferredWidth: 80; text: page.b ? page.b.db.toFixed(1) + " dB" : ""; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; horizontalAlignment: Text.AlignRight }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 10
                        Text { Layout.preferredWidth: 90; text: "BUS OUT"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                        MeterBar { Layout.fillWidth: true; implicitHeight: 10; peakDb: page.bm ? page.bm.peakDb : -120; rmsDb: page.bm ? page.bm.rmsDb : -120 }
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
