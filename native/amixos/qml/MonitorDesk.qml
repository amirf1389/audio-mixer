// MONITOR DESK (1.27): the right-hand column of the MIXER, about 18 % of its width. The listening keys first (DIM, MONO, MUTE; output
// A / B / A+B; the PHONES level), then TALKBACK (key, level, where it goes), CLEAR SOLO, and a master strip whose source is picked from
// MAIN LR, the four monitor mixes M1-M4 (AUX 1-4) or any bus B1-B16, with its own fader, meter, readout and COMP / INS lamps.
// The 8 px grid; every key 44 px high.
import QtQuick
import AmixOS
import QtQuick.Layouts
import QtQuick.Controls

Rectangle {
    id: md
    objectName: "monitorDesk"
    property var mon: engine.monitor
    property int tick: 0
    radius: Theme.radius; color: Theme.card; border.color: Theme.line; clip: true
    readonly property string sel: mon.masterSel || "MAIN"
    readonly property int bus: sel === "MAIN" ? -1 : sel[0] === "M" ? parseInt(sel.substring(1)) - 1 : parseInt(sel.substring(1)) - 1
    readonly property var bs: { engine.busState; return bus >= 0 ? engine.busState[bus] : null }
    readonly property real selDb: bus < 0 ? engine.masterDb : (bs ? bs.db : 0)
    readonly property bool compOn: bus < 0 ? !!engine.mastering().on : false
    readonly property bool insOn: bus < 0 ? false : !!(bs && bs.fx)
    component Key: Rectangle {
        id: k
        property string label; property bool lit: false; property color hue: Theme.accent; signal hit()
        Layout.fillWidth: true; implicitHeight: 44; radius: Theme.radiusS
        readonly property color face: lit ? Theme.harm(hue) : Theme.ov(0.06)
        gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(k.face) } GradientStop { position: 1; color: Theme.btnBottom(k.face) } }
        border.color: lit ? Qt.lighter(hue, 1.2) : Theme.lineSoft
        Rectangle { visible: Theme.cockpit; anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter; anchors.topMargin: 4; width: parent.width - 16; height: 3; radius: 1; color: k.lit ? k.hue : Theme.ov(0.12) }
        Text { anchors.centerIn: parent; width: parent.width - 8; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; text: i18n.t(k.label, i18n.lang); color: k.lit && !Theme.cockpit ? "#000" : Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: k.hit() }
    }
    component Head: Text { Layout.fillWidth: true; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
    component Lamp: Rectangle { property string label; property bool lit; Layout.fillWidth: true; implicitHeight: 24; radius: Theme.radiusS; color: lit ? Qt.rgba(Theme.emerald.r, Theme.emerald.g, Theme.emerald.b, 0.22) : Theme.ov(0.04); border.color: lit ? Theme.emerald : Theme.lineSoft
        Text { anchors.centerIn: parent; text: i18n.t(parent.label, i18n.lang); color: parent.lit ? Theme.emerald : Theme.dim; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } }

    Flickable {
        anchors.fill: parent; anchors.margins: 8; contentHeight: col.implicitHeight; clip: true; boundsBehavior: Flickable.StopAtBounds
        ColumnLayout {
            id: col; width: parent.width; spacing: 8
            RowLayout { Layout.fillWidth: true; spacing: 8
                Icon { name: "monitor"; color: Theme.emerald; width: 20; height: 20 }
                Text { text: i18n.t("MONITOR", i18n.lang); color: Theme.emerald; font.family: Theme.mono; font.pixelSize: 13; font.bold: true; font.letterSpacing: 2 } }
            RowLayout { Layout.fillWidth: true; spacing: 8
                Key { objectName: "mdDim"; label: "DIM"; lit: !!md.mon.dim; hue: Theme.amber; onHit: engine.setMonitor(md.mon.source, md.mon.db, !md.mon.dim, md.mon.mono, md.mon.mute) }
                Key { objectName: "mdMono"; label: "MONO"; lit: !!md.mon.mono; hue: Theme.cyan; onHit: engine.setMonitor(md.mon.source, md.mon.db, md.mon.dim, !md.mon.mono, md.mon.mute) }
                Key { objectName: "mdMute"; label: "MUTE"; lit: !!md.mon.mute; hue: Theme.danger; onHit: engine.setMonitor(md.mon.source, md.mon.db, md.mon.dim, md.mon.mono, !md.mon.mute) } }
            Head { text: i18n.t("OUTPUT", i18n.lang) }
            RowLayout { Layout.fillWidth: true; spacing: 8
                Repeater { model: ["A", "B", "A+B"]
                    delegate: Key { objectName: "mdOut" + index; label: modelData; lit: (md.mon.out || 0) === index; hue: Theme.emerald; onHit: engine.setMonitorOut(index) } } }
            Head { text: i18n.t("PHONES", i18n.lang) + "  " + Theme.dbText(md.mon.phonesDb || 0) + i18n.t(" dB", i18n.lang) }
            MSlider { objectName: "mdPhones"; Layout.fillWidth: true; implicitHeight: 44; tint: Theme.emerald; from: -60; to: 10; stepSize: 0.5; value: md.mon.phonesDb || 0; onMoved: engine.setPhonesDb(value) }
            Head { text: i18n.t("TALKBACK", i18n.lang) + "  " + Theme.dbText(md.mon.talkDb === undefined ? -10 : md.mon.talkDb) + i18n.t(" dB", i18n.lang) }
            Key { objectName: "mdTalk"; implicitHeight: 52; label: md.mon.talkOn ? "TALKBACK ON" : "TALKBACK"; lit: !!md.mon.talkOn; hue: Theme.amber; onHit: engine.setTalkback(!md.mon.talkOn, md.mon.talkDb, md.mon.talkDest || 0) }
            MSlider { objectName: "mdTalkDb"; Layout.fillWidth: true; implicitHeight: 44; tint: Theme.amber; from: -60; to: 10; stepSize: 0.5; value: md.mon.talkDb === undefined ? -10 : md.mon.talkDb; onMoved: engine.setTalkback(md.mon.talkOn, value, md.mon.talkDest || 0) }
            Combo { objectName: "mdTalkDest"; Layout.fillWidth: true; implicitHeight: 44
                model: { const l = [i18n.t("TO MAIN", i18n.lang)]; for (let k = 1; k <= 16; ++k) l.push(i18n.t("TO B", i18n.lang) + k); return l }
                currentIndex: md.mon.talkDest || 0; onActivated: function (i) { engine.setTalkback(md.mon.talkOn, md.mon.talkDb, i) } }
            Key { objectName: "mdClrSolo"; label: "CLR SOLO"; hue: Theme.amber; onHit: engine.clearSolo() }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.lineSoft }
            Head { text: i18n.t("MASTER STRIP", i18n.lang) }
            Combo { objectName: "mdMasterSel"; Layout.fillWidth: true; implicitHeight: 44
                readonly property var ids: { const l = ["MAIN", "M1", "M2", "M3", "M4"]; for (let k = 1; k <= 16; ++k) l.push("B" + k); return l }
                model: ids.map(function (x) { return x === "MAIN" ? "MAIN LR" : x })
                currentIndex: Math.max(0, ids.indexOf(md.sel)); onActivated: function (i) { engine.setMasterSel(ids[i]) } }
            RowLayout { Layout.fillWidth: true; spacing: 8
                Lamp { objectName: "mdComp"; label: "COMP"; lit: md.compOn }
                Lamp { objectName: "mdIns"; label: "INS"; lit: md.insOn } }
            RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 200; Layout.minimumHeight: 200; spacing: 8
                Item { Layout.fillWidth: true }
                Fader { objectName: "mdFader"; Layout.fillHeight: true; Layout.minimumHeight: 200; showLevels: false; value: md.selDb
                    onMoved: function (db) { if (md.bus < 0) engine.masterDb = db; else engine.setBusLevel(md.bus, db) } }
                MeterBar { Layout.fillHeight: true; implicitWidth: 8; peakDb: md.bus < 0 ? (engine.telv("masterPeakDb", md.tick) ?? -120) : (engine.busMeters[md.bus] !== undefined ? engine.busMeters[md.bus].peakDb : -120) }
                Item { Layout.fillWidth: true } }
            Rectangle { objectName: "mdDb"; Layout.fillWidth: true; implicitHeight: 32; radius: Theme.radiusS; color: Theme.deep; border.color: Theme.lineSoft
                Text { anchors.centerIn: parent; text: (md.bus < 0 ? i18n.t("MAIN", i18n.lang) : md.sel) + "  " + Theme.dbText(md.selDb) + i18n.t(" dB", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 13; font.bold: true } }
        }
    }
}
