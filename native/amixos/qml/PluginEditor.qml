// The editor window of a rack plug-in, drawn as a stack of rack units: brushed aluminium in the family colour, dark inset panels for the controls
// (formerly a wooden frame and a cream plate), a control strip and a display unit with the spectrum, the curve and the meters. Older notes:
// the controls in outlined
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
    // lettering: dark on the light plates, cream / silver / gold on the dark ones
    readonly property color ink: family === "tune" ? "#f1e3d6" : family === "echo" ? "#e9e4cc" : family === "mod" ? "#e4e8f3" : family === "heat" ? "#e3c27a" : family === "guard" ? "#f1f1ee" : family === "space" ? "#e6ecf2" : "#ece8dc"
    readonly property color plateInk: family === "silver" ? "#1d2126" : ink   // lettering straight on the plate (the name plate): dark on silver
    readonly property bool darkPlate: true   // every unit is a dark inset panel on brushed metal: light lettering throughout
    readonly property string face1: "Bahnschrift Condensed, Arial Narrow, Roboto Condensed, DejaVu Sans Condensed, " + Theme.sans   // a narrow engraver's face where the system has one
    readonly property color red: "#b91c1c"
    // THE LOOK OF EACH FAMILY: reverbs steel blue, delays sage green, modulation lavender, saturation and amps copper, the feedback killer signal red,
    // everything else the cream plate. tint lies over the plate; view names the live display the panel carries.
    readonly property var families: ({ convclip: "silver", wshape: "tune", iamfelement: "echo", iamfrender: "heat", maximizer: "heat", multiband: "guard", imager: "echo", autopan: "mod", doubler: "tune", opto: "guard", glue: "heat", tape: "echo", transient: "tune", deesser: "guard", mod: "mod", mseq: "heat", isoeq: "guard", reverb: "space", hall: "space", room: "space", delay: "echo", syncdelay: "echo", chorus: "mod", flanger: "mod", phaser: "mod", tremolo: "mod", wah: "mod",
        analog: "heat", drive: "heat", tubeamp: "heat", preamp: "heat", postamp: "heat", exciter: "heat", subsynth: "heat", antifeedback: "guard", desquash: "guard", pitch: "tune", barrier: "heat", sampler: "echo" })
    readonly property string family: families[fx] || ""
    // THE FINISH, as on real rack gear: reverbs brushed aluminium, delays the deep green enamel of tape echoes, modulation midnight blue,
    // saturation and amps black with gold lettering, the feedback killer workshop grey, the pitch shifter oxblood. The plate's own grain shows through the paint.
    readonly property color tint: family === "silver" ? "#e4e7eb" : family === "tune" ? "#4a1d28" : family === "space" ? "#3d4652" : family === "echo" ? "#24382b" : family === "mod" ? "#262f48" : family === "heat" ? "#202020" : family === "guard" ? "#4a4f56" : "#3a3f47"
    readonly property real tintAmount: family === "silver" ? 0.45 : 0.6   // the anodised colour of the family over the brushed aluminium: the grain shows through
    readonly property string hi: darkPlate ? "#90000000" : "#70ffffff"   // the engraving: a light edge on light plates, a shadow on dark ones
    readonly property color onInk: darkPlate ? "#14171a" : paper       // lettering on a key that is lit in the ink colour
    readonly property color accent: darkPlate ? "#ff6b5a" : red        // the signal red, brighter where the plate is dark
    readonly property color rule: Qt.rgba(ink.r, ink.g, ink.b, darkPlate ? 0.55 : 0.75)
    readonly property string view: family === "space" ? "decay" : family === "mod" ? "lfo" : (fx === "analog" || fx === "drive" || fx === "tubeamp" || fx === "preamp") ? "curve" : ""
    function pv(i, d) { return params[i] ? params[i].value : d }
    // the sections of each panel: [title, [parameter indices]] (the last parameter of a built-in effect is MIX: it lives in BLEND)
    readonly property var layouts: ({
        tubeamp: [["PRE AMP", [0]], ["TONE STACK", [1, 2, 3]], ["POWER AMP", [4, 5]]], preamp: [["INPUT", [0, 1]], ["COLOUR", [2]], ["OUTPUT", [3]]], postamp: [["OUTPUT", [0]], ["TONE", [1]], ["PROTECT", [2]]],
        hall: [["SPACE", [0, 1]], ["DAMPING", [2]], ["PRE DELAY", [3]], ["MOD", [4]]], room: [["SPACE", [0, 1]], ["DAMPING", [2]], ["REFLECTIONS", [3]]], reverb: [["REVERBERATION", [0, 1]], ["PRE DELAY", [2]], ["STEREO", [3]]],
        delay: [["ECHO", [0, 1]], ["TONE", [2]], ["STEREO", [3]]], syncdelay: [["TIME", [0, 1]], ["ECHO", [2, 3]], ["STEREO", [4]]], analog: [["INPUT STAGE", [0, 1]], ["TRANSFORMER", [2]], ["OUTPUT", [3]]], pitch: [["PITCH", [0, 1]], ["GRAIN", [2]]], barrier: [["LOUDNESS", [0, 3]], ["RUN-UP", [4]], ["IMPACT", [1]], ["SMOOTH", [2]]], dualeq: [["LISTEN FOR", [0, 1]], ["HOW FAR", [2]], ["HOW FAST", [3]]], antifeedback: [["DETECT", [0, 5]], ["NOTCH", [1, 2]], ["FILTERS", [3, 6, 4]]], subsynth: [["SUB", [0, 1]], ["SHAPE", [2]]], chorus: [["MOD", [0, 1]], ["STEREO", [2]]], flanger: [["MOD", [0, 1]], ["REGEN", [2]]], phaser: [["MOD", [0, 1]], ["REGEN", [2]]], tremolo: [["MOD", [0, 1]], ["WAVE", [2]]],
        wah: [["FOLLOWER", [0, 1]], ["FILTER", [2]]], crusher: [["DIGITAL", [0, 1]], ["FILTER", [2]]], exciter: [["HARMONICS", [0, 2]], ["TUNE", [1]]], drive: [["VALVE", [0]], ["TONE", [1]], ["OUTPUT", [2]]], sampler: [["SAMPLE", [0]], ["TRANSPORT", [1, 2]], ["LEVEL", [3]]],
        mseq: [["INPUT", [0, 1]], ["LOW", [2, 3, 4]], ["CONTOUR", [5, 6, 7, 8]], ["MID", [9, 10, 11, 12]], ["HIGH", [13, 14, 15]], ["FILTERS", [16, 17]], ["SATURATION", [18, 19]], ["M / S", [20, 21, 22]], ["MOVE", [23, 24]], ["OUTPUT", [25]]],
        isoeq: [["LOW", [0, 1, 2]], ["LOW MID", [3, 4]], ["HIGH MID", [5, 6, 7]], ["HIGH", [8, 9]], ["STAGE", [10, 11, 12]]],
        mod: [["MODE", [0]], ["LFO", [1, 2, 5]], ["REGEN", [3]], ["BBD", [4, 6]]],
        opto: [["LEVELING", [0, 1]], ["MODE", [2]], ["SIDECHAIN", [3]]], glue: [["THRESHOLD", [0, 1]], ["TIMING", [2, 3]], ["OUTPUT", [4, 5]]],
        tape: [["RECORD", [0]], ["TRANSPORT", [1, 3]], ["ALIGNMENT", [2]], ["REPRO", [4]]], transient: [["SHAPE", [0, 1]], ["OUTPUT", [2]]], deesser: [["DETECT", [0, 1]], ["REDUCE", [2]], ["MONITOR", [3]]],
        maximizer: [["LOUDNESS", [0]], ["CEILING", [1]], ["RELEASE", [2]]], multiband: [["CROSSOVER", [0, 1]], ["BANDS", [2, 3, 4]], ["DYNAMICS", [5, 6]]], imager: [["WIDTH", [0, 1]], ["SPLIT", [2]], ["OUTPUT", [3]]], autopan: [["MOTION", [0, 1]], ["WAVE", [2]]], doubler: [["VOICES", [0, 1]], ["STEREO", [2]], ["TONE", [3]]],
        iamfelement: [["DIRECTION", [0, 1]], ["DISTANCE", [2, 3]], ["RENDER", [4]]], iamfrender: [["MONITOR", [0]], ["LOUDNESS", [1, 2]], ["OUTPUT", [3]]],
        wshape: [["SHAPE", [0, 6]], ["LOW", [1, 2]], ["MID", [3, 4]], ["TOP", [5]], ["OUTPUT", [7]]],
        convclip: [["CONVERTERS", [0, 1]], ["GAIN", [2, 5]], ["COLOUR", [3, 4, 6]], ["CLIP", [7, 10]], ["WORD LENGTH", [8, 9]], ["MONITOR", [11]]] })
    readonly property var sections: { const n = params.length; if (!n) return []; const l = layouts[fx]; if (l) return l
        const out = [], m = Math.min(n, 24); for (let i = 0; i < m; i += 4) { const idx = []; for (let j = i; j < Math.min(m, i + 4); j++) idx.push(j); out.push([(i + 1) + " – " + Math.min(m, i + 4), idx]) } return out }
    readonly property int mixIndex: fx && params.length ? params.length - 1 : -1
    property string nativeNote: ""
    function openFor(strip_, slot_, kind_, title_) { nativeNote = ""; strip = strip_; slot = slot_; kind = kind_; title = title_; presets = engine.rackPresets(kind_); presetAt = -1; stateA = null; stateB = null; side = "A"; reload(); x = Math.max(4, (parent.width - width) / 2); y = Math.max(4, (parent.height - height) / 2); open() }
    function recentre() { if (visible && parent) { x = Math.max(4, (parent.width - width) / 2); y = Math.max(4, (parent.height - height) / 2) } }
    onWidthChanged: recentre(); onHeightChanged: recentre()
    function reload() { params = engine.pluginParams(strip, slot); const p = engine.plugins(strip)[slot]; bypass = p ? p.bypass : false; rev++ }
    function setParam(i, v) { engine.setPluginParam(strip, slot, params[i].id, v); const t = engine.pluginParams(strip, slot); if (t.length === params.length) params = t; presetAt = -1 }
    // the printed ends of a knob's scale: what the plug-in itself calls its lowest and highest position, without the unit when both share it
    function ends(i, hi) { if (!params[i]) return ""; rev; if (fx === "sampler" && i === 0) return hi ? "100" : "1";   // the sample number, not two long names
    const lo = engine.pluginParamText(strip, slot, params[i].id, 0), up = engine.pluginParamText(strip, slot, params[i].id, 1); if (!lo && !up) return hi ? "10" : "0"
        const strip_ = function (t) { return t.replace(/^\+/, "+").replace(/\.0(?=\D|$)/, "").replace(/ ?(dBFS|dB|Hz|ms|%|s|bit)$/, "") }; const t = hi ? up.replace(/\.0(?=\D|$)/, "") : strip_(lo); return !/\d/.test(t) && t.length > 5 ? t.substring(0, 4) + "." : t }   // a word-switch (CONSTANT, VIBRATO ...) is shortened so the two ends do not run into each other
    function snapshot() { const a = []; for (let i = 0; i < params.length; i++) a.push(params[i].value); return a }
    function restore(a) { if (!a) return; for (let i = 0; i < params.length && i < a.length; i++) engine.setPluginParam(strip, slot, params[i].id, a[i]); reload() }
    function pick(s) { if (s === side) return; if (side === "A") stateA = snapshot(); else stateB = snapshot(); side = s; restore(s === "A" ? stateA : stateB) }
    function preset(step) { if (!presets.length) return; presetAt = (presetAt + step + presets.length) % presets.length; engine.applyRackPreset(strip, slot, presets[presetAt].id); const at = presetAt; reload(); presetAt = at }
    readonly property int ear: 30   // the rack ears, left and right of every unit
    readonly property real fit: Math.min(1, (parent ? parent.width - 12 : 1200) / wood.width, (parent ? parent.height - 8 : 900) / wood.height)
    width: wood.width * fit; height: wood.height * fit
    property var spec: []
    property real inDb: -120
    property real outDb: -120
    function resetAll() { for (let i = 0; i < params.length; i++) engine.setPluginParam(strip, slot, params[i].id, params[i]["default"]); presetAt = -1; reload() }
    // THE CURVES the display draws: what the unit does to each frequency, worked out from its knobs with the same filters the engine runs
    function biq(t, f0, q, g) { const fs = 48000, A = Math.pow(10, g / 40), w = 2 * Math.PI * Math.min(f0, fs * 0.45) / fs, c = Math.cos(w), sn = Math.sin(w), al = sn / (2 * Math.max(q, 0.1)); let b0, b1, b2, a0, a1, a2
        if (t === "peak") { b0 = 1 + al * A; b1 = -2 * c; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * c; a2 = 1 - al / A }
        else if (t === "ls" || t === "hs") { const r = 2 * Math.sqrt(A) * al, s = t === "ls" ? 1 : -1; b0 = A * ((A + 1) - s * (A - 1) * c + r); b1 = 2 * s * A * ((A - 1) - s * (A + 1) * c); b2 = A * ((A + 1) - s * (A - 1) * c - r); a0 = (A + 1) + s * (A - 1) * c + r; a1 = -2 * s * ((A - 1) + s * (A + 1) * c); a2 = (A + 1) + s * (A - 1) * c - r }
        else if (t === "hp") { b0 = (1 + c) / 2; b1 = -(1 + c); b2 = b0; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al }
        else { b0 = (1 - c) / 2; b1 = 1 - c; b2 = b0; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al }
        return [b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0] }
    function magDb(k, f) { const w = 2 * Math.PI * f / 48000, cr = Math.cos(w), ci = -Math.sin(w), c2r = Math.cos(2 * w), c2i = -Math.sin(2 * w)
        const nr = k[0] + k[1] * cr + k[2] * c2r, ni = k[1] * ci + k[2] * c2i, dr = 1 + k[3] * cr + k[4] * c2r, di = k[3] * ci + k[4] * c2i; return 10 * Math.log10((nr * nr + ni * ni) / (dr * dr + di * di) + 1e-12) }
    function curves() { rev; const v = function (i, d) { return params[i] ? params[i].value : d }, db = function (x) { return (x - 0.5) * 24 }
        if (fx === "mseq" && params.length >= 26) {
            const om = function (x) { const o = x * 2 - 1; return o < 0 ? 1 + o : 1 }, os = function (x) { const o = x * 2 - 1; return o > 0 ? 1 - o : 1 }
            const bands = [["ls", 20 * Math.pow(15, v(3)), 0.7, db(v(2)), v(4)], ["peak", 60 * Math.pow(10, v(6)), 4 - 3.6 * v(7), db(v(5)), v(8)], ["peak", 300 * Math.pow(20, v(10)), 4 - 3.6 * v(11), db(v(9)), v(12)], ["hs", 2000 * Math.pow(10, v(14)), 0.7, db(v(13)), v(15)]]
            const filt = []; if (v(16) >= 0.02) filt.push(biq("hp", 20 * Math.pow(15, v(16)), 0.707, 0)); if (v(17) <= 0.98) filt.push(biq("lp", 2000 * Math.pow(10, v(17)), 0.707, 0))
            const mk = function (sel) { const ks = filt.slice(); for (const b of bands) ks.push(biq(b[0], b[1], b[2], b[3] * (sel ? om(b[4]) : os(b[4])))); const gain = db(v(0)) + db(v(25)); return function (f) { let d = gain; for (const k of ks) d += magDb(k, f); return d } }
            return [{ name: "MID", color: "#e8f4ff", width: 2.2, db: mk(true) }, { name: "SIDE", color: "#6fb0e6", width: 1.6, db: mk(false) }] }
        if (fx === "isoeq" && params.length >= 13) { const prop = v(10) >= 0.5, F = [31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000], ks = []
            for (let b = 0; b < 10; b++) { const g = db(v(b)); ks.push(biq("peak", F[b], prop ? 0.6 + 2.2 * Math.abs(g) / 12 : 1.414, g)) }
            const out = db(v(12)); return [{ name: "RESPONSE", color: "#e8f4ff", width: 2.2, db: function (f) { let d = out; for (const k of ks) d += magDb(k, f); return d } }] }
        if (fx === "wshape" && params.length >= 9) { const A = v(0), q = 1.4 - v(6) * 0.9
            const ks = [biq("ls", 90, 0.6, A * v(1) * 9), biq("peak", 350, q, -A * v(2) * 6), biq("peak", 1800, q, A * v(3) * 6), biq("peak", 5000, q * 1.2, -A * v(4) * 6), biq("hs", 10000, 0.6, A * v(5) * 9)]
            let sum = 0, n = 0; for (let f = 40; f <= 16000; f *= Math.pow(2, 1 / 3)) { let d = 0; for (const k of ks) d += magDb(k, f); sum += Math.pow(10, d / 10); n++ } const comp = -10 * Math.log10(sum / n) + (v(7) - 0.5) * 24
            return [{ name: "W CURVE", color: "#e8f4ff", width: 2.4, db: function (f) { let d = comp; for (const k of ks) d += magDb(k, f); return d } }] }
        return [] }
    readonly property int rowH: 262
    readonly property int rows: sections.length > 8 ? 2 : 1   // a unit with many controls stands in two rows
    readonly property int panelH: rowH * rows
    readonly property var sectionRows: { if (rows < 2) return [sections]; const h = Math.ceil(sections.length / 2); return [sections.slice(0, h), sections.slice(h)] }
    modal: false; dim: true; closePolicy: Popup.CloseOnEscape; padding: 0
    Overlay.modeless: Rectangle { color: "#b3000000" }   // the console steps back while a plug-in is open: the unit is seen against dark, not against the mixer
    Timer { interval: engine.uiAwake ? 40 : 500; running: ed.visible; repeat: true; onTriggered: { const m = engine.meterAt(ed.strip); const l = m ? (m.inDb !== undefined ? m.inDb : m.peakDb) : -120; ed.level = l; const tgt = ed.bypass ? -20 : Math.max(-20, Math.min(3, l + 12)); ed.needle += (tgt - ed.needle) * (tgt > ed.needle ? 0.35 : 0.12); vu.requestPaint()
        ed.inDb += ((m ? (m.inDb !== undefined ? m.inDb : -120) : -120) - ed.inDb) * 0.4; ed.outDb += ((m ? m.peakDb : -120) - ed.outDb) * 0.4
        if (++tick % 2 === 0) { engine.setScopeStrip(ed.strip); ed.spec = engine.spectrum(2); disp.requestPaint() } } property int tick: 0 }
    background: Item {}
    component Section: Rectangle { property string head; default property alias body: inner.data; height: ed.panelH - 16; radius: 12; border.color: "#05060a"; border.width: 1.5   // a dark inset panel in the brushed plate
        gradient: Gradient { GradientStop { position: 0; color: "#101216" } GradientStop { position: 1; color: "#1c1f25" } }
        Rectangle { anchors.fill: parent; anchors.margins: 1.5; radius: 11; color: "transparent"; border.color: "#14ffffff"; border.width: 1 }
        Text { id: st; anchors.horizontalCenter: parent.horizontalCenter; y: 7; text: i18n.t(parent.head, i18n.lang); color: ed.ink; font.family: ed.face1; style: Text.Sunken; styleColor: ed.hi; font.pixelSize: 11; font.weight: Font.DemiBold; font.letterSpacing: 1 }
        Item { id: inner; x: 0; y: 26; width: parent.width; height: parent.height - 26 } }
    // one rack unit: brushed aluminium in the family's anodised colour, rack ears with their slots and screws, a bevel along the edges
    component RackUnit: Item { id: ru; default property alias body: plate.data; width: wood.width
        Rectangle { anchors.fill: parent; radius: 3; color: "#2a2d33"; border.color: "#0a0b0d"; border.width: 1.5; clip: true
            Image { anchors.fill: parent; source: "qrc:/qt/qml/AmixOS/skin/brushed.png"; fillMode: Image.Stretch; smooth: true }
            Rectangle { objectName: "plateTint"; anchors.fill: parent; color: ed.tint; opacity: ed.tintAmount }
            Rectangle { anchors.fill: parent; color: "transparent"; radius: 3; border.color: "#38ffffff"; border.width: 1 }
            Repeater { model: 2; delegate: Item { x: index ? ru.width - ed.ear : 0; width: ed.ear; height: ru.height   // the ears: a seam, two slots and two screws
                Rectangle { x: index ? 0 : ed.ear - 1; width: 1; height: parent.height; color: "#55000000" }
                Repeater { model: ru.height > 120 ? 2 : 1; delegate: Item { x: 6; width: 18; height: 18; y: ru.height > 120 ? (index ? ru.height - 34 : 16) : (ru.height - 18) / 2
                    Rectangle { anchors.centerIn: parent; width: 18; height: 9; radius: 4.5; color: "#0b0c0f"; border.color: "#30ffffff" }
                    Image { anchors.centerIn: parent; width: 13; height: 13; source: "qrc:/qt/qml/AmixOS/skin/screw.png"; rotation: 30 + index * 70; smooth: true } } } } } }
        Item { id: plate; x: ed.ear; width: parent.width - 2 * ed.ear; height: parent.height } }
    // a group of keys on the control strip: a dark rounded inset
    component Inset: Rectangle { height: 48; radius: 10; border.color: "#05060a"; border.width: 1.5
        gradient: Gradient { GradientStop { position: 0; color: "#101216" } GradientStop { position: 1; color: "#1c1f25" } } }
    // a backlit square key: pale when off, lit from within when on
    component LitKey: Item { id: lk; property string label; property bool lit: false; property color litColor: "#e8f4ff"; signal clicked(); width: 32; height: 32; opacity: enabled ? 1 : 0.4; anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        Rectangle { anchors.fill: parent; radius: 4; border.color: "#000"; border.width: 1.5
            gradient: Gradient { GradientStop { position: 0; color: lk.lit ? Theme.btnTop(lk.litColor) : (lka.pressed ? "#9aa0a8" : "#d9dde2") } GradientStop { position: 1; color: lk.lit ? lk.litColor : (lka.pressed ? "#7d838b" : "#aeb3ba") } } }
        Rectangle { anchors.fill: parent; anchors.margins: -4; radius: 7; color: "transparent"; border.color: lk.litColor; border.width: 2; opacity: lk.lit ? 0.45 : 0 }
        Text { anchors.centerIn: parent; text: lk.label; color: "#14171a"; font.pixelSize: 13; font.bold: true }
        MouseArea { id: lka; anchors.fill: parent; enabled: lk.enabled; cursorShape: Qt.PointingHandCursor; onClicked: lk.clicked() } }
    component Key: Item { property string label; signal clicked(); width: 30; height: 26
        Image { anchors.fill: parent; source: ka.pressed ? "qrc:/qt/qml/AmixOS/skin/key-down.png" : "qrc:/qt/qml/AmixOS/skin/key.png"; smooth: true }
        Text { anchors.horizontalCenter: parent.horizontalCenter; y: (ka.pressed ? 4.5 : 3) + (parent.label === "✕" ? 2.5 : 0); text: i18n.t(parent.label, i18n.lang); color: "#ece8dc"; font.pixelSize: parent.label === "✕" ? 9 : 14; font.bold: true }
        MouseArea { id: ka; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked() } }
    contentItem: Item {
        // MOVE: drag the unit by its top strip; it stays where it is put until it is closed
        Item { z: 50; x: 60; width: parent.width - 180; height: 30
            DragHandler { target: null; property real sx: 0; property real sy: 0
                onActiveChanged: if (active) { sx = ed.x; sy = ed.y }
                onTranslationChanged: if (active) { ed.x = Math.max(-ed.width + 120, Math.min(ed.parent ? ed.parent.width - 120 : sx, sx + translation.x)); ed.y = Math.max(0, Math.min(ed.parent ? ed.parent.height - 60 : sy, sy + translation.y)) } }
            HoverHandler { cursorShape: Qt.SizeAllCursor } }
        Repeater { model: 6; delegate: Rectangle { x: (-4 - index * 3) * ed.fit + 4; y: (2 + index * 3) * ed.fit; width: (wood.width + 8 + index * 6) * ed.fit - 8; height: (wood.height + 6 + index * 5) * ed.fit; radius: 10 + index * 4; color: "#000"; opacity: 0.11 - index * 0.015 } }   // the unit's shadow
        // THE STACK: three rack units bolted one above the other: the controls, a control strip (compare, program, reset, bypass) and the display
        Column { id: wood; spacing: 3; transformOrigin: Item.TopLeft; scale: ed.fit; width: Math.max(face.implicitWidth, stripRow.implicitWidth) + 2 * ed.ear + 28
            RackUnit { id: mainUnit; height: ed.panelH + 28
                Row { id: face; x: 14; y: 14; spacing: 6
                    // the sections of the effect
                    Column { spacing: 6
                    Repeater { model: ed.sectionRows
                    delegate: Row { spacing: 6; readonly property var rowSecs: modelData
                    Repeater { model: rowSecs
                        delegate: Section { head: i18n.t(modelData[0], i18n.lang); readonly property var idx: modelData[1]; readonly property int cols: Math.ceil(idx.length / 2); height: ed.rowH - 16 - (ed.rows > 1 ? 3 : 0); width: Math.max(96, cols * 86 + 14, modelData[0].length * 8.4 + 26)
                            Grid { anchors.horizontalCenter: parent.horizontalCenter; y: idx.length <= cols ? 56 : 4; columns: cols; columnSpacing: 2; rowSpacing: 6
                                Repeater { model: idx
                                    delegate: Column { width: 84; spacing: 0; readonly property int pi: modelData
                                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: ed.params[pi] ? i18n.t(ed.params[pi].title, i18n.lang) : ""; color: ed.ink; font.family: ed.face1; style: Text.Sunken; styleColor: ed.hi; font.pixelSize: 10; font.weight: Font.DemiBold }
                                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: ed.params[pi] ? ed.params[pi].text : ""; color: ed.ink; font.family: ed.face1; style: Text.Sunken; styleColor: ed.hi; font.pixelSize: 9 }
                                        Knob { anchors.horizontalCenter: parent.horizontalCenter; width: 84; size: 44; vintage: true; paper: ed.ink; minText: ed.ends(pi, 0); maxText: ed.ends(pi, 1); label: ""; valueText: ""
                                            defaultValue: ed.params[pi] ? ed.params[pi]["default"] : 0.5; readonly property int rev: ed.rev; onRevChanged: sync(); Component.onCompleted: sync()
                                            function sync() { if (ed.params[pi]) value = ed.params[pi].value } onMoved: function (v) { ed.setParam(pi, v) } height: 74 } } } } } } } } }
                    // SYNC DELAY: the time read-out. The delay in milliseconds, the tempo, the note keys, TAP and the echoes as they die away
                    // the SAMPLE PLAYER's own file: any WAV / FLAC / AIFF / M4A / MP3 ... instead of a built-in sample
                    Section { id: smpFile; objectName: "samplerFile"; visible: ed.fx === "sampler"; head: "YOUR FILE"; width: 236
                        property string note: ""
                        property var dlg: null   // the system file picker, made only where Qt has QtQuick.Dialogs (the Windows build does)
                        readonly property string current: { ed.rev; return ed.fx === "sampler" ? engine.sampleFileOf(ed.strip, ed.slot) : "" }
                        function load(p) { const e = engine.loadSampleFile(ed.strip, ed.slot, p); smpFile.note = e === "" ? "Loaded: press PLAY" : e; ed.rev++ }
                        function browse() {
                            if (!dlg) { try { dlg = Qt.createQmlObject('import QtQuick.Dialogs; FileDialog { title: i18n.t("Choose an audio file", i18n.lang); nameFilters: ["Audio files (*.wav *.flac *.aif *.aiff *.aifc *.m4a *.aac *.mp4 *.mp3 *.wma *.ogg *.oga *.opus *.caf)", "All files (*)"] }', smpFile, "samplerFileDialog")
                                              dlg.accepted.connect(function () { smpFile.load(dlg.selectedFile.toString()) }) } catch (e) { smpFile.note = "Type the file's path and press LOAD."; return } }
                            dlg.open() }
                        Text { x: 10; y: 4; width: 216; text: smpFile.current !== "" ? smpFile.current : "Built-in sample (SAMPLE knob)"; color: ed.ink; font.family: ed.face1; font.pixelSize: 11; font.bold: true; elide: Text.ElideMiddle }
                        TextField { id: smpPath; objectName: "samplerPath"; x: 10; y: 24; width: 216; height: 26; font.pixelSize: 9; placeholderText: i18n.t("path: WAV, FLAC, AIFF, M4A, MP3 ...", i18n.lang); onAccepted: smpFile.load(text.trim()) }
                        Text { x: 10; y: 56; width: 216; wrapMode: Text.WordWrap; text: smpFile.note; color: ed.ink; opacity: 0.75; font.family: ed.face1; font.pixelSize: 8; maximumLineCount: 3; elide: Text.ElideRight }
                        Row { x: 10; y: 96; spacing: 6
                            Repeater { model: [{ t: "BROWSE…", k: "browse" }, { t: "LOAD", k: "load" }, { t: "BUILT-IN", k: "builtin" }]
                                Rectangle { required property var modelData; objectName: "sampler_" + modelData.k; width: 68; height: 28; radius: 4; color: "transparent"; border.color: modelData.k === "builtin" ? ed.ink : ed.accent; border.width: 1.5
                                    opacity: modelData.k === "builtin" && smpFile.current === "" ? 0.4 : 1
                                    Text { anchors.centerIn: parent; text: i18n.t(parent.modelData.t, i18n.lang); color: parent.modelData.k === "builtin" ? ed.ink : ed.accent; font.family: ed.face1; font.pixelSize: 10; font.bold: true }
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        onClicked: { const k = parent.modelData.k; if (k === "browse") smpFile.browse(); else if (k === "load") smpFile.load(smpPath.text.trim()); else if (smpFile.current !== "") { engine.loadSampleFile(ed.strip, ed.slot, ""); smpFile.note = ""; ed.rev++ } } } } } }
                    }
                    Section { id: dly; objectName: "delayTime"; visible: ed.fx === "syncdelay" && ed.params.length >= 3; head: "DELAY TIME"; width: 236
                        readonly property real bpm: ed.params[0] ? 60 + ed.params[0].value * 140 : 120
                        readonly property int note: ed.params[1] ? Math.min(5, Math.floor(ed.params[1].value * 5.999)) : 1
                        readonly property real ms: 60000 / bpm * [2, 1, 0.75, 0.5, 1 / 3, 0.25][note]
                        readonly property real fb: ed.params[2] ? ed.params[2].value * 0.92 : 0
                        property var taps: []
                        function tap() { const now = Date.now(); let t = taps.filter(function (x) { return now - x < 2200 }); t.push(now); if (t.length > 5) t = t.slice(t.length - 5); taps = t
                            if (t.length >= 2) { const b = 60000 * (t.length - 1) / (t[t.length - 1] - t[0]); ed.setParam(0, Math.max(0, Math.min(1, (b - 60) / 140))); ed.rev++ } }
                        Rectangle { x: 10; y: 2; width: 216; height: 86; radius: 4; color: "#0d1410"; border.color: "#22201b"; border.width: 1.5
                            Text { objectName: "delayMs"; x: 10; y: 4; text: dly.ms.toFixed(0); color: "#7dffa8"; font.family: Theme.mono; font.pixelSize: 30; font.bold: true }
                            Text { x: 10; y: 40; text: i18n.t("ms", i18n.lang); color: "#4fae74"; font.family: Theme.mono; font.pixelSize: 9 }
                            Text { anchors.right: parent.right; anchors.rightMargin: 10; y: 6; text: dly.bpm.toFixed(0) + " BPM"; color: "#7dffa8"; font.family: Theme.mono; font.pixelSize: 12; font.bold: true }
                            Text { anchors.right: parent.right; anchors.rightMargin: 10; y: 24; text: ["1/2", "1/4", "1/8 .", "1/8", "1/8 T", "1/16"][dly.note]; color: "#4fae74"; font.family: Theme.mono; font.pixelSize: 11 }
                            Row { x: 10; y: 54; spacing: 4; height: 26
                                Repeater { model: 12
                                    delegate: Item { width: 13; height: 26
                                        Rectangle { anchors.bottom: parent.bottom; width: 13; radius: 1; height: Math.max(1, 26 * Math.pow(dly.fb, index)); color: index === 0 ? "#7dffa8" : "#4fae74"; opacity: Math.pow(dly.fb, index) > 0.02 ? 1 : 0.25 } } } } }
                        Grid { x: 10; y: 96; columns: 3; columnSpacing: 6; rowSpacing: 6
                            Repeater { model: ["1/2", "1/4", "1/8 .", "1/8", "1/8 T", "1/16"]
                                delegate: Rectangle { width: 68; height: 24; radius: 3; color: dly.note === index ? ed.ink : "transparent"; border.color: ed.ink; border.width: 1.2
                                    Text { anchors.centerIn: parent; text: modelData; color: dly.note === index ? ed.onInk : ed.ink; font.family: ed.face1; font.pixelSize: 12; font.weight: Font.DemiBold }
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { ed.setParam(1, (index + 0.5) / 6); ed.rev++ } } } } }
                        Rectangle { x: 10; y: 160; width: 216; height: 34; radius: 4; color: tapA.pressed ? ed.accent : "transparent"; border.color: ed.accent; border.width: 1.5
                            Text { anchors.centerIn: parent; text: i18n.t("TAP TEMPO", i18n.lang); color: tapA.pressed ? ed.onInk : ed.accent; font.family: ed.face1; font.pixelSize: 13; font.weight: Font.Bold; font.letterSpacing: 2 }
                            MouseArea { id: tapA; objectName: "delayTap"; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onPressed: dly.tap() } }
                        Text { x: 10; y: 198; width: 216; horizontalAlignment: Text.AlignHCenter; text: i18n.t("TAP IN TIME WITH THE MUSIC", i18n.lang); color: ed.ink; opacity: 0.7; font.family: ed.face1; font.pixelSize: 8; font.letterSpacing: 1 } }
                    // THE LIVE DISPLAY of a family: the saturation curve, the reverb tail, or the sweep of the modulation
                    Section { objectName: "liveView"; visible: ed.view !== ""; head: ed.view === "curve" ? "TRANSFER" : ed.view === "decay" ? "TAIL" : "SWEEP"; width: 176
                        Rectangle { x: 10; y: 6; width: 156; height: 150; radius: 4; color: "#0d1410"; border.color: "#22201b"; border.width: 1.5
                            Canvas { id: lv; anchors.fill: parent; anchors.margins: 6
                                readonly property real a: ed.pv(0, 0.3); readonly property real b: ed.pv(1, 0.3); readonly property string kind: ed.view; readonly property int rev: ed.rev
                                onAChanged: requestPaint(); onBChanged: requestPaint(); onKindChanged: requestPaint(); onRevChanged: requestPaint(); Component.onCompleted: requestPaint()
                                onPaint: { const c = getContext("2d"), w = width, h = height; c.reset(); c.strokeStyle = "#1f3a29"; c.lineWidth = 1
                                    for (let k = 1; k < 4; k++) { c.beginPath(); c.moveTo(w * k / 4, 0); c.lineTo(w * k / 4, h); c.moveTo(0, h * k / 4); c.lineTo(w, h * k / 4); c.stroke() }
                                    c.strokeStyle = "#7dffa8"; c.lineWidth = 2; c.beginPath()
                                    if (kind === "curve") { const g = 1 + a * 7, n = Math.tanh(g); for (let i = 0; i <= 60; i++) { const x = i / 30 - 1, y = Math.tanh(g * x) / n; if (i) c.lineTo(i / 60 * w, h / 2 - y * h * 0.46); else c.moveTo(0, h / 2 - y * h * 0.46) }
                                        c.stroke(); c.strokeStyle = "#4fae74"; c.lineWidth = 1; c.beginPath(); c.moveTo(0, h * 0.96); c.lineTo(w, h * 0.04) }
                                    else if (kind === "decay") { const t = 0.12 + Math.max(a, b) * 0.88; for (let i = 0; i <= 60; i++) { const x = i / 60, y = Math.exp(-x * 5 / t); if (i) c.lineTo(x * w, h - y * h * 0.92); else c.moveTo(0, h - y * h * 0.92) }
                                        c.stroke(); c.strokeStyle = "#4fae74"; c.lineWidth = 1; c.beginPath(); for (let k = 0; k < 14; k++) { const x = k / 14, y = Math.exp(-x * 5 / t) * (k % 2 ? 0.55 : 0.8); c.moveTo(x * w + 3, h); c.lineTo(x * w + 3, h - y * h * 0.92) } }
                                    else { const cyc = 1 + a * 5, d = 0.15 + b * 0.8; for (let i = 0; i <= 90; i++) { const x = i / 90, y = Math.sin(x * cyc * 2 * Math.PI) * d; if (i) c.lineTo(x * w, h / 2 - y * h * 0.46); else c.moveTo(0, h / 2) } }
                                    c.stroke() } } }
                        Text { x: 10; y: 162; width: 156; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; lineHeight: 1.15
                            text: ed.view === "curve" ? "OUT AGAINST IN\nTHE BEND IS THE DRIVE" : ed.view === "decay" ? "HOW THE ROOM\nDIES AWAY" : "RATE ACROSS\nDEPTH UP AND DOWN"; color: ed.ink; opacity: 0.75; font.family: ed.face1; font.pixelSize: 8; font.letterSpacing: 1; style: Text.Sunken; styleColor: ed.hi } }
                    // the name plate, the lamp and the ON / OFF switch
                    Item { width: 150; height: ed.panelH - 16
                        MouseArea { anchors.fill: parent; property point p0; cursorShape: Qt.SizeAllCursor; onPressed: function (m) { p0 = Qt.point(m.x, m.y) } onPositionChanged: function (m) { if (pressed) { ed.x += (m.x - p0.x) * ed.fit; ed.y += (m.y - p0.y) * ed.fit } } }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; y: 6; text: i18n.t("AMFB  X1", i18n.lang); color: ed.plateInk; font.family: ed.face1; style: Text.Sunken; styleColor: ed.hi; font.pixelSize: 17; font.weight: Font.Bold; font.letterSpacing: 2 }
                        Image { anchors.horizontalCenter: parent.horizontalCenter; y: 28; width: 40; height: 40; source: "qrc:/qt/qml/AmixOS/skin/badge.png"; smooth: true; mipmap: true }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; y: 68; width: parent.width - 6; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; text: i18n.t(ed.title.toUpperCase(), i18n.lang); color: ed.plateInk; font.family: ed.face1; style: Text.Sunken; styleColor: ed.hi; font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 1 }
                        // THE POWER BUTTON: a backlit round key with the power symbol; its ring glows while the unit works and goes dark when bypassed
                        Item { id: pwr; objectName: "powerButton"; anchors.horizontalCenter: parent.horizontalCenter; y: 100; width: 76; height: 76
                            readonly property bool on: !ed.bypass; readonly property color glow: "#46c8ff"
                            Repeater { model: 3; delegate: Rectangle { anchors.centerIn: parent; width: 76 + index * 8; height: width; radius: width / 2; color: "transparent"; border.color: pwr.glow; border.width: 2; opacity: pwr.on ? 0.32 - index * 0.1 : 0 } }   // the halo
                            Rectangle { anchors.centerIn: parent; width: 72; height: 72; radius: 36; border.color: "#05060a"; border.width: 1.5   // the bezel
                                gradient: Gradient { GradientStop { position: 0; color: "#d4d8de" } GradientStop { position: 0.5; color: "#8a9099" } GradientStop { position: 1; color: "#4a4f57" } } }
                            Rectangle { anchors.centerIn: parent; width: 60; height: 60; radius: 30; border.color: pwr.on ? pwr.glow : "#2a2e35"; border.width: 2.5   // the key
                                gradient: Gradient { GradientStop { position: 0; color: pa.pressed ? "#0b0d10" : "#2a2e35" } GradientStop { position: 1; color: pa.pressed ? "#181b20" : "#0e1013" } } }
                            Canvas { id: pwrIcon; anchors.centerIn: parent; width: 30; height: 30; readonly property bool on: pwr.on; onOnChanged: requestPaint(); Component.onCompleted: requestPaint()
                                onPaint: { const c = getContext("2d"); c.reset(); c.strokeStyle = on ? "#bfeeff" : "#59606a"; c.lineWidth = 3; c.lineCap = "round"
                                    c.beginPath(); c.arc(15, 16, 11, -Math.PI / 2 + 0.75, Math.PI * 1.5 - 0.75, false); c.stroke(); c.beginPath(); c.moveTo(15, 2); c.lineTo(15, 14); c.stroke() } }
                            MouseArea { id: pa; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { engine.setPluginBypass(ed.strip, ed.slot, !ed.bypass); ed.bypass = !ed.bypass } } }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; y: 182; text: ed.bypass ? i18n.t("BYPASSED", i18n.lang) : i18n.t("ACTIVE", i18n.lang); color: ed.bypass ? "#8a9099" : "#46c8ff"; font.family: ed.face1; font.pixelSize: 10; font.weight: Font.Bold; font.letterSpacing: 2 }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; y: 204; horizontalAlignment: Text.AlignHCenter; text: "AMFB AUDIO\n" + (ed.fx ? "CH " + (ed.strip + 1) + " · SLOT " + (ed.slot + 1) : (ed.kind.indexOf("vst2:") === 0 ? "VST2" : "VST3") + " HOSTED"); color: ed.plateInk; font.family: ed.face1; style: Text.Sunken; styleColor: ed.hi; font.pixelSize: 9; font.letterSpacing: 2; lineHeight: 1.2 } }
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
                        Section { head: i18n.t("BLEND", i18n.lang); width: 150; height: ed.panelH - 16 - 110
                            Column { visible: ed.mixIndex >= 0; anchors.horizontalCenter: parent.horizontalCenter; y: 0; width: 84; spacing: 0
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: i18n.t("MIX", i18n.lang) + "  " + (ed.params[ed.mixIndex] ? ed.params[ed.mixIndex].text : ""); color: ed.ink; font.family: ed.face1; style: Text.Sunken; styleColor: ed.hi; font.pixelSize: 9; font.weight: Font.DemiBold }
                                Knob { anchors.horizontalCenter: parent.horizontalCenter; width: 84; height: 74; size: 44; vintage: true; paper: ed.ink; minText: "0"; maxText: "100 %"; defaultValue: ed.params[ed.mixIndex] ? ed.params[ed.mixIndex]["default"] : 1
                                    readonly property int rev: ed.rev; onRevChanged: sync(); Component.onCompleted: sync(); function sync() { if (ed.params[ed.mixIndex]) value = ed.params[ed.mixIndex].value } onMoved: function (v) { ed.setParam(ed.mixIndex, v) } } }
                            Text { visible: ed.mixIndex < 0; anchors.centerIn: parent; text: "—"; color: ed.ink } } }
                }
                Key { x: parent.width - 36; y: 6; width: 24; height: 21; label: "✕"; onClicked: ed.close() }
            }
            // THE CONTROL STRIP: compare A / B, the program window with its arrows, reset and bypass, as backlit square keys
            RackUnit { id: stripUnit; objectName: "controlStrip"; height: 66
                Row { id: stripRow; x: 14; y: 9; spacing: 10
                    Inset { width: 150; Row { anchors.centerIn: parent; spacing: 8
                        Text { anchors.verticalCenter: parent.verticalCenter; text: i18n.t("COMPARE", i18n.lang); color: ed.ink; font.family: ed.face1; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 1 }
                        LitKey { label: "A"; lit: ed.side === "A"; onClicked: ed.pick("A") } LitKey { label: "B"; lit: ed.side === "B"; onClicked: ed.pick("B") } } }
                    Inset { width: 330; Row { anchors.centerIn: parent; spacing: 8
                        Text { anchors.verticalCenter: parent.verticalCenter; text: i18n.t("PROGRAM", i18n.lang); color: ed.ink; font.family: ed.face1; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 1 }
                        LitKey { label: "◁"; enabled: ed.presets.length > 0; onClicked: ed.preset(-1) }
                        Rectangle { objectName: "programWindow"; anchors.verticalCenter: parent.verticalCenter; width: 150; height: 32; radius: 3; color: "#120805"; border.color: "#000"   // amber segments behind smoked glass
                            Text { anchors.centerIn: parent; width: parent.width - 10; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                                text: ed.presetAt >= 0 ? (ed.presetAt + 1 < 10 ? "0" : "") + (ed.presetAt + 1) + "  " + ed.presets[ed.presetAt].name.toUpperCase() : ed.presets.length ? "-- " + ed.presets.length + " PROGRAMS" : i18n.t("-- MANUAL --", i18n.lang)
                                color: "#ff9a3c"; style: Text.Outline; styleColor: "#40ff6a00"; font.family: Theme.mono; font.pixelSize: 12; font.bold: true } }
                        LitKey { label: "▷"; enabled: ed.presets.length > 0; onClicked: ed.preset(1) } } }
                    Inset { width: 104; Row { anchors.centerIn: parent; spacing: 8
                        Text { anchors.verticalCenter: parent.verticalCenter; text: i18n.t("RESET", i18n.lang); color: ed.ink; font.family: ed.face1; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 1 }
                        LitKey { objectName: "resetKey"; label: "↺"; onClicked: ed.resetAll() } } }
                    Inset { width: 118; Row { anchors.centerIn: parent; spacing: 8
                        Text { anchors.verticalCenter: parent.verticalCenter; text: i18n.t("BYPASS", i18n.lang); color: ed.ink; font.family: ed.face1; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 1 }
                        LitKey { label: ""; lit: ed.bypass; litColor: "#ff4a3a"; onClicked: { engine.setPluginBypass(ed.strip, ed.slot, !ed.bypass); ed.bypass = !ed.bypass } } } }
                    // a hosted VST opens in this AMFB panel (its parameters on our knobs); the plug-in's own window is one press away
                    Inset { visible: ed.kind.startsWith("vst3:") || ed.kind.startsWith("vst2:"); width: 168; Row { anchors.centerIn: parent; spacing: 8
                        Text { anchors.verticalCenter: parent.verticalCenter; text: i18n.t("PLUG-IN WINDOW", i18n.lang); color: ed.ink; font.family: ed.face1; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 1 }
                        LitKey { objectName: "nativeKey"; label: "⧉"; ToolTip.visible: ed.nativeNote !== ""; ToolTip.text: ed.nativeNote; onClicked: { if (!engine.openPluginWindow(ed.strip, ed.slot)) ed.nativeNote = i18n.t("This plug-in has no window of its own here: its controls are on this panel.", i18n.lang) } } } } } }
            // THE DISPLAY: what is playing (the spectrum of the channel), the curve the unit draws (EQs) and the levels in and out
            RackUnit { id: dispUnit; objectName: "displayUnit"; height: 214
                Rectangle { x: 14; y: 12; width: parent.width - 28; height: parent.height - 24; radius: 10; color: "#06080b"; border.color: "#000"; border.width: 2; clip: true
                    gradient: Gradient { GradientStop { position: 0; color: "#121a22" } GradientStop { position: 1; color: "#06090c" } }
                    Canvas { id: disp; objectName: "displayCurve"; x: 10; y: 8; width: parent.width - 250; height: parent.height - 16
                        readonly property int rev: ed.rev; onRevChanged: requestPaint(); Component.onCompleted: requestPaint()
                        onPaint: { const c = getContext("2d"), w = width, h = height; c.reset()
                            const fx = function (f) { return Math.log(f / 20) / Math.log(1000) * w }, fy = function (d) { return h / 2 - d / 18 * (h / 2 - 8) }
                            c.strokeStyle = "#1b2a36"; c.lineWidth = 1; c.fillStyle = "#4f6b80"; c.font = "9px sans-serif"
                            for (const f of [50, 100, 200, 500, 1000, 2000, 5000, 10000]) { c.beginPath(); c.moveTo(fx(f), 0); c.lineTo(fx(f), h); c.stroke(); c.fillText(f >= 1000 ? (f / 1000) + "k" : "" + f, fx(f) + 3, h - 4) }
                            for (const d of [-12, -6, 0, 6, 12]) { c.beginPath(); c.moveTo(0, fy(d)); c.lineTo(w, fy(d)); c.stroke(); c.fillText((d > 0 ? "+" : "") + d, 3, fy(d) - 2) }
                            const sp = ed.spec; if (sp && sp.length) {   // the spectrum, a soft filled mountain behind the curve
                                c.beginPath(); c.moveTo(0, h); for (let i = 0; i < sp.length; i++) { const f = 20 * Math.pow(1000, i / (sp.length - 1)), y = h - Math.max(0, Math.min(1, (sp[i] + 90) / 84)) * (h - 6); c.lineTo(fx(f), y) }
                                c.lineTo(w, h); c.closePath(); const g = c.createLinearGradient(0, 0, 0, h); g.addColorStop(0, "#5a7f9c"); g.addColorStop(1, "#0d151c"); c.fillStyle = g; c.globalAlpha = 0.55; c.fill(); c.globalAlpha = 1 }
                            const curves = ed.curves(); for (let k = 0; k < curves.length; k++) { const cv = curves[k]; c.strokeStyle = cv.color; c.lineWidth = cv.width; c.beginPath()
                                for (let i = 0; i <= 160; i++) { const f = 20 * Math.pow(1000, i / 160), y = fy(Math.max(-18, Math.min(18, cv.db(f)))); if (i) c.lineTo(i / 160 * w, y); else c.moveTo(0, y) } c.stroke() }
                            for (let k = 0; k < curves.length; k++) { c.fillStyle = curves[k].color; c.fillText(curves[k].name, w - 70, 14 + k * 13) } } }
                    // the meters: input and output, peak bars with a scale, like the right-hand side of a mastering display
                    Row { x: parent.width - 232; y: 14; spacing: 14
                        Repeater { model: [{ t: i18n.t("INPUT", i18n.lang), k: "in" }, { t: i18n.t("OUTPUT", i18n.lang), k: "out" }]
                            delegate: Column { spacing: 4; required property var modelData
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: i18n.t(modelData.t ?? "", i18n.lang); color: "#9fb7c9"; font.family: ed.face1; font.pixelSize: 10; font.weight: Font.DemiBold; font.letterSpacing: 1 }
                                Row { spacing: 4; anchors.horizontalCenter: parent.horizontalCenter
                                    Repeater { model: 2; delegate: Rectangle { width: 14; height: 150; color: "#0a0f14"; border.color: "#1f2b36"
                                        readonly property real db: modelData.k === "in" ? ed.inDb : ed.outDb
                                        Rectangle { anchors.bottom: parent.bottom; anchors.bottomMargin: 1; x: 1; width: 12; height: Math.max(0, Math.min(1, (parent.db + 72) / 72)) * 148
                                            gradient: Gradient { GradientStop { position: 0; color: "#e9f6ff" } GradientStop { position: 0.25; color: "#64b5f6" } GradientStop { position: 1; color: "#1e5a8a" } } }
                                        Rectangle { y: 1 + 148 * (6 / 72) ; x: 1; width: 12; height: 1; color: "#ff5a4a" } } } } } }
                        Column { spacing: 0; y: 18
                            Repeater { model: [0, -12, -24, -36, -48, -60, -72]; delegate: Text { height: 150 / 6.2; text: modelData; color: "#4f6b80"; font.pixelSize: 9; font.family: Theme.mono } } } } } }
        }
    }
}
