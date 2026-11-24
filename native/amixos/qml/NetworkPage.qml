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
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 24; radius: 8; gradient: Gradient { GradientStop { position: 0; color: on ? Theme.btnTop(onColor) : Theme.ov(0.11) } GradientStop { position: 1; color: on ? Theme.btnBottom(onColor) : Theme.ov(0.08) } } color: on ? onColor : Theme.ov(0.08); border.color: Theme.lineSoft
        Rectangle { anchors.fill: parent; anchors.margins: 1; radius: Math.max(0, parent.radius - 1); color: "transparent"; border.width: 1; border.color: Qt.rgba(1, 1, 1, parent.on ? 0.30 : 0.07) }
        Text { id: bl; anchors.centerIn: parent; text: i18n.t(label, i18n.lang); color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    // a QR code drawn with whole device pixels per module, a 4-module quiet zone on white, square and centred in whatever space it gets.
    // Drawn on a Canvas; when the Canvas does not draw it (not available, or no paint within 800 ms: seen with some Windows Direct3D 11
    // drivers) the same modules are shown as an Image (img: Remote.qrImage, a PNG), so the code is never a white square.
    component QrView: Rectangle { id: qv; property var mods: []; property string img: ""; property bool forceImage: false; property bool painted: false; property bool late: false
        readonly property bool useImage: img !== "" && (forceImage || (late && !painted))
        readonly property bool drawn: mods && mods.length > 1 && (useImage ? qi.status === Image.Ready : painted)   // the code is on screen
        radius: 8; color: "#ffffff"; onModsChanged: { painted = false; late = false; lateTimer.restart(); qc.requestPaint() }
        Timer { id: lateTimer; interval: 800; onTriggered: qv.late = true }
        Canvas { id: qc; visible: !qv.useImage; readonly property real dpr: Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1
            readonly property int n: (qv.mods && qv.mods.length > 1) ? qv.mods[0] : 21
            readonly property int px: Math.max(1, Math.floor(Math.min(qv.width, qv.height) * dpr / (n + 8)))   // device pixels per module
            width: px * (n + 8) / dpr; height: width; anchors.centerIn: parent
            canvasSize: Qt.size(px * (n + 8), px * (n + 8)); renderTarget: Canvas.Image; smooth: false
            onAvailableChanged: if (available) requestPaint()   // a paint asked for before the Canvas is ready is dropped
            onPxChanged: requestPaint(); onNChanged: requestPaint()
            onPaint: { const ctx = getContext("2d"); if (!ctx) return; ctx.reset(); ctx.fillStyle = "#ffffff"; ctx.fillRect(0, 0, canvasSize.width, canvasSize.height)
                const m = qv.mods; if (!m || m.length < 2) return; const k = m[0]; ctx.fillStyle = "#000000"
                for (let y = 0; y < k; ++y) for (let x = 0; x < k; ++x) if (m[1 + y * k + x]) ctx.fillRect((x + 4) * px, (y + 4) * px, px, px)
                qv.painted = true } }
        Image { id: qi; objectName: "qrImage"; visible: qv.useImage; source: qv.useImage ? qv.img : ""; anchors.centerIn: parent; width: Math.min(qv.width, qv.height); height: width
            fillMode: Image.PreserveAspectFit; smooth: false; mipmap: false; cache: false } }
    Popup { id: qrBig; objectName: "pairQrBig"; parent: Overlay.overlay; x: 0; y: 0; width: parent ? parent.width : 0; height: parent ? parent.height : 0; modal: true; padding: 0
        background: Rectangle { color: "#ffffff" }
        // made only while the popup is open: a window-sized Canvas repainted on every PIN change would slow the page down for nothing
        Loader { anchors.fill: parent; active: qrBig.visible; sourceComponent: Item { QrView { anchors.centerIn: parent; width: Math.min(parent.width, parent.height) - 40; height: width; radius: 0; mods: qrRow.mods; img: qrRow.img; forceImage: qrSmall.useImage } } }
        Text { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 4; text: i18n.t("Click or press Esc to close", i18n.lang); color: "#444"; font.family: Theme.mono; font.pixelSize: 10 }
        MouseArea { anchors.fill: parent; onClicked: qrBig.close() } }
    // A SHORT WINDOW (or a large interface zoom): the page scrolls instead of running past the bottom of the page area
    Flickable { id: pageScroll; objectName: "pageScroll"; anchors.fill: parent; contentWidth: width; contentHeight: Math.max(height, pageCol.implicitHeight + 20); clip: true
        boundsBehavior: Flickable.StopAtBounds; interactive: contentHeight > height + 1; ScrollIndicator.vertical: ScrollIndicator { }
    ColumnLayout {
        id: pageCol; x: 10; y: 10; width: pageScroll.width - 20; height: pageScroll.contentHeight - 20
        spacing: 8
        Rectangle { Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 8
                Text { text: i18n.t("NETWORK:", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Btn { objectName: "netViewControl"; label: i18n.t("REMOTE CONTROL", i18n.lang); on: page.view === 0; onClicked: page.view = 0 }
                Btn { objectName: "netViewAudio"; label: i18n.t("AUDIO + MIDI", i18n.lang); on: page.view === 1; onColor: Theme.cyan; onClicked: page.view = 1 }
                Btn { objectName: "netViewDante"; label: i18n.t("DANTE + CLOCK", i18n.lang); on: page.view === 2; onColor: "#ef4444"; onClicked: page.view = 2 }
                Rectangle { id: gb116; implicitWidth: devT.implicitWidth + 14; implicitHeight: 18; radius: 7; color: Qt.rgba(1, 0.7, 0.2, 0.18); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb116.color) } GradientStop { position: 1; color: Theme.btnBottom(gb116.color) } } border.color: Theme.amber
                    Text { id: devT; anchors.centerIn: parent; text: i18n.t("IN DEVELOPMENT IN THIS VERSION", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } }
                Text { text: i18n.t("OSC ", i18n.lang) + osc.status; color: osc.enabled ? Theme.emerald : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                Item { Layout.fillWidth: true }
                Text { text: i18n.t("RX ", i18n.lang) + osc.rx + i18n.t(" · TX ", i18n.lang) + osc.tx + (osc.lastClient ? i18n.t(" · client ", i18n.lang) + osc.lastClient : ""); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Btn { label: osc.enabled ? i18n.t("STOP SERVER", i18n.lang) : i18n.t("START SERVER", i18n.lang); on: !osc.enabled; onColor: Theme.emerald; onClicked: osc.enabled = !osc.enabled } } }
        AoipPanel { visible: page.view === 1; Layout.fillWidth: true; Layout.fillHeight: true }
        DantePanel { visible: page.view === 2; Layout.fillWidth: true; Layout.fillHeight: true; onOpenAudio: page.view = 1 }
        RowLayout { visible: page.view === 0; Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            Rectangle { Layout.preferredWidth: 400; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 8
                    Head { text: i18n.t("NETWORK CARDS", i18n.lang) }
                    Note { text: i18n.t("The IPv4 cards of this computer. With REACH = LAN, pick the card the remote is on (or ANY to listen on all of them).", i18n.lang) }
                    Flow { Layout.fillWidth: true; spacing: 4
                        Btn { label: i18n.t("ANY CARD", i18n.lang); on: osc.allowLan && osc.bindAddress === ""; onColor: Theme.cyan; onClicked: osc.bindAddress = "" }
                        Repeater { model: osc.interfaces; delegate: Btn { label: (modelData.kind === "wifi" ? "≋ " : modelData.kind === "ethernet" ? "⇄ " : modelData.kind === "loopback" ? "⟲ " : "") + i18n.t(modelData.name ?? "", i18n.lang) + "  " + modelData.ip; on: osc.bindAddress === modelData.ip; onColor: modelData.up ? Theme.emerald : Theme.amber; onClicked: osc.bindAddress = modelData.ip } } }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.lineSoft }
                    Head { text: i18n.t("OPEN SOUND CONTROL (UDP)", i18n.lang) }
                    RowLayout { spacing: 8; Text { Layout.preferredWidth: 110; text: i18n.t("RECEIVE PORT", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } SpinBox { palette.base: Theme.deep; palette.text: Theme.ink; palette.button: Theme.cardRaised; palette.buttonText: Theme.ink; palette.highlight: Theme.orange; palette.mid: Theme.line; from: 1; to: 65535; value: osc.port; editable: true; textFromValue: function (v) { return String(v) } valueFromText: function (t) { const n = parseInt(String(t).replace(/[^0-9]/g, "")); return isNaN(n) ? value : n } validator: IntValidator { bottom: 1; top: 65535 } onValueModified: osc.port = value; implicitWidth: 120 } }
                    RowLayout { spacing: 8; Text { Layout.preferredWidth: 110; text: i18n.t("METERS TO PORT", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } SpinBox { palette.base: Theme.deep; palette.text: Theme.ink; palette.button: Theme.cardRaised; palette.buttonText: Theme.ink; palette.highlight: Theme.orange; palette.mid: Theme.line; from: 1; to: 65535; value: osc.txPort; editable: true; textFromValue: function (v) { return String(v) } valueFromText: function (t) { const n = parseInt(String(t).replace(/[^0-9]/g, "")); return isNaN(n) ? value : n } validator: IntValidator { bottom: 1; top: 65535 } onValueModified: osc.txPort = value; implicitWidth: 120 } Btn { label: osc.meters ? i18n.t("METERS ON", i18n.lang) : i18n.t("METERS OFF", i18n.lang); on: osc.meters; onColor: Theme.cyan; onClicked: osc.meters = !osc.meters } }
                    RowLayout { spacing: 8; Text { Layout.preferredWidth: 110; text: i18n.t("REACH", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Btn { label: i18n.t("THIS COMPUTER", i18n.lang); on: !osc.allowLan; onColor: Theme.emerald; onClicked: osc.allowLan = false } Btn { label: i18n.t("LAN", i18n.lang); on: osc.allowLan; onColor: Theme.danger; onClicked: osc.allowLan = true } }
                    RowLayout { Layout.fillWidth: true; spacing: 8; Text { Layout.preferredWidth: 110; text: i18n.t("REMOTE ADDRESS", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Text { text: osc.remoteAddress; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 10; font.bold: true } Text { Layout.fillWidth: true; Layout.minimumWidth: 0; wrapMode: Text.WordWrap; text: i18n.t("← type this into the remote (TouchOSC, Mixing Station, the Android app)", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }   // wraps inside the card instead of running under the next one
                    RowLayout { visible: osc.allowLan; spacing: 8; Text { Layout.preferredWidth: 110; text: i18n.t("ALLOWED IPs", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                        Rectangle { id: gb117; Layout.fillWidth: true; implicitHeight: 22; radius: 7; color: Theme.deep; gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb117.color) } GradientStop { position: 1; color: Theme.btnBottom(gb117.color) } } border.color: Theme.lineSoft
                            TextInput { anchors.fill: parent; anchors.margins: 5; text: osc.allowedIps; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; selectByMouse: true; onEditingFinished: osc.allowedIps = text
                                Text { visible: !parent.text.length; text: i18n.t("empty = local policy: any private-network address, no public one · or list them: 192.168.1.20, 10.0.0.0/24", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 } } } }
                    Text { visible: osc.allowLan; Layout.fillWidth: true; text: i18n.t("LAN: anyone on the network can move the faders. OSC over UDP has no password and no encryption. Use it only on a network you own, and switch back to THIS COMPUTER afterwards.", i18n.lang); color: Theme.danger; font.family: Theme.mono; font.pixelSize: 7; font.bold: true; wrapMode: Text.WordWrap }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.lineSoft }
                    Head { text: i18n.t("ADDRESSES", i18n.lang) }
                    Repeater { model: osc.addresses; delegate: Text { text: modelData; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 7; Layout.fillWidth: true; wrapMode: Text.WordWrap } }
                    Item { Layout.fillHeight: true }
                    Note { text: i18n.t("Works with TouchOSC, Mixing Station (OSC layout), Open Stage Control and anything that sends OSC 1.0. Faders take dB values. The phone remote (the Android app) and the daemon's HTTP API are the authenticated way in; OSC is for controllers on a closed network. Network audio (AES67), Dante (through its sound card or AES67 mode) and the PTP clock are under AUDIO + MIDI and DANTE + CLOCK.", i18n.lang) } } }
            ColumnLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            Rectangle { Layout.fillWidth: true; implicitHeight: remCol.implicitHeight + 24; radius: Theme.radius; color: Theme.card; border.color: remote.running ? Theme.emerald : Theme.line
                property string shown: ""
                id: remCard
                Timer { id: hideTok; interval: 600000; onTriggered: remCard.shown = "" }
                Timer { interval: 30; running: true; onTriggered: remote.refreshTokens() }   // after the page's first frame: listing the phones may ask the service tool (a process) and must not hold the page back
                ColumnLayout { id: remCol; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12; spacing: 6
                    ScrollRow { Layout.fillWidth: true; spacing: 8
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
                    Note { visible: remote.running && remote.otherAddresses.length > 0; text: i18n.t("Other addresses (no router, VPN, mobile or public network, IPv6; a phone on the Wi-Fi does not reach these):", i18n.lang) + " " + remote.otherAddresses.join("  ") }
                    Text { objectName: "feedbackNote"; visible: remote.feedbackNote !== ""; Layout.fillWidth: true; text: remote.feedbackNote; color: Theme.danger; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; wrapMode: Text.WordWrap }
                    Text { visible: remCard.shown !== ""; text: i18n.t("TOKEN  ", i18n.lang) + remCard.shown; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                    RowLayout { id: pinRow; objectName: "pairPinRow"; visible: remote.running; spacing: 10; property string pin: ""; property int secs: 0
                        Timer { interval: 1000; running: pinRow.visible; repeat: true; triggeredOnStart: true; onTriggered: { pinRow.pin = remote.pairPin(); pinRow.secs = remote.pairSecondsLeft() } }
                        Head { text: i18n.t("PAIRING PIN", i18n.lang) }
                        Text { objectName: "pairPin"; text: pinRow.pin.substring(0, 3) + " " + pinRow.pin.substring(3); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 22; font.bold: true; font.letterSpacing: 3 }
                        Text { text: i18n.t("changes in ", i18n.lang) + pinRow.secs + " s"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 }
                        Note { text: i18n.t("Type this PIN into the phone once. It gets its own token for 30 days, tied to that phone's address. A new PIN every minute; one phone per PIN; five wrong tries close pairing until the next PIN.", i18n.lang) } }
                    // PAIRING QR CODE: scan it with the phone camera (opens the browser remote, which pairs by itself) or with SCAN QR in the
                    // Android app. It carries this computer's address and the minute's PIN, so it is redrawn when the PIN changes.
                    // Never a blank area: the code when the service runs and a phone can reach an address; otherwise the reason, the address
                    // to type and (with the service off) START. Without a PIN yet (no token) the code opens the remote, which asks for the PIN.
                    RowLayout { id: qrRow; objectName: "pairQrRow"; visible: true; spacing: 14; Layout.fillWidth: true
                        property string demoAddress: ""   // screenshots on a machine with no LAN address: an example address to draw the code for
                        onDemoAddressChanged: redraw()
                        // tests: the states the code must explain (undefined: as they really are)
                        property var simRunning: undefined; property var simAddresses: undefined; property var simPin: undefined
                        onSimRunningChanged: redraw(); onSimAddressesChanged: redraw(); onSimPinChanged: redraw()
                        readonly property bool up: simRunning !== undefined ? simRunning : remote.running
                        property int pick: 0; property string url: ""; property string qrText: ""; property var mods: []; property string img: ""; property string reason: ""; property var addrs: []
                        readonly property bool codeShown: qrText !== "" && qrSmall.drawn
                        readonly property bool reasonShown: reason !== "" && pairReason.visible
                        function redraw() {
                            const a = simAddresses !== undefined ? simAddresses : (remote.addresses.length ? remote.addresses : (demoAddress ? [demoAddress] : [])); qrRow.addrs = a
                            if (qrRow.pick >= a.length) qrRow.pick = 0
                            const base = a.length ? a[qrRow.pick] : "", pin = simPin !== undefined ? simPin : remote.pairPin()
                            qrRow.url = base ? base + "/remote" : ""
                            qrRow.qrText = up && base ? (pin ? base + "/remote#pin=" + pin : base + "/remote") : ""
                            qrRow.mods = qrRow.qrText ? remote.qr(qrRow.qrText) : []; qrRow.img = qrRow.qrText ? remote.qrImage(qrRow.qrText) : ""
                            qrRow.reason = !remote.available ? i18n.t("No QR code: the remote service program (amixos-daemon) is not installed next to the console.", i18n.lang)
                                : !up ? (remote.status !== "" && remote.status !== "off" ? i18n.t("No QR code: the remote service is not running. It said:", i18n.lang) + " " + remote.status : i18n.t("No QR code yet: the remote service is off. Press START to start it for the phones on this network.", i18n.lang))
                                : !base ? i18n.t("No QR code: this computer has no Wi-Fi or network address that a phone can reach. Connect it to the same Wi-Fi as the phone; the code appears by itself.", i18n.lang)
                                : !pin ? i18n.t("No pairing PIN yet: the service has not written its token. The code opens the remote; type the PIN there once it shows here.", i18n.lang) : ""
                            if (qrRow.mods.length < 2 && qrRow.qrText !== "") qrRow.reason = i18n.t("The QR code could not be made for this address: type it into the phone.", i18n.lang) }
                        Connections { target: pinRow; function onPinChanged() { qrRow.redraw() } }
                        Connections { target: remote; function onChanged() { qrRow.redraw() } }   // the service started or stopped, a network came or went: the code follows
                        Timer { interval: 2000; running: qrRow.visible && !qrRow.up; repeat: true; onTriggered: qrRow.redraw() }   // the status while it is off
                        onVisibleChanged: if (visible) redraw()
                        Component.onCompleted: redraw()
                        // the code is always shown whole: square, centred, a 4-module quiet zone, whole device pixels per module (crisp at any DPI);
                        // it grows with the row (168 .. 300 px) and never shrinks below what a camera reads. ENLARGE shows it over the whole window.
                        ColumnLayout { spacing: 4; Layout.alignment: Qt.AlignTop; visible: qrRow.qrText !== ""
                            // nothing may sit on the code: a button over its corner stopped phone cameras reading it
                            QrView { id: qrSmall; objectName: "pairQr"; mods: qrRow.mods; img: qrRow.img
                                 readonly property real side: Math.max(168, Math.min(300, qrRow.width * 0.38)); Layout.preferredWidth: side; Layout.preferredHeight: side; Layout.minimumWidth: 168; Layout.minimumHeight: 168 }
                            Btn { objectName: "pairQrEnlarge"; label: "ENLARGE"; Layout.alignment: Qt.AlignHCenter; implicitHeight: 18; onClicked: qrBig.open() } }
                        Rectangle { objectName: "pairNoCode"; visible: qrRow.qrText === ""; Layout.alignment: Qt.AlignTop; Layout.preferredWidth: 168; Layout.preferredHeight: 168; radius: 8; color: Theme.ov(0.05); border.color: Theme.amber
                            ColumnLayout { anchors.centerIn: parent; width: parent.width - 20; spacing: 8
                                Text { Layout.alignment: Qt.AlignHCenter; text: i18n.t("NO CODE", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 11; font.bold: true }
                                Btn { objectName: "pairStart"; visible: !qrRow.up && remote.available; Layout.alignment: Qt.AlignHCenter; label: i18n.t("START", i18n.lang); on: true; onColor: Theme.emerald; onClicked: remote.start(true) } } }
                        ColumnLayout { Layout.fillWidth: true; spacing: 5; Layout.alignment: Qt.AlignTop
                            Head { text: i18n.t("SCAN TO CONNECT", i18n.lang) }
                            Text { id: pairReason; objectName: "pairNoAddress"; visible: qrRow.reason !== ""; text: qrRow.reason; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                            Note { visible: qrRow.qrText !== ""; text: i18n.t("Phone camera: opens the browser remote on this computer and pairs by itself. AmF X1 app: REMOTE → SCAN QR. The code holds this computer's address and the PIN of this minute, so it changes with the PIN.", i18n.lang) }
                            Text { objectName: "pairQrUrl"; text: qrRow.url !== "" ? qrRow.url : (qrRow.up ? "http://127.0.0.1:" + remote.port + "/remote   " + i18n.t("(this computer only)", i18n.lang) : ""); visible: text !== ""; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                            Flow { Layout.fillWidth: true; spacing: 4; visible: qrRow.addrs.length > 1
                                Repeater { model: qrRow.addrs; delegate: Btn { label: modelData.replace("http://", "").replace(/:\d+$/, "") + (remote.adapterName(modelData) ? "  " + remote.adapterName(modelData) : ""); on: qrRow.pick === index; onColor: Theme.cyan; onClicked: { qrRow.pick = index; qrRow.redraw() } } } }
                            Note { visible: qrRow.addrs.length > 1; text: i18n.t("More than one network: pick the address of the Wi-Fi the phone is on.", i18n.lang) } } }
                    Note { objectName: "pairHelp"; visible: remote.running && remote.addresses.length > 0
                           text: i18n.t("If the phone cannot connect after scanning: the phone and this computer must be on the same Wi-Fi (not mobile data, not a guest network); pick the right address above when there is more than one; and Windows Firewall must allow AmF X1 on private networks (the setup program adds this rule, the zip version does not).", i18n.lang) }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.lineSoft }
                    // CONNECTED PHONES: who is linked right now (live streams), with DISCONNECT (now, 2 minutes' hold) and FORGET (the PIN again)
                    RowLayout { spacing: 8
                        Head { text: i18n.t("CONNECTED PHONES", i18n.lang) + "  " + remote.links.length }
                        Note { text: i18n.t("Phones and browsers linked right now. DISCONNECT drops one (it may come back in 2 minutes); FORGET removes its pairing, so it needs the PIN again.", i18n.lang) } }
                    Repeater { model: remote.links; objectName: "linksList"
                        delegate: RowLayout { Layout.fillWidth: true; spacing: 8
                            Rectangle { width: 7; height: 7; radius: 3.5; color: Theme.emerald }
                            Text { Layout.preferredWidth: 170; text: modelData.name; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; elide: Text.ElideRight }
                            Text { Layout.fillWidth: true; text: modelData.ip + " · " + (modelData.role === "admin" ? i18n.t("OWNER", i18n.lang) : modelData.role.toUpperCase()) + " · " + i18n.t("linked", i18n.lang) + " " + modelData.age; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideRight }
                            Btn { label: i18n.t("DISCONNECT", i18n.lang); on: true; onColor: Theme.amber; onClicked: remote.disconnectLink(modelData.id) }
                            Btn { visible: modelData.phone; label: i18n.t("FORGET", i18n.lang); on: true; onColor: Theme.danger; onClicked: remote.forgetLink(modelData.tokenId) } } }
                    Text { visible: remote.links.length === 0; text: remote.running ? i18n.t("No phone linked yet. Open AmF X1 on the phone: it finds this computer on the Wi-Fi by itself.", i18n.lang) : i18n.t("Start the phone remote to see the phones linked to it.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                    Text { visible: remote.linkNote !== ""; text: i18n.t(remote.linkNote, i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8; wrapMode: Text.WordWrap; Layout.fillWidth: true }
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
    } }
}
