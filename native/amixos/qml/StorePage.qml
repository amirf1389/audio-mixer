// STORE: everything that can go into the console, laid out like an app store. Built-in effects (by category), the extensions installed
// with AmF X1, and the VST3 / VST2 plug-ins found on this computer. Each tile: its icon, name, kind, ON / OFF (effects and extensions) and
// INSERT (into the selected channel's rack). ADD VST takes a folder (or a plug-in file: its folder is used) and rescans. ADD EXTENSION
// goes through the signed local update: an extension is program code, so only a vendor-signed package can bring one in.
// Nothing is downloaded from here and nothing is sent.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AmixOS

Flickable {
    id: page
    objectName: "storePage"
    signal go(string id)
    clip: true
    contentHeight: col.implicitHeight + 32
    function t(s) { return i18n.t(s, i18n.lang) }
    property string cat: "ALL"
    property string query: ""
    property int rev: 0
    property string note: ""
    readonly property var groups: ({ "EQ / TONE": ["dualeq", "mseq", "isoeq", "wshape", "exciter", "subsynth"], "DYNAMICS": ["opto", "glue", "multiband", "maximizer", "transient", "deesser", "desquash", "peakremover", "barrier", "rider", "clean", "antifeedback"],
        "SATURATION": ["analog", "drive", "tubeamp", "preamp", "postamp", "regulator", "protect", "tape", "convclip", "crusher"], "MODULATION": ["chorus", "flanger", "phaser", "tremolo", "wah", "mod", "autopan", "pitch", "doubler"],
        "SPACE / DELAY": ["reverb", "hall", "room", "delay", "syncdelay", "pingpong"], "SPATIAL": ["iamfelement", "iamfrender", "imager", "sampler"], "SYSTEM": ["aec", "hybrid", "logic"] })
    readonly property var groupIcon: ({ "EQ / TONE": "tone", "DYNAMICS": "comp", "SATURATION": "drive", "MODULATION": "sine", "SPACE / DELAY": "reverb", "SPATIAL": "orbit", "SYSTEM": "rack", "EXTENSIONS": "extension", "VST PLUG-INS": "vst", "OTHER": "fx" })
    readonly property var groupHue: ({ "EQ / TONE": "#38bdf8", "DYNAMICS": "#f59e0b", "SATURATION": "#f97316", "MODULATION": "#a78bfa", "SPACE / DELAY": "#34d399", "SPATIAL": "#22d3ee", "SYSTEM": "#94a3b8", "EXTENSIONS": "#f472b6", "VST PLUG-INS": "#818cf8", "OTHER": "#94a3b8" })
    readonly property var featured: ["fx:subsynth", "fx:antifeedback", "fx:barrier", "fx:dualeq"]
    readonly property var categories: ["ALL", "EQ / TONE", "DYNAMICS", "SATURATION", "MODULATION", "SPACE / DELAY", "SPATIAL", "SYSTEM", "EXTENSIONS", "VST PLUG-INS"]
    function groupOf(id) { const k = id.replace("fx:", ""); for (const g in groups) if (groups[g].indexOf(k) >= 0) return g; return "OTHER" }
    // one list: built-in effects and extensions from the engine's store, plus the scanned VST plug-ins
    readonly property var items: {
        page.rev; const l = [];
        for (const s of engine.storeItems()) l.push({ id: s.id, name: s.name, kind: s.kind, group: s.kind === "extension" ? "EXTENSIONS" : page.groupOf(s.id), on: s.on, installed: s.installed,
            note: s.kind === "extension" ? (!s.installed ? "not installed next to the console" : s.loads === false ? "cannot load: " + s.error : s.note) : "built-in effect", insert: s.kind === "effect" ? s.id : "" })
        for (const v of engine.vstCatalog()) l.push({ id: "vst:" + v.kind, name: v.name, kind: "vst", group: "VST PLUG-INS", on: true, installed: true, note: (v.vendor ? v.vendor + " · " : "") + v.format, insert: v.kind })
        return l }
    readonly property var shown: { const q = page.query.toLowerCase(); return page.items.filter(function (x) { return (page.cat === "ALL" || x.group === page.cat) && (q === "" || (x.name + " " + x.note + " " + x.group).toLowerCase().indexOf(q) >= 0) }) }
    function insert(kind) { const s = engine.selectedStrip; const slots = engine.plugins(s); for (let k = 0; k < slots.length; ++k) if (slots[k].kind === "") { engine.setPlugin(s, k, kind); page.note = page.t("Inserted into CH ") + (s + 1) + page.t(", slot ") + (k + 1) + "."; return }
        page.note = page.t("The rack of CH ") + (s + 1) + page.t(" is full.") }
    function count(g) { let n = 0; for (const x of page.items) if (g === "ALL" || x.group === g) ++n; return n }

    component Key: Rectangle { id: kk; property string label; property string icon; property bool lit: false; property color hue: Theme.accent; signal clicked()
        implicitWidth: kRow.implicitWidth + 22; implicitHeight: 30; radius: 15
        color: lit ? Theme.harm(hue) : (kH.hovered ? Theme.ov(0.10) : Theme.ov(0.05)); border.color: lit ? Theme.harm(hue) : Theme.lineSoft
        Row { id: kRow; anchors.centerIn: parent; spacing: 6
            Icon { visible: kk.icon !== ""; width: 14; height: 14; anchors.verticalCenter: parent.verticalCenter; name: kk.icon; color: kk.lit ? "#0b0f14" : Theme.muted; stroke: 2 }
            Text { anchors.verticalCenter: parent.verticalCenter; text: page.t(kk.label); color: kk.lit ? "#0b0f14" : Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } }
        HoverHandler { id: kH; cursorShape: Qt.PointingHandCursor }
        MouseArea { anchors.fill: parent; onClicked: kk.clicked() } }
    component Tile: Rectangle { id: tl; property var it; readonly property color hue: Theme.harm(page.groupHue[it.group] || "#94a3b8")
        implicitHeight: 92; radius: 14; color: Theme.card; border.color: tH.hovered ? Qt.rgba(hue.r, hue.g, hue.b, 0.6) : Theme.lineSoft; opacity: it.installed ? 1 : 0.55
        HoverHandler { id: tH }
        RowLayout { anchors.fill: parent; anchors.margins: 12; spacing: 12
            Rectangle { Layout.preferredWidth: 56; Layout.preferredHeight: 56; radius: 14   // the app tile
                gradient: Gradient { GradientStop { position: 0; color: Qt.rgba(tl.hue.r, tl.hue.g, tl.hue.b, 0.55) } GradientStop { position: 1; color: Qt.rgba(tl.hue.r, tl.hue.g, tl.hue.b, 0.18) } }
                border.color: Qt.rgba(1, 1, 1, 0.10)
                Icon { anchors.centerIn: parent; width: 28; height: 28; name: page.groupIcon[tl.it.group] || "fx"; color: "#f8fafc"; stroke: 1.9 } }
            ColumnLayout { Layout.fillWidth: true; spacing: 2
                Text { Layout.fillWidth: true; text: page.t(tl.it.name); color: Theme.ink; font.family: Theme.sans; font.pixelSize: 13; font.bold: true; elide: Text.ElideRight }
                Text { Layout.fillWidth: true; text: page.t(tl.it.group); color: Qt.rgba(tl.hue.r, tl.hue.g, tl.hue.b, 0.95); font.family: Theme.mono; font.pixelSize: 8; font.bold: true; font.letterSpacing: 1 }
                Text { Layout.fillWidth: true; text: page.t(tl.it.note); color: Theme.dim; font.family: Theme.sans; font.pixelSize: 10; elide: Text.ElideRight } }
            ColumnLayout { spacing: 6
                Rectangle { visible: tl.it.kind !== "vst"; objectName: "store_" + tl.it.id; implicitWidth: 58; implicitHeight: 24; radius: 12; enabled: tl.it.installed
                    color: tl.it.on ? Theme.harm(tl.hue) : Theme.ov(0.06); border.color: tl.it.on ? Theme.harm(tl.hue) : Theme.lineSoft
                    Text { anchors.centerIn: parent; text: tl.it.on ? page.t("ON") : page.t("OFF"); color: tl.it.on ? "#0b0f14" : Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { engine.setStoreEnabled(tl.it.id, !tl.it.on); page.rev++ } } }
                Rectangle { visible: tl.it.insert !== "" && tl.it.on; implicitWidth: 58; implicitHeight: 24; radius: 12; color: iH.hovered ? Theme.ov(0.12) : Theme.ov(0.05); border.color: Theme.lineSoft
                    Text { anchors.centerIn: parent; text: page.t("INSERT"); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                    HoverHandler { id: iH; cursorShape: Qt.PointingHandCursor }
                    MouseArea { anchors.fill: parent; onClicked: page.insert(tl.it.insert) } } } } }

    ColumnLayout {
        id: col; width: page.width - 32; x: 16; y: 16; spacing: 14
        // the banner
        Rectangle { Layout.fillWidth: true; implicitHeight: 128; radius: 18; clip: true
            gradient: Gradient { orientation: Gradient.Horizontal; GradientStop { position: 0; color: Qt.rgba(0.49, 0.23, 0.93, 0.55) } GradientStop { position: 0.6; color: Qt.rgba(0.92, 0.35, 0.05, 0.35) } GradientStop { position: 1; color: Qt.rgba(0.92, 0.35, 0.05, 0.12) } }
            border.color: Qt.rgba(1, 1, 1, 0.08)
            Image { anchors.right: parent.right; anchors.rightMargin: 24; anchors.verticalCenter: parent.verticalCenter; width: 92; height: 92; source: "qrc:/qt/qml/AmixOS/icons/amfx1.png"; smooth: true; mipmap: true }
            ColumnLayout { anchors.left: parent.left; anchors.leftMargin: 24; anchors.verticalCenter: parent.verticalCenter; spacing: 4
                Text { text: page.t("STORE"); color: "#f8fafc"; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 4 }
                Text { text: page.t("Effects, extensions and plug-ins"); color: "#ffffff"; font.family: Theme.sans; font.pixelSize: 24; font.weight: Font.Black }
                Text { text: page.count("ALL") + page.t(" items on this computer · insert into CH ") + (engine.selectedStrip + 1) + page.t(" · downloads only from the signed list"); color: Qt.rgba(1, 1, 1, 0.75); font.family: Theme.sans; font.pixelSize: 11 } } }
        // search and the add actions
        RowLayout { Layout.fillWidth: true; spacing: 10
            Rectangle { Layout.fillWidth: true; implicitHeight: 36; radius: 18; color: Theme.ov(0.06); border.color: sF.activeFocus ? Theme.accent : Theme.lineSoft
                Icon { x: 12; anchors.verticalCenter: parent.verticalCenter; width: 16; height: 16; name: "search"; color: Theme.muted }
                TextField { id: sF; objectName: "storeSearch"; anchors.fill: parent; anchors.leftMargin: 34; anchors.rightMargin: 12; background: null; color: Theme.ink; font.family: Theme.sans; font.pixelSize: 12
                    placeholderText: page.t("Search effects, extensions, plug-ins…"); placeholderTextColor: Theme.dim; onTextChanged: page.query = text } }
            Key { objectName: "storeDownloads"; label: "DOWNLOADS" + (updater.plugins.length ? " " + updater.plugins.length : ""); icon: "store"; lit: dlPanel.visible; hue: "#34d399"; onClicked: { dlPanel.visible = !dlPanel.visible; addVst.visible = false; addExt.visible = false } }
            Key { objectName: "storeAddVst"; label: "ADD VST"; icon: "plus"; lit: addVst.visible; hue: "#818cf8"; onClicked: { addVst.visible = !addVst.visible; addExt.visible = false; dlPanel.visible = false } }
            Key { objectName: "storeAddExt"; label: "ADD EXTENSION"; icon: "plus"; lit: addExt.visible; hue: "#f472b6"; onClicked: { addExt.visible = !addExt.visible; addVst.visible = false } }
            Key { label: "RESCAN"; icon: "refresh"; onClicked: { page.note = page.t("Rescan: ") + settings.rescanVst() + page.t(" plug-ins found."); page.rev++ } } }
        // DOWNLOADS: the plug-ins the vendor-signed list offers. DOWNLOAD fetches one over https, checks its size and sha256 against the signed
        // list and saves it into the console's own plug-in folder; REMOVE deletes that file. Nothing is run and nothing else is fetched.
        Rectangle { id: dlPanel; objectName: "storeDownloadPanel"; visible: false; Layout.fillWidth: true; implicitHeight: dlCol.implicitHeight + 24; radius: 8; color: Theme.card; border.color: Qt.rgba(0.204, 0.827, 0.6, 0.5)
            ColumnLayout { id: dlCol; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12; spacing: 8
                RowLayout { Layout.fillWidth: true; spacing: 8; Icon { width: 18; height: 18; name: "store"; color: Theme.harm("#34d399") }
                    Text { text: page.t("PLUG-IN DOWNLOADS"); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
                    Item { Layout.fillWidth: true }
                    Key { objectName: "storeDlCheck"; label: updater.state === "checking" ? "CHECKING" : "CHECK NOW"; icon: "refresh"; hue: "#34d399"; onClicked: updater.check() } }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.family: Theme.sans; font.pixelSize: 11
                       text: page.t("Plug-ins offered by AMFB Audio's signed list. A download comes over https, is checked against the signed size and sha256, and is saved into the console's own plug-in folder. Nothing is run by the download.") }
                Text { objectName: "storeDlMessage"; visible: updater.pluginMessage.length > 0; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: page.t(updater.pluginMessage); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 10; font.bold: true }
                Text { visible: updater.plugins.length === 0; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 10
                       text: page.t("The signed list offers no plug-ins for this computer yet. CHECK NOW reads the list again.") }
                Repeater { model: updater.plugins
                    delegate: Rectangle { objectName: "storeDl_" + modelData.id; Layout.fillWidth: true; implicitHeight: 50; radius: 6; color: Theme.ov(0.06); border.color: modelData.installed ? Qt.rgba(0.204, 0.827, 0.6, 0.6) : Theme.line
                        Rectangle { x: 1; y: 5; width: 4; height: parent.height - 10; radius: 2; color: modelData.installed ? "#34d399" : Theme.muted }
                        Rectangle { visible: modelData.busy; anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 1; height: 3; radius: 1; color: "#34d399"; width: (parent.width - 2) * modelData.progress / 100 }
                        RowLayout { anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 10; spacing: 10
                            ColumnLayout { Layout.fillWidth: true; spacing: 1
                                Text { Layout.fillWidth: true; elide: Text.ElideRight; text: modelData.name + (modelData.version ? "  " + modelData.version : ""); color: Theme.ink; font.family: Theme.sans; font.pixelSize: 12; font.bold: true }
                                Text { Layout.fillWidth: true; elide: Text.ElideRight; text: (modelData.note ? modelData.note + "  ·  " : "") + modelData.file + "  ·  " + (modelData.size / 1048576).toFixed(1) + " MB"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } }
                            Text { visible: modelData.busy; text: modelData.progress + " %"; color: "#34d399"; font.family: Theme.mono; font.pixelSize: 10; font.bold: true }
                            Text { visible: modelData.installed && !modelData.busy; text: page.t("INSTALLED"); color: "#34d399"; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                            Key { visible: !modelData.busy; label: modelData.installed ? "DOWNLOAD AGAIN" : "DOWNLOAD"; icon: "plus"; lit: !modelData.installed; hue: "#34d399"; onClicked: updater.downloadPlugin(modelData.id) }
                            Key { visible: modelData.installed && !modelData.busy; label: "REMOVE"; icon: "folder"; hue: "#f0304f"; onClicked: { updater.removePlugin(modelData.id); page.rev++ } } } } } } }
        Connections { target: updater; function onPluginInstalled() { page.rev++ } }
        // ADD VST: a folder (or one plug-in file: its folder) joins the scan
        Rectangle { id: addVst; visible: false; Layout.fillWidth: true; implicitHeight: avCol.implicitHeight + 24; radius: 14; color: Theme.card; border.color: Qt.rgba(0.506, 0.549, 0.973, 0.5)
            ColumnLayout { id: avCol; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12; spacing: 8
                RowLayout { spacing: 8; Icon { width: 18; height: 18; name: "vst"; color: Theme.harm("#818cf8") }
                    Text { text: page.t("ADD VST PLUG-INS"); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 } }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.family: Theme.sans; font.pixelSize: 11
                       text: page.t("Type or paste a folder with .vst3 / .dll / .so plug-ins, or one plug-in file (its folder is used). The folder is added to the scan and the list is read again. The standard folders are always scanned:") + " " + settings.vstDefaultDirs.join(" · ") }
                RowLayout { Layout.fillWidth: true; spacing: 8
                    Rectangle { Layout.fillWidth: true; implicitHeight: 32; radius: 8; color: Theme.deep; border.color: Theme.lineSoft
                        TextField { id: vstIn; objectName: "storeVstPath"; anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; background: null; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; placeholderText: page.t("C:/Program Files/Common Files/VST3  or  …/MyPlugin.vst3"); placeholderTextColor: Theme.dim } }
                    Key { label: "ADD + SCAN"; icon: "folder"; lit: true; hue: "#818cf8"
                        onClicked: { let p = vstIn.text.trim().replace(/\\/g, "/"); if (!p) return; if (/\.(vst3|dll|so|vst)$/i.test(p)) p = p.substring(0, p.lastIndexOf("/"));
                            const n = settings.addVstDir(p); page.note = page.t("Folder added: ") + p + " · " + n + page.t(" plug-ins found."); vstIn.text = ""; page.rev++; page.cat = "VST PLUG-INS" } }
                    Key { label: "OPEN VST FOLDER"; icon: "folder"; onClicked: settings.openVstFolder("") } }
                Repeater { model: settings.vstDirs
                    delegate: Text { text: "•  " + modelData; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9; elide: Text.ElideMiddle; Layout.fillWidth: true } } } }
        // ADD EXTENSION: program code, so only through the vendor-signed local update
        Rectangle { id: addExt; visible: false; Layout.fillWidth: true; implicitHeight: aeCol.implicitHeight + 24; radius: 14; color: Theme.card; border.color: Qt.rgba(0.957, 0.447, 0.714, 0.5)
            ColumnLayout { id: aeCol; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12; spacing: 8
                RowLayout { spacing: 8; Icon { width: 18; height: 18; name: "extension"; color: Theme.harm("#f472b6") }
                    Text { text: page.t("ADD AN EXTENSION"); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
                    Icon { width: 14; height: 14; name: "lock"; color: Theme.muted } }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.family: Theme.sans; font.pixelSize: 11
                       text: page.t("An extension runs as part of the console, so it is only taken from a package signed by AMFB Audio: put the extension's update folder (update.json and its files) on a USB stick or in Downloads and install it with LOCAL UPDATE. The signature and every file's checksum are checked first; an unsigned or changed file is refused.") }
                RowLayout { spacing: 8
                    Key { label: "LOCAL UPDATE"; icon: "refresh"; lit: true; hue: "#f472b6"; onClicked: page.go("engctl") }
                    Key { label: "SHOW EXTENSIONS"; icon: "extension"; onClicked: page.cat = "EXTENSIONS" } } } }
        Text { visible: page.note !== ""; Layout.fillWidth: true; text: page.note; color: Theme.emerald; font.family: Theme.sans; font.pixelSize: 11; wrapMode: Text.WordWrap }
        // categories
        Flow { Layout.fillWidth: true; spacing: 8
            Repeater { model: page.categories
                delegate: Key { objectName: "storeCat_" + index; label: modelData + "  " + page.count(modelData); icon: modelData === "ALL" ? "store" : (page.groupIcon[modelData] || "fx"); lit: page.cat === modelData; hue: modelData === "ALL" ? Theme.accent : page.groupHue[modelData]; onClicked: page.cat = modelData } } }
        // featured
        ColumnLayout { visible: page.cat === "ALL" && page.query === ""; Layout.fillWidth: true; spacing: 8
            Text { text: page.t("FEATURED"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; font.letterSpacing: 3 }
            GridLayout { Layout.fillWidth: true; columns: page.width > 1100 ? 4 : 2; columnSpacing: 10; rowSpacing: 10
                Repeater { model: page.items.filter(function (x) { return page.featured.indexOf(x.id) >= 0 })
                    delegate: Tile { Layout.fillWidth: true; it: modelData; implicitHeight: 104 } } } }
        // everything
        Text { text: page.cat === "ALL" ? page.t("EVERYTHING") : page.t(page.cat); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; font.letterSpacing: 3 }
        GridLayout { objectName: "storeGrid"; Layout.fillWidth: true; columns: page.width > 1100 ? 3 : 2; columnSpacing: 10; rowSpacing: 10
            Repeater { model: page.shown; delegate: Tile { Layout.fillWidth: true; it: modelData } } }
        ColumnLayout { visible: page.shown.length === 0; Layout.fillWidth: true; spacing: 6
            Icon { Layout.alignment: Qt.AlignHCenter; width: 40; height: 40; name: page.cat === "VST PLUG-INS" ? "vst" : "search"; color: Theme.dim }
            Text { Layout.alignment: Qt.AlignHCenter; text: page.cat === "VST PLUG-INS" ? page.t("No VST plug-ins found yet: press ADD VST and give their folder.") : page.t("Nothing matches."); color: Theme.muted; font.family: Theme.sans; font.pixelSize: 12 } }
    }
}
