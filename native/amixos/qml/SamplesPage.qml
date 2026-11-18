// SAMPLES: 1000 CUSTOM PADS (20 banks of 50), 1000 SOUND EFFECTS (20 families of 50) and the 100 BUILT-IN one-shots, played polyphonically into
// the master. A pad plays a built-in sample, a sound effect or an audio file on this computer, at its own gain and colour; the pads are kept in
// one file that the remote service shares, so the phones show and play the same pads (a change on either side reaches the other within a
// second). Everything is rendered in the program: no sample files are shipped, nothing is downloaded. Keys 1-9 / 0 play the first ten pads shown.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "samplesPage"
    property string view: "PADS"            // PADS, EFFECTS, BUILT-IN
    property int bank: 0                    // 0..19: pads bank*50+1 .. bank*50+50
    property int sel: 1                     // the pad being edited
    property string family: samples.sfxFamilies.length ? samples.sfxFamilies[0] : ""
    property int pickedSfx: 0               // an effect chosen in EFFECTS, ready to put on the selected pad
    property string cat: "ALL"
    readonly property var all: samples.list
    readonly property var colors: [Theme.amber, Theme.cyan, Theme.emerald, "#a78bfa", "#f472b6", "#fb7185", "#38bdf8", Theme.muted]
    readonly property var padColors: ({ "Drums": Theme.amber, "Percussion": "#fb7185", "Bass": Theme.emerald, "Lead": Theme.cyan, "Pad": "#a78bfa", "Keys": "#f472b6", "FX": "#38bdf8", "Test": Theme.muted })
    readonly property var bankPads: { samples.padsVersion; return samples.pads(bank * 50 + 1, bank * 50 + 50) }
    readonly property var selPad: { samples.padsVersion; return samples.pad(sel) }
    readonly property var familyFx: samples.sfx(family)
    function shown() { const o = []; for (let i = 0; i < all.length; ++i) if (cat === "ALL" || all[i].category === cat) o.push(all[i]); return o }
    function t(s) { return i18n.t(s, i18n.lang) }
    focus: true
    Keys.onPressed: function (e) {
        const k = e.key; const n = k === Qt.Key_0 ? 9 : k >= Qt.Key_1 && k <= Qt.Key_9 ? k - Qt.Key_1 : -1
        if (n >= 0) { if (view === "PADS") samples.playPad(bank * 50 + 1 + n); else if (view === "EFFECTS") { if (n < familyFx.length) samples.playSfx(familyFx[n].id) } else { const l = shown(); if (n < l.length) samples.play(l[n].id) } e.accepted = true }
        else if (k === Qt.Key_Escape) { samples.stopAll(); e.accepted = true }
        else if (view === "PADS" && (k === Qt.Key_PageDown || k === Qt.Key_PageUp)) { bank = Math.max(0, Math.min(19, bank + (k === Qt.Key_PageDown ? 1 : -1))); e.accepted = true } }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 24; radius: 8; gradient: Gradient { GradientStop { position: 0; color: on ? Theme.btnTop(onColor) : Theme.ov(0.11) } GradientStop { position: 1; color: on ? Theme.btnBottom(onColor) : Theme.ov(0.08) } } color: on ? onColor : Theme.ov(0.08); border.color: Theme.lineSoft
        Rectangle { anchors.fill: parent; anchors.margins: 1; radius: Math.max(0, parent.radius - 1); color: "transparent"; border.width: 1; border.color: Qt.rgba(1, 1, 1, parent.on ? 0.30 : 0.07) }
        Text { id: bl; anchors.centerIn: parent; text: page.t(label); color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    component Chip: Rectangle { property string label; property bool on: false; signal clicked(); implicitWidth: cl.implicitWidth + 14; implicitHeight: 20; radius: 4; color: on ? Theme.amber : Theme.ov(0.06)
        Text { id: cl; anchors.centerIn: parent; text: label; color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    component Small: Text { color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        // VIEW BAR
        Rectangle { Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Text { text: page.t("SAMPLES:"); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Repeater { model: [["PADS", "1000 PADS"], ["EFFECTS", "1000 SOUND EFFECTS"], ["BUILT-IN", "100 BUILT-IN"]]
                    delegate: Chip { objectName: "samplesView" + modelData[0]; label: page.t(modelData[1]); on: page.view === modelData[0]; onClicked: { page.view = modelData[0]; page.forceActiveFocus() } } }
                Item { Layout.fillWidth: true }
                Text { text: samples.voices + page.t(" playing · peak ") + (samples.peakDb > -100 ? samples.peakDb.toFixed(1) : "−∞") + page.t(" dBFS"); color: samples.voices > 0 ? Theme.emerald : Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                Text { text: samples.message; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideRight; Layout.maximumWidth: 320 } } }
        // SUB BAR: banks / families / categories
        Rectangle { Layout.fillWidth: true; implicitHeight: 30; radius: 8; color: Theme.card; border.color: Theme.line
            Flickable { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; contentWidth: subRow.implicitWidth; clip: true; flickableDirection: Flickable.HorizontalFlick
                Row { id: subRow; anchors.verticalCenter: parent.verticalCenter; spacing: 4
                    Text { anchors.verticalCenter: parent.verticalCenter; text: page.view === "PADS" ? page.t("BANK") : page.view === "EFFECTS" ? page.t("FAMILY") : page.t("CATEGORY"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; rightPadding: 4 }
                    Repeater { model: page.view === "PADS" ? 20 : 0
                        delegate: Chip { objectName: "padBank" + index; label: String(index * 50 + 1) + "-" + String(index * 50 + 50); on: page.bank === index; onClicked: page.bank = index } }
                    Repeater { model: page.view === "EFFECTS" ? samples.sfxFamilies : []
                        delegate: Chip { label: page.t(modelData).toUpperCase(); on: page.family === modelData; onClicked: page.family = modelData } }
                    Repeater { model: page.view === "BUILT-IN" ? ["ALL"].concat(samples.categories) : []
                        delegate: Chip { label: page.t(modelData).toUpperCase(); on: page.cat === modelData; onClicked: page.cat = modelData } } } } }
        RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                // 1000 CUSTOM PADS: 50 per bank; click plays, right-click (or the pencil) edits
                GridView { id: padGrid; objectName: "padGrid"; visible: page.view === "PADS"; anchors.fill: parent; anchors.margins: 8; clip: true; model: page.bankPads
                    cellWidth: Math.max(104, Math.floor(width / 10)); cellHeight: 60; ScrollBar.vertical: ScrollBar {}
                    delegate: Item { width: padGrid.cellWidth; height: padGrid.cellHeight
                        Rectangle { id: cp; anchors.fill: parent; anchors.margins: 3; radius: 7; property color pc: page.colors[modelData.color] || Theme.muted
                            property bool empty: modelData.kind === "empty"; property bool hot: samples.lastPad === modelData.n && samples.voices > 0; property bool chosen: page.sel === modelData.n
                            color: hot ? pc : empty ? Theme.ov(0.02) : Theme.ov(0.05); border.color: chosen ? Theme.ink : hot ? pc : Qt.rgba(pc.r, pc.g, pc.b, empty ? 0.18 : 0.45); border.width: chosen ? 2 : 1
                            Rectangle { anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 1; height: 3; radius: 2; color: cp.pc; opacity: cp.empty ? 0.3 : 1 }
                            ColumnLayout { anchors.fill: parent; anchors.margins: 6; anchors.topMargin: 8; spacing: 1
                                Text { Layout.fillWidth: true; elide: Text.ElideRight; text: cp.empty ? page.t("(empty)") : modelData.name; color: cp.hot ? "#000" : cp.empty ? Theme.dim : Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                                Text { Layout.fillWidth: true; elide: Text.ElideRight; text: (modelData.kind === "file" ? (modelData.missing ? page.t("FILE MISSING") : page.t("FILE")) : modelData.kind === "sfx" ? page.t("EFFECT") : modelData.kind === "sample" ? page.t("SAMPLE") : "") + (modelData.gainDb !== 0 ? " · " + (modelData.gainDb > 0 ? "+" : "") + modelData.gainDb.toFixed(1) + " dB" : "")
                                    color: modelData.missing ? Theme.danger : cp.hot ? "#222" : Theme.dim; font.family: Theme.mono; font.pixelSize: 6 }
                                Item { Layout.fillHeight: true }
                                Text { text: (index < 10 ? page.t("KEY ") + ((index + 1) % 10) + " · " : "") + "#" + modelData.n + (modelData.custom ? " ✎" : ""); color: cp.hot ? "#222" : Theme.dim; font.family: Theme.mono; font.pixelSize: 6 } }
                            MouseArea { anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onPressed: function (m) { page.sel = modelData.n; if (m.button === Qt.LeftButton) samples.playPad(modelData.n); page.forceActiveFocus() } } } } }
                // 1000 SOUND EFFECTS: 50 per family; click plays, double-click puts it on the selected pad
                GridView { id: fxGrid; objectName: "sfxGrid"; visible: page.view === "EFFECTS"; anchors.fill: parent; anchors.margins: 8; clip: true; model: page.familyFx
                    cellWidth: Math.max(104, Math.floor(width / 10)); cellHeight: 52; ScrollBar.vertical: ScrollBar {}
                    delegate: Item { width: fxGrid.cellWidth; height: fxGrid.cellHeight
                        Rectangle { anchors.fill: parent; anchors.margins: 3; radius: 7; property bool chosen: page.pickedSfx === modelData.id
                            color: chosen ? Theme.cyan : Theme.ov(0.05); border.color: chosen ? Theme.cyan : Theme.lineSoft
                            ColumnLayout { anchors.fill: parent; anchors.margins: 6; spacing: 1
                                Text { Layout.fillWidth: true; elide: Text.ElideRight; text: page.t(modelData.family) + " " + modelData.name.slice(modelData.family.length + 1); color: parent.parent.chosen ? "#000" : Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                                Text { text: modelData.seconds.toFixed(2) + " s · #" + modelData.id; color: parent.parent.chosen ? "#222" : Theme.dim; font.family: Theme.mono; font.pixelSize: 6 } }
                            MouseArea { anchors.fill: parent; onClicked: { page.pickedSfx = modelData.id; samples.playSfx(modelData.id); page.forceActiveFocus() }
                                onDoubleClicked: samples.setPad(page.sel, "sfx:" + modelData.id, "", page.selPad.gainDb, page.selPad.color) } } } }
                // 100 BUILT-IN
                GridView { id: grid; visible: page.view === "BUILT-IN"; anchors.fill: parent; anchors.margins: 8; clip: true; cellWidth: 118; cellHeight: 62; model: page.shown()
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Item { width: grid.cellWidth; height: grid.cellHeight
                        Rectangle { id: pad; anchors.fill: parent; anchors.margins: 3; radius: 7; property color pc: page.padColors[modelData.category] || Theme.muted; property bool hot: samples.lastPlayed === modelData.id && samples.voices > 0
                            color: hot ? pc : Theme.ov(0.05); border.color: hot ? pc : Qt.rgba(pc.r, pc.g, pc.b, 0.45); border.width: 1
                            Rectangle { anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 1; height: 3; radius: 2; color: pad.pc }
                            ColumnLayout { anchors.fill: parent; anchors.margins: 6; anchors.topMargin: 8; spacing: 1
                                Text { Layout.fillWidth: true; elide: Text.ElideRight; text: page.t(modelData.name ?? ""); color: pad.hot ? "#000" : Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                                Text { text: modelData.category.toUpperCase() + " · " + modelData.seconds.toFixed(1) + " s"; color: pad.hot ? "#222" : Theme.dim; font.family: Theme.mono; font.pixelSize: 6 }
                                Item { Layout.fillHeight: true }
                                Text { text: (index < 10 ? page.t("KEY ") + ((index + 1) % 10) + " · " : "") + "#" + modelData.id; color: pad.hot ? "#222" : Theme.dim; font.family: Theme.mono; font.pixelSize: 6 } }
                            MouseArea { anchors.fill: parent; onPressed: samples.play(modelData.id) } } } } }
            // RIGHT: the selected pad, the player, export
            Rectangle { Layout.preferredWidth: 250; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                Flickable { anchors.fill: parent; anchors.margins: 12; contentHeight: side.implicitHeight; clip: true
                ColumnLayout { id: side; width: parent.width; spacing: 7
                    Text { text: page.t("PAD ") + page.sel; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
                    TextField { id: padName; objectName: "padName"; Layout.fillWidth: true; implicitHeight: 24; font.family: Theme.mono; font.pixelSize: 9; color: Theme.ink; selectByMouse: true; maximumLength: 48
                        text: page.selPad.name ?? ""; placeholderText: page.t("pad name"); background: Rectangle { radius: 4; color: Theme.ov(0.06); border.color: Theme.lineSoft }
                        onEditingFinished: if (text !== page.selPad.name) samples.setPad(page.sel, page.selPad.source, text, page.selPad.gainDb, page.selPad.color) }
                    Small { Layout.fillWidth: true; text: page.selPad.kind === "file" ? page.t("FILE: ") + page.selPad.file + (page.selPad.missing ? page.t(" (not found: plug the drive in or choose it again)") : "") : page.selPad.kind === "sfx" ? page.t("SOUND EFFECT ") + page.selPad.source.slice(4) : page.selPad.kind === "sample" ? page.t("BUILT-IN SAMPLE ") + page.selPad.source.slice(7) : page.t("Empty: choose an effect or a file."); elide: Text.ElideMiddle; maximumLineCount: 3 }
                    Text { text: page.t("PAD GAIN  ") + ((page.selPad.gainDb ?? 0) > 0 ? "+" : "") + (page.selPad.gainDb ?? 0).toFixed(1) + page.t(" dB"); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8 }
                    Slider { objectName: "padGain"; Layout.fillWidth: true; from: -40; to: 12; stepSize: 0.5; value: page.selPad.gainDb ?? 0; onPressedChanged: if (!pressed && value !== page.selPad.gainDb) samples.setPad(page.sel, page.selPad.source, page.selPad.name, value, page.selPad.color) }
                    Row { spacing: 4
                        Repeater { model: 8; delegate: Rectangle { width: 22; height: 16; radius: 3; color: page.colors[index]; border.width: page.selPad.color === index ? 2 : 0; border.color: Theme.ink
                            MouseArea { anchors.fill: parent; onClicked: samples.setPad(page.sel, page.selPad.source, page.selPad.name, page.selPad.gainDb, index) } } } }
                    Flow { Layout.fillWidth: true; spacing: 4
                        Btn { label: "PLAY"; on: true; onColor: Theme.emerald; onClicked: samples.playPad(page.sel) }
                        Btn { objectName: "padAssignSfx"; label: page.pickedSfx > 0 ? "PUT EFFECT #" + page.pickedSfx : "PICK AN EFFECT"; on: page.pickedSfx > 0; onColor: Theme.cyan
                            onClicked: { if (page.pickedSfx > 0) samples.setPad(page.sel, "sfx:" + page.pickedSfx, "", page.selPad.gainDb, page.selPad.color); else page.view = "EFFECTS" } }
                        Btn { label: "CHOOSE FILE…"; onClicked: fileBox.browse() }
                        Btn { label: "EMPTY"; onClicked: samples.setPad(page.sel, "", "", 0, page.selPad.color) }
                        Btn { objectName: "padReset"; label: "RESET"; on: page.selPad.custom === true; onColor: Theme.danger; onClicked: samples.resetPad(page.sel) } }
                    Item { id: fileBox; Layout.fillWidth: true; implicitHeight: filePath.implicitHeight
                        property var dlg: null   // the system file picker, made only where Qt has QtQuick.Dialogs (the Windows build does)
                        function browse() {
                            if (!dlg) { try { dlg = Qt.createQmlObject('import QtQuick.Dialogs; FileDialog { nameFilters: samples.audioFilters() }', fileBox, "padFileDialog"); dlg.title = page.t("Choose an audio file")
                                              dlg.accepted.connect(function () { samples.setPadFile(page.sel, dlg.selectedFile.toString()) }) } catch (e) { filePath.forceActiveFocus(); return } }
                            dlg.open() }
                        TextField { id: filePath; objectName: "padFilePath"; width: parent.width; implicitHeight: 24; font.family: Theme.mono; font.pixelSize: 8; color: Theme.ink; selectByMouse: true; placeholderText: page.t("or type a path: WAV, FLAC, AIFF, MP3 ...")
                            background: Rectangle { radius: 4; color: Theme.ov(0.06); border.color: Theme.lineSoft }
                            onAccepted: if (text.trim() !== "" && samples.setPadFile(page.sel, text.trim())) text = "" } }
                    Small { Layout.fillWidth: true; text: page.t("Click a pad to play it, right-click to edit it. In SOUND EFFECTS click to listen and double-click to put the effect on this pad. A file plays its first 30 seconds. The phones show the same 1000 pads.") }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.lineSoft }
                    Text { text: page.t("PLAYER"); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
                    Text { text: page.t("GAIN  ") + (samples.gainDb > 0 ? "+" : "") + samples.gainDb.toFixed(1) + page.t(" dB"); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8 }
                    Slider { Layout.fillWidth: true; from: -40; to: 12; stepSize: 0.5; value: samples.gainDb; onMoved: samples.gainDb = value }
                    Btn { label: "STOP ALL  (Esc)"; onColor: Theme.danger; on: samples.voices > 0; onClicked: samples.stopAll() }
                    Small { Layout.fillWidth: true; text: page.t("16 voices; a 17th press steals the oldest. Sounds in memory: ") + samples.keptMb.toFixed(0) + " MB" }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.lineSoft }
                    Text { text: page.t("EXPORT"); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
                    Small { Layout.fillWidth: true; text: page.t("Writes the 100 samples as 24-bit WAV into\n") + samples.folder + page.t("\nfor ARRANGE, STREAM & MEDIA or any other program. Files that exist are kept."); wrapMode: Text.WrapAnywhere }
                    Btn { label: "EXPORT 100 WAV"; on: true; onColor: Theme.cyan; onClicked: samples.exportAll() }
                    Small { Layout.fillWidth: true; text: page.t("Rendered in the program (sine, FM, noise, filters, envelopes): no sample files are shipped, nothing to download, identical on every computer.") } } } } }
    }
}
