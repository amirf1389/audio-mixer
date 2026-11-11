// The editor window of a rack plug-in, drawn as a vintage rack unit: a wooden frame, a cream faceplate, the controls in outlined
// sections with their titles, black skirted knobs with scale marks, a PROGRAM window with - / + for the presets, an A / B selector, a lamp
// with an ON / OFF switch (bypass) and a needle meter for the input level. Our own design in that tradition; no other maker's name, badge
// or panel is copied. Hosted VST plug-ins get the same panel with their parameters in sections of four. Drag the name plate to move it.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls

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
    property var presets: []
    property int presetAt: -1
    property var stateA: null
    property var stateB: null
    property string side: "A"
    property real level: -120
    property real needle: -40
    readonly property string fx: kind.indexOf("fx:") === 0 ? kind.substring(3) : ""
    readonly property color paper: "#e8e3d3"
    readonly property color paperLo: "#d6d0be"
    readonly property color ink: "#1b1b1b"
    readonly property color red: "#b91c1c"
    // the sections of each panel: [title, [parameter indices]] (the last parameter of a built-in effect is MIX: it lives in BLEND)
    readonly property var layouts: ({
        tubeamp: [["PRE AMP", [0]], ["TONE STACK", [1, 2, 3]], ["POWER AMP", [4, 5]]], preamp: [["INPUT", [0, 1]], ["COLOUR", [2]], ["OUTPUT", [3]]], postamp: [["OUTPUT", [0]], ["TONE", [1]], ["PROTECT", [2]]],
        hall: [["SPACE", [0, 1]], ["DAMPING", [2]], ["PRE DELAY", [3]], ["MOD", [4]]], room: [["SPACE", [0, 1]], ["DAMPING", [2]], ["REFLECTIONS", [3]]], reverb: [["REVERBERATION", [0, 1]], ["PRE DELAY", [2]], ["STEREO", [3]]],
        delay: [["ECHO", [0, 1]], ["TONE", [2]], ["STEREO", [3]]], chorus: [["MOD", [0, 1]], ["STEREO", [2]]], flanger: [["MOD", [0, 1]], ["REGEN", [2]]], phaser: [["MOD", [0, 1]], ["REGEN", [2]]], tremolo: [["MOD", [0, 1]], ["WAVE", [2]]],
        wah: [["FOLLOWER", [0, 1]], ["FILTER", [2]]], crusher: [["DIGITAL", [0, 1]], ["FILTER", [2]]], exciter: [["HARMONICS", [0, 2]], ["TUNE", [1]]], drive: [["VALVE", [0]], ["TONE", [1]], ["OUTPUT", [2]]] })
    readonly property var sections: { const n = params.length; if (!n) return []; const l = layouts[fx]; if (l) return l
        const out = [], m = Math.min(n, 24); for (let i = 0; i < m; i += 4) { const idx = []; for (let j = i; j < Math.min(m, i + 4); j++) idx.push(j); out.push([(i + 1) + " – " + Math.min(m, i + 4), idx]) } return out }
    readonly property int mixIndex: fx && params.length ? params.length - 1 : -1
    function openFor(strip_, slot_, kind_, title_) { strip = strip_; slot = slot_; kind = kind_; title = title_; presets = engine.rackPresets(kind_); presetAt = -1; stateA = null; stateB = null; side = "A"; reload(); x = Math.max(4, (parent.width - width) / 2); y = Math.max(4, (parent.height - height) / 2); open() }
    function reload() { params = engine.pluginParams(strip, slot); const p = engine.plugins(strip)[slot]; bypass = p ? p.bypass : false; rev++ }
    function setParam(i, v) { engine.setPluginParam(strip, slot, params[i].id, v); const t = engine.pluginParams(strip, slot); if (t.length === params.length) params = t; presetAt = -1 }
    function snapshot() { const a = []; for (let i = 0; i < params.length; i++) a.push(params[i].value); return a }
    function restore(a) { if (!a) return; for (let i = 0; i < params.length && i < a.length; i++) engine.setPluginParam(strip, slot, params[i].id, a[i]); reload() }
    function pick(s) { if (s === side) return; if (side === "A") stateA = snapshot(); else stateB = snapshot(); side = s; restore(s === "A" ? stateA : stateB) }
    function preset(step) { if (!presets.length) return; presetAt = (presetAt + step + presets.length) % presets.length; engine.applyRackPreset(strip, slot, presets[presetAt].id); const at = presetAt; reload(); presetAt = at }
    readonly property real fit: Math.min(1, (parent ? parent.width - 12 : 1200) / (face.implicitWidth + 36))
    width: (face.implicitWidth + 36) * fit; height: (panelH + 36) * fit
    readonly property int panelH: 262
    modal: false; closePolicy: Popup.CloseOnEscape; padding: 0
    Timer { interval: 40; running: ed.visible; repeat: true; onTriggered: { const m = engine.meters[ed.strip]; const l = m ? (m.inDb !== undefined ? m.inDb : m.peakDb) : -120; ed.level = l; const tgt = ed.bypass ? -40 : Math.max(-40, Math.min(3, l + 12)); ed.needle += (tgt - ed.needle) * (tgt > ed.needle ? 0.35 : 0.12); vu.requestPaint() } }
    background: Item {}
    component Section: Rectangle { property string head; default property alias body: inner.data; height: ed.panelH - 16; color: "transparent"; border.color: ed.ink; border.width: 1; radius: 7
        Text { id: st; anchors.horizontalCenter: parent.horizontalCenter; y: 7; text: parent.head; color: ed.ink; font.family: Theme.sans; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1 }
        Item { id: inner; x: 0; y: 26; width: parent.width; height: parent.height - 26 } }
    component Key: Rectangle { property string label; signal clicked(); width: 22; height: 20; radius: 3; color: ka.pressed ? "#3a3a3a" : "#161616"; border.color: "#000"
        Text { anchors.centerIn: parent; text: label; color: "#f5f5f0"; font.pixelSize: 13; font.bold: true } MouseArea { id: ka; anchors.fill: parent; onClicked: parent.clicked() } }
    contentItem: Item {
        Rectangle { id: wood; width: face.implicitWidth + 36; height: ed.panelH + 36; transformOrigin: Item.TopLeft; scale: ed.fit; radius: 5   // the wooden frame
            gradient: Gradient { GradientStop { position: 0; color: "#9a6a36" } GradientStop { position: 0.5; color: "#7d5225" } GradientStop { position: 1; color: "#8c5d2c" } } border.color: "#3b2410"; border.width: 2
            Rectangle { x: 14; y: 14; width: parent.width - 28; height: parent.height - 28; radius: 4; border.color: "#8d897c"   // the faceplate
                gradient: Gradient { GradientStop { position: 0; color: ed.paper } GradientStop { position: 1; color: ed.paperLo } }
                Repeater { model: 4; delegate: Rectangle { width: 9; height: 9; radius: 4.5; x: index % 2 ? parent.width - 14 : 5; y: index < 2 ? 5 : parent.height - 14; color: "#8a8a84"; border.color: "#44443f"; Rectangle { anchors.centerIn: parent; width: 6; height: 1.3; color: "#3a3a36"; rotation: 20 + index * 50 } } }
                Row { id: face; x: 8; y: 8; spacing: 6
                    // PROGRAM (presets) + the A / B selector
                    Section { head: ed.presets.length ? "PROGRAM" : "SETTING"; width: 138
                        Rectangle { visible: ed.presets.length > 0; x: 10; y: 4; width: parent.width - 20; height: 62; radius: 5; color: "#0e0e0e"; border.color: "#000"
                            Rectangle { x: 6; y: 5; width: parent.width - 12; height: 28; radius: 2; gradient: Gradient { GradientStop { position: 0; color: "#f3e9b8" } GradientStop { position: 1; color: "#d9c98a" } }
                                Text { anchors.centerIn: parent; width: parent.width - 8; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; text: ed.presetAt >= 0 ? ed.presets[ed.presetAt].name : "—  " + ed.presets.length + "  —"; color: "#2a2410"; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                                Rectangle { y: parent.height / 2; width: parent.width; height: 1; color: ed.red; opacity: 0.5 } }
                            Key { x: 22; y: 38; label: "–"; onClicked: ed.preset(-1) } Key { x: parent.width - 44; y: 38; label: "+"; onClicked: ed.preset(1) } }
                        Text { visible: ed.presets.length === 0; x: 10; y: 16; width: parent.width - 20; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter; text: "CH " + (ed.strip + 1) + "\nSLOT " + (ed.slot + 1); color: ed.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; y: 84; text: "SELECT"; color: ed.ink; font.family: Theme.sans; font.pixelSize: 10; font.weight: Font.DemiBold }
                        Item { anchors.horizontalCenter: parent.horizontalCenter; y: 102; width: 90; height: 96   // the A / B selector: a pointer knob
                            Text { x: 6; y: 0; text: "A"; color: ed.side === "A" ? ed.red : ed.ink; font.family: Theme.sans; font.pixelSize: 12; font.bold: true } Text { x: 74; y: 0; text: "B"; color: ed.side === "B" ? ed.red : ed.ink; font.family: Theme.sans; font.pixelSize: 12; font.bold: true }
                            Rectangle { anchors.horizontalCenter: parent.horizontalCenter; y: 22; width: 54; height: 54; radius: 27; color: "#101010"; border.color: "#000"
                                Rectangle { anchors.centerIn: parent; width: 16; height: 56; radius: 6; color: "#1d1d1d"; border.color: "#3a3a3a"; rotation: ed.side === "A" ? -32 : 32; Behavior on rotation { NumberAnimation { duration: 120 } } Rectangle { anchors.horizontalCenter: parent.horizontalCenter; y: 3; width: 3; height: 16; radius: 1; color: "#f5f5f0" } } }
                            MouseArea { anchors.fill: parent; onClicked: ed.pick(ed.side === "A" ? "B" : "A") } } }
                    // the sections of the effect
                    Repeater { model: ed.sections
                        delegate: Section { head: modelData[0]; readonly property var idx: modelData[1]; readonly property int cols: Math.ceil(idx.length / 2); width: Math.max(96, cols * 86 + 14)
                            Grid { anchors.horizontalCenter: parent.horizontalCenter; y: idx.length <= 1 ? 54 : idx.length <= cols ? 54 : 2; columns: cols; columnSpacing: 2; rowSpacing: 2
                                Repeater { model: idx
                                    delegate: Column { width: 84; spacing: 0; readonly property int pi: modelData
                                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: ed.params[pi] ? ed.params[pi].title : ""; color: ed.ink; font.family: Theme.sans; font.pixelSize: 10; font.weight: Font.DemiBold }
                                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: ed.params[pi] ? ed.params[pi].text : ""; color: ed.ink; font.family: Theme.sans; font.pixelSize: 9 }
                                        Knob { anchors.horizontalCenter: parent.horizontalCenter; width: 84; size: 50; vintage: true; paper: ed.ink; minText: "0"; maxText: "10"; label: ""; valueText: ""
                                            defaultValue: ed.params[pi] ? ed.params[pi]["default"] : 0.5; readonly property int rev: ed.rev; onRevChanged: sync(); Component.onCompleted: sync()
                                            function sync() { if (ed.params[pi]) value = ed.params[pi].value } onMoved: function (v) { ed.setParam(pi, v) } height: 70 } } } } } }
                    // the name plate, the lamp and the ON / OFF switch
                    Item { width: 150; height: ed.panelH - 16
                        MouseArea { anchors.fill: parent; property point p0; cursorShape: Qt.SizeAllCursor; onPressed: function (m) { p0 = Qt.point(m.x, m.y) } onPositionChanged: function (m) { if (pressed) { ed.x += (m.x - p0.x) * ed.fit; ed.y += (m.y - p0.y) * ed.fit } } }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; y: 6; text: "AMFB  X1"; color: ed.ink; font.family: Theme.sans; font.pixelSize: 17; font.weight: Font.Bold; font.letterSpacing: 2 }
                        Rectangle { anchors.horizontalCenter: parent.horizontalCenter; y: 34; width: 26; height: 26; radius: 2; color: ed.red; border.color: "#6b0f0f"; Text { anchors.centerIn: parent; text: "A"; color: "#f3e9b8"; font.family: Theme.sans; font.pixelSize: 15; font.weight: Font.Black } }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; y: 68; width: parent.width - 6; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; text: ed.title.toUpperCase(); color: ed.ink; font.family: Theme.sans; font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 1 }
                        Rectangle { anchors.horizontalCenter: parent.horizontalCenter; y: 112; width: 46; height: 46; radius: 23; color: "#ef4444"; opacity: ed.bypass ? 0 : 0.22 }
                        Rectangle { anchors.horizontalCenter: parent.horizontalCenter; y: 123; width: 24; height: 24; radius: 12; border.color: "#3b0a0a"; border.width: 2   // the lamp
                            gradient: Gradient { GradientStop { position: 0; color: ed.bypass ? "#5b1414" : "#ff6b6b" } GradientStop { position: 1; color: ed.bypass ? "#3a0c0c" : "#dc2626" } } }
                        Row { anchors.horizontalCenter: parent.horizontalCenter; y: 166; spacing: 8
                            Text { anchors.verticalCenter: parent.verticalCenter; text: "OFF"; color: ed.ink; font.family: Theme.sans; font.pixelSize: 9; font.weight: Font.DemiBold }
                            Rectangle { width: 34; height: 22; radius: 11; color: "#bdb8a8"; border.color: "#5a574d"   // the toggle
                                Rectangle { width: 12; height: 26; radius: 5; y: -2; x: ed.bypass ? 4 : 18; color: "#d9d9d6"; border.color: "#55554f"; Behavior on x { NumberAnimation { duration: 90 } } }
                                MouseArea { anchors.fill: parent; anchors.margins: -6; onClicked: { engine.setPluginBypass(ed.strip, ed.slot, !ed.bypass); ed.bypass = !ed.bypass } } }
                            Text { anchors.verticalCenter: parent.verticalCenter; text: "ON"; color: ed.ink; font.family: Theme.sans; font.pixelSize: 9; font.weight: Font.DemiBold } }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; y: 204; horizontalAlignment: Text.AlignHCenter; text: "AMFB AUDIO\n" + (ed.fx ? "CH " + (ed.strip + 1) + " · SLOT " + (ed.slot + 1) : (ed.kind.indexOf("vst2:") === 0 ? "VST2" : "VST3") + " HOSTED"); color: ed.ink; font.family: Theme.sans; font.pixelSize: 9; font.letterSpacing: 2; lineHeight: 1.2 } }
                    // the meter and BLEND
                    Column { spacing: 6; width: 150
                        Rectangle { width: 150; height: 104; radius: 5; color: "#77736a"; border.color: "#3e3c36"   // the needle meter (input level; 0 = -12 dBFS)
                            Canvas { id: vu; x: 6; y: 6; width: 138; height: 92
                                onPaint: { const c = getContext("2d"), w = width, h = height; c.clearRect(0, 0, w, h); const g = c.createRadialGradient(w / 2, h * 0.9, 4, w / 2, h * 0.6, w * 0.8); g.addColorStop(0, ed.bypass ? "#cfc79a" : "#fff3b8"); g.addColorStop(1, ed.bypass ? "#a39b73" : "#e2c873"); c.fillStyle = g; c.fillRect(0, 0, w, h)
                                    const cx = w / 2, cy = h * 1.05, r = h * 0.86, a0 = Math.PI * 1.25, a1 = Math.PI * 1.75, at = function (db) { return a0 + (a1 - a0) * (Math.max(-40, Math.min(3, db)) + 40) / 43 }
                                    c.lineWidth = 2; c.strokeStyle = "#1b1b1b"; c.beginPath(); c.arc(cx, cy, r, a0, at(0)); c.stroke(); c.strokeStyle = "#b91c1c"; c.lineWidth = 4; c.beginPath(); c.arc(cx, cy, r, at(0), a1); c.stroke()
                                    c.font = "7px sans-serif"; c.fillStyle = "#1b1b1b"; const marks = [-40, -20, -10, -5, 0, 3]; for (let i = 0; i < marks.length; i++) { const t = at(marks[i]); c.strokeStyle = marks[i] >= 0 ? "#b91c1c" : "#1b1b1b"; c.lineWidth = 1.2; c.beginPath(); c.moveTo(cx + Math.cos(t) * (r - 7), cy + Math.sin(t) * (r - 7)); c.lineTo(cx + Math.cos(t) * (r + 3), cy + Math.sin(t) * (r + 3)); c.stroke(); c.fillText((marks[i] > 0 ? "+" : "") + marks[i], cx + Math.cos(t) * (r - 17) - 5, cy + Math.sin(t) * (r - 17) + 2) }
                                    c.fillText("VU", cx - 5, h * 0.62); const n = at(ed.needle); c.strokeStyle = "#111"; c.lineWidth = 1.6; c.beginPath(); c.moveTo(cx + Math.cos(n) * 12, cy + Math.sin(n) * 12); c.lineTo(cx + Math.cos(n) * (r + 1), cy + Math.sin(n) * (r + 1)); c.stroke()
                                    c.fillStyle = "#2a2a2a"; c.fillRect(0, h - 12, w, 12) } } }
                        Section { head: "BLEND"; width: 150; height: ed.panelH - 16 - 110
                            Column { visible: ed.mixIndex >= 0; anchors.horizontalCenter: parent.horizontalCenter; y: 0; width: 84; spacing: 0
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "MIX  " + (ed.params[ed.mixIndex] ? ed.params[ed.mixIndex].text : ""); color: ed.ink; font.family: Theme.sans; font.pixelSize: 9; font.weight: Font.DemiBold }
                                Knob { anchors.horizontalCenter: parent.horizontalCenter; width: 84; height: 68; size: 48; vintage: true; paper: ed.ink; minText: "0"; maxText: "100"; defaultValue: ed.params[ed.mixIndex] ? ed.params[ed.mixIndex]["default"] : 1
                                    readonly property int rev: ed.rev; onRevChanged: sync(); Component.onCompleted: sync(); function sync() { if (ed.params[ed.mixIndex]) value = ed.params[ed.mixIndex].value } onMoved: function (v) { ed.setParam(ed.mixIndex, v) } } }
                            Text { visible: ed.mixIndex < 0; anchors.centerIn: parent; text: "—"; color: ed.ink } } }
                }
                Rectangle { x: parent.width - 26; y: 6; width: 18; height: 14; radius: 2; color: "#161616"; Text { anchors.centerIn: parent; text: "✕"; color: "#f5f5f0"; font.pixelSize: 9 } MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: ed.close() } }
            }
        }
    }
}
