// MIXER: 32 strips in two pages of 16 (or all), each with colour, name, pan, mute, solo, MORE menu, fader, post-fader meter (bars or an analogue VU face), input meter, gain-reduction meter,
// DSP badges (GATE / EQ / X / COMP / LIM / DLY / Ø: lit when that stage is on, click to open its page for this channel), DCA / mute-group /
// sends readout; click a strip to select it and edit its whole chain in the side panel; the master and the MONITOR (control room: source, level, dim, mono, mute) on the right.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "mixerPage"
    property int bank: 0      // 0 = 1-16, 1 = 17-32, 2 = all
    property bool sidePanel: true
    property bool vuMode: false        // bar meters or analogue VU faces on every strip and the master
    property bool monitorPanel: true   // the control-room section next to the master
    readonly property var mon: engine.monitor
    signal go(string id)
    readonly property int sel: engine.selectedStrip
    readonly property var selSt: engine.stripState[sel]
    readonly property var selCh: engine.channelParams(sel)
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Text { text: "PAGES:"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Repeater {
                    model: ["PAGE 1: CH 01-16", "PAGE 2: CH 17-32", "ALL 32 CH"]
                    delegate: Rectangle {
                        implicitWidth: lbl.implicitWidth + 16; implicitHeight: 20; radius: 4
                        color: page.bank === index ? Theme.amber : Qt.rgba(1, 1, 1, 0.06)
                        Text { id: lbl; anchors.centerIn: parent; text: modelData; color: page.bank === index ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                        MouseArea { anchors.fill: parent; onClicked: page.bank = index }
                    }
                }
                Rectangle { implicitWidth: 90; implicitHeight: 20; radius: 4; color: page.sidePanel ? Qt.rgba(0, 0.95, 1, 0.18) : Qt.rgba(1, 1, 1, 0.06); border.color: page.sidePanel ? Theme.cyan : "transparent"; Text { anchors.centerIn: parent; text: "CHANNEL PANEL"; color: page.sidePanel ? Theme.cyan : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.sidePanel = !page.sidePanel } }
                Rectangle { implicitWidth: 70; implicitHeight: 20; radius: 4; color: page.vuMode ? Qt.rgba(0.984, 0.749, 0.141, 0.2) : Qt.rgba(1, 1, 1, 0.06); border.color: page.vuMode ? Theme.amber : "transparent"; Text { anchors.centerIn: parent; text: page.vuMode ? "VU METERS" : "BAR METERS"; color: page.vuMode ? Theme.amber : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.vuMode = !page.vuMode } }
                Rectangle { visible: page.vuMode; implicitWidth: 86; implicitHeight: 20; radius: 4; color: Qt.rgba(1, 1, 1, 0.06); Text { anchors.centerIn: parent; text: "0 VU = " + engine.vuRefDb + " dBFS"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } MouseArea { anchors.fill: parent; onClicked: engine.vuRefDb = engine.vuRefDb === -18 ? -20 : engine.vuRefDb === -20 ? -14 : -18 } }
                Rectangle { implicitWidth: 64; implicitHeight: 20; radius: 4; color: page.monitorPanel ? Qt.rgba(0.2, 0.9, 0.5, 0.18) : Qt.rgba(1, 1, 1, 0.06); border.color: page.monitorPanel ? Theme.emerald : "transparent"; Text { anchors.centerIn: parent; text: "MONITOR"; color: page.monitorPanel ? Theme.emerald : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.monitorPanel = !page.monitorPanel } }
                Item { Layout.fillWidth: true }
                Text { text: engine.running ? engine.status : "ENGINE IDLE: connect a sound card"; color: engine.running ? Theme.emerald : Theme.amber; font.family: Theme.mono; font.pixelSize: 8 }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; clip: true
                Flickable {
                    anchors.fill: parent; anchors.margins: 8
                    contentWidth: strips.implicitWidth; contentHeight: height
                    flickableDirection: Flickable.HorizontalFlick; boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: strips
                        spacing: 4
                        height: parent.height
                        Repeater {
                            model: engine.strips
                            delegate: Item {
                                visible: page.bank === 2 || Math.floor(index / 16) === page.bank
                                width: visible ? 74 : 0
                                height: strips.height
                                property var st: engine.stripState[index]
                                property var mt: engine.meters[index]
                                property color sc: Theme.stripColors[st ? st.color : 0]
                                property bool selected: page.sel === index
                                Rectangle { anchors.fill: parent; radius: 6; color: selected ? Qt.rgba(1, 1, 1, 0.06) : (index % 2 ? Qt.rgba(1, 1, 1, 0.025) : Qt.rgba(0, 0, 0, 0.25)); border.color: selected ? Theme.ink : Theme.lineSoft; border.width: selected ? 1.5 : 1
                                    MouseArea { anchors.fill: parent; onPressed: function (m) { engine.selectedStrip = index; m.accepted = false } } }
                                Rectangle { anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 1; height: 3; radius: 2; color: sc }
                                // beyond the licence: the strip is muted in the engine and shown locked; a click opens LICENSE
                                Rectangle { anchors.fill: parent; z: 10; radius: 6; color: Qt.rgba(0, 0, 0, 0.72); visible: index >= engine.licensedChannels
                                    ColumnLayout { anchors.centerIn: parent; spacing: 6
                                        Text { Layout.alignment: Qt.AlignHCenter; text: "🔒"; color: Theme.amber; font.pixelSize: 18 }
                                        Text { Layout.alignment: Qt.AlignHCenter; text: "CH " + (index + 1); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                                        Text { Layout.alignment: Qt.AlignHCenter; text: index < 16 ? "PRO" : "STUDIO"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 7; font.bold: true }
                                        Text { Layout.alignment: Qt.AlignHCenter; text: "UNLOCK ›"; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 7 } }
                                    MouseArea { anchors.fill: parent; onClicked: page.go("renew") } }
                                ColumnLayout {
                                    anchors.fill: parent; anchors.margins: 4; anchors.topMargin: 6; spacing: 3
                                    TextInput { Layout.fillWidth: true; text: st ? st.name : ""; color: st && st.effectiveMute ? Theme.dim : Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; horizontalAlignment: Text.AlignHCenter; selectByMouse: true; onEditingFinished: engine.setName(index, text); onActiveFocusChanged: if (activeFocus) engine.selectedStrip = index }
                                    // DSP badges: lit when on, click opens the page for this channel
                                    Flow { Layout.fillWidth: true; spacing: 2
                                        Repeater {
                                            model: [ { k: "gateOn", t: "GATE", p: "gate" }, { k: "eqOn", t: "EQ", p: "dsp" }, { k: "xoverOn", t: "X", p: "dsp" }, { k: "compOn", t: "COMP", p: "dyn" }, { k: "limOn", t: "LIM", p: "dsp" }, { k: "delayOn", t: "DLY", p: "dsp" }, { k: "polarity", t: "Ø", p: "dsp" } ]
                                            delegate: Rectangle { implicitWidth: bt.implicitWidth + 6; implicitHeight: 11; radius: 2; color: st && st[modelData.k] ? Qt.rgba(0, 0.95, 1, 0.22) : Qt.rgba(1, 1, 1, 0.04); border.color: st && st[modelData.k] ? Theme.cyan : "transparent"
                                                Text { id: bt; anchors.centerIn: parent; text: modelData.t; color: st && st[modelData.k] ? Theme.cyan : Theme.dim; font.family: Theme.mono; font.pixelSize: 6; font.bold: true }
                                                MouseArea { anchors.fill: parent; onClicked: { engine.selectedStrip = index; page.go(modelData.p) } } }
                                        }
                                    }
                                    Dial { id: pan; Layout.alignment: Qt.AlignHCenter; implicitWidth: 28; implicitHeight: 28; from: -1; to: 1; value: st ? st.pan : 0; onMoved: engine.setPan(index, value)
                                        background: Rectangle { radius: width / 2; color: "#05070a"; border.color: Theme.line }
                                        handle: Rectangle { x: pan.background.x + pan.background.width / 2 - width / 2; y: pan.background.y + 3; width: 4; height: 8; radius: 2; color: Theme.cyan; transform: Rotation { angle: pan.angle; origin.x: 2; origin.y: pan.background.height / 2 - 3 } } }
                                    RowLayout { Layout.alignment: Qt.AlignHCenter; spacing: 3
                                        Rectangle { implicitWidth: 26; implicitHeight: 16; radius: 3; color: st && st.mute ? Theme.danger : (st && st.effectiveMute ? Qt.rgba(1, 0.165, 0.373, 0.3) : Qt.rgba(1, 1, 1, 0.08)); Text { anchors.centerIn: parent; text: "M"; color: st && st.mute ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setMute(index, !(st && st.mute)) } }
                                        Rectangle { implicitWidth: 26; implicitHeight: 16; radius: 3; color: st && st.solo ? Theme.amber : Qt.rgba(1, 1, 1, 0.08); Text { anchors.centerIn: parent; text: "S"; color: st && st.solo ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setSolo(index, !(st && st.solo)) } } }
                                    // MORE: everything that does not fit on a 74 px strip
                                    Rectangle { Layout.fillWidth: true; implicitHeight: 14; radius: 3; color: more.visible ? Theme.cyan : Qt.rgba(1, 1, 1, 0.06); border.color: Theme.lineSoft
                                        Text { anchors.centerIn: parent; text: "MORE ▾"; color: more.visible ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true }
                                        MouseArea { anchors.fill: parent; onClicked: { engine.selectedStrip = index; more.open() } }
                                        Menu { id: more; width: 190
                                            MenuItem { text: "Channel panel ›"; onTriggered: { page.sidePanel = true } }
                                            MenuItem { text: "Open EQ / tone"; onTriggered: page.go("dsp") }
                                            MenuItem { text: "Open compressor"; onTriggered: page.go("dyn") }
                                            MenuItem { text: "Open gate"; onTriggered: page.go("gate") }
                                            MenuItem { text: "Sends to buses"; onTriggered: page.go("bus") }
                                            MenuSeparator {}
                                            MenuItem { text: "Fader to 0 dB"; onTriggered: engine.setGain(index, 0) }
                                            MenuItem { text: "Pan to centre"; onTriggered: engine.setPan(index, 0) }
                                            MenuItem { text: "Flatten DSP (EQ, dyn, delay, Ø)"; onTriggered: engine.resetStripDsp(index) }
                                            MenuItem { text: "Reset channel (keep name, colour)"; onTriggered: engine.resetStrip(index) }
                                            MenuSeparator {}
                                            MenuItem { text: "Copy channel"; onTriggered: engine.copyChannel(index) }
                                            MenuItem { text: "Paste channel (DSP, sends, pan)"; enabled: engine.channelClipboard; onTriggered: engine.pasteChannel(index) }
                                            MenuSeparator {}
                                            MenuItem { text: (engine.recallSafe(index) ? "✓ " : "") + "Recall safe (scenes skip it)"; onTriggered: engine.setRecallSafe(index, !engine.recallSafe(index)) }
                                            MenuItem { text: "Listen here: monitor = master"; onTriggered: engine.setMonitor(0, page.mon.db, page.mon.dim, page.mon.mono, page.mon.mute) }
                                        } }
                                    VuMeter { visible: page.vuMode; Layout.fillWidth: true; Layout.preferredHeight: 40; vuDb: mt ? mt.vuDb : -120; refDb: engine.vuRefDb; peakLamp: mt ? mt.clip : false; label: "VU" }
                                    RowLayout { Layout.fillHeight: true; Layout.alignment: Qt.AlignHCenter; spacing: 4
                                        Fader { Layout.fillHeight: true; value: st ? st.db : 0; onMoved: function (db) { engine.setGain(index, db) } }
                                        ColumnLayout { Layout.fillHeight: true; spacing: 0
                                            RowLayout { Layout.fillHeight: true; spacing: 2
                                                MeterBar { Layout.fillHeight: true; Layout.topMargin: 8; implicitWidth: 5; peakDb: mt ? mt.inDb : -120; rmsDb: mt ? mt.inDb : -120; floorDb: -60 }   // input (pre-fader)
                                                MeterBar { Layout.fillHeight: true; Layout.topMargin: 8; implicitWidth: 9; peakDb: mt ? mt.peakDb : -120; rmsDb: mt ? mt.rmsDb : -120; holdDb: mt ? mt.holdDb : -120; clip: mt ? mt.clip : false }   // post-fader
                                                Rectangle { Layout.fillHeight: true; Layout.topMargin: 8; implicitWidth: 4; radius: 2; color: "#05070a"; border.color: Theme.lineSoft   // gain reduction, from the top
                                                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 1; height: Math.min(1, Math.max(0, -(mt ? mt.compGrDb : 0) / 24)) * (parent.height - 2); radius: 1; color: Theme.amber } } }
                                            Text { Layout.alignment: Qt.AlignHCenter; text: "IN  OUT GR"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 5 } } }
                                    Text { Layout.alignment: Qt.AlignHCenter; text: Theme.dbText(st ? st.db : 0) + (st && st.dca > 0 && st.effectiveDb !== st.db ? " (" + Theme.dbText(st.effectiveDb) + ")" : ""); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 7 }
                                    // groups and sends readout
                                    RowLayout { Layout.alignment: Qt.AlignHCenter; spacing: 2
                                        Rectangle { visible: st && st.dca > 0; implicitWidth: 22; implicitHeight: 11; radius: 2; color: Qt.rgba(0.984, 0.749, 0.141, 0.2); Text { anchors.centerIn: parent; text: "D" + (st ? st.dca : 0); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 6; font.bold: true } }
                                        Repeater { model: 8; delegate: Rectangle { visible: st && ((st.muteGroups >> index) & 1); implicitWidth: 5; implicitHeight: 5; radius: 2.5; color: Theme.pink } }
                                        Text { visible: st && st.sendsTo.length; text: st && st.sendsTo.length ? "→" + st.sendsTo.join("") : ""; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 6 } }
                                }
                            }
                        }
                    }
                }
            }
            // the selected channel's whole chain, editable
            Rectangle {
                visible: page.sidePanel
                Layout.preferredWidth: 230; Layout.fillHeight: true; radius: Theme.radius; color: Theme.cardRaised; border.color: Theme.cyan
                Flickable {
                    anchors.fill: parent; anchors.margins: 8; contentHeight: side.implicitHeight; clip: true
                    ColumnLayout {
                        id: side; width: parent.width; spacing: 6
                        RowLayout { Layout.fillWidth: true
                            Rectangle { implicitWidth: 10; implicitHeight: 10; radius: 5; color: Theme.stripColors[page.selSt ? page.selSt.color : 0] }
                            Text { Layout.fillWidth: true; text: (page.selSt ? page.selSt.name : "") + "  ·  CH " + (page.sel + 1); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; elide: Text.ElideRight } }
                        Text { text: "COLOUR"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
                        RowLayout { spacing: 4; Repeater { model: 8; delegate: Rectangle { implicitWidth: 20; implicitHeight: 14; radius: 3; color: Theme.stripColors[index]; border.color: page.selSt && page.selSt.color === index ? Theme.ink : "transparent"; border.width: 2; MouseArea { anchors.fill: parent; onClicked: engine.setColor(page.sel, index) } } } }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.lineSoft }
                        // gate
                        RowLayout { Layout.fillWidth: true
                            Rectangle { implicitWidth: 44; implicitHeight: 18; radius: 3; color: page.selCh.gateOn ? Theme.pink : Qt.rgba(1, 1, 1, 0.08); Text { anchors.centerIn: parent; text: "GATE"; color: page.selCh.gateOn ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setGate(page.sel, !page.selCh.gateOn, page.selCh.gateThresholdDb, page.selCh.gateRangeDb, page.selCh.gateAttackMs, page.selCh.gateHoldMs, page.selCh.gateReleaseMs) } }
                            Text { text: "THR " + Theme.dbText(page.selCh.gateThresholdDb === undefined ? -50 : page.selCh.gateThresholdDb); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7 } Item { Layout.fillWidth: true }
                            Text { text: "OPEN ›"; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 7; Layout.preferredWidth: 34; MouseArea { anchors.fill: parent; onClicked: page.go("gate") } } }
                        Slider { Layout.fillWidth: true; from: -90; to: 0; value: page.selCh.gateThresholdDb === undefined ? -50 : page.selCh.gateThresholdDb; onMoved: engine.setGate(page.sel, page.selCh.gateOn, value, page.selCh.gateRangeDb, page.selCh.gateAttackMs, page.selCh.gateHoldMs, page.selCh.gateReleaseMs) }
                        // compressor
                        RowLayout { Layout.fillWidth: true
                            Rectangle { implicitWidth: 44; implicitHeight: 18; radius: 3; color: page.selCh.compOn ? Theme.emerald : Qt.rgba(1, 1, 1, 0.08); Text { anchors.centerIn: parent; text: "COMP"; color: page.selCh.compOn ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setCompressor(page.sel, !page.selCh.compOn, page.selCh.compThresholdDb, page.selCh.compRatio, page.selCh.compKneeDb, page.selCh.compAttackMs, page.selCh.compReleaseMs, page.selCh.compMakeupDb) } }
                            Text { text: "THR " + Theme.dbText(page.selCh.compThresholdDb === undefined ? -20 : page.selCh.compThresholdDb) + "  " + Number(page.selCh.compRatio || 4).toFixed(1) + ":1"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7 } Item { Layout.fillWidth: true }
                            Text { text: "OPEN ›"; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 7; Layout.preferredWidth: 34; MouseArea { anchors.fill: parent; onClicked: page.go("dyn") } } }
                        Slider { Layout.fillWidth: true; from: -60; to: 0; value: page.selCh.compThresholdDb === undefined ? -20 : page.selCh.compThresholdDb; onMoved: engine.setCompressor(page.sel, page.selCh.compOn, value, page.selCh.compRatio, page.selCh.compKneeDb, page.selCh.compAttackMs, page.selCh.compReleaseMs, page.selCh.compMakeupDb) }
                        // tone
                        RowLayout { Layout.fillWidth: true
                            Text { text: "TONE: " + (page.selSt && page.selSt.eqOn ? "EQ " : "") + (page.selSt && page.selSt.xoverOn ? "X-OVER " : "") + (page.selSt && page.selSt.limOn ? "LIM " : "") + (page.selSt && page.selSt.delayOn ? Number(page.selCh.delayMs).toFixed(2) + " ms " : "") + (page.selSt && page.selSt.polarity ? "Ø " : "") + (page.selSt && !(page.selSt.eqOn || page.selSt.xoverOn || page.selSt.limOn || page.selSt.delayOn || page.selSt.polarity) ? "flat" : ""); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; Layout.fillWidth: true; elide: Text.ElideRight }
                            Text { text: "OPEN ›"; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 7; Layout.preferredWidth: 34; MouseArea { anchors.fill: parent; onClicked: page.go("dsp") } } }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.lineSoft }
                        // sends
                        RowLayout { Layout.fillWidth: true; Text { text: "SENDS"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; Layout.fillWidth: true } Text { text: "OPEN ›"; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 7; Layout.preferredWidth: 34; MouseArea { anchors.fill: parent; onClicked: page.go("bus") } } }
                        Repeater { model: engine.busState; delegate: RowLayout { Layout.fillWidth: true; spacing: 4
                            property var sd: engine.send(page.sel, index)
                            Connections { target: engine; function onStripsChanged() { sd = engine.send(page.sel, index) } function onSelectedStripChanged() { sd = engine.send(page.sel, index) } }
                            Text { text: modelData.name; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; Layout.preferredWidth: 44; elide: Text.ElideRight }
                            Slider { Layout.fillWidth: true; from: -60; to: 10; value: sd ? sd.db : -60; onMoved: engine.setSend(page.sel, index, value, sd ? sd.pre : false) }
                            Rectangle { implicitWidth: 28; implicitHeight: 14; radius: 2; color: sd && sd.pre ? Theme.cyan : Qt.rgba(1, 1, 1, 0.08); Text { anchors.centerIn: parent; text: sd && sd.pre ? "PRE" : "POST"; color: sd && sd.pre ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 6; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setSend(page.sel, index, sd ? sd.db : -60, !(sd && sd.pre)) } } } }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.lineSoft }
                        // dca
                        RowLayout { Layout.fillWidth: true; Text { text: "DCA"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
                            Repeater { model: 9; delegate: Rectangle { implicitWidth: 18; implicitHeight: 14; radius: 2; color: page.selSt && page.selSt.dca === index ? Theme.amber : Qt.rgba(1, 1, 1, 0.08); Text { anchors.centerIn: parent; text: index === 0 ? "-" : index; color: page.selSt && page.selSt.dca === index ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setDca(page.sel, index) } } } }
                    }
                }
            }
            Rectangle {
                Layout.preferredWidth: 120; Layout.fillHeight: true; radius: Theme.radius; color: Theme.cardRaised; border.color: Theme.orange
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 8; spacing: 6
                    Text { Layout.alignment: Qt.AlignHCenter; text: "MASTER"; color: Theme.orange; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; font.letterSpacing: 2 }
                    VuMeter { visible: page.vuMode; Layout.fillWidth: true; Layout.preferredHeight: 60; vuDb: engine.telemetry.masterVuDb === undefined ? -120 : engine.telemetry.masterVuDb; refDb: engine.vuRefDb; peakLamp: engine.telemetry.masterClip === true; label: "VU  MASTER" }
                    RowLayout { Layout.fillHeight: true; Layout.alignment: Qt.AlignHCenter; spacing: 8
                        Fader { Layout.fillHeight: true; value: engine.masterDb; onMoved: function (db) { engine.setMaster(db) } }
                        MeterBar { Layout.fillHeight: true; Layout.topMargin: 8; peakDb: engine.telemetry.masterLDb; rmsDb: engine.telemetry.masterRmsDb; clip: engine.telemetry.masterClip }
                        MeterBar { Layout.fillHeight: true; Layout.topMargin: 8; peakDb: engine.telemetry.masterRDb; rmsDb: engine.telemetry.masterRmsDb; clip: engine.telemetry.masterClip } }
                    Text { Layout.alignment: Qt.AlignHCenter; text: Theme.dbText(engine.masterDb) + " dB"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9 }
                    Text { Layout.alignment: Qt.AlignHCenter; text: (engine.telemetry.shortTerm <= -119 ? "-inf" : engine.telemetry.shortTerm.toFixed(1)) + " LUFS"; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8 }
                    Text { Layout.alignment: Qt.AlignHCenter; text: "LIMITER -0.5 dBFS"; color: Theme.emerald; font.family: Theme.mono; font.pixelSize: 7 }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 18; radius: 3; color: Qt.rgba(1, 1, 1, 0.08); Text { anchors.centerIn: parent; text: "CLEAR CLIPS"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.clearClips() } }
                }
            }
            // MONITOR: the control room. What the speakers / headphones get after the master meters: a source (master or a bus), level, DIM, MONO, MUTE.
            // The master mix and its meters are not touched: muting the monitor does not mute the show.
            Rectangle {
                visible: page.monitorPanel
                Layout.preferredWidth: 118; Layout.fillHeight: true; radius: Theme.radius; color: Theme.cardRaised; border.color: Theme.emerald
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 8; spacing: 5
                    Text { Layout.alignment: Qt.AlignHCenter; text: "MONITOR"; color: Theme.emerald; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; font.letterSpacing: 2 }
                    Text { Layout.alignment: Qt.AlignHCenter; text: "CONTROL ROOM"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 6; font.letterSpacing: 1 }
                    Text { text: "SOURCE"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
                    Flow { Layout.fillWidth: true; spacing: 3
                        Repeater { model: 9
                            delegate: Rectangle { implicitWidth: index === 0 ? 46 : 22; implicitHeight: 14; radius: 2; color: page.mon.source === index ? Theme.emerald : Qt.rgba(1, 1, 1, 0.08)
                                Text { anchors.centerIn: parent; text: index === 0 ? "MASTER" : "B" + index; color: page.mon.source === index ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 6; font.bold: true }
                                MouseArea { anchors.fill: parent; onClicked: engine.setMonitor(index, page.mon.db, page.mon.dim, page.mon.mono, page.mon.mute) } } } }
                    RowLayout { Layout.fillHeight: true; Layout.alignment: Qt.AlignHCenter; spacing: 6
                        Fader { Layout.fillHeight: true; value: page.mon.db; onMoved: function (db) { engine.setMonitor(page.mon.source, db, page.mon.dim, page.mon.mono, page.mon.mute) } }
                        MeterBar { Layout.fillHeight: true; Layout.topMargin: 8; implicitWidth: 7; peakDb: engine.telemetry.monitorLDb === undefined ? -120 : engine.telemetry.monitorLDb; rmsDb: -120 }
                        MeterBar { Layout.fillHeight: true; Layout.topMargin: 8; implicitWidth: 7; peakDb: engine.telemetry.monitorRDb === undefined ? -120 : engine.telemetry.monitorRDb; rmsDb: -120 } }
                    Text { Layout.alignment: Qt.AlignHCenter; text: Theme.dbText(page.mon.db) + " dB" + (page.mon.dim ? "  DIM -20" : ""); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8 }
                    RowLayout { Layout.fillWidth: true; spacing: 3
                        Rectangle { Layout.fillWidth: true; implicitHeight: 18; radius: 3; color: page.mon.dim ? Theme.amber : Qt.rgba(1, 1, 1, 0.08); Text { anchors.centerIn: parent; text: "DIM"; color: page.mon.dim ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setMonitor(page.mon.source, page.mon.db, !page.mon.dim, page.mon.mono, page.mon.mute) } }
                        Rectangle { Layout.fillWidth: true; implicitHeight: 18; radius: 3; color: page.mon.mono ? Theme.cyan : Qt.rgba(1, 1, 1, 0.08); Text { anchors.centerIn: parent; text: "MONO"; color: page.mon.mono ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setMonitor(page.mon.source, page.mon.db, page.mon.dim, !page.mon.mono, page.mon.mute) } } }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 22; radius: 3; color: page.mon.mute ? Theme.danger : Qt.rgba(1, 1, 1, 0.08); border.color: page.mon.mute ? Theme.danger : Theme.lineSoft; Text { anchors.centerIn: parent; text: page.mon.mute ? "MONITOR MUTED" : "MUTE"; color: page.mon.mute ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.setMonitor(page.mon.source, page.mon.db, page.mon.dim, page.mon.mono, !page.mon.mute) } }
                    Text { Layout.fillWidth: true; text: "After the master meters: the show is not affected"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 6; wrapMode: Text.WordWrap }
                }
            }
        }
    }
}
