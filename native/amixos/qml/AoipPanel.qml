// NETWORK AUDIO AND MIDI: an AES67 stream out (the master), an AES67 stream in (as mixer inputs), the streams announced on the network,
// network MIDI and the MIDI inputs of this computer. Everything is off until switched on; the audio is not encrypted, and the page says so.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: panel
    objectName: "aoipPanel"
    component Head: Text { color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
    component Note: Text { Layout.fillWidth: true; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }
    component Lab: Text { Layout.preferredWidth: 92; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
    component Val: Text { color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8 }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(34, bl.implicitWidth + 14); implicitHeight: 22; radius: 8; gradient: Gradient { GradientStop { position: 0; color: on ? Theme.btnTop(onColor) : Theme.ov(0.12) } GradientStop { position: 1; color: on ? Theme.btnBottom(onColor) : Theme.ov(0.035) } } color: on ? onColor : Theme.ov(0.07); border.color: on ? onColor : Theme.line
        Rectangle { anchors.fill: parent; anchors.margins: 1; radius: Math.max(0, parent.radius - 1); color: "transparent"; border.width: 1; border.color: Qt.rgba(1, 1, 1, parent.on ? 0.30 : 0.07) }
        Text { id: bl; anchors.centerIn: parent; text: i18n.t(parent.label, i18n.lang); color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    component Card: Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; clip: true }
    readonly property var st: net.stats
    function cascade(sendTo, receiveFrom) {
        net.stopSend(); net.stopReceive()
        net.txAddress = sendTo; net.txPort = 5004; net.txChannels = 2; net.txBits = 24
        net.rxAddress = receiveFrom; net.rxPort = 5004; net.rxChannels = 2; net.rxBits = 24; net.rxRate = 48000; net.rxLatency = 20; net.rxFirstStrip = 31
        net.startReceive(); net.startSend()
    }

    ColumnLayout {
        anchors.fill: parent; spacing: 8
        Rectangle { id: gb2; visible: net.message !== ""; Layout.fillWidth: true; implicitHeight: 26; radius: 7; color: Theme.ov(0.06); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb2.color) } GradientStop { position: 1; color: Theme.btnBottom(gb2.color) } } border.color: Theme.danger
            Text { anchors.fill: parent; anchors.leftMargin: 10; verticalAlignment: Text.AlignVCenter; text: net.message; color: Theme.danger; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideRight } }
        RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            // ------------------------------------------------------------------ SEND
            Card { border.color: net.txOn ? Theme.emerald : Theme.line
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 7
                    RowLayout { spacing: 8
                        Head { text: i18n.t("SEND · AES67", i18n.lang) }
                        Text { text: net.txOn ? i18n.t("ON AIR", i18n.lang) : i18n.t("OFF", i18n.lang); color: net.txOn ? Theme.emerald : Theme.dim; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                        Item { Layout.fillWidth: true }
                        Btn { objectName: "aoipSendBtn"; label: net.txOn ? i18n.t("STOP", i18n.lang) : i18n.t("START", i18n.lang); on: true; onColor: net.txOn ? Theme.danger : Theme.emerald; onClicked: net.txOn ? net.stopSend() : net.startSend() } }
                    Note { text: i18n.t("The mixer's master as one stream on the network. Receivers that list AES67 streams will show it by its name.", i18n.lang) }
                    RowLayout { spacing: 6; Lab { text: i18n.t("NAME", i18n.lang) } Field { Layout.fillWidth: true; enabled: !net.txOn; text: net.txName; onEditingFinished: net.txName = text } }
                    RowLayout { spacing: 6; Lab { text: i18n.t("TO ADDRESS", i18n.lang) } Field { Layout.fillWidth: true; enabled: !net.txOn; text: net.txAddress; onEditingFinished: net.txAddress = text } }
                    RowLayout { spacing: 6; Lab { text: i18n.t("PORT", i18n.lang) } Field { implicitWidth: 70; enabled: !net.txOn; text: net.txPort; inputMethodHints: Qt.ImhDigitsOnly; onEditingFinished: net.txPort = parseInt(text) || 5004 } }
                    RowLayout { spacing: 4; Lab { text: i18n.t("CHANNELS", i18n.lang) }
                        Repeater { model: [1, 2, 4, 8]; delegate: Btn { label: modelData; on: net.txChannels === modelData; onClicked: if (!net.txOn) net.txChannels = modelData } } }
                    RowLayout { spacing: 4; Lab { text: i18n.t("SAMPLE FORMAT", i18n.lang) }
                        Repeater { model: [24, 16]; delegate: Btn { label: "L" + modelData; on: net.txBits === modelData; onClicked: if (!net.txOn) net.txBits = modelData } } }
                    Note { text: net.txChannels === 1 ? i18n.t("1 channel: left and right of the master summed to mono.", i18n.lang) : net.txChannels === 2 ? i18n.t("2 channels: master left and right.", i18n.lang) : i18n.t("Channels 1-2: master. Channels 3 and up: strips 1, 2, 3 … before their faders (a multitrack feed).", i18n.lang) }
                    RowLayout { spacing: 4; Lab { text: i18n.t("CASCADE", i18n.lang) }   // two consoles as one: each sends its master to the other and receives the other's master on strips 31-32
                        Btn { objectName: "cascadeA"; label: i18n.t("CONSOLE A", i18n.lang); onColor: Theme.cyan; on: net.txOn && net.rxOn && net.txAddress === "239.69.0.1" && net.rxAddress === "239.69.0.2"; onClicked: panel.cascade("239.69.0.1", "239.69.0.2") }
                        Btn { objectName: "cascadeB"; label: i18n.t("CONSOLE B", i18n.lang); onColor: Theme.cyan; on: net.txOn && net.rxOn && net.txAddress === "239.69.0.2" && net.rxAddress === "239.69.0.1"; onClicked: panel.cascade("239.69.0.2", "239.69.0.1") }
                        Btn { visible: net.txOn || net.rxOn; label: i18n.t("OFF", i18n.lang); onClicked: { net.stopSend(); net.stopReceive() } } }
                    Note { text: i18n.t("CASCADE links two consoles on one network: press CONSOLE A on one and CONSOLE B on the other. Each hears the other's master on its strips 31-32. Do not send those two strips back to the master of the other side at full level in a loop.", i18n.lang) }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.lineSoft }
                    RowLayout { spacing: 6; Lab { text: i18n.t("PACKETS SENT", i18n.lang) } Val { text: panel.st.txPackets } }
                    RowLayout { spacing: 6; Lab { text: i18n.t("SAMPLE RATE", i18n.lang) } Val { text: net.txOn ? panel.st.txRate + " Hz" : "—" } }
                    Text { visible: net.txOn && panel.st.txMismatch; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: i18n.t("The sound card changed its sample rate: stop and start the stream.", i18n.lang); color: Theme.danger; font.family: Theme.mono; font.pixelSize: 8 }
                    Text { visible: net.txOn && panel.st.txRate !== 48000; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: i18n.t("AES67 expects 48 000 Hz. Set the sound card to 48 kHz for equipment that accepts nothing else.", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8 }
                    Item { Layout.fillHeight: true }
                    RowLayout { spacing: 4; Lab { text: i18n.t("NETWORK CARD", i18n.lang) }
                        Flow { Layout.fillWidth: true; spacing: 4
                            Btn { label: i18n.t("AUTO", i18n.lang); on: net.card === ""; onColor: Theme.cyan; onClicked: net.card = "" }
                            Repeater { model: net.cards; delegate: Btn { label: modelData; on: net.card === modelData; onColor: Theme.cyan; onClicked: net.card = modelData } } } }
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: i18n.t("Network audio is not encrypted and has no password: anyone on this network can listen to the stream. Use it on a network you own.", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 7 } } }
            // --------------------------------------------------------------- RECEIVE
            Card { border.color: net.rxOn ? (panel.st.rxLive ? Theme.emerald : Theme.amber) : Theme.line
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 7
                    RowLayout { spacing: 8
                        Head { text: i18n.t("RECEIVE · AES67", i18n.lang) }
                        Text { text: !net.rxOn ? i18n.t("OFF", i18n.lang) : panel.st.rxLive ? i18n.t("RECEIVING", i18n.lang) : i18n.t("WAITING", i18n.lang); color: !net.rxOn ? Theme.dim : panel.st.rxLive ? Theme.emerald : Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                        Item { Layout.fillWidth: true }
                        Btn { objectName: "aoipRecvBtn"; label: net.rxOn ? i18n.t("STOP", i18n.lang) : i18n.t("START", i18n.lang); on: true; onColor: net.rxOn ? Theme.danger : Theme.emerald; onClicked: net.rxOn ? net.stopReceive() : net.startReceive() } }
                    RowLayout { spacing: 6; Lab { text: i18n.t("FROM ADDRESS", i18n.lang) } Field { Layout.fillWidth: true; enabled: !net.rxOn; text: net.rxAddress; onEditingFinished: net.rxAddress = text } Field { implicitWidth: 62; enabled: !net.rxOn; text: net.rxPort; inputMethodHints: Qt.ImhDigitsOnly; onEditingFinished: net.rxPort = parseInt(text) || 5004 } }
                    Note { text: i18n.t("A group address (239.x.x.x) for a stream sent to everyone, or 0.0.0.0 for one sent straight to this computer.", i18n.lang) }
                    RowLayout { spacing: 4; Lab { text: i18n.t("CHANNELS", i18n.lang) }
                        Repeater { model: [1, 2, 4, 8, 16]; delegate: Btn { label: modelData; on: net.rxChannels === modelData; onClicked: if (!net.rxOn) net.rxChannels = modelData } }
                        Item { implicitWidth: 6 }
                        Repeater { model: [24, 16]; delegate: Btn { label: "L" + modelData; on: net.rxBits === modelData; onClicked: if (!net.rxOn) net.rxBits = modelData } } }
                    RowLayout { spacing: 4; Lab { text: i18n.t("SAMPLE RATE", i18n.lang) }
                        Repeater { model: [44100, 48000, 96000]; delegate: Btn { label: modelData / 1000 + "k"; on: net.rxRate === modelData; onClicked: if (!net.rxOn) net.rxRate = modelData } } }
                    RowLayout { spacing: 4; Lab { text: i18n.t("BUFFER", i18n.lang) }
                        Repeater { model: [10, 20, 40, 80, 200]; delegate: Btn { label: modelData + " ms"; on: net.rxLatency === modelData; onClicked: if (!net.rxOn) net.rxLatency = modelData } } }
                    RowLayout { spacing: 4; Lab { text: i18n.t("INTO STRIPS", i18n.lang) }
                        Btn { label: i18n.t("NONE", i18n.lang); on: net.rxFirstStrip === 0; onClicked: net.rxFirstStrip = 0 }
                        Repeater { model: [1, 3, 5, 7, 9, 13, 17, 25]; delegate: Btn { label: modelData + (net.rxChannels > 1 ? "-" + Math.min(32, modelData + net.rxChannels - 1) : ""); on: net.rxFirstStrip === modelData; onColor: Theme.emerald; onClicked: net.rxFirstStrip = modelData } } }
                    Note { text: i18n.t("Those strips listen to the network stream instead of the sound card while the receiver is on, and go back to their own input when it stops.", i18n.lang) }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.lineSoft }
                    RowLayout { spacing: 10
                        Val { text: i18n.t("PACKETS ", i18n.lang) + panel.st.rxPackets } Val { text: i18n.t("LOST ", i18n.lang) + panel.st.rxLost; color: panel.st.rxLost > 0 ? Theme.amber : Theme.cyan }
                        Val { text: i18n.t("GAPS ", i18n.lang) + panel.st.rxUnderruns; color: panel.st.rxUnderruns > 1 ? Theme.amber : Theme.cyan } Val { text: i18n.t("BUFFER ", i18n.lang) + Number(panel.st.rxFillMs).toFixed(0) + " ms" }
                        Val { visible: panel.st.rxFrom !== ""; text: i18n.t("FROM ", i18n.lang) + panel.st.rxFrom } }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.lineSoft }
                    RowLayout { spacing: 8
                        Head { text: i18n.t("STREAMS ON THIS NETWORK", i18n.lang) } Item { Layout.fillWidth: true }
                        Btn { objectName: "aoipFindBtn"; label: net.discoverOn ? i18n.t("STOP LISTENING", i18n.lang) : i18n.t("LISTEN", i18n.lang); on: net.discoverOn; onColor: Theme.cyan; onClicked: net.setDiscover(!net.discoverOn) } }
                    ListView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 3; model: net.streams
                        delegate: Rectangle { width: ListView.view.width; implicitHeight: 30; radius: 4; color: Theme.ov(0.05); border.color: Theme.lineSoft
                            RowLayout { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 6; spacing: 8
                                ColumnLayout { Layout.fillWidth: true; spacing: 0
                                    Text { Layout.fillWidth: true; elide: Text.ElideRight; text: i18n.t(modelData.name ?? "", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                                    Text { Layout.fillWidth: true; elide: Text.ElideRight; text: modelData.address + ":" + modelData.port + "  ·  " + modelData.channels + " ch L" + modelData.bits + " " + modelData.rate / 1000 + "k  ·  " + modelData.origin; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7 } }
                                Btn { label: i18n.t("USE", i18n.lang); on: true; onColor: Theme.emerald; onClicked: if (!net.rxOn) net.useStream(index) } } } }
                    Note { visible: net.streams.length === 0; text: net.discoverOn ? i18n.t("Nothing announced yet. Senders announce every 30 seconds or so.", i18n.lang) : i18n.t("LISTEN shows the AES67 streams other equipment announces (SAP), ready to pick.", i18n.lang) } } }
            // ------------------------------------------------------------------ MIDI
            ColumnLayout { Layout.preferredWidth: 330; Layout.maximumWidth: 330; Layout.fillHeight: true; spacing: 8
                Card { Layout.fillHeight: false; implicitHeight: midiCol.implicitHeight + 24; border.color: (net.midiNetOn || net.midiPort !== "") ? Theme.emerald : Theme.line
                    ColumnLayout { id: midiCol; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12; spacing: 6
                        RowLayout { spacing: 8; Head { text: i18n.t("MIDI CONTROL", i18n.lang) } Item { Layout.fillWidth: true } Val { text: net.midiCount > 0 ? net.midiLast : i18n.t("no message yet", i18n.lang); color: net.midiCount > 0 ? Theme.emerald : Theme.dim } }
                        Note { text: i18n.t("Volume (CC 7) on MIDI channel 1-15 moves the fader of that strip, channel 16 the master. Pan is CC 10. Notes 0-31 mute, 32-63 solo.", i18n.lang) }
                        RowLayout { spacing: 4; Lab { Layout.preferredWidth: 78; text: i18n.t("THIS COMPUTER", i18n.lang) }
                            Flow { Layout.fillWidth: true; spacing: 4
                                Btn { label: i18n.t("NONE", i18n.lang); on: net.midiPort === ""; onClicked: net.openMidi("") }
                                Repeater { model: net.midiInputs; delegate: Btn { label: i18n.t(modelData.name ?? "", i18n.lang); on: net.midiPort === modelData.id; onColor: Theme.emerald; onClicked: net.openMidi(modelData.id) } }
                                Btn { label: "⟳"; onClicked: net.rescanMidi() } } }
                        RowLayout { spacing: 4; Lab { Layout.preferredWidth: 78; text: i18n.t("NETWORK MIDI", i18n.lang) }
                            Btn { objectName: "midiNetBtn"; label: net.midiNetOn ? i18n.t("ON", i18n.lang) : i18n.t("OFF", i18n.lang); on: net.midiNetOn; onColor: Theme.emerald; onClicked: net.setMidiNet(!net.midiNetOn) }
                            Field { implicitWidth: 56; enabled: !net.midiNetOn; text: net.midiNetPort; inputMethodHints: Qt.ImhDigitsOnly; onEditingFinished: net.midiNetPort = parseInt(text) || 5004 }
                            Btn { label: i18n.t("THIS COMPUTER", i18n.lang); on: !net.midiLan; onClicked: net.midiLan = false } Btn { label: i18n.t("LAN", i18n.lang); on: net.midiLan; onColor: Theme.danger; onClicked: net.midiLan = true } }
                        Note { text: net.midiNetOn ? i18n.t("Connect from the other device's network MIDI set-up to this computer's address, port ", i18n.lang) + net.midiNetPort + (panel.st.midiPeers > 0 ? i18n.t(" · connected: ", i18n.lang) + panel.st.midiPeer : "") : i18n.t("A network MIDI (RTP-MIDI) session that tablets and computers connect to by address.", i18n.lang) }
                        Text { visible: net.midiLan; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: i18n.t("LAN: any device on the network can connect and move the faders. Network MIDI has no password.", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 7 }
                        RowLayout { spacing: 4; Lab { Layout.preferredWidth: 78; text: i18n.t("LEARN", i18n.lang) }
                            Repeater { model: [["fader", "FADER"], ["pan", "PAN"], ["mute", "MUTE"], ["solo", "SOLO"]]; delegate: Btn { label: i18n.t(modelData[1], i18n.lang); onClicked: net.learn(modelData[0], engine.selectedStrip) } }
                            Btn { label: i18n.t("MASTER", i18n.lang); onClicked: net.learn("master", 0) }
                            Btn { visible: net.midiLearned > 0; label: i18n.t("CLEAR ", i18n.lang) + net.midiLearned; onColor: Theme.danger; onClicked: net.clearLearned() } }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: net.midiLearning ? i18n.t("Move the control on your controller now …", i18n.lang) : i18n.t("LEARN binds the next control you move to the selected strip (strip ", i18n.lang) + (engine.selectedStrip + 1) + ").";
                            color: net.midiLearning ? Theme.amber : Theme.dim; font.family: Theme.mono; font.pixelSize: 7
                            MouseArea { anchors.fill: parent; enabled: net.midiLearning; onClicked: net.cancelLearn() } } } }
                Card {
                    ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 5
                        Head { text: i18n.t("WHAT THIS BUILD CARRIES", i18n.lang) }
                        ListView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 5; model: net.transports
                            delegate: ColumnLayout { width: ListView.view.width; spacing: 1
                                RowLayout { spacing: 6
                                    Rectangle { implicitWidth: 7; implicitHeight: 7; radius: 4; color: modelData.available ? Theme.emerald : Theme.dim }
                                    Text { text: i18n.t(modelData.name ?? "", i18n.lang); color: modelData.available ? Theme.ink : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } }
                                Text { Layout.fillWidth: true; Layout.leftMargin: 13; wrapMode: Text.WordWrap; text: i18n.t(modelData.note, i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } } } } }
            }
        }
    }
}
