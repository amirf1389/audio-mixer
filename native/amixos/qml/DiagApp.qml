// AMF X1 DIAGNOSTICS: the separate check program (Diag.cpp). The system check with a fix for anything not right, every output (speakers,
// headphones ...) with TEST TONE, every input (microphones ...) with LISTEN and a live meter, the ASIO drivers, and the report.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AmixOS

ApplicationWindow {
    id: win
    visible: true; width: 1180; height: 820; minimumWidth: 760; minimumHeight: 520
    title: "AmF X1 Diagnostics · " + diag.system.app
    color: Theme.obsidian
    Component.onCompleted: diag.refresh()
    function lvlColor(l) { return l === "ok" ? Theme.emerald : l === "warn" ? Theme.amber : Theme.danger }
    function stateColor(s) { return s === "active" ? Theme.emerald : s === "unplugged" ? Theme.amber : Theme.dim }
    component Key: Rectangle { id: kk; property string label; property string icon; property bool lit: false; property color hue: Theme.accent; signal clicked()
        implicitWidth: kRow.implicitWidth + 22; implicitHeight: 30; radius: 15; opacity: enabled ? 1 : 0.45
        color: lit ? Theme.harm(hue) : (kH.hovered ? Theme.ov(0.10) : Theme.ov(0.05)); border.color: lit ? Theme.harm(hue) : Theme.lineSoft
        Row { id: kRow; anchors.centerIn: parent; spacing: 6
            Icon { visible: kk.icon !== ""; width: 14; height: 14; anchors.verticalCenter: parent.verticalCenter; name: kk.icon; color: kk.lit ? "#0b0f14" : Theme.muted; stroke: 2 }
            Text { anchors.verticalCenter: parent.verticalCenter; text: kk.label; color: kk.lit ? "#0b0f14" : Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } }
        HoverHandler { id: kH; cursorShape: Qt.PointingHandCursor }
        MouseArea { anchors.fill: parent; onClicked: kk.clicked() } }
    component Card: Rectangle { default property alias body: cc.data; property string title; property string icon; property color hue: Theme.cyan
        Layout.fillWidth: true; implicitHeight: cc.implicitHeight + 50; radius: 14; color: Theme.card; border.color: Theme.lineSoft
        Row { x: 14; y: 12; spacing: 8; Icon { width: 18; height: 18; name: parent.parent.icon; color: Theme.harm(parent.parent.hue) }
            Text { text: parent.parent.title; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 2 } }
        ColumnLayout { id: cc; x: 14; y: 40; width: parent.width - 28; spacing: 6 } }
    component Endpoint: Rectangle { id: ep; property var m; property bool isIn: false
        Layout.fillWidth: true; implicitHeight: 48; radius: 10; color: diag.testing === m.id ? Qt.rgba(Theme.cyan.r, Theme.cyan.g, Theme.cyan.b, 0.12) : Theme.ov(0.03); border.color: Theme.lineSoft
        RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 10; spacing: 10
            Icon { width: 20; height: 20; name: ep.isIn ? (ep.m.kind === "microphone" || ep.m.kind === "headset" ? "radio" : "plug") : (ep.m.kind === "headphones" || ep.m.kind === "headset" ? "sine" : ep.m.kind === "digital" ? "card" : "amp"); color: Theme.muted }
            ColumnLayout { Layout.fillWidth: true; spacing: 1
                Text { Layout.fillWidth: true; text: (ep.m.isDefault ? "★ " : "") + ep.m.name; color: ep.m.state === "active" ? Theme.ink : Theme.muted; font.family: Theme.sans; font.pixelSize: 12; font.bold: true; elide: Text.ElideRight }
                Text { text: ep.m.kind.toUpperCase() + (ep.m.channels ? " · " + ep.m.channels + " ch · " + ep.m.rate + " Hz" : ""); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 } }
            Rectangle { implicitWidth: stT.implicitWidth + 14; implicitHeight: 20; radius: 10; color: "transparent"; border.color: win.stateColor(ep.m.state)
                Text { id: stT; anchors.centerIn: parent; text: ep.m.state.toUpperCase(); color: win.stateColor(ep.m.state); font.family: Theme.mono; font.pixelSize: 8; font.bold: true } }
            Rectangle { visible: ep.isIn && diag.testing === ep.m.id; implicitWidth: 120; implicitHeight: 8; radius: 4; color: Theme.deep
                Rectangle { height: parent.height; radius: 4; width: parent.width * Math.max(0, Math.min(1, (diag.level + 60) / 60)); color: diag.level > -3 ? Theme.danger : diag.level > -18 ? Theme.amber : Theme.emerald } }
            Key { objectName: (ep.isIn ? "listen_" : "test_") + index; enabled: ep.m.state === "active"; label: ep.isIn ? (diag.testing === ep.m.id ? "STOP" : "LISTEN") : (diag.testing === ep.m.id ? "PLAYING" : "TEST TONE"); icon: ep.isIn ? "radio" : "play"; lit: diag.testing === ep.m.id; hue: Theme.cyan
                onClicked: { if (diag.testing === ep.m.id) diag.stop(); else if (ep.isIn) diag.listen(ep.m.id); else diag.testOutput(ep.m.id) } } } }

    Aurora { anchors.fill: parent; z: -1; motion: true; accent: Theme.cyan }
    Flickable {
        anchors.fill: parent; contentHeight: col.implicitHeight + 32; clip: true
        ColumnLayout {
            id: col; width: parent.width - 40; x: 20; y: 20; spacing: 14
            RowLayout { Layout.fillWidth: true; spacing: 14
                Image { source: "qrc:/qt/qml/AmixOS/icons/amfx1.png"; Layout.preferredWidth: 56; Layout.preferredHeight: 56; smooth: true; mipmap: true }
                ColumnLayout { spacing: 2
                    Text { text: "AmF X1 Diagnostics"; color: Theme.ink; font.family: Theme.sans; font.pixelSize: 24; font.weight: Font.Black }
                    Text { text: diag.system.os + " · " + diag.system.cpu + " · " + diag.system.cores + " cores · engine " + diag.system.backends; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 } }
                Item { Layout.fillWidth: true }
                Key { objectName: "diagRefresh"; label: diag.busy ? "CHECKING..." : "RUN CHECK"; icon: "refresh"; lit: true; hue: Theme.cyan; enabled: !diag.busy; onClicked: diag.refresh() }
                Key { label: "SOUND SETTINGS"; icon: "sliders"; onClicked: diag.openSoundSettings() }
                Key { label: "OPEN CONSOLE"; icon: "mixer"; onClicked: diag.openConsole() } }
            Text { visible: diag.message !== ""; Layout.fillWidth: true; text: diag.message; color: Theme.cyan; font.family: Theme.sans; font.pixelSize: 11; wrapMode: Text.WordWrap }
            Card { title: "SYSTEM CHECK"; icon: "pulse"; hue: Theme.emerald
                Repeater { model: diag.checks
                    delegate: RowLayout { Layout.fillWidth: true; spacing: 10
                        Rectangle { width: 10; height: 10; radius: 5; color: win.lvlColor(modelData.level); Layout.alignment: Qt.AlignTop; Layout.topMargin: 4 }
                        ColumnLayout { Layout.fillWidth: true; spacing: 1
                            Text { Layout.fillWidth: true; text: modelData.name + " · " + modelData.detail; color: Theme.ink; font.family: Theme.sans; font.pixelSize: 11; wrapMode: Text.WordWrap }
                            Text { visible: modelData.fix !== ""; Layout.fillWidth: true; text: "→ " + modelData.fix; color: Theme.amber; font.family: Theme.sans; font.pixelSize: 10; wrapMode: Text.WordWrap } } } } }
            RowLayout { Layout.fillWidth: true; spacing: 14
                Card { Layout.alignment: Qt.AlignTop; title: "OUTPUTS · SPEAKERS, HEADPHONES"; icon: "amp"; hue: Theme.cyan
                    Repeater { model: diag.outputs; delegate: Endpoint { m: modelData; isIn: false } }
                    Text { visible: diag.outputs.length === 0; text: "No output found."; color: Theme.muted; font.pixelSize: 11 } }
                Card { Layout.alignment: Qt.AlignTop; title: "INPUTS · MICROPHONES, LINE"; icon: "radio"; hue: Theme.violet
                    Repeater { model: diag.inputs; delegate: Endpoint { m: modelData; isIn: true } }
                    Text { visible: diag.inputs.length === 0; text: "No input found."; color: Theme.muted; font.pixelSize: 11 } } }
            Card { title: "ASIO DRIVERS"; icon: "card"; hue: Theme.amber
                Repeater { model: diag.asio
                    delegate: RowLayout { Layout.fillWidth: true; spacing: 10
                        Text { Layout.fillWidth: true; text: (modelData.maker ? modelData.maker + " · " : "") + modelData.name + "  ·  " + modelData.type.toUpperCase() + "  ·  " + modelData.in + " in / " + modelData.out + " out"; color: Theme.ink; font.family: Theme.sans; font.pixelSize: 11; elide: Text.ElideRight }
                        Text { text: modelData.status.toUpperCase(); color: modelData.status === "ready" ? Theme.emerald : modelData.status === "virtual" ? Theme.dim : Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } } }
                Text { visible: diag.asio.length === 0; text: "No ASIO driver installed: WASAPI is used, which covers speakers, headphones and USB microphones."; color: Theme.muted; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true } }
            Card { title: "REPORT"; icon: "list"; hue: Theme.pink
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.pixelSize: 11; text: "A plain-text summary of everything above, for your own records or for support. Nothing is sent: you choose where it goes." }
                RowLayout { spacing: 8
                    Key { objectName: "diagSave"; label: "SAVE REPORT"; icon: "folder"; lit: true; hue: Theme.pink; onClicked: diag.saveReport("") }
                    Key { label: "COPY REPORT"; icon: "list"; onClicked: diag.copyReport() } } }
        }
    }
}
