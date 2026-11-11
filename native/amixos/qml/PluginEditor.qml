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
    property real needle: -20
    readonly property string fx: kind.indexOf("fx:") === 0 ? kind.substring(3) : ""
    readonly property color paper: "#e8e3d3"
    readonly property color paperLo: "#d6d0be"
    readonly property color ink: "#22201b"
    readonly property string face1: "Bahnschrift Condensed, Arial Narrow, Roboto Condensed, DejaVu Sans Condensed, " + Theme.sans   // a narrow engraver's face where the system has one
    readonly property color red: "#b91c1c"
    // the sections of each panel: [title, [parameter indices]] (the last parameter of a built-in effect is MIX: it lives in BLEND)
    readonly property var layouts: ({
        tubeamp: [["PRE AMP", [0]], ["TONE STACK", [1, 2, 3]], ["POWER AMP", [4, 5]]], preamp: [["INPUT", [0, 1]], ["COLOUR", [2]], ["OUTPUT", [3]]], postamp: [["OUTPUT", [0]], ["TONE", [1]], ["PROTECT", [2]]],
        hall: [["SPACE", [0, 1]], ["DAMPING", [2]], ["PRE DELAY", [3]], ["MOD", [4]]], room: [["SPACE", [0, 1]], ["DAMPING", [2]], ["REFLECTIONS", [3]]], reverb: [["REVERBERATION", [0, 1]], ["PRE DELAY", [2]], ["STEREO", [3]]],
        delay: [["ECHO", [0, 1]], ["TONE", [2]], ["STEREO", [3]]], syncdelay: [["TIME", [0, 1]], ["ECHO", [2, 3]], ["STEREO", [4]]], chorus: [["MOD", [0, 1]], ["STEREO", [2]]], flanger: [["MOD", [0, 1]], ["REGEN", [2]]], phaser: [["MOD", [0, 1]], ["REGEN", [2]]], tremolo: [["MOD", [0, 1]], ["WAVE", [2]]],
        wah: [["FOLLOWER", [0, 1]], ["FILTER", [2]]], crusher: [["DIGITAL", [0, 1]], ["FILTER", [2]]], exciter: [["HARMONICS", [0, 2]], ["TUNE", [1]]], drive: [["VALVE", [0]], ["TONE", [1]], ["OUTPUT", [2]]] })
    readonly property var sections: { const n = params.length; if (!n) return []; const l = layouts[fx]; if (l) return l
        const out = [], m = Math.min(n, 24); for (let i = 0; i < m; i += 4) { const idx = []; for (let j = i; j < Math.min(m, i + 4); j++) idx.push(j); out.push([(i + 1) + " – " + Math.min(m, i + 4), idx]) } return out }
    readonly property int mixIndex: fx && params.length ? params.length - 1 : -1
    function openFor(strip_, slot_, kind_, title_) { strip = strip_; slot = slot_; kind = kind_; title = title_; presets = engine.rackPresets(kind_); presetAt = -1; stateA = null; stateB = null; side = "A"; reload(); x = Math.max(4, (parent.width - width) / 2); y = Math.max(4, (parent.height - height) / 2); open() }
    function reload() { params = engine.pluginParams(strip, slot); const p = engine.plugins(strip)[slot]; bypass = p ? p.bypass : false; rev++ }
    function setParam(i, v) { engine.setPluginParam(strip, slot, params[i].id, v); const t = engine.pluginParams(strip, slot); if (t.length === params.length) params = t; presetAt = -1 }
    // the printed ends of a knob's scale: what the plug-in itself calls its lowest and highest position, without the unit when both share it
    function ends(i, hi) { if (!params[i]) return ""; rev; const lo = engine.pluginParamText(strip, slot, params[i].id, 0), up = engine.pluginParamText(strip, slot, params[i].id, 1); if (!lo && !up) return hi ? "10" : "0"
        const strip_ = function (t) { return t.replace(/^\+/, "+").replace(/\.0(?=\D|$)/, "").replace(/ ?(dBFS|dB|Hz|ms|%|s|bit)$/, "") }; return hi ? up.replace(/\.0(?=\D|$)/, "") : strip_(lo) }
    function snapshot() { const a = []; for (let i = 0; i < params.length; i++) a.push(params[i].value); return a }
    function restore(a) { if (!a) return; for (let i = 0; i < params.length && i < a.length; i++) engine.setPluginParam(strip, slot, params[i].id, a[i]); reload() }
    function pick(s) { if (s === side) return; if (side === "A") stateA = snapshot(); else stateB = snapshot(); side = s; restore(s === "A" ? stateA : stateB) }
    function preset(step) { if (!presets.length) return; presetAt = (presetAt + step + presets.length) % presets.length; engine.applyRackPreset(strip, slot, presets[presetAt].id); const at = presetAt; reload(); presetAt = at }
    readonly property real fit: Math.min(1, (parent ? parent.width - 12 : 1200) / (face.implicitWidth + 36))
    width: (face.implicitWidth + 36) * fit; height: (panelH + 36) * fit
    readonly property int panelH: 262
    modal: false; dim: true; closePolicy: Popup.CloseOnEscape; padding: 0
    Overlay.modeless: Rectangle { color: "#b3000000" }   // the console steps back while a plug-in is open: the unit is seen against dark, not against the mixer
    Timer { interval: 40; running: ed.visible; repeat: true; onTriggered: { const m = engine.meters[ed.strip]; const l = m ? (m.inDb !== undefined ? m.inDb : m.peakDb) : -120; ed.level = l; const tgt = ed.bypass ? -20 : Math.max(-20, Math.min(3, l + 12)); ed.needle += (tgt - ed.needle) * (tgt > ed.needle ? 0.35 : 0.12); vu.requestPaint() } }
    background: Item {}
    component Section: Rectangle { property string head; default property alias body: inner.data; height: ed.panelH - 16; color: "transparent"; border.color: "#c022201b"; border.width: 1; radius: 6
        Text { id: st; anchors.horizontalCenter: parent.horizontalCenter; y: 7; text: parent.head; color: ed.ink; font.family: ed.face1; style: Text.Sunken; styleColor: "#70ffffff"; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1 }
        Item { id: inner; x: 0; y: 26; width: parent.width; height: parent.height - 26 } }
    component Key: Item { property string label; signal clicked(); width: 30; height: 26
        Image { anchors.fill: parent; source: ka.pressed ? "qrc:/qt/qml/AmixOS/skin/key-down.png" : "qrc:/qt/qml/AmixOS/skin/key.png"; smooth: true }
        Text { anchors.horizontalCenter: parent.horizontalCenter; y: (ka.pressed ? 4.5 : 3) + (parent.label === "✕" ? 2.5 : 0); text: parent.label; color: "#ece8dc"; font.pixelSize: parent.label === "✕" ? 9 : 14; font.bold: true }
        MouseArea { id: ka; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked() } }
    contentItem: Item {
        // MOVE: drag the unit by its top strip; it stays where it is put until it is closed
        Item { z: 50; x: 60; width: parent.width - 180; height: 30
            DragHandler { target: null; property real sx: 0; property real sy: 0
                onActiveChanged: if (active) { sx = ed.x; sy = ed.y }
                onTranslationChanged: if (active) { ed.x = Math.max(-ed.width + 120, Math.min(ed.parent ? ed.parent.width - 120 : sx, sx + translation.x)); ed.y = Math.max(0, Math.min(ed.parent ? ed.parent.height - 60 : sy, sy + translation.y)) } }
            HoverHandler { cursorShape: Qt.SizeAllCursor } }
        Repeater { model: 6; delegate: Rectangle { x: (-4 - index * 3) * ed.fit + 4; y: (2 + index * 3) * ed.fit; width: (wood.width + 8 + index * 6) * ed.fit - 8; height: (wood.height + 6 + index * 5) * ed.fit; radius: 10 + index * 4; color: "#000"; opacity: 0.11 - index * 0.015 } }   // the unit's shadow
        Rectangle { id: wood; width: face.implicitWidth + 36; height: ed.panelH + 36; transformOrigin: Item.TopLeft; scale: ed.fit; radius: 5   // the wooden frame
            color: "#7d5225"; border.color: "#2e1b0b"; border.width: 2; clip: true
            Image { anchors.fill: parent; anchors.margins: 2; source: "qrc:/qt/qml/AmixOS/skin/wood.png"; fillMode: Image.PreserveAspectCrop; smooth: true }
            Rectangle { x: 11; y: 11; width: parent.width - 22; height: parent.height - 22; radius: 5; color: "transparent"; border.color: "#40000000"; border.width: 3 }   // the plate's shadow in the frame
            Rectangle { x: 14; y: 14; width: parent.width - 28; height: parent.height - 28; radius: 4; border.color: "#8d897c"   // the faceplate
                color: ed.paper; clip: true
                Image { anchors.fill: parent; source: "qrc:/qt/qml/AmixOS/skin/plate.png"; fillMode: Image.PreserveAspectCrop; smooth: true }
                Rectangle { anchors.fill: parent; color: "transparent"; border.color: "#80ffffff"; border.width: 1; radius: 4 }
                Repeater { model: 4; delegate: Image { source: "qrc:/qt/qml/AmixOS/skin/screw.png"; width: 15; height: 15; x: index % 2 ? parent.width - 17 : 2; y: index < 2 ? 2 : parent.height - 17; rotation: 20 + index * 67; smooth: true } }
                Row { id: face; x: 8; y: 8; spacing: 6
                    // PROGRAM (presets) + the A / B selector
                    Section { head: ed.presets.length ? "PROGRAM" : "COMPARE"; width: 138
                        Rectangle { visible: ed.presets.length > 0; x: 10; y: 4; width: parent.width - 20; height: 62; radius: 5; color: "#0e0e0e"; border.color: "#000"
                            Rectangle { x: 6; y: 5; width: parent.width - 12; height: 28; radius: 2; gradient: Gradient { GradientStop { position: 0; color: "#f3e9b8" } GradientStop { position: 1; color: "#d9c98a" } }
                                Text { anchors.centerIn: parent; width: parent.width - 8; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; text: ed.presetAt >= 0 ? ed.presets[ed.presetAt].name : "—  " + ed.presets.length + "  —"; color: "#2a2410"; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                                Rectangle { y: parent.height / 2; width: parent.width; height: 1; color: ed.red; opacity: 0.5 } }
                            Key { x: 20; y: 35; label: "–"; onClicked: ed.preset(-1) } Key { x: parent.width - 50; y: 35; label: "+"; onClicked: ed.preset(1) } }
                        Item { visible: ed.presets.length === 0; x: 13; y: 12; width: 112; height: 50   // the serial plate: brass, stamped with the channel and the slot
                            Image { width: 112; height: 30; source: "qrc:/qt/qml/AmixOS/skin/serial.png"; smooth: true; mipmap: true }
                            Text { x: 0; y: 3; width: 112; horizontalAlignment: Text.AlignHCenter; text: "TYPE  X1 – " + (ed.fx ? ed.fx.toUpperCase().substring(0, 6) : "VST"); color: "#3a2c0e"; font.family: Theme.mono; font.pixelSize: 7; font.bold: true; font.letterSpacing: 1 }
                            Text { x: 0; y: 14; width: 112; horizontalAlignment: Text.AlignHCenter; text: "SER. No.  " + (ed.strip + 1 < 10 ? "0" : "") + (ed.strip + 1) + " – " + (ed.slot + 1 < 10 ? "0" : "") + (ed.slot + 1); color: "#3a2c0e"; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; font.letterSpacing: 1 } }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; y: 84; text: "SELECT"; color: ed.ink; font.family: ed.face1; style: Text.Sunken; styleColor: "#70ffffff"; font.pixelSize: 10; font.weight: Font.DemiBold }
                        Item { anchors.horizontalCenter: parent.horizontalCenter; y: 112; width: 118; height: 60   // the A / B switch: the same miniature toggle, thrown left for A and right for B
                            Column { x: 6; y: 12; spacing: 2
                                Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 6; height: 6; radius: 3; color: ed.side === "A" ? ed.red : "transparent"; border.color: ed.side === "A" ? ed.red : ed.ink; border.width: 1.2 }
                                Text { text: "A"; color: ed.side === "A" ? ed.red : ed.ink; font.family: ed.face1; font.pixelSize: 13; font.weight: Font.Bold; style: Text.Sunken; styleColor: "#70ffffff" } }
                            Image { anchors.horizontalCenter: parent.horizontalCenter; y: 0; width: 58; height: 58; source: "qrc:/qt/qml/AmixOS/skin/toggle.png"; smooth: true; mipmap: true; mirror: ed.side === "A" }
                            Column { x: parent.width - width - 6; y: 12; spacing: 2
                                Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 6; height: 6; radius: 3; color: ed.side === "B" ? ed.red : "transparent"; border.color: ed.side === "B" ? ed.red : ed.ink; border.width: 1.2 }
                                Text { text: "B"; color: ed.side === "B" ? ed.red : ed.ink; font.family: ed.face1; font.pixelSize: 13; font.weight: Font.Bold; style: Text.Sunken; styleColor: "#70ffffff" } }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: ed.pick(ed.side === "A" ? "B" : "A") } }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; y: 184; width: parent.width - 16; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; text: "TWO SETTINGS\nTO COMPARE"; color: ed.ink; opacity: 0.8; font.family: ed.face1; font.pixelSize: 8; font.letterSpacing: 1; lineHeight: 1.15; style: Text.Sunken; styleColor: "#70ffffff" } }
                    // the sections of the effect
                    Repeater { model: ed.sections
                        delegate: Section { head: modelData[0]; readonly property var idx: modelData[1]; readonly property int cols: Math.ceil(idx.length / 2); width: Math.max(96, cols * 86 + 14, modelData[0].length * 8.4 + 26)
                            Grid { anchors.horizontalCenter: parent.horizontalCenter; y: idx.length <= cols ? 56 : 4; columns: cols; columnSpacing: 2; rowSpacing: 6
                                Repeater { model: idx
                                    delegate: Column { width: 84; spacing: 0; readonly property int pi: modelData
                                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: ed.params[pi] ? ed.params[pi].title : ""; color: ed.ink; font.family: ed.face1; style: Text.Sunken; styleColor: "#70ffffff"; font.pixelSize: 10; font.weight: Font.DemiBold }
                                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: ed.params[pi] ? ed.params[pi].text : ""; color: ed.ink; font.family: ed.face1; style: Text.Sunken; styleColor: "#70ffffff"; font.pixelSize: 9 }
                                        Knob { anchors.horizontalCenter: parent.horizontalCenter; width: 84; size: 44; vintage: true; paper: ed.ink; minText: ed.ends(pi, 0); maxText: ed.ends(pi, 1); label: ""; valueText: ""
                                            defaultValue: ed.params[pi] ? ed.params[pi]["default"] : 0.5; readonly property int rev: ed.rev; onRevChanged: sync(); Component.onCompleted: sync()
                                            function sync() { if (ed.params[pi]) value = ed.params[pi].value } onMoved: function (v) { ed.setParam(pi, v) } height: 74 } } } } } }
                    // the name plate, the lamp and the ON / OFF switch
                    Item { width: 150; height: ed.panelH - 16
                        MouseArea { anchors.fill: parent; property point p0; cursorShape: Qt.SizeAllCursor; onPressed: function (m) { p0 = Qt.point(m.x, m.y) } onPositionChanged: function (m) { if (pressed) { ed.x += (m.x - p0.x) * ed.fit; ed.y += (m.y - p0.y) * ed.fit } } }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; y: 6; text: "AMFB  X1"; color: ed.ink; font.family: ed.face1; style: Text.Sunken; styleColor: "#70ffffff"; font.pixelSize: 17; font.weight: Font.Bold; font.letterSpacing: 2 }
                        Image { anchors.horizontalCenter: parent.horizontalCenter; y: 28; width: 40; height: 40; source: "qrc:/qt/qml/AmixOS/skin/badge.png"; smooth: true; mipmap: true }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; y: 68; width: parent.width - 6; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; text: ed.title.toUpperCase(); color: ed.ink; font.family: ed.face1; style: Text.Sunken; styleColor: "#70ffffff"; font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 1 }
                        Image { anchors.horizontalCenter: parent.horizontalCenter; y: 100; width: 70; height: 70; source: ed.bypass ? "qrc:/qt/qml/AmixOS/skin/lamp-off.png" : "qrc:/qt/qml/AmixOS/skin/lamp-on.png"; smooth: true }
                        Item { anchors.horizontalCenter: parent.horizontalCenter; y: 156; width: 128; height: 44   // the ON / OFF switch: marks with a ring (off) and a dot (on), the miniature toggle between them
                            Column { x: 4; y: 4; spacing: 2
                                Row { anchors.horizontalCenter: parent.horizontalCenter; spacing: 3; Rectangle { width: 6; height: 6; radius: 3; color: "transparent"; border.color: ed.ink; border.width: 1.2 } Rectangle { width: 3; height: 3; radius: 1.5; y: 1.5; color: ed.ink } }
                                Text { text: "OFF"; color: ed.ink; font.family: ed.face1; font.pixelSize: 10; font.weight: Font.DemiBold; style: Text.Sunken; styleColor: "#70ffffff" } }
                            Image { anchors.horizontalCenter: parent.horizontalCenter; y: -6; width: 56; height: 56; source: "qrc:/qt/qml/AmixOS/skin/toggle.png"; smooth: true; mipmap: true; mirror: ed.bypass }
                            Column { x: parent.width - width - 6; y: 4; spacing: 2
                                Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 6; height: 6; radius: 3; color: ed.ink; Rectangle { anchors.centerIn: parent; width: 2.4; height: 2.4; radius: 1.2; color: ed.bypass ? ed.ink : "#f5f5f0" } }
                                Text { text: "ON"; color: ed.ink; font.family: ed.face1; font.pixelSize: 10; font.weight: Font.DemiBold; style: Text.Sunken; styleColor: "#70ffffff" } }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { engine.setPluginBypass(ed.strip, ed.slot, !ed.bypass); ed.bypass = !ed.bypass } } }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; y: 204; horizontalAlignment: Text.AlignHCenter; text: "AMFB AUDIO\n" + (ed.fx ? "CH " + (ed.strip + 1) + " · SLOT " + (ed.slot + 1) : (ed.kind.indexOf("vst2:") === 0 ? "VST2" : "VST3") + " HOSTED"); color: ed.ink; font.family: ed.face1; style: Text.Sunken; styleColor: "#70ffffff"; font.pixelSize: 9; font.letterSpacing: 2; lineHeight: 1.2 } }
                    // the meter and BLEND
                    Column { spacing: 6; width: 150
                        Rectangle { width: 150; height: 104; radius: 5; border.color: "#2b2924"; border.width: 1   // the needle meter (input level; 0 = -12 dBFS)
                            gradient: Gradient { GradientStop { position: 0; color: "#8e8a80" } GradientStop { position: 0.5; color: "#6d695f" } GradientStop { position: 1; color: "#57534a" } }
                            Item { id: vu; x: 7; y: 7; width: 136; height: 90; clip: true; function requestPaint() {}
                                Image { anchors.fill: parent; source: "qrc:/qt/qml/AmixOS/skin/meter.png"; smooth: true; opacity: ed.bypass ? 0.62 : 1 }
                                Rectangle { anchors.fill: parent; color: "#000"; opacity: ed.bypass ? 0.28 : 0 }
                                Item { x: parent.width / 2; y: parent.height * 1.18; rotation: -45 + 90 * (Math.max(-20, Math.min(3, ed.needle)) + 20) / 23   // the needle and its shadow, pivoted below the dial
                                    Rectangle { x: 1.5; y: -parent.parent.height * 0.90 + 3; width: 1.6; height: parent.parent.height * 0.82; color: "#55000000" }
                                    Rectangle { x: -0.8; y: -parent.parent.height * 0.90; width: 1.6; height: parent.parent.height * 0.82; color: "#161412"; antialiasing: true } }
                                Rectangle { y: parent.height * 0.87; width: parent.width; height: parent.height * 0.13; color: "#1a1814" }
                                Image { anchors.fill: parent; source: "qrc:/qt/qml/AmixOS/skin/meter-glass.png"; smooth: true } }
                            Rectangle { x: 6; y: 6; width: 138; height: 92; radius: 2; color: "transparent"; border.color: "#1c1a16"; border.width: 2 } }
                        Section { head: "BLEND"; width: 150; height: ed.panelH - 16 - 110
                            Column { visible: ed.mixIndex >= 0; anchors.horizontalCenter: parent.horizontalCenter; y: 0; width: 84; spacing: 0
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "MIX  " + (ed.params[ed.mixIndex] ? ed.params[ed.mixIndex].text : ""); color: ed.ink; font.family: ed.face1; style: Text.Sunken; styleColor: "#70ffffff"; font.pixelSize: 9; font.weight: Font.DemiBold }
                                Knob { anchors.horizontalCenter: parent.horizontalCenter; width: 84; height: 74; size: 44; vintage: true; paper: ed.ink; minText: "0"; maxText: "100 %"; defaultValue: ed.params[ed.mixIndex] ? ed.params[ed.mixIndex]["default"] : 1
                                    readonly property int rev: ed.rev; onRevChanged: sync(); Component.onCompleted: sync(); function sync() { if (ed.params[ed.mixIndex]) value = ed.params[ed.mixIndex].value } onMoved: function (v) { ed.setParam(ed.mixIndex, v) } } }
                            Text { visible: ed.mixIndex < 0; anchors.centerIn: parent; text: "—"; color: ed.ink } } }
                }
                Key { x: parent.width - 36; y: 4; width: 24; height: 21; label: "✕"; onClicked: ed.close() }
            }
        }
    }
}
