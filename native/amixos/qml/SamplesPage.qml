// SAMPLES: 100 built-in one-shots (drums, percussion, bass, leads, pads, keys, FX, test signals) on pads, played polyphonically into the
// master. Rendered in this program at the engine's rate (no files needed); EXPORT writes them as WAV into ~/.amixos/samples for ARRANGE
// or any other program. Keys 1-9 / 0 play the first ten pads of the shown category.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "samplesPage"
    property string cat: "ALL"
    readonly property var all: samples.list
    function shown() { const o = []; for (let i = 0; i < all.length; ++i) if (cat === "ALL" || all[i].category === cat) o.push(all[i]); return o }
    readonly property var padColors: ({ "Drums": Theme.amber, "Percussion": "#fb7185", "Bass": Theme.emerald, "Lead": Theme.cyan, "Pad": "#a78bfa", "Keys": "#f472b6", "FX": "#38bdf8", "Test": Theme.muted })
    focus: true
    Keys.onPressed: function (e) { const k = e.key; const n = k === Qt.Key_0 ? 9 : k >= Qt.Key_1 && k <= Qt.Key_9 ? k - Qt.Key_1 : -1; if (n >= 0) { const l = shown(); if (n < l.length) samples.play(l[n].id); e.accepted = true } else if (k === Qt.Key_Escape) samples.stopAll() }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 24; radius: 5; color: on ? onColor : Qt.rgba(1, 1, 1, 0.08); border.color: Theme.lineSoft
        Text { id: bl; anchors.centerIn: parent; text: label; color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle { Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Text { text: "SAMPLES:"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Repeater { model: ["ALL"].concat(samples.categories); delegate: Rectangle { implicitWidth: cl.implicitWidth + 14; implicitHeight: 20; radius: 4; color: page.cat === modelData ? Theme.amber : Qt.rgba(1, 1, 1, 0.06)
                    Text { id: cl; anchors.centerIn: parent; text: modelData.toUpperCase(); color: page.cat === modelData ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.cat = modelData } } }
                Item { Layout.fillWidth: true }
                Text { text: samples.voices + " playing · peak " + (samples.peakDb > -100 ? samples.peakDb.toFixed(1) : "−∞") + " dBFS"; color: samples.voices > 0 ? Theme.emerald : Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                Text { text: samples.message; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideRight; Layout.maximumWidth: 300 } } }
        RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                GridView { id: grid; anchors.fill: parent; anchors.margins: 8; clip: true; cellWidth: 118; cellHeight: 62; model: page.shown()
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Item { width: grid.cellWidth; height: grid.cellHeight
                        Rectangle { id: pad; anchors.fill: parent; anchors.margins: 3; radius: 7; property color pc: page.padColors[modelData.category] || Theme.muted; property bool hot: samples.lastPlayed === modelData.id && samples.voices > 0
                            color: hot ? pc : Qt.rgba(1, 1, 1, 0.05); border.color: hot ? pc : Qt.rgba(pc.r, pc.g, pc.b, 0.45); border.width: 1
                            Rectangle { anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 1; height: 3; radius: 2; color: pad.pc }
                            ColumnLayout { anchors.fill: parent; anchors.margins: 6; anchors.topMargin: 8; spacing: 1
                                Text { Layout.fillWidth: true; elide: Text.ElideRight; text: modelData.name; color: pad.hot ? "#000" : Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                                Text { text: modelData.category.toUpperCase() + " · " + modelData.seconds.toFixed(1) + " s"; color: pad.hot ? "#222" : Theme.dim; font.family: Theme.mono; font.pixelSize: 6 }
                                Item { Layout.fillHeight: true }
                                Text { text: (index < 10 ? "KEY " + ((index + 1) % 10) + " · " : "") + "#" + modelData.id; color: pad.hot ? "#222" : Theme.dim; font.family: Theme.mono; font.pixelSize: 6 } }
                            MouseArea { anchors.fill: parent; onPressed: samples.play(modelData.id) } } } } }
            Rectangle { Layout.preferredWidth: 230; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 8
                    Text { text: "PLAYER"; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
                    Text { text: "GAIN  " + (samples.gainDb > 0 ? "+" : "") + samples.gainDb.toFixed(1) + " dB"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8 }
                    Slider { Layout.fillWidth: true; from: -40; to: 12; stepSize: 0.5; value: samples.gainDb; onMoved: samples.gainDb = value }
                    Btn { label: "STOP ALL  (Esc)"; onColor: Theme.danger; on: samples.voices > 0; onClicked: samples.stopAll() }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.lineSoft }
                    Text { text: "EXPORT"; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
                    Text { Layout.fillWidth: true; text: "Writes the 100 samples as 24-bit WAV into\n" + samples.folder + "\nfor ARRANGE, STREAM & MEDIA or any other program. Files that exist are kept."; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WrapAnywhere }
                    Btn { label: "EXPORT 100 WAV"; on: true; onColor: Theme.cyan; onClicked: samples.exportAll() }
                    Item { Layout.fillHeight: true }
                    Text { Layout.fillWidth: true; text: "8 voices; a ninth trigger steals the oldest. Rendered in the program (sine, FM, noise, filters, envelopes): no sample files are shipped, nothing to download, identical on every computer."; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap } } } }
    }
}
