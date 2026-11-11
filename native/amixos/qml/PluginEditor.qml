// The editor window of a rack plug-in, drawn like a piece of hardware: a faceplate in the effect's own colours, rotary knobs for every
// parameter, a power lamp, a bypass switch and (on the tube amp) valves that glow with the gain. Hosted VST plug-ins get the same window
// with one knob per parameter. Drag the title to move it.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: ed
    objectName: "plugEd"
    property int strip: 0
    property int slot: 0
    property string kind: ""
    property string title: ""
    property var params: []
    property bool bypass: false
    property int rev: 0
    readonly property string fx: kind.indexOf("fx:") === 0 ? kind.substring(3) : ""
    // the faceplate of each effect: [top, bottom, accent, knob cap, lettering]
    readonly property var skin: ({ tubeamp: ["#3a2a18", "#17100a", "#f5b942", "#efe6cf", "#f6ead0"], preamp: ["#565c66", "#2a2e35", "#ef4444", "#15181d", "#f1f5f9"], postamp: ["#4b5563", "#1f2937", "#38bdf8", "#15181d", "#f1f5f9"],
        hall: ["#14285a", "#070d22", "#7dd3fc", "#0c1226", "#e0f2fe"], room: ["#1d3b3a", "#0a1717", "#5eead4", "#0c1a1a", "#ccfbf1"], reverb: ["#1e2a52", "#0a0f24", "#93c5fd", "#0c1226", "#dbeafe"],
        delay: ["#3b2f14", "#171204", "#fbbf24", "#1c1606", "#fef3c7"], chorus: ["#2a1d4d", "#0f0a20", "#c4b5fd", "#120c26", "#ede9fe"], flanger: ["#3b1747", "#16081b", "#f0abfc", "#170a1c", "#fae8ff"], phaser: ["#3d2410", "#170d05", "#fb923c", "#1a0f06", "#ffedd5"],
        tremolo: ["#14402a", "#06170f", "#4ade80", "#081a11", "#dcfce7"], wah: ["#44403c", "#1c1917", "#facc15", "#0c0a09", "#fef9c3"], crusher: ["#1f2937", "#030712", "#22d3ee", "#020617", "#cffafe"], exciter: ["#4a1d1d", "#1a0808", "#fda4af", "#1c0a0a", "#ffe4e6"], drive: ["#4a2412", "#1a0b05", "#fb7185", "#1d0d07", "#ffe4e6"] })
    readonly property var sk: skin[fx] || ["#2b3038", "#14171c", "#a78bfa", "#15181d", "#e5e7eb"]
    function openFor(strip_, slot_, kind_, title_) { strip = strip_; slot = slot_; kind = kind_; title = title_; reload(); x = Math.max(8, (parent.width - width) / 2); y = Math.max(8, (parent.height - height) / 2); open() }
    function reload() { params = engine.pluginParams(strip, slot); const p = engine.plugins(strip)[slot]; bypass = p ? p.bypass : false; rev++ }
    function setParam(i, v) { engine.setPluginParam(strip, slot, params[i].id, v); const t = engine.pluginParams(strip, slot); if (t.length === params.length) params = t }
    readonly property int cols: Math.min(6, Math.max(3, params.length <= 6 ? params.length : Math.ceil(params.length / Math.ceil(params.length / 6))))
    width: Math.max(420, cols * 96 + 56); height: Math.min(parent.height - 16, 150 + Math.ceil(Math.max(1, params.length) / cols) * 106 + (fx === "tubeamp" ? 56 : 0))
    modal: false; closePolicy: Popup.CloseOnEscape; padding: 0
    background: Rectangle { radius: 14; border.color: "#000"; border.width: 2; gradient: Gradient { GradientStop { position: 0; color: ed.sk[0] } GradientStop { position: 1; color: ed.sk[1] } }
        Rectangle { anchors.fill: parent; anchors.margins: 6; radius: 10; color: "transparent"; border.color: Qt.rgba(1, 1, 1, 0.10) }
        Repeater { model: 4; delegate: Rectangle { width: 10; height: 10; radius: 5; x: index % 2 ? parent.width - 22 : 12; y: index < 2 ? 12 : parent.height - 22; color: "#9ca3af"; border.color: "#111"; Rectangle { anchors.centerIn: parent; width: 7; height: 1.5; color: "#374151"; rotation: 35 + index * 40 } } } }
    contentItem: Item {
        MouseArea { height: 52; width: parent.width; property point p0; cursorShape: Qt.SizeAllCursor; onPressed: function (m) { p0 = Qt.point(m.x, m.y) } onPositionChanged: function (m) { if (pressed) { ed.x += m.x - p0.x; ed.y += m.y - p0.y } } }
        RowLayout { x: 30; y: 14; width: parent.width - 60; spacing: 10
            Rectangle { width: 12; height: 12; radius: 6; color: ed.bypass ? "#3f1d1d" : "#ef4444"; border.color: "#000"; Rectangle { anchors.centerIn: parent; width: 22; height: 22; radius: 11; color: "#ef4444"; opacity: ed.bypass ? 0 : 0.25 } }   // the power lamp
            Column { spacing: 0
                Text { text: ed.title.toUpperCase(); color: ed.sk[4]; font.family: Theme.mono; font.pixelSize: 17; font.weight: Font.Black; font.letterSpacing: 4 }
                Text { text: (ed.fx ? "AMFB AUDIO" : i18n.t("HOSTED PLUG-IN", i18n.lang)) + "  ·  CH " + (ed.strip + 1 < 10 ? "0" : "") + (ed.strip + 1) + "  ·  " + i18n.t("SLOT ", i18n.lang) + (ed.slot + 1); color: ed.sk[2]; font.family: Theme.mono; font.pixelSize: 7; font.letterSpacing: 2 } }
            Item { Layout.fillWidth: true }
            Rectangle { implicitWidth: 74; implicitHeight: 24; radius: 12; color: ed.bypass ? "#111827" : ed.sk[2]; border.color: "#000"   // the bypass switch
                Text { anchors.centerIn: parent; text: ed.bypass ? i18n.t("BYPASSED", i18n.lang) : i18n.t("ACTIVE", i18n.lang); color: ed.bypass ? "#9ca3af" : "#000"; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                MouseArea { anchors.fill: parent; onClicked: { engine.setPluginBypass(ed.strip, ed.slot, !ed.bypass); ed.bypass = !ed.bypass } } }
            Rectangle { implicitWidth: 24; implicitHeight: 24; radius: 12; color: "#111827"; border.color: "#000"; Text { anchors.centerIn: parent; text: "✕"; color: "#e5e7eb"; font.pixelSize: 11 } MouseArea { anchors.fill: parent; onClicked: ed.close() } } }
        Row { visible: ed.fx === "tubeamp"; x: (parent.width - width) / 2; y: 58; spacing: 26   // the valves: they glow with the GAIN
            Repeater { model: 3; delegate: Item { width: 34; height: 46
                    readonly property real heat: ed.bypass ? 0.08 : 0.3 + 0.7 * (ed.params.length ? ed.params[0].value : 0.4)
                    Rectangle { anchors.centerIn: parent; width: 60; height: 60; radius: 30; color: "#f59e0b"; opacity: parent.heat * 0.22 }
                    Rectangle { anchors.horizontalCenter: parent.horizontalCenter; y: 2; width: 24; height: 38; radius: 11; color: Qt.rgba(1, 0.85, 0.6, 0.10); border.color: Qt.rgba(1, 1, 1, 0.35)
                        Rectangle { anchors.horizontalCenter: parent.horizontalCenter; y: 9; width: 8; height: 22; radius: 3; color: Qt.rgba(1, 0.45 + 0.3 * parent.parent.heat, 0.1, 0.35 + 0.65 * parent.parent.heat) } }
                    Rectangle { anchors.horizontalCenter: parent.horizontalCenter; y: 38; width: 28; height: 7; radius: 2; color: "#111" } } } }
        Flickable { x: 28; y: ed.fx === "tubeamp" ? 116 : 62; width: parent.width - 56; height: parent.height - y - 40; contentHeight: grid.implicitHeight; clip: true
            Grid { id: grid; columns: ed.cols; columnSpacing: (parent.width - ed.cols * 80) / Math.max(1, ed.cols - 1) - 0.01; rowSpacing: 8
                Repeater { model: ed.params.length
                    delegate: Knob { width: 80; size: 58; tint: ed.sk[2]; cap: ed.sk[3]; ink: ed.sk[4]
                        label: ed.params[index] ? ed.params[index].title : ""; valueText: ed.params[index] ? ed.params[index].text : ""; defaultValue: ed.params[index] ? ed.params[index]["default"] : 0.5
                        property int seen: -1; readonly property int rev: ed.rev; onRevChanged: sync(); Component.onCompleted: sync()
                        function sync() { if (ed.params[index]) value = ed.params[index].value }
                        onMoved: function (v) { ed.setParam(index, v) } } } } }
        Text { x: 30; y: parent.height - 30; text: i18n.t("drag a knob up or down · Shift = fine · double-click = default · wheel", i18n.lang); color: Qt.rgba(1, 1, 1, 0.35); font.family: Theme.mono; font.pixelSize: 7 }
        Text { anchors.right: parent.right; anchors.rightMargin: 30; y: parent.height - 30; text: ed.fx ? "MADE BY AMFB AUDIO" : ""; color: Qt.rgba(1, 1, 1, 0.35); font.family: Theme.mono; font.pixelSize: 7; font.letterSpacing: 2 }
    }
}
