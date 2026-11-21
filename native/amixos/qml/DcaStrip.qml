// A DCA GROUP CHANNEL on the MIXER: one group in use as a strip of its own, after the input strips and before the master. Its fader is an
// offset (-inf .. +10 dB) on its members' gain: their own faders do not move. MUTE mutes every member. The name can be edited in place,
// the colour picked from the swatches, ASSIGN opens the DCA ASSIGN panel for this group. When this group is the DUCKER's target, a
// gain-reduction meter shows how far the music is down now; the voice key group shows KEY.
import QtQuick
import AmixOS
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: ds
    property var d: ({})
    property int tick: 0
    signal assign(int group)
    readonly property color dc: Theme.stripColors[d.color || 0]
    readonly property real gr: d.duck === 1 ? (tick, engine.duckGrDb) : 0
    objectName: "dcaStrip" + (d.index || 0)
    radius: 6; color: Qt.rgba(dc.r, dc.g, dc.b, 0.07); border.color: dc; border.width: 1
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 5; spacing: 4
        Rectangle { Layout.fillWidth: true; implicitHeight: 18; radius: 3; color: ds.dc
            Text { anchors.centerIn: parent; text: i18n.t("DCA", i18n.lang) + " " + (ds.d.index || 0); color: "#0b0f14"; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } }
        TextInput { objectName: "dcaName" + (ds.d.index || 0); Layout.fillWidth: true; text: ds.d.name || ""; color: ds.dc; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; horizontalAlignment: Text.AlignHCenter; selectByMouse: true; clip: true
            onEditingFinished: engine.setDcaName(ds.d.index, text) }
        Text { Layout.alignment: Qt.AlignHCenter; text: (ds.d.count || 0) + " " + i18n.t("CH", i18n.lang) + (ds.d.duck === 1 ? " · " + i18n.t("DUCKED", i18n.lang) : ds.d.duck === 2 ? " · " + i18n.t("KEY", i18n.lang) : ""); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true }
        Rectangle { objectName: "dcaMute" + (ds.d.index || 0); Layout.fillWidth: true; implicitHeight: 20; radius: 5; color: ds.d.mute ? Theme.danger : Theme.ov(0.08); border.color: Theme.lineSoft
            Text { anchors.centerIn: parent; text: i18n.t("MUTE", i18n.lang); color: ds.d.mute ? "#fff" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: engine.setDcaMute(ds.d.index, !ds.d.mute) } }
        Rectangle { objectName: "dcaAssignBtn" + (ds.d.index || 0); Layout.fillWidth: true; implicitHeight: 20; radius: 5; color: Theme.ov(0.06); border.color: ds.dc
            Text { anchors.centerIn: parent; text: i18n.t("ASSIGN", i18n.lang); color: ds.dc; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: ds.assign(ds.d.index) } }
        RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 3
            Fader { objectName: "dcaFader" + (ds.d.index || 0); Layout.fillHeight: true; Layout.fillWidth: true; showLevels: false; tint: ds.dc; from: -60; to: 10; value: ds.d.db || 0; onMoved: function (db) { engine.setDcaLevel(ds.d.index, db) } }
            // the ducker's gain reduction on its target group: 0 at the top, the depth at the bottom
            Rectangle { visible: ds.d.duck === 1; Layout.fillHeight: true; implicitWidth: 6; radius: 2; color: Theme.ov(0.08)
                Rectangle { anchors.top: parent.top; width: parent.width; radius: 2; color: Theme.cyan; height: parent.height * Math.min(1, -ds.gr / 40) } } }
        Text { Layout.alignment: Qt.AlignHCenter; text: (ds.d.db <= -59.95 ? "-inf" : Theme.dbText(ds.d.db || 0)) + (ds.d.duck === 1 && ds.gr < -0.1 ? "  " + ds.gr.toFixed(1) : ""); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 10; font.bold: true }
        Row { Layout.alignment: Qt.AlignHCenter; spacing: 2
            Repeater { model: 8; delegate: Rectangle { width: 8; height: 8; radius: 2; color: Theme.stripColors[index]; border.color: (ds.d.color || 0) === index ? Theme.ink : "transparent"
                MouseArea { anchors.fill: parent; onClicked: engine.setDcaColor(ds.d.index, index) } } } }
    }
}
