// NETWORK: the network cards of this computer, the remote address to type into a controller, an allowed-IP list, and the OSC remote-control server (UDP). Off by default; binds this computer only unless LAN is allowed, and says plainly that
// OSC over UDP has no authentication or encryption. Dante / AES67 / PTP from the browser mock-up are not part of this build.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "networkPage"
    component Head: Text { color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
    component Note: Text { Layout.fillWidth: true; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 24; radius: 4; color: on ? onColor : Qt.rgba(1, 1, 1, 0.08); border.color: Theme.lineSoft
        Text { id: bl; anchors.centerIn: parent; text: label; color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle { Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 8
                Text { text: "NETWORK:"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Text { text: "OSC " + osc.status; color: osc.enabled ? Theme.emerald : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                Item { Layout.fillWidth: true }
                Text { text: "RX " + osc.rx + " · TX " + osc.tx + (osc.lastClient ? " · client " + osc.lastClient : ""); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Btn { label: osc.enabled ? "STOP SERVER" : "START SERVER"; on: !osc.enabled; onColor: Theme.emerald; onClicked: osc.enabled = !osc.enabled } } }
        RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            Rectangle { Layout.preferredWidth: 400; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 8
                    Head { text: "NETWORK CARDS" }
                    Note { text: "The IPv4 cards of this computer. With REACH = LAN, pick the card the remote is on (or ANY to listen on all of them)." }
                    Flow { Layout.fillWidth: true; spacing: 4
                        Btn { label: "ANY CARD"; on: osc.allowLan && osc.bindAddress === ""; onColor: Theme.cyan; onClicked: osc.bindAddress = "" }
                        Repeater { model: osc.interfaces; delegate: Btn { label: (modelData.kind === "wifi" ? "📶 " : modelData.kind === "ethernet" ? "🖧 " : modelData.kind === "loopback" ? "⟲ " : "") + modelData.name + "  " + modelData.ip; on: osc.bindAddress === modelData.ip; onColor: modelData.up ? Theme.emerald : Theme.amber; onClicked: osc.bindAddress = modelData.ip } } }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.lineSoft }
                    Head { text: "OPEN SOUND CONTROL (UDP)" }
                    RowLayout { spacing: 8; Text { Layout.preferredWidth: 110; text: "RECEIVE PORT"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } SpinBox { from: 1; to: 65535; value: osc.port; editable: true; onValueModified: osc.port = value; implicitWidth: 120 } }
                    RowLayout { spacing: 8; Text { Layout.preferredWidth: 110; text: "METERS TO PORT"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } SpinBox { from: 1; to: 65535; value: osc.txPort; editable: true; onValueModified: osc.txPort = value; implicitWidth: 120 } Btn { label: osc.meters ? "METERS ON" : "METERS OFF"; on: osc.meters; onColor: Theme.cyan; onClicked: osc.meters = !osc.meters } }
                    RowLayout { spacing: 8; Text { Layout.preferredWidth: 110; text: "REACH"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Btn { label: "THIS COMPUTER"; on: !osc.allowLan; onColor: Theme.emerald; onClicked: osc.allowLan = false } Btn { label: "LAN"; on: osc.allowLan; onColor: Theme.danger; onClicked: osc.allowLan = true } }
                    RowLayout { spacing: 8; Text { Layout.preferredWidth: 110; text: "REMOTE ADDRESS"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Text { text: osc.remoteAddress; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 10; font.bold: true } Text { text: "← type this into the remote (TouchOSC, Mixing Station, the Android app)"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
                    RowLayout { visible: osc.allowLan; spacing: 8; Text { Layout.preferredWidth: 110; text: "ALLOWED IPs"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                        Rectangle { Layout.fillWidth: true; implicitHeight: 22; radius: 4; color: "#05070a"; border.color: Theme.lineSoft
                            TextInput { anchors.fill: parent; anchors.margins: 5; text: osc.allowedIps; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; selectByMouse: true; onEditingFinished: osc.allowedIps = text
                                Text { visible: !parent.text.length; text: "empty = any address on the LAN · e.g. 192.168.1.20, 10.0.0.0/24"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 } } } }
                    Text { visible: osc.allowLan; Layout.fillWidth: true; text: "LAN: anyone on the network can move the faders. OSC over UDP has no password and no encryption. Use it only on a network you own, and switch back to THIS COMPUTER afterwards."; color: Theme.danger; font.family: Theme.mono; font.pixelSize: 7; font.bold: true; wrapMode: Text.WordWrap }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.lineSoft }
                    Head { text: "ADDRESSES" }
                    Repeater { model: osc.addresses; delegate: Text { text: modelData; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 7; Layout.fillWidth: true; wrapMode: Text.WordWrap } }
                    Item { Layout.fillHeight: true }
                    Note { text: "Works with TouchOSC, Mixing Station (OSC layout), Open Stage Control and anything that sends OSC 1.0. Faders take dB values. The phone remote (the Android app) and the daemon's HTTP API are the authenticated way in; OSC is for controllers on a closed network. Dante, AES67 and PTP clocking are not part of this build." } } }
            Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; clip: true
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 6
                    Head { text: "TRAFFIC" }
                    ListView { id: lv; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: osc.log; onCountChanged: positionViewAtEnd()
                        delegate: Text { text: modelData; color: modelData.indexOf("unknown") >= 0 ? Theme.amber : Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } }
                    Text { visible: osc.log.length === 0; text: "No OSC traffic yet. START SERVER, point a controller at this computer's address and the RECEIVE PORT."; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 } } } }
    }
}
