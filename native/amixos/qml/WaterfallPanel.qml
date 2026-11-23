// WATERFALL 3D: the panel around the Waterfall3D view. The view (drag to turn and tilt it; double-click: back to 3D), the frequency, level
// and time labels placed on the block's edges as it turns, and the controls: VIEW (3D, FROM ABOVE = a spectrogram, FLAT = the spectrum with
// its history fading behind), SOURCE (master, monitor, any bus or channel), SPEED, HISTORY, RANGE, FREEZE, and the view's own CPU cost.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import AmixOS.Views
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: wp
    objectName: "waterfallPanel"
    property bool paused: false
    property var sources: []
    readonly property QtObject consoleEngine: engine   // the engine context object (inside the view, `engine` names its own property)
    property int sourceIndex: 0
    property int preset: 0                                   // 0 3D, 1 from above, 2 flat; -1 turned by hand
    readonly property var presets: [{ n: "3D", yaw: -28, pitch: 32 }, { n: "FROM ABOVE", yaw: 0, pitch: 90 }, { n: "FLAT", yaw: 0, pitch: 0 }]
    property real yawTo: -28
    property real pitchTo: 32
    function usePreset(i) { preset = i; yawTo = presets[i].yaw; pitchTo = presets[i].pitch }
    component Pill: Rectangle { property string label; property bool on: false; property color onColor: Theme.cyan; signal clicked()
        implicitWidth: Math.max(34, pl.implicitWidth + 14); implicitHeight: 22; radius: 7
        gradient: Gradient { GradientStop { position: 0; color: on ? Theme.btnTop(onColor) : Theme.ov(0.11) } GradientStop { position: 1; color: on ? Theme.btnBottom(onColor) : Theme.ov(0.08) } }
        border.color: on ? onColor : Theme.lineSoft
        Text { id: pl; anchors.centerIn: parent; text: i18n.t(parent.label, i18n.lang); color: parent.on ? "#000" : Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked() } }
    component Lab: Text { color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }

    ColumnLayout {
        anchors.fill: parent; spacing: 6
        Rectangle {   // the view
            Layout.fillWidth: true; Layout.fillHeight: true; radius: 6; color: "#06080c"; border.color: Theme.lineSoft; clip: true
            Waterfall3D {
                id: wf; objectName: "waterfall3d"; anchors.fill: parent
                engine: wp.consoleEngine; source: wp.sources.length ? wp.sources[Math.min(wp.sourceIndex, wp.sources.length - 1)].v : -1
                frozen: wp.paused || freeze.on
                yaw: wp.yawTo; pitch: wp.pitchTo
                Behavior on yaw { enabled: !drag.pressed; NumberAnimation { duration: 420; easing.type: Easing.OutCubic } }
                Behavior on pitch { enabled: !drag.pressed; NumberAnimation { duration: 420; easing.type: Easing.OutCubic } }
            }
            // the labels, on the block's front edge (frequency), its left side (level) and its far end (time)
            Repeater { model: [50, 100, 200, 500, 1000, 2000, 5000, 10000]
                delegate: Text { readonly property point p: (wf.viewTick, wf.project(modelData, wf.dbMin, 0)); x: p.x - width / 2; y: p.y + 4
                    text: modelData >= 1000 ? modelData / 1000 + "k" : modelData; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 } }
            Repeater { model: 4
                delegate: Text { readonly property real db: wf.dbMax - index * (wf.dbMax - wf.dbMin) / 3; readonly property point p: (wf.viewTick, wf.project(20, db, 0)); x: p.x - width - 6; y: p.y - height / 2; visible: wf.pitch < 70
                    text: Math.round(db); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 } }
            Text { readonly property point p: (wf.viewTick, wf.project(20000, wf.dbMin, 1)); x: Math.min(parent.width - width - 4, p.x + 6); y: Math.max(2, p.y - height - 2)
                text: "−" + (wf.history / wf.speed).toFixed(1) + " s"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
            Text { readonly property point p: (wf.viewTick, wf.project(20000, wf.dbMin, 0)); x: Math.min(parent.width - width - 4, p.x + 6); y: Math.min(parent.height - height - 2, p.y - height / 2)
                text: i18n.t("now", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
            Text { anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 8; text: wf.live ? "" : (freeze.on || wp.paused ? i18n.t("FROZEN", i18n.lang) : ""); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
            Text { objectName: "wfCpu"; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8; text: i18n.t("view CPU %1 ms/s", i18n.lang).arg(wf.cpuMs.toFixed(1)); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
            MouseArea { id: drag; anchors.fill: parent; cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                property point last
                onPressed: function (m) { last = Qt.point(m.x, m.y) }
                onPositionChanged: function (m) { wp.preset = -1; wp.yawTo = wf.yaw + (m.x - last.x) * 0.45; wp.pitchTo = Math.max(0, Math.min(90, wf.pitch + (m.y - last.y) * 0.35)); last = Qt.point(m.x, m.y) }
                onDoubleClicked: wp.usePreset(0) }
        }
        Flow {   // the controls
            Layout.fillWidth: true; spacing: 6
            Row { spacing: 4
                Lab { anchors.verticalCenter: parent.verticalCenter; text: i18n.t("VIEW", i18n.lang) }
                Repeater { model: wp.presets; delegate: Pill { objectName: "wfView" + index; label: modelData.n; on: wp.preset === index; onColor: Theme.orange; onClicked: wp.usePreset(index) } } }
            Row { spacing: 4
                Lab { anchors.verticalCenter: parent.verticalCenter; text: i18n.t("SOURCE", i18n.lang) }
                Combo { objectName: "wfSource"; width: 150; model: wp.sources.map(function (s) { return s.t }); currentIndex: wp.sourceIndex; onActivated: function (i) { wp.sourceIndex = i } } }
            Row { spacing: 4
                Lab { anchors.verticalCenter: parent.verticalCenter; text: i18n.t("SPEED", i18n.lang) }
                Repeater { model: [[10, "SLOW"], [30, "NORMAL"], [60, "FAST"]]; delegate: Pill { label: modelData[1]; on: wf.speed === modelData[0]; onClicked: wf.speed = modelData[0] } } }
            Row { spacing: 4
                Lab { anchors.verticalCenter: parent.verticalCenter; text: i18n.t("HISTORY", i18n.lang) }
                Repeater { model: [48, 80, 160]; delegate: Pill { label: modelData + ""; on: wf.history === modelData; onClicked: wf.history = modelData } } }
            Row { spacing: 4
                Lab { anchors.verticalCenter: parent.verticalCenter; text: i18n.t("RANGE dB", i18n.lang) }
                Repeater { model: [[-72, 0], [-96, 0], [-120, 0], [-60, -12]]; delegate: Pill { label: modelData[0] + "…" + modelData[1]; on: wf.dbMin === modelData[0] && wf.dbMax === modelData[1]; onClicked: { wf.dbMax = modelData[1]; wf.dbMin = modelData[0]; wf.dbMax = modelData[1] } } } }
            Pill { id: freeze; objectName: "wfFreeze"; property bool on2: false; label: "FREEZE"; on: on2; onColor: Theme.amber; onClicked: on2 = !on2 }
            Pill { label: "CLEAR"; onClicked: wf.clear() }
        }
    }
}
