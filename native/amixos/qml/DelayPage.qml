// DELAY: the time alignment of every channel on one page (its own page, apart from the microphone / TONE pages).
//   LIMIT   the longest delay a channel may take here (5 / 10 / 20 / 50 ms); lowering it pulls longer delays down to it
//   per channel: delay in ms, the same in samples at the current rate and in metres of sound (343 m/s), fine steps of 0.1 ms
// The delay sits at the front of the channel chain (Ø → DLY → GATE → X → EQ → COMP → LIM), so it moves the whole channel in time.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AmixOS

Flickable {
    id: page
    objectName: "delayPage"
    contentHeight: col.implicitHeight + 24; clip: true; boundsBehavior: Flickable.StopAtBounds
    function t(s) { return i18n.t(s, i18n.lang) }
    property real limit: 20
    property var delays: []
    readonly property real rate: engine.sampleRate > 0 ? engine.sampleRate : 48000
    function reload() { const d = []; for (let i = 0; i < engine.strips; ++i) { const p = engine.channelParams(i); d.push(p && p.delayMs !== undefined ? p.delayMs : 0) } delays = d }
    function setLimit(ms) {
        limit = ms
        for (let i = 0; i < delays.length; ++i) if (delays[i] > ms) engine.setDelay(i, ms)
        reload()
    }
    function put(i, ms) { engine.setDelay(i, Math.max(0, Math.min(limit, Math.round(ms * 100) / 100))); reload() }
    function pageShown() { reload(); let mx = 0; for (let i = 0; i < delays.length; ++i) mx = Math.max(mx, delays[i]); limit = mx > 20 ? 50 : mx > 10 ? 20 : mx > 5 ? 10 : 20 }   // also when shown again from the page cache
    Component.onCompleted: pageShown()
    Connections { target: engine; function onStripsChanged() { page.reload() } }

    component Btn: Rectangle { id: bt; property string label; property bool on: false; signal clicked(); implicitWidth: btT.implicitWidth + 20; implicitHeight: 24; radius: 8
        color: bt.on ? Qt.rgba(0.235, 0.784, 0.859, 0.2) : Theme.ov(0.06); border.color: bt.on ? Theme.cyan : Theme.line
        gradient: Gradient { GradientStop { position: 0; color: bt.on ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.34) : Theme.ov(0.11) } GradientStop { position: 1; color: bt.on ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.12) : Theme.ov(0.035) } }
        Rectangle { anchors.fill: parent; anchors.margins: 1; radius: Math.max(0, parent.radius - 1); color: "transparent"; border.width: 1; border.color: Qt.rgba(1, 1, 1, bt.on ? 0.20 : 0.06) }
        Text { id: btT; anchors.centerIn: parent; text: bt.label; color: bt.on ? Theme.cyan : Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: bt.clicked() } }

    ColumnLayout {
        id: col
        x: 12; y: 12; width: page.width - 24; spacing: 12

        Rectangle {
            Layout.fillWidth: true; implicitHeight: head.implicitHeight + 28; radius: Theme.radius; color: Theme.card; border.color: Theme.line
            ColumnLayout {
                id: head; anchors.fill: parent; anchors.margins: 14; spacing: 8
                RowLayout {
                    Layout.fillWidth: true; spacing: 10
                    Text { text: page.t("DELAY · TIME ALIGNMENT"); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 13; font.bold: true; font.letterSpacing: 1.5 }
                    Item { Layout.fillWidth: true }
                    Text { text: page.t("LIMIT"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                    Repeater { model: [5, 10, 20, 50]; Btn { required property int modelData; label: modelData + " ms"; on: page.limit === modelData; onClicked: page.setLimit(modelData) } }
                    Btn { label: page.t("ZERO ALL"); onClicked: { for (let i = 0; i < page.delays.length; ++i) if (page.delays[i] > 0) engine.setDelay(i, 0); page.reload() } }
                }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.pixelSize: 10
                       text: page.t("Delay each channel so sources at different distances arrive together: 1 ms is about 34 cm of sound. The delay is at the front of the channel, before gate, EQ and dynamics. Lowering the limit pulls longer delays down to it.") }
            }
        }

        GridLayout {
            Layout.fillWidth: true; columns: page.width > 1100 ? 2 : 1; columnSpacing: 12; rowSpacing: 6
            Repeater {
                model: page.delays.length
                Rectangle {
                    id: row
                    required property int index
                    readonly property real ms: page.delays[index] !== undefined ? page.delays[index] : 0
                    readonly property var st: engine.stripAt(index, engine.stripTick)
                    Layout.fillWidth: true; implicitHeight: 40; radius: 6; color: Theme.card; border.color: row.ms > 0 ? Theme.cyan : Theme.line
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 8
                        Text { text: page.t("CH ") + (row.index + 1 < 10 ? "0" : "") + (row.index + 1); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; Layout.preferredWidth: 42 }
                        Text { text: row.st && row.st.name ? row.st.name : ""; color: Theme.muted; font.pixelSize: 9; elide: Text.ElideRight; Layout.preferredWidth: 90 }
                        Btn { label: "−"; onClicked: page.put(row.index, row.ms - 0.1) }
                        MSlider { Layout.fillWidth: true; from: 0; to: page.limit; stepSize: 0.01; value: Math.min(row.ms, page.limit)
                                 Accessible.name: page.t("Delay of channel ") + (row.index + 1); onMoved: page.put(row.index, value) }
                        Btn { label: "+"; onClicked: page.put(row.index, row.ms + 0.1) }
                        Text { Layout.preferredWidth: 150; horizontalAlignment: Text.AlignRight; font.family: Theme.mono; font.pixelSize: 10
                               color: row.ms > 0 ? Theme.cyan : Theme.dim
                               text: row.ms.toFixed(2) + " ms · " + Math.round(row.ms * page.rate / 1000) + page.t(" smp") + " · " + (row.ms * 0.343).toFixed(2) + " m" }
                    }
                }
            }
        }
    }
}
