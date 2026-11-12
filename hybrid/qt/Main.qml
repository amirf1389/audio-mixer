import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    width: 1760; height: 500; visible: true
    title: "AmF X1 Hybrid Mixer"; color: "#14161a"
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 12; spacing: 8
        Text { color: bridge.connected ? "#4cc38a" : "#e5534b"; font.pixelSize: 12
               text: bridge.connected ? "Engine running · " + bridge.sampleRate + " Hz · source underruns " + bridge.underruns
                                      : "The mixer engine is not running" }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 6
            Repeater { model: bridge.stripCount + 1
                       MixerStrip { required property int index; mixer: bridge; stripIndex: index; Layout.fillHeight: true } }
        }
    }
}
