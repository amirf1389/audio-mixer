// A vertical level meter: -60 .. 0 dBFS, green / amber / red zones, peak hold line, clip lamp
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)

Item {
    id: meter
    property real peakDb: -120
    property real rmsDb: -120
    property real holdDb: -120
    property bool clip: false
    property real floorDb: -60
    implicitWidth: 10
    // CLIP LATCH: the lamp stays lit after a peak above 0 dBFS (or the engine's clip flag) until the meter is clicked
    property bool latched: false
    onPeakDbChanged: if (peakDb > 0) latched = true
    onClipChanged: if (clip) latched = true
    MouseArea { anchors.fill: parent; onClicked: meter.latched = false }
    implicitHeight: 160
    function norm(db) { return Math.max(0, Math.min(1, (db - floorDb) / -floorDb)); }
    Rectangle { anchors.fill: parent; radius: 3; color: Theme.deep; border.color: Theme.lineSoft }
    readonly property real inset: width < 8 ? 1 : 2   // a narrow bar (the IN column) keeps a fill that can be seen
    Item {  // rms: the colour zones belong to the scale (red only near 0 dBFS), the level uncovers them from the bottom
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: meter.inset
        height: (parent.height - 2 * meter.inset) * meter.norm(meter.rmsDb)
        clip: true; opacity: 0.95
        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: meter.height - 2 * meter.inset; radius: Math.min(2, width / 2)
            gradient: Gradient { GradientStop { position: 0.0; color: Theme.danger } GradientStop { position: 0.012; color: Theme.amber } GradientStop { position: -3 / meter.floorDb; color: Theme.amber } GradientStop { position: -3 / meter.floorDb + 0.001; color: Theme.emerald } GradientStop { position: 0.45; color: Theme.emerald } GradientStop { position: 1.0; color: "#047857" } } }
        // no height animation: the values arrive 30 times a second already smoothed (VU / RMS in the engine); an animation per meter kept 32+ animations running and the window redrawing at full rate
    }
    Rectangle {  // peak
        anchors.left: parent.left; anchors.right: parent.right; anchors.margins: meter.inset
        y: meter.inset + (parent.height - 2 * meter.inset) * (1 - meter.norm(meter.peakDb)) - 1
        height: 2; color: Theme.ov(0.8); visible: meter.peakDb > meter.floorDb
    }
    Rectangle {  // hold
        anchors.left: parent.left; anchors.right: parent.right; anchors.margins: meter.inset
        y: meter.inset + (parent.height - 2 * meter.inset) * (1 - meter.norm(meter.holdDb)) - 1
        height: 1; color: Theme.amber; visible: meter.holdDb > meter.floorDb
    }
    Rectangle { anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter; anchors.topMargin: -8; width: 6; height: 4; radius: 1; color: meter.clip || meter.latched ? Theme.danger : Theme.ov(0.12) }
}
