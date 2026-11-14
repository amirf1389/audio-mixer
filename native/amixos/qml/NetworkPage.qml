// NETWORK: the network cards of this computer, the remote address to type into a controller, an allowed-IP list, and the OSC remote-control server (UDP). Off by default; binds this computer only unless LAN is allowed, and says plainly that
// OSC over UDP has no authentication or encryption. The AUDIO + MIDI view (AoipPanel.qml) holds network audio on the open AES67 profile and MIDI.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "networkPage"
    property int view: 0   // 0 = remote control (OSC, phone remote), 1 = network audio and MIDI
    component Head: Text { color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
    component Note: Text { Layout.fillWidth: true; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 24; radius: 4; color: on ? onColor : Theme.ov(0.08); border.color: Theme.lineSoft
        Text { id: bl; anchors.centerIn: parent; text: i18n.t(label, i18n.lang); color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle { Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 8
                Text { text: i18n.t("NETWORK:", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Btn { objectName: "netViewControl"; label: i18n.t("REMOTE CONTROL", i18n.lang); on: page.view === 0; onClicked: page.view = 0 }
                Btn { objectName: "netViewAudio"; label: i18n.t("AUDIO + MIDI", i18n.lang); on: page.view === 1; onColor: Theme.cyan; onClicked: page.view = 1 }
                Rectangle { implicitWidth: devT.implicitWidth + 14; implicitHeight: 18; radius: 3; color: Qt.rgba(1, 0.7, 0.2, 0.18); border.color: Theme.amber
                    Text { id: devT; anchors.centerIn: parent; text: i18n.t("IN DEVELOPMENT IN THIS VERSION", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } }
                Text { text: i18n.t("OSC ", i18n.lang) + osc.status; color: osc.enabled ? Theme.emerald : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                Item { Layout.fillWidth: true }
                Text { text: i18n.t("RX ", i18n.lang) + osc.rx + i18n.t(" · TX ", i18n.lang) + osc.tx + (osc.lastClient ? i18n.t(" · client ", i18n.lang) + osc.lastClient : ""); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Btn { label: osc.enabled ? i18n.t("STOP SERVER", i18n.lang) : i18n.t("START SERVER", i18n.lang); on: !osc.enabled; onColor: Theme.emerald; onClicked: osc.enabled = !osc.enabled } } }
        AoipPanel { visible: page.view === 1; Layout.fillWidth: true; Layout.fillHeight: true }
        RowLayout { visible: page.view === 0; Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            Rectangle { Layout.preferredWidth: 400; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 8
                    Head { text: i18n.t("NETWORK CARDS", i18n.lang) }
                    Note { text: i18n.t("The IPv4 cards of this computer. With REACH = LAN, pick the card the remote is on (or ANY to listen on all of them).", i18n.lang) }
                    Flow { Layout.fillWidth: true; spacing: 4
                        Btn { label: i18n.t("ANY CARD", i18n.lang); on: osc.allowLan && osc.bindAddress === ""; onColor: Theme.cyan; onClicked: osc.bindAddress = "" }
                        Repeater { model: osc.interfaces; delegate: Btn { label: (modelData.kind === "wifi" ? "📶 " : modelData.kind === "ethernet" ? "🖧 " : modelData.kind === "loopback" ? "⟲ " : "") + i18n.t(modelData.name ?? "", i18n.lang) + "  " + modelData.ip; on: osc.bindAddress === modelData.ip; onColor: modelData.up ? Theme.emerald : Theme.amber; onClicked: osc.bindAddress = modelData.ip } } }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.lineSoft }
                    Head { text: i18n.t("OPEN SOUND CONTROL (UDP)", i18n.lang) }
                    RowLayout { spacing: 8; Text { Layout.preferredWidth: 110; text: i18n.t("RECEIVE PORT", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } SpinBox { palette.base: Theme.deep; palette.text: Theme.ink; palette.button: Theme.cardRaised; palette.buttonText: Theme.ink; palette.highlight: Theme.orange; palette.mid: Theme.line; from: 1; to: 65535; value: osc.port; editable: true; onValueModified: osc.port = value; implicitWidth: 120 } }
                    RowLayout { spacing: 8; Text { Layout.preferredWidth: 110; text: i18n.t("METERS TO PORT", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } SpinBox { palette.base: Theme.deep; palette.text: Theme.ink; palette.button: Theme.cardRaised; palette.buttonText: Theme.ink; palette.highlight: Theme.orange; palette.mid: Theme.line; from: 1; to: 65535; value: osc.txPort; editable: true; onValueModified: osc.txPort = value; implicitWidth: 120 } Btn { label: osc.meters ? i18n.t("METERS ON", i18n.lang) : i18n.t("METERS OFF", i18n.lang); on: osc.meters; onColor: Theme.cyan; onClicked: osc.meters = !osc.meters } }
                    RowLayout { spacing: 8; Text { Layout.preferredWidth: 110; text: i18n.t("REACH", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Btn { label: i18n.t("THIS COMPUTER", i18n.lang); on: !osc.allowLan; onColor: Theme.emerald; onClicked: osc.allowLan = false } Btn { label: i18n.t("LAN", i18n.lang); on: osc.allowLan; onColor: Theme.danger; onClicked: osc.allowLan = true } }
                    RowLayout { spacing: 8; Text { Layout.preferredWidth: 110; text: i18n.t("REMOTE ADDRESS", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Text { text: osc.remoteAddress; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 10; font.bold: true } Text { text: i18n.t("← type this into the remote (TouchOSC, Mixing Station, the Android app)", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
                    RowLayout { visible: osc.allowLan; spacing: 8; Text { Layout.preferredWidth: 110; text: i18n.t("ALLOWED IPs", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                        Rectangle { Layout.fillWidth: true; implicitHeight: 22; radius: 4; color: Theme.deep; border.color: Theme.lineSoft
                            TextInput { anchors.fill: parent; anchors.margins: 5; text: osc.allowedIps; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; selectByMouse: true; onEditingFinished: osc.allowedIps = text
                                Text { visible: !parent.text.length; text: i18n.t("empty = local policy: any private-network address, no public one · or list them: 192.168.1.20, 10.0.0.0/24", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 } } } }
                    Text { visible: osc.allowLan; Layout.fillWidth: true; text: i18n.t("LAN: anyone on the network can move the faders. OSC over UDP has no password and no encryption. Use it only on a network you own, and switch back to THIS COMPUTER afterwards.", i18n.lang); color: Theme.danger; font.family: Theme.mono; font.pixelSize: 7; font.bold: true; wrapMode: Text.WordWrap }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.lineSoft }
                    Head { text: i18n.t("ADDRESSES", i18n.lang) }
                    Repeater { model: osc.addresses; delegate: Text { text: modelData; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 7; Layout.fillWidth: true; wrapMode: Text.WordWrap } }
                    Item { Layout.fillHeight: true }
                    Note { text: i18n.t("Works with TouchOSC, Mixing Station (OSC layout), Open Stage Control and anything that sends OSC 1.0. Faders take dB values. The phone remote (the Android app) and the daemon's HTTP API are the authenticated way in; OSC is for controllers on a closed network. Dante, AES67 and PTP clocking are not part of this build.", i18n.lang) } } }
            ColumnLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            Rectangle { Layout.fillWidth: true; implicitHeight: remCol.implicitHeight + 24; radius: Theme.radius; color: Theme.card; border.color: remote.running ? Theme.emerald : Theme.line
                property string shown: ""
                id: remCard
                Timer { id: hideTok; interval: 600000; onTriggered: remCard.shown = "" }
                Component.onCompleted: remote.refreshTokens()
                ColumnLayout { id: remCol; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12; spacing: 6
                    RowLayout { spacing: 8
                        Head { text: i18n.t("PHONE REMOTE · WI-FI", i18n.lang) }
                        Text { text: remote.running ? i18n.t("ON", i18n.lang) : i18n.t("OFF", i18n.lang); color: remote.running ? Theme.emerald : Theme.dim; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                        Item { Layout.fillWidth: true }
                        Btn { visible: !remote.running; label: i18n.t("START FOR PHONES ON THIS NETWORK", i18n.lang); on: remote.available; onColor: Theme.emerald; onClicked: remote.start(true) }
                        Btn { visible: !remote.running; label: i18n.t("THIS COMPUTER ONLY", i18n.lang); on: remote.available; onClicked: remote.start(false) }
                        Btn { visible: remote.running; label: i18n.t("STOP", i18n.lang); on: true; onColor: Theme.danger; onClicked: remote.stop() }
                        Btn { label: remCard.shown ? i18n.t("HIDE TOKEN", i18n.lang) : i18n.t("SHOW TOKEN (10 min)", i18n.lang); onClicked: { if (remCard.shown) remCard.shown = ""; else { remCard.shown = remote.token() || i18n.t("no token yet: start the service once", i18n.lang); hideTok.restart() } } } }
                    Text { Layout.fillWidth: true; text: remote.available ? remote.status : i18n.t("The remote service program (amixos-daemon) is not installed next to the console.", i18n.lang); color: remote.running ? Theme.emerald : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; wrapMode: Text.WordWrap }
                    Repeater { model: remote.running ? remote.addresses : []; delegate: Text { text: modelData + "   " + i18n.t("← type this into the phone app", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } }
                    Repeater { model: remote.running ? remote.addresses : []; delegate: Text { text: modelData + "/remote   " + i18n.t("← or open this in any browser (phone, tablet, laptop) and type the PIN", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } }
                    Text { visible: remCard.shown !== ""; text: i18n.t("TOKEN  ", i18n.lang) + remCard.shown; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                    RowLayout { id: pinRow; objectName: "pairPinRow"; visible: remote.running; spacing: 10; property string pin: ""; property int secs: 0
                        Timer { interval: 1000; running: pinRow.visible; repeat: true; triggeredOnStart: true; onTriggered: { pinRow.pin = remote.pairPin(); pinRow.secs = remote.pairSecondsLeft() } }
                        Head { text: i18n.t("PAIRING PIN", i18n.lang) }
                        Text { objectName: "pairPin"; text: pinRow.pin.substring(0, 3) + " " + pinRow.pin.substring(3); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 22; font.bold: true; font.letterSpacing: 3 }
                        Text { text: i18n.t("changes in ", i18n.lang) + pinRow.secs + " s"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 }
                        Note { text: i18n.t("Type this PIN into the phone once. It gets its own token for 30 days, tied to that phone's address. A new PIN every minute; one phone per PIN; five wrong tries close pairing until the next PIN.", i18n.lang) } }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.lineSoft }
                    RowLayout { spacing: 8
                        Head { text: i18n.t("AMF X1 NETWORK · TOKENS", i18n.lang) }
                        Note { text: i18n.t("One token per phone or operator. CONTROL may change the console, VIEW only watches the meters. Revoke one and the others keep working.", i18n.lang) } }
                    Repeater { model: remote.tokens
                        delegate: RowLayout { Layout.fillWidth: true; spacing: 8
                            Rectangle { width: 6; height: 6; radius: 3; color: modelData.state === "active" ? Theme.emerald : Theme.ov(0.25) }
                            Text { Layout.fillWidth: true; text: i18n.t(modelData.name ?? "", i18n.lang); color: modelData.state === "active" ? Theme.ink : Theme.dim; font.family: Theme.mono; font.pixelSize: 9; elide: Text.ElideRight }
                            Text { text: modelData.role.toUpperCase() + " · " + modelData.state.toUpperCase() + " · " + modelData.id; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                            Btn { visible: modelData.state === "active"; label: i18n.t("REVOKE", i18n.lang); on: true; onColor: Theme.danger; onClicked: remote.revokeToken(modelData.id) } } }
                    Text { visible: remote.tokens.length === 0; text: i18n.t("No named tokens yet. The master token (SHOW TOKEN) still works.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                    RowLayout { id: tokNew; spacing: 6; property string role: "control"; property int days: 0; property string fresh: ""
                        Timer { id: hideNew; interval: 600000; onTriggered: tokNew.fresh = "" }
                        Field { id: tokName; objectName: "tokName"; Layout.preferredWidth: 150; implicitHeight: 24; placeholderText: i18n.t("name (whose phone)", i18n.lang); font.family: Theme.mono; font.pixelSize: 8 }
                        Btn { label: i18n.t("CONTROL", i18n.lang); on: tokNew.role === "control"; onColor: Theme.cyan; onClicked: tokNew.role = "control" }
                        Btn { label: i18n.t("VIEW", i18n.lang); on: tokNew.role === "view"; onColor: Theme.cyan; onClicked: tokNew.role = "view" }
                        Repeater { model: [[0, "NO END"], [1, "1 DAY"], [30, "30 DAYS"]]; delegate: Btn { label: i18n.t(modelData[1], i18n.lang); on: tokNew.days === modelData[0]; onClicked: tokNew.days = modelData[0] } }
                        Btn { objectName: "tokMake"; label: i18n.t("MAKE TOKEN", i18n.lang); on: remote.available; onColor: Theme.emerald; onClicked: { tokNew.fresh = remote.newToken(tokName.text, tokNew.role, tokNew.days); if (tokNew.fresh !== "") { tokName.text = ""; hideNew.restart() } } } }
                    Text { visible: tokNew.fresh !== ""; Layout.fillWidth: true; text: tokNew.fresh; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; wrapMode: Text.WrapAnywhere }
                    Text { visible: remote.tokenNote !== ""; Layout.fillWidth: true; text: i18n.t(remote.tokenNote, i18n.lang) + (remote.running ? " " + i18n.t("The service was restarted to take the change.", i18n.lang) : ""); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; wrapMode: Text.WordWrap }
                    Note { text: i18n.t("LOCAL POLICY: the service answers this computer and private-network addresses only (10.x, 172.16-31.x, 192.168.x); every control call needs the token. Nothing is encrypted on the wire: use it on your own Wi-Fi only; beyond that put the nginx TLS front (deploy/nginx) before it. While the service runs it holds the sound card with its own engine: the console stops its engine and takes the card back when the service stops.", i18n.lang) } } }
            Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; clip: true
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 6
                    Head { text: i18n.t("TRAFFIC", i18n.lang) }
                    ListView { id: lv; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: osc.log; onCountChanged: positionViewAtEnd()
                        delegate: Text { text: modelData; color: modelData.indexOf("unknown") >= 0 ? Theme.amber : Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } }
                    Text { visible: osc.log.length === 0; text: i18n.t("No OSC traffic yet. START SERVER, point a controller at this computer's address and the RECEIVE PORT.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 } } } } }
    }
}
