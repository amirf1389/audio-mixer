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
    implicitHeight: 160
    function norm(db) { return Math.max(0, Math.min(1, (db - floorDb) / -floorDb)); }
    Rectangle { anchors.fill: parent; radius: 3; color: Theme.deep; border.color: Theme.lineSoft }
    Rectangle {  // rms
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 2
        height: (parent.height - 4) * meter.norm(meter.rmsDb)
        radius: 2
        gradient: Gradient { GradientStop { position: 0.0; color: Theme.danger } GradientStop { position: 0.15; color: Theme.amber } GradientStop { position: 0.45; color: Theme.emerald } GradientStop { position: 1.0; color: "#047857" } }
        opacity: 0.95
        // no height animation: the values arrive 30 times a second already smoothed (VU / RMS in the engine); an animation per meter kept 32+ animations running and the window redrawing at full rate
    }
    Rectangle {  // peak
        anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 2
        y: 2 + (parent.height - 4) * (1 - meter.norm(meter.peakDb)) - 1
        height: 2; color: Theme.ov(0.8); visible: meter.peakDb > meter.floorDb
    }
    Rectangle {  // hold
        anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 2
        y: 2 + (parent.height - 4) * (1 - meter.norm(meter.holdDb)) - 1
        height: 1; color: Theme.amber; visible: meter.holdDb > meter.floorDb
    }
    Rectangle { anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter; anchors.topMargin: -8; width: 6; height: 4; radius: 1; color: meter.clip ? Theme.danger : "#2a2f3a" }
}
