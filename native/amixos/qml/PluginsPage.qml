// PLUGINS: the manager for the VST3 / VST2 plug-ins the scan found: search, folders, rescan, and INSERT into a strip's first free rack slot.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "pluginsPage"
    property string filter: ""
    property string format: ""
    property int tick: 0
    readonly property var all: (tick, engine.vstCatalog())
    readonly property var shown: { const f = filter.toLowerCase(); let r = []; for (let i = 0; i < all.length; ++i) { const p = all[i]; if (format !== "" && p.format !== format) continue; if (f !== "" && (p.name + " " + p.vendor + " " + p.module).toLowerCase().indexOf(f) < 0) continue; r.push(p) } return r }
    readonly property var builtIns: engine.pluginCatalog().filter(function (p) { return p.builtIn })
    function insert(kind) { const s = engine.selectedStrip; const slots = engine.plugins(s); for (let k = 0; k < slots.length; ++k) if (slots[k].kind === "") { engine.setPlugin(s, k, kind); return true } return false }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 22; radius: 4; color: on ? onColor : Theme.ov(0.08); border.color: Theme.lineSoft
        Text { id: bl; anchors.centerIn: parent; text: label; color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle { Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Text { text: i18n.t("PLUGINS:", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Text { text: page.all.length + i18n.t(" found · ", i18n.lang) + page.builtIns.length + i18n.t(" built-in stages", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Repeater { model: [["", "ALL"], ["VST3", "VST3"], ["VST2", "VST2 / DLL"]]; delegate: Btn { label: modelData[1]; on: page.format === modelData[0]; onClicked: page.format = modelData[0] } }
                Field { Layout.preferredWidth: 220; placeholderText: i18n.t("search name, vendor, file", i18n.lang); text: page.filter; onTextChanged: page.filter = text; font.family: Theme.mono; font.pixelSize: 9; implicitHeight: 24 }
                Item { Layout.fillWidth: true }
                Text { text: i18n.t("INSERT target: CH ", i18n.lang) + (engine.selectedStrip + 1); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                Btn { label: i18n.t("RESCAN", i18n.lang); on: true; onColor: Theme.cyan; onClicked: { settings.rescanVst(); page.tick++ } } } }
        RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; clip: true
                ListView { anchors.fill: parent; anchors.margins: 10; clip: true; spacing: 3; model: page.shown
                    delegate: Rectangle { width: ListView.view.width; height: 40; radius: 6; color: Theme.ov(0.03); border.color: Theme.lineSoft
                        RowLayout { anchors.fill: parent; anchors.margins: 8; spacing: 10
                            Rectangle { implicitWidth: 44; implicitHeight: 18; radius: 3; color: modelData.format === "VST3" ? Theme.violet : Theme.amber; Text { anchors.centerIn: parent; text: modelData.format; color: "#000"; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } }
                            ColumnLayout { Layout.fillWidth: true; spacing: 1
                                Text { text: modelData.name + (modelData.vendor ? "  ·  " + modelData.vendor : ""); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                Text { text: modelData.module + (modelData.subCategories ? "   [" + modelData.subCategories + "]" : ""); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; elide: Text.ElideMiddle; Layout.fillWidth: true } }
                            Btn { label: i18n.t("INSERT", i18n.lang); on: true; onColor: Theme.cyan; onClicked: page.insert(modelData.kind) } } }
                    Text { visible: page.shown.length === 0; anchors.centerIn: parent; text: page.all.length === 0 ? i18n.t("No plug-ins found yet. Add the folders on the right and press RESCAN.", i18n.lang) : i18n.t("Nothing matches the filter.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9 } } }
            Rectangle { Layout.preferredWidth: 340; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 6
                    Text { text: i18n.t("FOLDERS", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
                    Repeater { model: settings.vstDefaultDirs; delegate: Text { text: "• " + modelData; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; elide: Text.ElideMiddle; Layout.fillWidth: true } }
                    Repeater { model: settings.vstDirs; delegate: RowLayout { Layout.fillWidth: true; Text { text: "• " + modelData; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 7; elide: Text.ElideMiddle; Layout.fillWidth: true } Btn { label: i18n.t("REMOVE", i18n.lang); onClicked: { settings.removeVstDir(modelData); page.tick++ } } } }
                    RowLayout { Layout.fillWidth: true; spacing: 6
                        Field { id: dirIn; Layout.fillWidth: true; placeholderText: i18n.t("another folder with .vst3 / .dll / .so", i18n.lang); font.family: Theme.mono; font.pixelSize: 8; implicitHeight: 24; onAccepted: { settings.addVstDir(text); text = ""; page.tick++ } }
                        Btn { label: i18n.t("ADD", i18n.lang); on: true; onClicked: { settings.addVstDir(dirIn.text); dirIn.text = ""; page.tick++ } } }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.lineSoft }
                    Text { text: i18n.t("BUILT-IN STAGES", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
                    Flow { Layout.fillWidth: true; spacing: 4
                        Repeater { model: page.builtIns; delegate: Btn { label: modelData.name; onClicked: page.insert(modelData.kind) } } }
                    Item { Layout.fillHeight: true }
                    Text { Layout.fillWidth: true; text: i18n.t("INSERT puts the plug-in into the first free slot of the selected strip (pick the strip on the MIXER). A plug-in that fails to load shows red in the rack with the reason. VST2 .dll / .so and VST3 bundles are both hosted; no plug-in editor window yet, parameters appear as generic controls.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap } } } }
    }
}
