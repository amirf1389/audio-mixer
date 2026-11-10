// A 4..8 digit PIN pad: digits, CLR, ENTER; emits entered(pin) and clears itself
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Layouts

Item {
    id: pad
    property string pin: ""
    property string hint: "ENTER PIN"
    signal entered(string pin)
    implicitWidth: 170; implicitHeight: col.implicitHeight
    ColumnLayout {
        id: col; anchors.horizontalCenter: parent.horizontalCenter; spacing: 6
        Rectangle { Layout.fillWidth: true; implicitHeight: 30; radius: 6; color: Theme.deep; border.color: Theme.line
            Text { anchors.centerIn: parent; text: pad.pin.length ? "●".repeat(pad.pin.length) : pad.hint; color: pad.pin.length ? Theme.cyan : Theme.dim; font.family: Theme.mono; font.pixelSize: pad.pin.length ? 14 : 8; font.letterSpacing: 4 } }
        Grid { columns: 3; spacing: 6; Layout.alignment: Qt.AlignHCenter
            Repeater { model: ["1", "2", "3", "4", "5", "6", "7", "8", "9", "CLR", "0", "ENTER"]
                delegate: Rectangle { width: 52; height: 36; radius: 6; color: modelData === "ENTER" ? Theme.amber : modelData === "CLR" ? Qt.rgba(1, 0.16, 0.37, 0.25) : Theme.ov(0.08); border.color: Theme.lineSoft
                    Text { anchors.centerIn: parent; text: modelData; color: modelData === "ENTER" ? "#000" : Theme.ink; font.family: Theme.mono; font.pixelSize: modelData.length > 1 ? 8 : 13; font.bold: true }
                    MouseArea { anchors.fill: parent; onClicked: { if (modelData === "CLR") pad.pin = ""; else if (modelData === "ENTER") { if (pad.pin.length >= 4) { pad.entered(pad.pin); pad.pin = "" } } else if (pad.pin.length < 8) pad.pin += modelData } } } } }
    }
}
