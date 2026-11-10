// ROUTING: two matrices. INPUTS: which device input feeds each strip (one dot per row; a row with no dot reads silence; one input may feed several
// strips). OUTPUTS: what each device output carries: OFF, MASTER L / R, MONITOR L / R (the control room, after DIM / MONO / MUTE) or a bus side.
// Defaults: input n -> CH n, MONITOR on OUT 1-2, BUS k on OUT 3+2k / 4+2k. Inputs and outputs beyond what the open device has are shown dim.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "routingPage"
    property int tab: 0          // 0 inputs, 1 outputs
    property int bank: 0         // strips 1-16 / 17-32 (inputs) ; outputs 1-16 / 17-32
    readonly property var inRoutes: engine.inputRoutes
    readonly property var outRoutes: engine.outputRoutes
    readonly property var sources: engine.outputSources
    readonly property int cell: 22
    function srcColor(group) { return group === "master" ? Theme.orange : group === "monitor" ? Theme.emerald : group === "bus" ? Theme.cyan : Theme.dim }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Text { text: "ROUTING:"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Repeater { model: ["INPUTS → STRIPS", "SOURCES → OUTPUTS"]; delegate: Rectangle { implicitWidth: tl.implicitWidth + 16; implicitHeight: 20; radius: 4; color: page.tab === index ? Theme.amber : Theme.ov(0.06)
                    Text { id: tl; anchors.centerIn: parent; text: modelData; color: page.tab === index ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.tab = index } } }
                Repeater { model: page.tab === 0 ? ["CH 01-16", "CH 17-32"] : ["OUT 1-16", "OUT 17-32"]; delegate: Rectangle { implicitWidth: 66; implicitHeight: 20; radius: 4; color: page.bank === index ? Theme.ov(0.16) : Theme.ov(0.06); Text { anchors.centerIn: parent; text: modelData; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.bank = index } } }
                Item { Layout.fillWidth: true }
                Text { text: engine.running ? (engine.deviceInputs + " device inputs · " + engine.deviceOutputs + " outputs") : "no device open: the matrix is stored and applied when one opens"; color: engine.running ? Theme.emerald : Theme.amber; font.family: Theme.mono; font.pixelSize: 8 }
                Rectangle { implicitWidth: 70; implicitHeight: 20; radius: 4; color: Theme.ov(0.08); border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: "DEFAULTS"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.resetRouting() } }
            }
        }
        // INPUTS -> STRIPS: rows = strips of this bank, columns = device inputs 1..32 (+ NONE)
        Rectangle {
            visible: page.tab === 0
            Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; clip: true
            Flickable { anchors.fill: parent; anchors.margins: 10; contentWidth: inGrid.implicitWidth; contentHeight: inGrid.implicitHeight; boundsBehavior: Flickable.StopAtBounds
                ColumnLayout { id: inGrid; spacing: 2
                    RowLayout { spacing: 2
                        Text { Layout.preferredWidth: 96; text: "STRIP  ⟵  INPUT"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
                        Rectangle { implicitWidth: page.cell + 8; implicitHeight: page.cell; color: "transparent"; Text { anchors.centerIn: parent; text: "NONE"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 6; font.bold: true } }
                        Repeater { model: 32; delegate: Rectangle { implicitWidth: page.cell; implicitHeight: page.cell; color: "transparent"
                            Text { anchors.centerIn: parent; text: index + 1; color: index < engine.deviceInputs ? Theme.ink : Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.bold: index < engine.deviceInputs } } } }
                    Repeater { model: 16
                        delegate: RowLayout { spacing: 2
                            readonly property int strip: page.bank * 16 + index
                            readonly property var st: engine.stripState[strip]
                            readonly property int route: page.inRoutes[strip] === undefined ? strip : page.inRoutes[strip]
                            Rectangle { Layout.preferredWidth: 96; implicitHeight: page.cell; radius: 3; color: Theme.ov(0.04)
                                Rectangle { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 3; radius: 1; color: Theme.stripColors[st ? st.color : 0] }
                                Text { anchors.centerIn: parent; text: "CH " + (strip + 1 < 10 ? "0" : "") + (strip + 1) + "  " + (st ? st.name : ""); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 7; elide: Text.ElideRight; width: parent.width - 10; horizontalAlignment: Text.AlignHCenter } }
                            Rectangle { implicitWidth: page.cell + 8; implicitHeight: page.cell; radius: 3; color: route < 0 ? Theme.danger : Theme.ov(0.04); border.color: Theme.lineSoft
                                Text { anchors.centerIn: parent; text: route < 0 ? "—" : ""; color: "#000"; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                                MouseArea { anchors.fill: parent; onClicked: engine.setInputRoute(strip, -1) } }
                            Repeater { model: 32
                                delegate: Rectangle { implicitWidth: page.cell; implicitHeight: page.cell; radius: 3
                                    readonly property bool on: route === index
                                    readonly property bool avail: index < engine.deviceInputs
                                    color: on ? (avail ? Theme.cyan : Theme.amber) : (avail ? Theme.ov(0.05) : Theme.ov(0.015)); border.color: on ? "transparent" : Theme.lineSoft
                                    Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 4; color: "#000"; visible: on }
                                    MouseArea { anchors.fill: parent; onClicked: engine.setInputRoute(strip, index) } } } } }
                    Text { text: "A dot on an unavailable input (amber) is kept and becomes live when a device with that many inputs opens. One input may feed several strips (a mic to two channels with different processing)."; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap; Layout.preferredWidth: 700; Layout.topMargin: 6 }
                }
            }
        }
        // SOURCES -> OUTPUTS: rows = outputs of this bank, columns = sources
        Rectangle {
            visible: page.tab === 1
            Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; clip: true
            Flickable { anchors.fill: parent; anchors.margins: 10; contentWidth: outGrid.implicitWidth; contentHeight: outGrid.implicitHeight; boundsBehavior: Flickable.StopAtBounds
                ColumnLayout { id: outGrid; spacing: 2
                    RowLayout { spacing: 2
                        Text { Layout.preferredWidth: 96; text: "OUTPUT  ⟵  SOURCE"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
                        Repeater { model: page.sources; delegate: Item { implicitWidth: page.cell + 6; implicitHeight: 62
                            Text { x: (parent.width + 6) / 2; y: parent.height - 4; text: modelData.name; color: page.srcColor(modelData.group); font.family: Theme.mono; font.pixelSize: 6; font.bold: true; rotation: -90; transformOrigin: Item.BottomLeft } } } }
                    Repeater { model: 16
                        delegate: RowLayout { spacing: 2
                            readonly property int out: page.bank * 16 + index
                            readonly property int route: page.outRoutes[out] === undefined ? 0 : page.outRoutes[out]
                            readonly property bool avail: out < engine.deviceOutputs
                            Rectangle { Layout.preferredWidth: 96; implicitHeight: page.cell; radius: 3; color: Theme.ov(0.04)
                                Text { anchors.centerIn: parent; text: "OUT " + (out + 1) + (avail ? "" : "  ·"); color: avail ? Theme.ink : Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.bold: avail } }
                            Repeater { model: page.sources
                                delegate: Rectangle { implicitWidth: page.cell + 6; implicitHeight: page.cell; radius: 3
                                    readonly property bool on: route === modelData.index
                                    color: on ? page.srcColor(modelData.group) : (avail ? Theme.ov(0.05) : Theme.ov(0.015)); border.color: on ? "transparent" : Theme.lineSoft
                                    Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 4; color: "#000"; visible: on && modelData.group !== "off" }
                                    Text { anchors.centerIn: parent; text: "—"; color: "#000"; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; visible: on && modelData.group === "off" }
                                    MouseArea { anchors.fill: parent; onClicked: engine.setOutputRoute(out, modelData.index) } } } } }
                    Text { text: "MONITOR is the control-room signal (master or a bus, after the monitor level, DIM, MONO and MUTE). Put MASTER on the outputs that feed the PA or recorder and MONITOR on the speakers: muting the monitor then never mutes the show. A source may go to several outputs."; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap; Layout.preferredWidth: 700; Layout.topMargin: 6 }
                }
            }
        }
    }
}
