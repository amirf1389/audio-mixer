// EFFECTS: one card per bus (FX 1-8 are meant for these; AUX buses can carry one too). 600 PRESETS: a browser by family, one click loads kind + values on the bus. Pick an effect (REVERB, TAPE DELAY, CHORUS, DRIVE, or none), set its parameters with the plug-in's own display
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
    property bool browse: false                            // the 300-preset browser instead of the parameter panel
    property string presetCat: "Reverb"
    property string presetFilter: ""
    readonly property var allPresets: engine.fxPresets()
    readonly property var presetCats: { const c = []; for (let i = 0; i < allPresets.length; ++i) if (c.indexOf(allPresets[i].category) < 0) c.push(allPresets[i].category); return c }
    function presetsShown() { const f = presetFilter.toLowerCase(); const out = []; for (let i = 0; i < allPresets.length; ++i) { const p = allPresets[i]; if (p.category !== presetCat) continue; if (f && p.name.toLowerCase().indexOf(f) < 0) continue; out.push(p) } return out }
    readonly property var catalog: engine.fxCatalog()
    readonly property var b: engine.busState[bus]
    readonly property var fx: (tick, engine.busFx(bus))
    // the bus meters only while the page is shown: engine.busMeters converts all 16 buses on every meter update, also behind another page
    property var bm: ({ peakDb: -120, rmsDb: -120, holdDb: -120, clip: false })
    Binding on bm { when: page.visible; value: engine.busMeters[page.bus]; restoreMode: Binding.RestoreNone }
    Connections { target: engine; function onBusesChanged() { page.tick++ } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Text { text: i18n.t("EFFECTS:", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Repeater { model: engine.busState; delegate: Rectangle { id: gb31; implicitWidth: bl.implicitWidth + 16; implicitHeight: 20; radius: 7; color: page.bus === index ? Theme.amber : Theme.ov(0.06); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb31.color) } GradientStop { position: 1; color: Theme.btnBottom(gb31.color) } } border.color: modelData.fx !== "" ? Theme.violet : "transparent"
                    Text { id: bl; anchors.centerIn: parent; text: i18n.t(modelData.name ?? "", i18n.lang) + (modelData.fx !== "" ? " ·" : ""); color: page.bus === index ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.bus = index } } }
                Item { Layout.fillWidth: true }
                Rectangle { id: gb32; implicitWidth: Math.max(100, children[0].implicitWidth + 12); implicitHeight: 20; radius: 7; color: page.browse ? Theme.violet : Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb32.color) } GradientStop { position: 1; color: Theme.btnBottom(gb32.color) } } border.color: Theme.lineSoft
                    Text { anchors.centerIn: parent; text: i18n.t("300 PRESETS", i18n.lang).replace("300", "600").replace("۳۰۰", "۶۰۰"); color: page.browse ? "#000" : Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.browse = !page.browse } }
                Text { visible: page.fx.preset > 0; text: i18n.t("preset #", i18n.lang) + page.fx.preset; color: Theme.violet; font.family: Theme.mono; font.pixelSize: 8 }
                Text { text: page.fx.kind !== "" ? page.fx.name + i18n.t(" on ", i18n.lang) + (page.b ? page.b.name : "") : i18n.t("no effect on this bus", i18n.lang); color: page.fx.kind !== "" ? Theme.violet : Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            // the effect chooser
            Rectangle {
                Layout.preferredWidth: 190; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 10; spacing: 4
                    Text { text: i18n.t("EFFECT", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.letterSpacing: 2 }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 30; radius: 6; color: page.fx.kind === "" ? Theme.ov(0.16) : Theme.ov(0.05); border.color: Theme.lineSoft
                        Text { anchors.centerIn: parent; text: i18n.t("NONE", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setBusFx(page.bus, "") } }
                    // THE LIST SCROLLS: 51 effects do not fit the panel's height (the lower ones were cut off and could not be reached)
                    Flickable { id: fxList; objectName: "fxList"; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; contentHeight: fxCol.implicitHeight; boundsBehavior: Flickable.StopAtBounds; flickableDirection: Flickable.VerticalFlick
                    Column { id: fxCol; width: fxList.width - 8; spacing: 4
                    Repeater { model: page.catalog; delegate: Rectangle { width: fxCol.width; height: 30; implicitHeight: 30; radius: 6; color: page.fx.kind === modelData.kind ? Theme.violet : Theme.ov(0.05); border.color: Theme.lineSoft
                        Text { anchors.centerIn: parent; text: i18n.t(modelData.name ?? "", i18n.lang); color: page.fx.kind === modelData.kind ? "#000" : Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setBusFx(page.bus, modelData.kind) } } } }
                    Rectangle { visible: fxList.contentHeight > fxList.height; anchors.right: parent.right; width: 4; radius: 2; color: Theme.ov(0.35); y: fxList.height > 0 ? fxList.contentY / fxList.contentHeight * fxList.height : 0; height: Math.max(24, fxList.height * fxList.height / Math.max(1, fxList.contentHeight)) } }
                    Text { Layout.fillWidth: true; text: i18n.t("Send strips to this bus on SEND TO (POST for effects). The wet signal returns to the master at RETURN and to the bus outputs ", i18n.lang) + (page.b ? page.b.outputs : "") + "."; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }
                }
            }
            // the preset browser: 600 presets by family; one click loads the preset onto this bus
            Rectangle {
                visible: page.browse; Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 12; spacing: 8
                    RowLayout { Layout.fillWidth: true; spacing: 6
                        Text { text: i18n.t("PRESETS · ", i18n.lang) + page.allPresets.length; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 12; font.bold: true; font.letterSpacing: 2 }
                        Repeater { model: page.presetCats; delegate: Rectangle { implicitWidth: pcl.implicitWidth + 14; implicitHeight: 20; radius: 4; color: page.presetCat === modelData ? Theme.violet : Theme.ov(0.06)
                            Text { id: pcl; anchors.centerIn: parent; text: modelData.toUpperCase(); color: page.presetCat === modelData ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.presetCat = modelData } } }
                        Item { Layout.fillWidth: true }
                        Rectangle { id: gb33; implicitWidth: 160; implicitHeight: 22; radius: 7; color: Theme.deep; gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb33.color) } GradientStop { position: 1; color: Theme.btnBottom(gb33.color) } } border.color: Theme.lineSoft
                            TextInput { anchors.fill: parent; anchors.margins: 5; text: page.presetFilter; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; selectByMouse: true; onTextChanged: page.presetFilter = text
                                Text { visible: !parent.text.length; text: i18n.t("search…", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9 } } } }
                    Text { Layout.fillWidth: true; text: i18n.t("Load onto ", i18n.lang) + (page.b ? page.b.name : "") + i18n.t(". The preset sets the effect, its values and MIX; the bus RETURN opens if it was down. Values can then be edited on the parameter panel (the preset number clears when you do).", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }
                    GridView { id: grid; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; cellWidth: 150; cellHeight: 34; model: page.presetsShown()
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Item { width: grid.cellWidth; height: grid.cellHeight
                            Rectangle { anchors.fill: parent; anchors.margins: 2; radius: 5; color: page.fx.preset === modelData.id ? Theme.violet : Theme.ov(0.05); border.color: page.fx.preset === modelData.id ? Theme.violet : Theme.lineSoft
                                ColumnLayout { anchors.fill: parent; anchors.margins: 5; spacing: 0
                                    Text { Layout.fillWidth: true; elide: Text.ElideRight; text: i18n.t(modelData.name ?? "", i18n.lang); color: page.fx.preset === modelData.id ? "#000" : Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                                    Text { text: "#" + modelData.id + " · " + modelData.kind + i18n.t(" · mix ", i18n.lang) + Math.round(modelData.mix * 100) + "%"; color: page.fx.preset === modelData.id ? "#222" : Theme.dim; font.family: Theme.mono; font.pixelSize: 6 } }
                                MouseArea { anchors.fill: parent; onClicked: engine.applyFxPreset(page.bus, modelData.id) } } } }
                }
            }
            // parameters
            Rectangle {
                visible: !page.browse; Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 12; spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: page.fx.kind !== "" ? page.fx.name.toUpperCase() : i18n.t("NO EFFECT", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 12; font.bold: true; font.letterSpacing: 1.5 }
                        Item { Layout.fillWidth: true }
                        Rectangle { id: gb34; visible: page.fx.kind !== ""; implicitWidth: 64; implicitHeight: 22; radius: 7; color: page.fx.bypass ? Theme.amber : Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb34.color) } GradientStop { position: 1; color: Theme.btnBottom(gb34.color) } } border.color: Theme.lineSoft
                            Text { anchors.centerIn: parent; text: i18n.t("BYPASS", i18n.lang); color: page.fx.bypass ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setBusFxBypass(page.bus, !page.fx.bypass) } }
                        Rectangle { id: gb35; visible: page.fx.kind !== ""; implicitWidth: 64; implicitHeight: 22; radius: 7; color: Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb35.color) } GradientStop { position: 1; color: Theme.btnBottom(gb35.color) } } border.color: Theme.lineSoft
                            Text { anchors.centerIn: parent; text: i18n.t("DEFAULTS", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                            MouseArea { anchors.fill: parent; onClicked: { const ps = page.fx.params; for (let i = 0; i < ps.length; ++i) engine.setBusFxParam(page.bus, ps[i].id, ps[i]["default"]); engine.setBusFxMix(page.bus, 1) } } }
                    }
                    Text { visible: page.fx.kind === ""; text: i18n.t("Pick an effect on the left. Each aux bus can carry one: a reverb for vocals on BUS 1, a tape delay on BUS 2, a chorus on BUS 3 ...", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Repeater {
                        model: page.fx.params
                        delegate: RowLayout {
                            Layout.fillWidth: true; spacing: 10
                            Text { Layout.preferredWidth: 90; text: i18n.t(modelData.title ?? "", i18n.lang).toUpperCase(); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                            MSlider { Layout.fillWidth: true; from: 0; to: 1; stepSize: 0.001; value: modelData.value; onMoved: engine.setBusFxParam(page.bus, modelData.id, value) }
                            Text { Layout.preferredWidth: 80; text: i18n.t(modelData.text ?? "", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; horizontalAlignment: Text.AlignRight }
                        }
                    }
                    RowLayout {
                        visible: page.fx.kind !== ""
                        Layout.fillWidth: true; spacing: 10
                        Text { Layout.preferredWidth: 90; text: i18n.t("MIX", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                        MSlider { Layout.fillWidth: true; from: 0; to: 1; stepSize: 0.01; value: page.fx.mix; onMoved: engine.setBusFxMix(page.bus, value) }
                        Text { Layout.preferredWidth: 80; text: Math.round(page.fx.mix * 100) + i18n.t(" % wet", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; horizontalAlignment: Text.AlignRight }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 10
                        Text { Layout.preferredWidth: 90; text: i18n.t("RETURN", i18n.lang); color: Theme.emerald; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                        MSlider { Layout.fillWidth: true; from: -60; to: 10; stepSize: 0.5; value: page.b ? page.b.returnDb : -60; onMoved: engine.setBusReturn(page.bus, value) }
                        Text { Layout.preferredWidth: 80; text: page.b ? (page.b.returnDb <= -60 ? i18n.t("off", i18n.lang) : page.b.returnDb.toFixed(1) + i18n.t(" dB", i18n.lang)) : ""; color: Theme.emerald; font.family: Theme.mono; font.pixelSize: 9; horizontalAlignment: Text.AlignRight }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 10
                        Text { Layout.preferredWidth: 90; text: i18n.t("BUS LEVEL", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                        MSlider { Layout.fillWidth: true; from: -60; to: 10; stepSize: 0.5; value: page.b ? page.b.db : 0; onMoved: engine.setBusLevel(page.bus, value) }
                        Text { Layout.preferredWidth: 80; text: page.b ? page.b.db.toFixed(1) + i18n.t(" dB", i18n.lang) : ""; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; horizontalAlignment: Text.AlignRight }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 10
                        Text { Layout.preferredWidth: 90; text: i18n.t("BUS OUT", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                        MeterBar { Layout.fillWidth: true; implicitHeight: 10; peakDb: page.bm ? page.bm.peakDb : -120; rmsDb: page.bm ? page.bm.rmsDb : -120 }
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
