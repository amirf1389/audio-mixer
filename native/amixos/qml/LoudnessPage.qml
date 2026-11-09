// LOUDNESS: EBU R128 / ITU-R BS.1770-4 from the engine (K-weighted, gated), true peak, loudness range, stereo correlation
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Layouts

Flickable {
    contentHeight: col.implicitHeight + 24; clip: true
    ColumnLayout {
        id: col; width: parent.width - 20; x: 10; y: 10; spacing: 10
        Panel {
            Layout.fillWidth: true
            title: "LOUDNESS • EBU R128"
            subtitle: "Momentary 400 ms, short-term 3 s, integrated (gated -70 / -10 LU), loudness range, 4x true peak. Target -23 LUFS broadcast, -14 LUFS streaming."
            GridLayout {
                columns: width > 700 ? 6 : 3; Layout.fillWidth: true; columnSpacing: 10; rowSpacing: 10
                Repeater {
                    model: [
                        { k: "momentary", t: "MOMENTARY", u: "LUFS", c: Theme.cyan }, { k: "shortTerm", t: "SHORT-TERM", u: "LUFS", c: Theme.cyan }, { k: "integrated", t: "INTEGRATED", u: "LUFS", c: Theme.amber },
                        { k: "lra", t: "RANGE", u: "LU", c: Theme.violet }, { k: "truePeak", t: "TRUE PEAK", u: "dBTP", c: Theme.danger }, { k: "correlation", t: "CORRELATION", u: "", c: Theme.emerald } ]
                    delegate: Rectangle { Layout.fillWidth: true; implicitHeight: 86; radius: 8; color: Qt.rgba(0, 0, 0, 0.3); border.color: Theme.lineSoft
                        property real v: engine.telemetry[modelData.k] === undefined ? -120 : engine.telemetry[modelData.k]
                        ColumnLayout { anchors.centerIn: parent; spacing: 2
                            Text { Layout.alignment: Qt.AlignHCenter; text: modelData.t; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.letterSpacing: 1.5 }
                            Text { Layout.alignment: Qt.AlignHCenter; text: modelData.k === "correlation" ? (v > 0 ? "+" : "") + v.toFixed(2) : (v <= -119 ? "-inf" : v.toFixed(1)); color: modelData.c; font.family: Theme.mono; font.pixelSize: 22; font.bold: true }
                            Text { Layout.alignment: Qt.AlignHCenter; text: modelData.u; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 } } }
                }
            }
            Rectangle {   // the integrated loudness against the two targets
                Layout.fillWidth: true; implicitHeight: 26; radius: 6; color: "#05070a"; border.color: Theme.lineSoft
                property real v: engine.telemetry.integrated === undefined ? -120 : engine.telemetry.integrated
                Rectangle { x: 2; y: 2; height: parent.height - 4; radius: 4; width: Math.max(0, Math.min(1, (parent.v + 60) / 60)) * (parent.width - 4); color: parent.v > -12 ? Theme.danger : parent.v > -16 ? Theme.amber : Theme.emerald; Behavior on width { NumberAnimation { duration: 120 } } }
                Rectangle { x: 2 + (parent.width - 4) * ((-23 + 60) / 60); width: 2; height: parent.height; color: Theme.amber; Text { anchors.bottom: parent.top; text: "-23"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 7 } }
                Rectangle { x: 2 + (parent.width - 4) * ((-14 + 60) / 60); width: 2; height: parent.height; color: Theme.cyan; Text { anchors.bottom: parent.top; text: "-14"; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 7 } }
            }
        }
    }
}
