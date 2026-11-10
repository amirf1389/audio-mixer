// ACCESS: the permission matrix: which profile may touch faders, DSP, routing, loudness, the archive (scenes / presets / import) and the
// system pages. Editing needs ADMIN mode (PIN). Pages a profile may not use are hidden from the rail; the fader surface honours FADERS.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "accessPage"
    readonly property var mx: access.matrix
    readonly property var permText: ({ faders: "FADERS, MUTES, SOLOS, PANS", dsp: "EQ · DYNAMICS · EFFECTS · SENDS · ARRANGE · TOOLS", routing: "CONNECTIONS · ROUTING · LIVE SOURCES", loudness: "LOUDNESS · LIMITER", archive: "SCENES · PRESETS · IMPORT / OVERWRITE", system: "SETTINGS · CALIBRATE · DIAGNOSTICS · ACCOUNT · ACCESS · ADMIN · LICENSE" })
    component Head: Text { color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 24; radius: 4; color: on ? onColor : Theme.ov(0.08); border.color: Theme.lineSoft
        Text { id: bl; anchors.centerIn: parent; text: label; color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle { Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 8
                Text { text: i18n.t("ACCESS:", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Text { text: i18n.t("role-based permission matrix", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Item { Layout.fillWidth: true }
                Text { text: access.adminMode ? i18n.t("ADMIN MODE: editing allowed", i18n.lang) : i18n.t("read-only: enter the ADMIN PIN to edit", i18n.lang); color: access.adminMode ? Theme.emerald : Theme.amber; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                Btn { label: i18n.t("DEFAULTS", i18n.lang); on: access.adminMode; onClicked: access.resetMatrix() } } }
        RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 4
                    Head { text: i18n.t("PERMISSION PERIMETERS", i18n.lang) }
                    GridLayout { columns: 1 + access.roles.length; columnSpacing: 6; rowSpacing: 4; Layout.fillWidth: true
                        Text { text: ""; Layout.preferredWidth: 260 }
                        Repeater { model: access.roles; delegate: Text { text: modelData.name; color: access.role === modelData.id ? Theme.amber : Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true } }
                        Repeater { model: access.perms.length * (1 + access.roles.length)
                            delegate: Item { readonly property int cols: 1 + access.roles.length; readonly property int c: index % cols; readonly property string pm: access.perms[Math.floor(index / cols)]; Layout.fillWidth: c > 0; Layout.preferredWidth: c === 0 ? 260 : 100; implicitHeight: 34
                                Text { visible: c === 0; anchors.verticalCenter: parent.verticalCenter; text: pm.toUpperCase() + ":  " + page.permText[pm]; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; width: parent.width; elide: Text.ElideRight }
                                Rectangle { visible: c > 0; anchors.centerIn: parent; width: 64; height: 24; radius: 12
                                    readonly property string rid: c > 0 ? access.roles[c - 1].id : ""
                                    readonly property bool on: rid !== "" && page.mx[rid] !== undefined ? page.mx[rid][pm] === true : false
                                    color: on ? Theme.emerald : Theme.ov(0.08); border.color: Theme.lineSoft; opacity: access.adminMode ? 1 : 0.8
                                    Rectangle { width: 18; height: 18; radius: 9; y: 3; x: parent.on ? parent.width - 21 : 3; color: parent.on ? "#000" : Theme.dim; Behavior on x { NumberAnimation { duration: 120 } } }
                                    MouseArea { anchors.fill: parent; onClicked: access.setPerm(parent.rid, pm, !parent.on) } } } } }
                    Item { Layout.fillHeight: true }
                    Text { Layout.fillWidth: true; text: i18n.t("What a denied perimeter does: FADERS off = the surface is read-only (meters still run). DSP / ROUTING / LOUDNESS / ARCHIVE / SYSTEM off = those pages leave the rail for that profile. FOH MASTER always keeps SYSTEM. ", i18n.lang) + access.message; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap } } }
            Rectangle { Layout.preferredWidth: 230; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 8
                    Head { text: i18n.t("ADMIN SECURITY PIN", i18n.lang) }
                    Text { Layout.fillWidth: true; text: access.adminMode ? i18n.t("ADMIN mode is on for 30 minutes or until LOCK CONSOLE / LEAVE.", i18n.lang) : i18n.t("Enter the PIN to edit the matrix, the edition and the audit log.", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }
                    PinPad { Layout.alignment: Qt.AlignHCenter; hint: i18n.t("ADMIN PIN", i18n.lang); onEntered: function (pin) { access.enterAdmin(pin) } }
                    Btn { visible: access.adminMode; label: i18n.t("LEAVE ADMIN MODE", i18n.lang); onClicked: access.leaveAdmin() }
                    Item { Layout.fillHeight: true } } } }
    }
}
