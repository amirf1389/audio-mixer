// AI MIX: the gain-sharing AUTOMIX (member strips share one unity gain in proportion to their level: the microphone being spoken into
// takes it, the others fall back) and the master AUTO LEVEL (short-term loudness steered toward a target under the brick-wall limiter).
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "aiMixPage"
    property int bank: 0
    readonly property var tel: engine.telemetry
    readonly property int members: { let n = 0; for (let i = 0; i < engine.stripState.length; ++i) if (engine.stripState[i].automix) ++n; return n }
    component Head: Text { color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
    component Note: Text { Layout.fillWidth: true; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 22; radius: 4; color: on ? onColor : Qt.rgba(1, 1, 1, 0.08); border.color: Theme.lineSoft
        Text { id: bl; anchors.centerIn: parent; text: label; color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle { Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Text { text: "AI MIX:"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Text { text: page.members + " strips in AUTOMIX · AUTO LEVEL " + (engine.autoLevelOn ? "ON (" + (page.tel.autoLevelDb >= 0 ? "+" : "") + page.tel.autoLevelDb.toFixed(1) + " dB)" : "off"); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Repeater { model: ["CH 01-16", "CH 17-32"]; delegate: Rectangle { implicitWidth: 66; implicitHeight: 20; radius: 4; color: page.bank === index ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(1, 1, 1, 0.06); Text { anchors.centerIn: parent; text: modelData; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.bank = index } } }
                Item { Layout.fillWidth: true }
                Btn { label: "ALL ON (THIS BANK)"; onClicked: { for (let i = 0; i < 16; ++i) engine.setAutomix(page.bank * 16 + i, true, engine.stripState[page.bank * 16 + i].amWeight) } }
                Btn { label: "ALL OFF"; onClicked: { for (let i = 0; i < engine.stripState.length; ++i) engine.setAutomix(i, false, 1) } } } }
        RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 6
                    Head { text: "AUTOMIX  (gain sharing, 10 ms attack / 300 ms release, -40 dB floor)" }
                    GridLayout { columns: 2; columnSpacing: 10; rowSpacing: 4; Layout.fillWidth: true
                        Repeater { model: 16
                            delegate: Rectangle { readonly property int s: page.bank * 16 + index; readonly property var st: engine.stripState[s]; readonly property var mt: engine.meters[s]; Layout.fillWidth: true; implicitHeight: 34; radius: 6; color: st && st.automix ? Qt.rgba(0, 0.95, 1, 0.08) : Qt.rgba(1, 1, 1, 0.03); border.color: st && st.automix ? Theme.cyan : Theme.lineSoft
                                RowLayout { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 8
                                    Rectangle { width: 36; height: 20; radius: 10; color: st && st.automix ? Theme.cyan : Qt.rgba(1, 1, 1, 0.1); Rectangle { width: 14; height: 14; radius: 7; y: 3; x: st && st.automix ? 19 : 3; color: st && st.automix ? "#000" : Theme.dim } MouseArea { anchors.fill: parent; onClicked: engine.setAutomix(s, !(st && st.automix), st ? st.amWeight : 1) } }
                                    Text { Layout.preferredWidth: 80; text: "CH " + (s + 1 < 10 ? "0" : "") + (s + 1) + " " + (st ? st.name : ""); color: Theme.stripColors[st ? st.color : 0]; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; elide: Text.ElideRight }
                                    Text { text: "W"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
                                    Slider { Layout.fillWidth: true; from: 0.25; to: 4; stepSize: 0.05; value: st ? st.amWeight : 1; enabled: st && st.automix; onMoved: engine.setAutomix(s, true, value) }
                                    Text { Layout.preferredWidth: 30; text: st ? st.amWeight.toFixed(2) : ""; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7 }
                                    Rectangle { Layout.preferredWidth: 90; implicitHeight: 8; radius: 2; color: Qt.rgba(1, 1, 1, 0.06)
                                        Rectangle { readonly property real g: mt ? Math.max(-40, Math.min(0, mt.amGainDb)) : 0; width: parent.width * (1 + g / 40); height: parent.height; radius: 2; color: g > -6 ? Theme.emerald : g > -20 ? Theme.amber : Theme.dim } }
                                    Text { Layout.preferredWidth: 46; text: st && st.automix && mt ? mt.amGainDb.toFixed(1) + " dB" : "—"; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 7; horizontalAlignment: Text.AlignRight } } } } }
                    Item { Layout.fillHeight: true }
                    Note { text: "Switch on the microphones of a panel, a talk show or a conference: the strip with the voice takes the gain, the others drop so the room noise and comb filtering do not add up. WEIGHT favours a strip (the host at 2, guests at 1). The bar and dB show each strip's live share; -40 dB is the floor." } } }
            Rectangle { Layout.preferredWidth: 360; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 8
                    RowLayout { Head { text: "AUTO LEVEL" } Item { Layout.fillWidth: true } Btn { label: engine.autoLevelOn ? "ON" : "OFF"; on: engine.autoLevelOn; onColor: Theme.emerald; onClicked: engine.autoLevelOn = !engine.autoLevelOn } }
                    Text { text: "TARGET"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.letterSpacing: 1 }
                    Flow { Layout.fillWidth: true; spacing: 4
                        Repeater { model: [{ t: "-14 STREAMING", v: -14 }, { t: "-16 PODCAST", v: -16 }, { t: "-18 LIVE", v: -18 }, { t: "-23 BROADCAST", v: -23 }]; delegate: Btn { label: modelData.t; on: Math.abs(engine.autoLevelTarget - modelData.v) < 0.01; onColor: Theme.cyan; onClicked: engine.autoLevelTarget = modelData.v } } }
                    RowLayout { spacing: 8; Layout.fillWidth: true
                        Slider { Layout.fillWidth: true; from: -30; to: -6; stepSize: 0.5; value: engine.autoLevelTarget; onMoved: engine.autoLevelTarget = value }
                        Text { Layout.preferredWidth: 70; text: engine.autoLevelTarget.toFixed(1) + " LUFS"; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; horizontalAlignment: Text.AlignRight } }
                    Text { text: "MAX LIFT"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.letterSpacing: 1 }
                    RowLayout { spacing: 8; Layout.fillWidth: true
                        Slider { Layout.fillWidth: true; from: 0; to: 24; stepSize: 1; value: engine.autoLevelMax; onMoved: engine.autoLevelMax = value }
                        Text { Layout.preferredWidth: 70; text: "+" + engine.autoLevelMax.toFixed(0) + " dB"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; horizontalAlignment: Text.AlignRight } }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.lineSoft }
                    GridLayout { columns: 2; columnSpacing: 12; rowSpacing: 4
                        Text { text: "GAIN NOW"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Text { text: (page.tel.autoLevelDb >= 0 ? "+" : "") + page.tel.autoLevelDb.toFixed(1) + " dB"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 12; font.bold: true }
                        Text { text: "SHORT-TERM"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Text { text: page.tel.shortTerm > -119 ? page.tel.shortTerm.toFixed(1) + " LUFS" : "—"; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 12; font.bold: true }
                        Text { text: "TRUE PEAK"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Text { text: page.tel.truePeak > -119 ? page.tel.truePeak.toFixed(1) + " dBTP" : "—"; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 12; font.bold: true } }
                    Note { text: "\"No low, but loud\": a slow gain (+1 dB/s up, -6 dB/s down, never more than MAX LIFT, never below -12 dB) steers the 3 s loudness toward the target. The master limiter stays in front of the output, so nothing clips; the LOUDNESS page shows the result. It starts acting 3 s after switch-on, when the window is full." }
                    Item { Layout.fillHeight: true } } } }
    }
}
