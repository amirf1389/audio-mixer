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
    readonly property int members: { let n = 0; for (let i = 0; i < engine.stripState.length; ++i) if (engine.stripAt(i, engine.stripTick).automix) ++n; return n }
    component Head: Text { color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
    component Note: Text { Layout.fillWidth: true; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 22; radius: 8; gradient: Gradient { GradientStop { position: 0; color: on ? Theme.btnTop(onColor) : Theme.ov(0.11) } GradientStop { position: 1; color: on ? Theme.btnBottom(onColor) : Theme.ov(0.08) } } color: on ? onColor : Theme.ov(0.08); border.color: Theme.lineSoft
        Rectangle { anchors.fill: parent; anchors.margins: 1; radius: Math.max(0, parent.radius - 1); color: "transparent"; border.width: 1; border.color: Qt.rgba(1, 1, 1, parent.on ? 0.30 : 0.07) }
        Text { id: bl; anchors.centerIn: parent; text: i18n.t(label, i18n.lang); color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle { Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Text { text: i18n.t("AI MIX:", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Text { text: page.members + i18n.t(" strips in AUTOMIX · AUTO LEVEL ", i18n.lang) + (engine.autoLevelOn ? i18n.t("ON (", i18n.lang) + (page.tel.autoLevelDb >= 0 ? "+" : "") + page.tel.autoLevelDb.toFixed(1) + i18n.t(" dB)", i18n.lang) : i18n.t("off", i18n.lang)); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Repeater { model: ["CH 01-16", "CH 17-32"]; delegate: Rectangle { id: gb1; implicitWidth: 66; implicitHeight: 20; radius: 7; color: page.bank === index ? Theme.ov(0.16) : Theme.ov(0.06); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb1.color) } GradientStop { position: 1; color: Theme.btnBottom(gb1.color) } } Text { anchors.centerIn: parent; text: modelData; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.bank = index } } }
                Item { Layout.fillWidth: true }
                Btn { label: i18n.t("ALL ON (THIS BANK)", i18n.lang); onClicked: { for (let i = 0; i < 16; ++i) engine.setAutomix(page.bank * 16 + i, true, engine.stripAt(page.bank * 16 + i, engine.stripTick).amWeight) } }
                Btn { label: i18n.t("ALL OFF", i18n.lang); onClicked: { for (let i = 0; i < engine.stripState.length; ++i) engine.setAutomix(i, false, 1) } } } }
        RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 6
                    Head { text: i18n.t("AUTOMIX  (gain sharing, 10 ms attack / 300 ms release, -40 dB floor)", i18n.lang) }
                    GridLayout { columns: 2; columnSpacing: 10; rowSpacing: 4; Layout.fillWidth: true
                        Repeater { model: 16
                            delegate: Rectangle { readonly property int s: page.bank * 16 + index; readonly property var st: engine.stripAt(s, engine.stripTick); readonly property var mt: (engine.meterTick, engine.meterAt(s)); Layout.fillWidth: true; implicitHeight: 34; radius: 6; color: st && st.automix ? Qt.rgba(0.235, 0.784, 0.859, 0.08) : Theme.ov(0.03); border.color: st && st.automix ? Theme.cyan : Theme.lineSoft
                                RowLayout { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 8
                                    Rectangle { width: 36; height: 20; radius: 10; color: st && st.automix ? Theme.cyan : Theme.ov(0.1); Rectangle { width: 14; height: 14; radius: 7; y: 3; x: st && st.automix ? 19 : 3; color: st && st.automix ? "#000" : Theme.dim } MouseArea { anchors.fill: parent; onClicked: engine.setAutomix(s, !(st && st.automix), st ? st.amWeight : 1) } }
                                    Text { Layout.preferredWidth: 80; text: i18n.t("CH ", i18n.lang) + (s + 1 < 10 ? "0" : "") + (s + 1) + " " + (st ? st.name : ""); color: Theme.stripColors[st ? st.color : 0]; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; elide: Text.ElideRight }
                                    Text { text: "W"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
                                    MSlider { Layout.fillWidth: true; from: 0.25; to: 4; stepSize: 0.05; value: st ? st.amWeight : 1; enabled: st && st.automix; onMoved: engine.setAutomix(s, true, value) }
                                    Text { Layout.preferredWidth: 30; text: st ? st.amWeight.toFixed(2) : ""; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7 }
                                    Rectangle { Layout.preferredWidth: 90; implicitHeight: 8; radius: 2; color: Theme.ov(0.06)
                                        Rectangle { readonly property real g: mt ? Math.max(-40, Math.min(0, mt.amGainDb)) : 0; width: parent.width * (1 + g / 40); height: parent.height; radius: 2; color: g > -6 ? Theme.emerald : g > -20 ? Theme.amber : Theme.dim } }
                                    Text { Layout.preferredWidth: 46; text: st && st.automix && mt ? mt.amGainDb.toFixed(1) + i18n.t(" dB", i18n.lang) : "—"; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 7; horizontalAlignment: Text.AlignRight } } } } }
                    Item { Layout.fillHeight: true }
                    Note { text: i18n.t("Switch on the microphones of a panel, a talk show or a conference: the strip with the voice takes the gain, the others drop so the room noise and comb filtering do not add up. WEIGHT favours a strip (the host at 2, guests at 1). The bar and dB show each strip's live share; -40 dB is the floor.", i18n.lang) } } }
            Rectangle { Layout.preferredWidth: 360; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 8
                    RowLayout { Head { text: i18n.t("AUTO LEVEL", i18n.lang) } Item { Layout.fillWidth: true } Btn { label: engine.autoLevelOn ? i18n.t("ON", i18n.lang) : i18n.t("OFF", i18n.lang); on: engine.autoLevelOn; onColor: Theme.emerald; onClicked: engine.autoLevelOn = !engine.autoLevelOn } }
                    Text { text: i18n.t("TARGET", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.letterSpacing: 1 }
                    Flow { Layout.fillWidth: true; spacing: 4
                        Repeater { model: [{ t: "-14 STREAMING", v: -14 }, { t: "-16 PODCAST", v: -16 }, { t: "-18 LIVE", v: -18 }, { t: "-23 BROADCAST", v: -23 }]; delegate: Btn { label: i18n.t(modelData.t ?? "", i18n.lang); on: Math.abs(engine.autoLevelTarget - modelData.v) < 0.01; onColor: Theme.cyan; onClicked: engine.autoLevelTarget = modelData.v } } }
                    RowLayout { spacing: 8; Layout.fillWidth: true
                        MSlider { Layout.fillWidth: true; from: -30; to: -6; stepSize: 0.5; value: engine.autoLevelTarget; onMoved: engine.autoLevelTarget = value }
                        Text { Layout.preferredWidth: 70; text: engine.autoLevelTarget.toFixed(1) + i18n.t(" LUFS", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; horizontalAlignment: Text.AlignRight } }
                    Text { text: i18n.t("MAX LIFT", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.letterSpacing: 1 }
                    RowLayout { spacing: 8; Layout.fillWidth: true
                        MSlider { Layout.fillWidth: true; from: 0; to: 24; stepSize: 1; value: engine.autoLevelMax; onMoved: engine.autoLevelMax = value }
                        Text { Layout.preferredWidth: 70; text: "+" + engine.autoLevelMax.toFixed(0) + i18n.t(" dB", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; horizontalAlignment: Text.AlignRight } }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.lineSoft }
                    GridLayout { columns: 2; columnSpacing: 12; rowSpacing: 4
                        Text { text: i18n.t("GAIN NOW", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Text { text: (page.tel.autoLevelDb >= 0 ? "+" : "") + page.tel.autoLevelDb.toFixed(1) + i18n.t(" dB", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 12; font.bold: true }
                        Text { text: i18n.t("SHORT-TERM", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Text { text: page.tel.shortTerm > -119 ? page.tel.shortTerm.toFixed(1) + i18n.t(" LUFS", i18n.lang) : "—"; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 12; font.bold: true }
                        Text { text: i18n.t("TRUE PEAK", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } Text { text: page.tel.truePeak > -119 ? page.tel.truePeak.toFixed(1) + i18n.t(" dBTP", i18n.lang) : "—"; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 12; font.bold: true } }
                    Note { text: i18n.t("\"No low, but loud\": a slow gain (+1 dB/s up, -6 dB/s down, never more than MAX LIFT, never below -12 dB) steers the 3 s loudness toward the target. The master limiter stays in front of the output, so nothing clips; the LOUDNESS page shows the result. It starts acting 3 s after switch-on, when the window is full.", i18n.lang) }
                    Item { Layout.fillHeight: true } } } }
    }
}
