// PLUG-IN BUILDER: your own VST3 plug-in from AMFB blocks. Left: the blocks; middle: your chain, each knob FIXED (set here) or sent TO DAW;
// right: the faceplate (drag the DAW knobs where you want them), the name, the colour, SAVE / EXPORT / TRY IN THE MIXER, saved designs.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import AmixOS

Item {
    id: page
    objectName: "builderPage"
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked()
        implicitWidth: Math.max(48, bl.implicitWidth + 16); implicitHeight: 24; radius: 8; gradient: Gradient { GradientStop { position: 0; color: on ? Theme.btnTop(onColor) : Theme.ov(0.11) } GradientStop { position: 1; color: on ? Theme.btnBottom(onColor) : Theme.ov(0.08) } } color: on ? onColor : Theme.ov(0.08); border.color: on ? onColor : Theme.line
        Rectangle { anchors.fill: parent; anchors.margins: 1; radius: Math.max(0, parent.radius - 1); color: "transparent"; border.width: 1; border.color: Qt.rgba(1, 1, 1, parent.on ? 0.28 : 0.07) }
        Text { id: bl; anchors.centerIn: parent; text: i18n.t(label, i18n.lang); color: parent.on ? "#000" : Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked() } }
    component Field: Rectangle { id: gb17; property alias text: ti.text; signal edited(string t); implicitHeight: 26; radius: 7; color: Theme.deep; gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb17.color) } GradientStop { position: 1; color: Theme.btnBottom(gb17.color) } } border.color: ti.activeFocus ? Theme.orange : Theme.line
        TextInput { id: ti; anchors.fill: parent; anchors.margins: 6; color: Theme.ink; font.pixelSize: 11; clip: true; selectByMouse: true; onEditingFinished: parent.edited(text) } }

    DropArea { anchors.fill: parent; z: 50; keys: ["text/uri-list"]
        onDropped: function (drop) { if (drop.hasUrls && drop.urls.length) { builder.setPreviewFile(drop.urls[0].toString()); if (!builder.previewOn) builder.previewPlay(true) } } }
    RowLayout {
        anchors.fill: parent; anchors.margins: 14; spacing: 12

        // ── THE BLOCKS ──────────────────────────────────────────────────────────────────────────────────────────────────────────────
        Rectangle { Layout.preferredWidth: 200; Layout.fillHeight: true; radius: 8; color: Theme.card; border.color: Theme.line
            ColumnLayout { anchors.fill: parent; anchors.margins: 10; spacing: 6
                Text { text: i18n.t("BLOCKS", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; font.letterSpacing: 2 }
                Text { text: i18n.t("Click to add to the chain (up to 6).", i18n.lang); color: Theme.dim; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                ListView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 3; model: builder.catalog
                    delegate: Rectangle { width: ListView.view.width; height: 24; radius: 4; color: blkHov.hovered ? Theme.ov(0.12) : Theme.ov(0.04)
                        Text { anchors.verticalCenter: parent.verticalCenter; x: 8; text: "+  " + i18n.t(modelData.name, i18n.lang); color: Theme.ink; font.pixelSize: 10; width: parent.width - 12; elide: Text.ElideRight }
                        HoverHandler { id: blkHov } MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: builder.addBlock(modelData.kind) } } } } }

        // ── THE CHAIN ───────────────────────────────────────────────────────────────────────────────────────────────────────────────
        Rectangle { Layout.fillWidth: true; Layout.minimumWidth: 380; Layout.fillHeight: true; radius: 8; color: Theme.card; border.color: Theme.line
            Flickable { anchors.fill: parent; anchors.margins: 10; contentHeight: chainCol.implicitHeight; clip: true
                ColumnLayout { id: chainCol; width: parent.width; spacing: 8
                    Text { text: i18n.t("SIGNAL CHAIN · IN → OUT", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; font.letterSpacing: 2 }
                    Text { visible: builder.blocks.length === 0; text: i18n.t("Empty: add blocks from the left. Each knob is either FIXED at the value you set here, or TO DAW: a knob the DAW shows and can automate.", i18n.lang); color: Theme.dim; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Repeater { model: builder.blocks
                        delegate: Rectangle { objectName: "builderBlock" + index; Layout.fillWidth: true; implicitHeight: bc.implicitHeight + 16; radius: 6; color: Theme.ov(0.05); border.color: Theme.lineSoft
                            readonly property int bi: index
                            ColumnLayout { id: bc; x: 8; y: 8; width: parent.width - 16; spacing: 4
                                RowLayout { spacing: 6
                                    Text { text: (bi + 1) + ".  " + i18n.t(modelData.name, i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; Layout.fillWidth: true }
                                    Btn { label: "▲"; onClicked: builder.moveBlock(bi, -1) } Btn { label: "▼"; onClicked: builder.moveBlock(bi, 1) }
                                    Btn { label: "REMOVE"; onColor: Theme.danger; onClicked: builder.removeBlock(bi) } }
                                Repeater { model: modelData.knobs
                                    delegate: RowLayout { spacing: 8; Layout.fillWidth: true
                                        Text { text: i18n.t(modelData.title, i18n.lang); color: Theme.muted; font.pixelSize: 9; font.family: Theme.mono; Layout.preferredWidth: 110; elide: Text.ElideRight }
                                        Rectangle { Layout.fillWidth: true; implicitHeight: 6; radius: 3; color: Theme.deep
                                            Rectangle { width: parent.width * modelData.value; height: parent.height; radius: 3; color: modelData.exposed ? Theme.cyan : Theme.orange }
                                            MouseArea { anchors.fill: parent; anchors.margins: -6; function at(x) { builder.setKnob(bi, modelData.id, Math.max(0, Math.min(1, x / width))) } onPressed: function (m) { at(m.x) } onPositionChanged: function (m) { at(m.x) } } }
                                        Text { text: modelData.text; color: Theme.ink; font.pixelSize: 9; font.family: Theme.mono; Layout.preferredWidth: 70 }
                                        Btn { objectName: "expose_" + bi + "_" + modelData.id; label: modelData.exposed ? "TO DAW" : "FIXED"; on: modelData.exposed; onColor: Theme.cyan; onClicked: builder.exposeKnob(bi, modelData.id, !modelData.exposed) }
                                        Field { visible: modelData.exposed; Layout.preferredWidth: 110; text: modelData.label; onEdited: function (t) { builder.setKnobLabel(bi, modelData.id, t) } } } } } } } } } }

        // ── THE FACEPLATE, NAME, EXPORT ─────────────────────────────────────────────────────────────────────────────────────────────
        ColumnLayout { Layout.preferredWidth: 430; Layout.maximumWidth: 430; Layout.fillWidth: false; Layout.fillHeight: true; spacing: 10
            Rectangle { id: plate; objectName: "builderPlate"; Layout.fillWidth: true; implicitHeight: 180; radius: 10; color: "#16191e"; border.color: builder.color; border.width: 2
                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 30; radius: 10; color: builder.color
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 10; color: builder.color }
                    Text { x: 12; anchors.verticalCenter: parent.verticalCenter; text: builder.name.toUpperCase(); color: "#000"; font.family: Theme.mono; font.pixelSize: 12; font.weight: Font.Black; font.letterSpacing: 2 }
                    Text { anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter; text: builder.vendor; color: "#000"; font.pixelSize: 9 } }
                Item { id: area; anchors.fill: parent; anchors.topMargin: 34; anchors.margins: 8
                    Text { anchors.centerIn: parent; visible: builder.exposed.length === 0; text: i18n.t("The DAW's knobs appear here: set a knob to TO DAW, then drag it into place.", i18n.lang); color: Theme.dim; font.pixelSize: 9; width: parent.width - 30; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter }
                    Repeater { model: builder.exposed
                        delegate: Item { width: 70; height: 82; x: modelData.x * (area.width - width); y: modelData.y * (area.height - height)
                            Rectangle { id: face; width: 46; height: 46; radius: 23; anchors.horizontalCenter: parent.horizontalCenter; color: "#262b33"; border.color: builder.color; border.width: 2
                                Rectangle { width: 3; height: 16; radius: 1; color: builder.color; x: parent.width / 2 - 1.5; y: 4; transformOrigin: Item.Bottom; transform: Rotation { origin.x: 1.5; origin.y: 19; angle: -135 + 270 * modelData.value } } }
                            Text { anchors.top: face.bottom; anchors.topMargin: 3; anchors.horizontalCenter: parent.horizontalCenter; text: modelData.label; color: Theme.ink; font.pixelSize: 9; font.bold: true; width: parent.width; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
                            Text { anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; text: modelData.text; color: Theme.cyan; font.pixelSize: 8; font.family: Theme.mono }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.SizeAllCursor; drag.target: parent; drag.axis: Drag.XAndYAxis
                                onReleased: builder.placeKnob(modelData.block, modelData.id, parent.x / Math.max(1, area.width - parent.width), parent.y / Math.max(1, area.height - parent.height)) } } } } }
            // PREVIEW: hear the design live (the program's own engine on the sound card)
            Rectangle { objectName: "builderPreview"; Layout.fillWidth: true; implicitHeight: pvCol.implicitHeight + 16; radius: 8; color: Theme.card; border.color: builder.previewOn ? Theme.emerald : Theme.line
                ColumnLayout { id: pvCol; x: 8; y: 8; width: parent.width - 16; spacing: 6
                    RowLayout { spacing: 6
                        Text { text: i18n.t("PREVIEW", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; font.letterSpacing: 2 }
                        Btn { objectName: "previewPlay"; label: builder.previewOn ? "■ STOP" : "▶ PLAY"; on: builder.previewOn; onColor: Theme.emerald; onClicked: builder.previewPlay(!builder.previewOn) }
                        Btn { label: builder.previewBypass ? "B · BYPASS" : "A · DESIGN"; on: builder.previewBypass; onColor: Theme.amber; onClicked: builder.setPreviewBypass(!builder.previewBypass) }
                        Item { Layout.fillWidth: true }
                        // IN / OUT meters
                        Column { spacing: 2
                            Repeater { model: [["IN", builder.previewInDb], ["OUT", builder.previewOutDb]]
                                delegate: Row { spacing: 4; Text { text: modelData[0]; color: Theme.dim; font.pixelSize: 8; font.family: Theme.mono; width: 22 }
                                    Rectangle { width: 110; height: 6; radius: 3; color: Theme.deep
                                        Rectangle { height: parent.height; radius: 3; width: parent.width * Math.max(0, Math.min(1, (modelData[1] + 60) / 60)); color: modelData[1] > -1 ? Theme.danger : modelData[1] > -12 ? Theme.amber : Theme.emerald } } } } } }
                    Flow { Layout.fillWidth: true; spacing: 4
                        Repeater { model: ["SAMPLE", "PINK NOISE", "SWEEP", "MICROPHONE", "FILE…"]
                            delegate: Btn { label: modelData; on: builder.previewSource === index; onColor: Theme.cyan; onClicked: { if (index === 4) { builder.setPreviewSource(4); pathBox.forceActiveFocus() } else builder.setPreviewSource(index) } } } }
                    RowLayout { spacing: 8; visible: builder.previewSource === 0
                        Text { text: i18n.t("SAMPLE", i18n.lang); color: Theme.muted; font.pixelSize: 9; font.family: Theme.mono }
                        ComboBox { Layout.fillWidth: true; model: builder.samples.map(x => x.id + "  " + x.category + " · " + x.name); currentIndex: Math.max(0, builder.previewSample - 1); onActivated: function (k) { builder.setPreviewSample(builder.samples[k].id) } } }
                    RowLayout { visible: builder.previewSource === 4; spacing: 6
                        Field { id: pathBox; Layout.fillWidth: true; text: builder.previewFile; onEdited: function (t) { if (t.trim() !== "") builder.setPreviewFile(t.trim()) } }
                        Text { text: i18n.t("or drop a file on the window", i18n.lang); color: Theme.dim; font.pixelSize: 8 } }
                    RowLayout { spacing: 8
                        Text { text: i18n.t("LEVEL", i18n.lang); color: Theme.muted; font.pixelSize: 9; font.family: Theme.mono }
                        MSlider { Layout.fillWidth: true; from: -30; to: 6; value: builder.previewGainDb; onMoved: builder.setPreviewGainDb(value) }
                        Text { text: (builder.previewGainDb > 0 ? "+" : "") + builder.previewGainDb.toFixed(1) + " dB"; color: Theme.ink; font.pixelSize: 9; font.family: Theme.mono } }
                    Text { visible: builder.previewNote !== ""; text: builder.previewNote; color: builder.previewOn ? Theme.emerald : Theme.amber; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true } } }
            GridLayout { columns: 2; columnSpacing: 8; rowSpacing: 6; Layout.fillWidth: true
                Text { text: i18n.t("NAME", i18n.lang); color: Theme.muted; font.pixelSize: 9; font.family: Theme.mono } Field { Layout.fillWidth: true; text: builder.name; onEdited: function (t) { builder.name = t } }
                Text { text: i18n.t("VENDOR", i18n.lang); color: Theme.muted; font.pixelSize: 9; font.family: Theme.mono } Field { Layout.fillWidth: true; text: builder.vendor; onEdited: function (t) { builder.vendor = t } }
                Text { text: i18n.t("CATEGORY", i18n.lang); color: Theme.muted; font.pixelSize: 9; font.family: Theme.mono }
                Flow { Layout.fillWidth: true; spacing: 4; Repeater { model: ["Fx", "Fx|EQ", "Fx|Dynamics", "Fx|Reverb", "Fx|Delay", "Fx|Modulation", "Fx|Distortion", "Fx|Tools"]; delegate: Btn { label: modelData.replace("Fx|", ""); on: builder.category === modelData; onClicked: builder.category = modelData } } }
                Text { text: i18n.t("COLOUR", i18n.lang); color: Theme.muted; font.pixelSize: 9; font.family: Theme.mono }
                Row { spacing: 5; Repeater { model: ["#EA580C", "#22D3EE", "#A78BFA", "#34D399", "#F472B6", "#FBBF24", "#60A5FA", "#E5E7EB"]; delegate: Rectangle { width: 20; height: 20; radius: 10; color: modelData; border.color: builder.color === modelData ? "#fff" : "transparent"; border.width: 2; MouseArea { anchors.fill: parent; onClicked: builder.color = modelData } } } } }
            Flow { Layout.fillWidth: true; spacing: 6
                Btn { label: "NEW"; onClicked: builder.newDesign() }
                Btn { label: "SAVE"; onClicked: builder.save() }
                Btn { objectName: "builderExport"; label: "EXPORT VST3"; on: true; onColor: Theme.emerald; onClicked: builder.exportPlugin() }
                Btn { visible: engine !== null; label: "TRY IN THE MIXER"; on: true; onColor: Theme.cyan; onClicked: builder.tryOnStrip(engine.selectedStrip) } }
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 9; color: builder.shellFound ? Theme.dim : Theme.danger
                text: builder.shellFound ? i18n.t("Exports to ", i18n.lang) + builder.exportDir + i18n.t(": every DAW (and this console) finds VST3 plug-ins there.", i18n.lang) : i18n.t("The plug-in shell is not installed next to the console: Install AmF X1 Extras to export.", i18n.lang) }
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: builder.note; color: Theme.emerald; font.pixelSize: 9; visible: builder.note !== "" }
            Text { text: i18n.t("SAVED DESIGNS", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; font.letterSpacing: 2 }
            ListView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 3; model: builder.designs
                delegate: Rectangle { width: ListView.view.width; height: 26; radius: 4; color: Theme.ov(0.05)
                    Text { x: 8; anchors.verticalCenter: parent.verticalCenter; text: modelData.name + "   ·   " + modelData.blocks + i18n.t(" blocks · ", i18n.lang) + modelData.knobs + i18n.t(" knobs · ", i18n.lang) + modelData.when; color: Theme.ink; font.pixelSize: 9; width: parent.width - 120; elide: Text.ElideRight }
                    Row { anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter; spacing: 4
                        Btn { label: "OPEN"; onClicked: builder.load(modelData.file) } Btn { label: "✕"; onClicked: builder.remove(modelData.file) } } } }
        }
    }
}
