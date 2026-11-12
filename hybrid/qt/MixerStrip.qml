// One channel strip of the hybrid mixer: source name, stereo meter with RMS, peak hold and true-peak
// read-out, phase bar, level fader, balance, width, mute and solo.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root
    required property var mixer
    property int stripIndex: 0            // 0-15, or 16 for the master bus
    readonly property bool master: stripIndex >= mixer.stripCount
    readonly property var level: mixer.levels.length > stripIndex ? mixer.levels[stripIndex] : null
    property var params: ({})
    function reload() { params = master ? { source: "stereo bus", gainDb: mixer.masterGainDb } : mixer.strip(stripIndex) }
    function norm(db) { return Math.max(0, Math.min(1, (db + 60) / 66)) }     // -60 .. +6 dB on the meter

    implicitWidth: 96; implicitHeight: 430
    radius: 8; color: "#1e2228"
    border.color: master ? "#e0a340" : "#313741"; border.width: 1
    opacity: !master && level && !level.active ? 0.6 : 1
    Component.onCompleted: reload()
    Connections { target: root.mixer; function onParamsChanged() { root.reload() } }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 8; spacing: 6

        Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; color: "#e8eaee"; font.bold: true; font.pixelSize: 13
               text: root.master ? "MASTER" : (root.stripIndex + 1) }
        Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; color: "#9aa3b0"; font.pixelSize: 10; elide: Text.ElideRight
               text: root.params.source || "—" }

        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 6
            Row {                                   // the meter
                Layout.fillHeight: true; spacing: 2
                Repeater {
                    model: 2
                    Rectangle {
                        id: bar
                        required property int index
                        readonly property real peak: root.level ? root.norm(index ? root.level.peakR : root.level.peakL) : 0
                        readonly property real rms: root.level ? root.norm(index ? root.level.rmsR : root.level.rmsL) : 0
                        width: 9; height: parent.height; color: "#0c0d10"; radius: 2; clip: true
                        Rectangle { width: parent.width; height: parent.height * bar.peak; anchors.bottom: parent.bottom
                                    color: bar.peak > root.norm(0) ? "#e5534b" : bar.peak > root.norm(-9) ? "#e0a340" : "#2f7d5b" }
                        Rectangle { width: parent.width; height: parent.height * bar.rms; anchors.bottom: parent.bottom; color: "#4cc38a" }
                        Rectangle { width: parent.width; height: 2; color: "#e8eaee"; visible: root.level && root.level.hold > -59
                                    y: parent.height * (1 - (root.level ? root.norm(root.level.hold) : 0)) - 1 }
                    }
                }
            }
            Slider {
                id: fader
                Layout.fillHeight: true; orientation: Qt.Vertical
                from: -60; to: 12; stepSize: 0.5
                value: Math.max(-60, root.params.gainDb !== undefined ? root.params.gainDb : 0)
                Accessible.name: (root.master ? "Master" : "Strip " + (root.stripIndex + 1)) + " level"
                onMoved: {
                    const db = value <= -60 ? -90 : value
                    if (root.master) root.mixer.masterGainDb = db; else root.mixer.setGainDb(root.stripIndex, db)
                }
            }
        }

        Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 10
               color: root.level && root.level.truePeak > 0 ? "#e5534b" : "#9aa3b0"
               text: root.level && root.level.truePeak > -89 ? root.level.truePeak.toFixed(1) + " dBTP" : "— dBTP" }
        Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; color: "#e8eaee"; font.pixelSize: 11
               text: fader.value <= -60 ? "-∞ dB" : (fader.value > 0 ? "+" : "") + fader.value.toFixed(1) + " dB" }

        Rectangle {                                 // phase: left = out of phase, right = in phase
            Layout.fillWidth: true; height: 6; radius: 3; color: "#0c0d10"
            Rectangle { width: 2; height: parent.height; x: parent.width / 2 - 1; color: "#313741" }
            Rectangle { width: 6; height: parent.height; radius: 3; visible: root.level !== null
                        color: root.level && root.level.phase < -0.2 ? "#e5534b" : "#4cc38a"
                        x: (parent.width - width) * ((root.level ? root.level.phase : 0) + 1) / 2 }
        }

        Slider { visible: !root.master; Layout.fillWidth: true; from: -1; to: 1; stepSize: 0.05
                 value: root.params.pan !== undefined ? root.params.pan : 0; Accessible.name: "Balance"
                 onMoved: root.mixer.setPan(root.stripIndex, value) }
        Slider { visible: !root.master; Layout.fillWidth: true; from: 0; to: 2; stepSize: 0.05
                 value: root.params.width !== undefined ? root.params.width : 1; Accessible.name: "Stereo width"
                 onMoved: root.mixer.setWidth(root.stripIndex, value) }
        RowLayout {
            visible: !root.master; Layout.fillWidth: true; spacing: 4
            Repeater {
                model: [{ key: "mute", label: "M", name: "Mute", on: "#e0a340" }, { key: "solo", label: "S", name: "Solo", on: "#4cc38a" }]
                Button {
                    id: toggle
                    required property var modelData
                    Layout.fillWidth: true; checkable: true; checked: !!root.params[modelData.key]
                    Accessible.name: modelData.name
                    background: Rectangle { radius: 4; color: toggle.checked ? toggle.modelData.on : "#2a2f37"
                                            border.color: toggle.visualFocus ? "#e8eaee" : "#313741" }
                    contentItem: Text { text: toggle.modelData.label; font.bold: true; horizontalAlignment: Text.AlignHCenter
                                        color: toggle.checked ? "#14161a" : "#e8eaee" }
                    onToggled: modelData.key === "mute" ? root.mixer.setMute(root.stripIndex, checked) : root.mixer.setSolo(root.stripIndex, checked)
                }
            }
        }
    }
}
