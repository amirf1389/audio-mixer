// ASSIGN · MUTE GROUPS (1.27): eight mute groups on one page. Across the top, the eight group MUTE keys (name, colour, how many sources);
// below, the group picked for editing: its name and colour, then one collapsible section per kind of source (channels, AUX buses,
// FX buses, mains / matrix) with a toggle per source. It is the same model the mixer uses (Engine: a mute-group bitmask per strip and,
// since 1.27, per bus), so a group set up here mutes on the MIXER, the phones and the browser remote alike.
// Layout: an 8 px grid; every key is at least 44 px high (touch screens); the source cells are 64 x 44.
import QtQuick
import AmixOS
import QtQuick.Layouts

Flickable {
    id: page
    objectName: "assignPage"
    contentHeight: col.implicitHeight + 32; clip: true; boundsBehavior: Flickable.StopAtBounds
    property int edit: 1                                   // the group being edited, 1..8
    readonly property var groups: engine.muteGroupState
    readonly property var g: groups.length >= edit ? groups[edit - 1] : ({ name: "", active: false, color: 0, members: [], buses: [] })
    readonly property var buses: engine.busState
    property var open: ({ ch: true, aux: true, fx: false, main: false })
    function toggleOpen(k) { const o = Object.assign({}, open); o[k] = !o[k]; open = o }
    function tint(c) { return Theme.stripColors[Math.max(0, Math.min(7, c || 0))] }
    function sources(m) { return (m.members ? m.members.length : 0) + (m.buses ? m.buses.length : 0) }

    // a key of this page: 44 px high at least, the console's push-key look (square and lit in COCKPIT)
    component Key: Rectangle {
        id: k
        property string label; property string sub: ""; property bool lit: false; property color hue: Theme.accent; property bool can: true
        signal hit()
        implicitWidth: 64; implicitHeight: 44; radius: Theme.radiusS; opacity: can ? 1 : 0.4
        readonly property color face: lit ? Theme.harm(hue) : Theme.ov(0.06)
        gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(k.face) } GradientStop { position: 1; color: Theme.btnBottom(k.face) } }
        border.color: lit ? Qt.lighter(hue, 1.2) : Theme.lineSoft; border.width: 1
        Rectangle { visible: Theme.cockpit; anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter; anchors.topMargin: 4; width: parent.width - 16; height: 3; radius: 1; color: k.lit ? k.hue : Theme.ov(0.12) }
        Column { anchors.centerIn: parent; spacing: 2
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: k.label; color: k.lit && !Theme.cockpit ? "#000" : Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; elide: Text.ElideRight; width: Math.min(implicitWidth, k.width - 8); horizontalAlignment: Text.AlignHCenter }
            Text { visible: k.sub !== ""; anchors.horizontalCenter: parent.horizontalCenter; text: k.sub; color: k.lit && !Theme.cockpit ? "#000" : Theme.dim; font.family: Theme.mono; font.pixelSize: 9; elide: Text.ElideRight; width: Math.min(implicitWidth, k.width - 8); horizontalAlignment: Text.AlignHCenter } }
        MouseArea { anchors.fill: parent; enabled: k.can; cursorShape: Qt.PointingHandCursor; onClicked: k.hit() }
    }
    // a collapsible section: a 44 px head with the count, the body below
    component Section: ColumnLayout {
        id: sec
        property string key; property string title; property int count: 0; property string note: ""
        default property alias body: secBody.data
        Layout.fillWidth: true; spacing: 8
        Rectangle { Layout.fillWidth: true; implicitHeight: 44; radius: Theme.radiusS; color: Theme.head
            RowLayout { anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 8
                Text { text: page.open[sec.key] ? "▾" : "▸"; color: Theme.muted; font.pixelSize: 14 }
                Text { text: i18n.t(sec.title, i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 12; font.bold: true; font.letterSpacing: 2 }
                Item { Layout.fillWidth: true }
                Text { text: sec.count + i18n.t(" IN GROUP", i18n.lang); color: sec.count ? page.tint(page.g.color) : Theme.dim; font.family: Theme.mono; font.pixelSize: 11; font.bold: true } }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: page.toggleOpen(sec.key) } }
        Text { visible: page.open[sec.key] && sec.note !== ""; Layout.fillWidth: true; text: i18n.t(sec.note, i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 10; wrapMode: Text.WordWrap }
        Flow { id: secBody; visible: page.open[sec.key]; Layout.fillWidth: true; spacing: 8 }
    }

    ColumnLayout {
        id: col; x: 16; y: 16; width: page.width - 32; spacing: 16
        RowLayout { Layout.fillWidth: true; spacing: 8
            Icon { name: "assign"; color: Theme.accent; implicitWidth: 24; implicitHeight: 24 }
            Text { text: i18n.t("ASSIGN · MUTE GROUPS", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 16; font.bold: true; font.letterSpacing: 3 }
            Item { Layout.fillWidth: true }
            Key { objectName: "mgAllOff"; implicitWidth: 136; label: i18n.t("ALL GROUPS OFF", i18n.lang); hue: Theme.muted; can: page.groups.some(function (m) { return m.active }); onHit: { for (let i = 1; i <= 8; ++i) engine.setMuteGroupActive(i, false) } }
        }
        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 11
            text: i18n.t("A mute group silences every source in it with one press. Pick a group below its MUTE key to edit its name, colour and sources.", i18n.lang) }

        // THE GROUP MUTE ROW: eight keys, 64 px high, the group's colour as a bar on top
        GridLayout { Layout.fillWidth: true; columns: page.width < 900 ? 4 : 8; columnSpacing: 8; rowSpacing: 8
            Repeater { model: page.groups
                delegate: ColumnLayout { Layout.fillWidth: true; spacing: 8
                    readonly property color c: page.tint(modelData.color)
                    Rectangle { id: mr; objectName: "mgMute" + modelData.index; Layout.fillWidth: true; implicitHeight: 64; radius: Theme.radiusS
                        readonly property color face: modelData.active ? Theme.harm(Theme.danger) : Theme.ov(0.06)
                        gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(mr.face) } GradientStop { position: 1; color: Theme.btnBottom(mr.face) } }
                        border.color: modelData.active ? Theme.danger : Theme.lineSoft
                        Rectangle { anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 1; height: 4; radius: 2; color: mr.parent.c }
                        Column { anchors.centerIn: parent; spacing: 2; width: parent.width - 16
                            Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; text: modelData.name; color: modelData.active && !Theme.cockpit ? "#fff" : Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true }
                            Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: modelData.active ? i18n.t("MUTED", i18n.lang) : i18n.t("MUTE", i18n.lang); color: modelData.active ? (Theme.cockpit ? Theme.amber : "#fff") : Theme.dim; font.family: Theme.mono; font.pixelSize: 10; font.bold: true } }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: engine.setMuteGroupActive(modelData.index, !modelData.active) } }
                    Key { objectName: "mgEdit" + modelData.index; Layout.fillWidth: true; label: i18n.t("EDIT", i18n.lang); sub: page.sources(modelData) + i18n.t(" SOURCES", i18n.lang); lit: page.edit === modelData.index; hue: parent.c; onHit: page.edit = modelData.index }
                } }
        }

        // THE GROUP BEING EDITED: name, colour, sources
        Rectangle { Layout.fillWidth: true; implicitHeight: editCol.implicitHeight + 32; radius: Theme.radius; color: Theme.card; border.color: Theme.line
            ColumnLayout { id: editCol; x: 16; y: 16; width: parent.width - 32; spacing: 16
                RowLayout { Layout.fillWidth: true; spacing: 16
                    Rectangle { implicitWidth: 240; implicitHeight: 44; radius: Theme.radiusS; color: Theme.deep; border.color: nameIn.activeFocus ? Theme.accent : Theme.line
                        TextInput { id: nameIn; objectName: "mgName"; anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; verticalAlignment: TextInput.AlignVCenter; text: page.g.name; maximumLength: 16; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 13; font.bold: true; selectByMouse: true
                            onEditingFinished: engine.setMuteGroupName(page.edit, text) } }
                    Row { spacing: 8
                        Repeater { model: 8
                            delegate: Rectangle { objectName: "mgColor" + index; width: 44; height: 44; radius: Theme.radiusS; color: Theme.stripColors[index]; border.width: page.g.color === index ? 3 : 1; border.color: page.g.color === index ? Theme.ink : Theme.lineSoft
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: engine.setMuteGroupColor(page.edit, index) } } } }
                    Item { Layout.fillWidth: true }
                    Text { text: page.sources(page.g) + i18n.t(" SOURCES", i18n.lang); color: page.tint(page.g.color); font.family: Theme.mono; font.pixelSize: 14; font.bold: true }
                }
                Section { key: "ch"; title: "CHANNELS"; count: page.g.members ? page.g.members.length : 0
                    Repeater { model: page.open.ch ? engine.strips : 0
                        delegate: Key { objectName: "mgCh" + index; label: String(index + 1); sub: { const s = engine.stripLite(index); return s && s.name ? s.name : "" }
                            lit: page.g.members ? page.g.members.indexOf(index) >= 0 : false; hue: page.tint(page.g.color); can: index < engine.licensedChannels
                            onHit: engine.setMuteGroupMember(index, page.edit, !lit) } } }
                Section { key: "aux"; title: "AUX BUSES"; count: page.g.buses ? page.g.buses.filter(function (b) { return b < 8 }).length : 0
                    Repeater { model: page.open.aux ? 8 : 0
                        delegate: Key { objectName: "mgAux" + index; implicitWidth: 96; label: "AUX " + (index + 1); sub: page.buses[index] ? page.buses[index].name : ""
                            lit: page.g.buses ? page.g.buses.indexOf(index) >= 0 : false; hue: page.tint(page.g.color); onHit: engine.setBusMuteGroup(index, page.edit, !lit) } } }
                Section { key: "fx"; title: "FX BUSES"; count: page.g.buses ? page.g.buses.filter(function (b) { return b >= 8 }).length : 0
                    Repeater { model: page.open.fx ? 8 : 0
                        delegate: Key { objectName: "mgFx" + index; implicitWidth: 96; label: "FX " + (index + 1); sub: page.buses[index + 8] ? page.buses[index + 8].name : ""
                            lit: page.g.buses ? page.g.buses.indexOf(index + 8) >= 0 : false; hue: page.tint(page.g.color); onHit: engine.setBusMuteGroup(index + 8, page.edit, !lit) } } }
                Section { key: "main"; title: "MAINS · MATRIX"; count: 0; note: "The main mix and the matrix outputs stay out of mute groups, so one group key can never silence the whole show. Use MASTER MUTE on the mixer."
                    Repeater { model: page.open.main ? ["MAIN LR", "M1", "M2", "M3", "M4"] : []
                        delegate: Key { implicitWidth: 96; label: modelData; can: false } } }
            }
        }
    }
}
