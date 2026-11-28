// ICON: the console's own line icons, drawn (24 x 24 grid, round strokes) so they stay sharp at every size and take any colour.
// name: an icon name or a page id (the rail passes page ids; they map to icons below). Unknown names draw nothing (has() is false).
import QtQuick
import AmixOS

Canvas {
    id: ic
    property string name
    property color color: Theme.ink
    property real stroke: 1.8
    implicitWidth: 20; implicitHeight: 20
    readonly property string key: alias[name] !== undefined ? alias[name] : name
    readonly property bool has: paths[key] !== undefined
    onKeyChanged: requestPaint(); onColorChanged: requestPaint(); onWidthChanged: requestPaint(); onHeightChanged: requestPaint()
    onPaint: { const c = getContext("2d"); c.reset(); const d = paths[key]; if (!d) return; const s = Math.min(width, height) / 24;
        c.save(); c.translate((width - 24 * s) / 2, (height - 24 * s) / 2); c.scale(s, s); c.lineWidth = stroke; c.lineCap = "round"; c.lineJoin = "round";
        c.strokeStyle = color; c.beginPath(); c.path = d; c.stroke(); c.restore() }
    readonly property var alias: ({ dsp: "tone", miceq: "tone", rtaeq: "bars", dyn: "comp", bus: "send", dca: "grid", io: "plug", meter: "meter", lufs: "gauge",
        osc: "sine", asio: "card", sys: "sliders", acct: "user", access: "key", renew: "badge", calib: "target", boot: "pulse", engctl: "bolt", ai: "chip",
        assistant: "chat", master: "diamond", surround: "orbit", live: "radio", media: "play", acoustics: "ripple", presets: "list", scenes: "flag", frame: "rack" })
    readonly property var paths: ({
        home: "M3 11 L12 4 L21 11 M5 10 V20 H19 V10 M10 20 V14 H14 V20",
        mixer: "M6 3 V21 M12 3 V21 M18 3 V21 M4 8 H8 M10 15 H14 M16 11 H20",
        arrange: "M3 4 H12 V8 H3 Z M8 10 H21 V14 H8 Z M3 16 H15 V20 H3 Z",
        tone: "M2 15 C6 15 7 6 10 6 C13 6 13 18 16 18 C19 18 19 12 22 12",
        bars: "M4 20 V14 M8 20 V8 M12 20 V11 M16 20 V5 M20 20 V12",
        diamond: "M12 3 L21 12 L12 21 L3 12 Z M12 8 L16 12 L12 16 L8 12 Z",
        comp: "M3 3 V21 H21 M3 21 L11 13 L21 9",
        gate: "M3 18 H8 V6 H16 V18 H21",
        fx: "M12 3 L13.8 10.2 L21 12 L13.8 13.8 L12 21 L10.2 13.8 L3 12 L10.2 10.2 Z",
        delay: "M4 12 H6 M9 8 A6 6 0 0 1 9 16 M13 5 A10 10 0 0 1 13 19 M17 3 A13 13 0 0 1 17 21",
        orbit: "M3 12 A9 9 0 1 0 21 12 A9 9 0 1 0 3 12 M9 12 A3 3 0 1 0 15 12 A3 3 0 1 0 9 12",
        send: "M3 12 H15 M11 7 L16 12 L11 17 M20 5 V19",
        grid: "M4 4 H10 V10 H4 Z M14 4 H20 V10 H14 Z M4 14 H10 V20 H4 Z M14 14 H20 V20 H14 Z",
        plug: "M9 3 V8 M15 3 V8 M6 8 H18 V12 A6 6 0 0 1 6 12 Z M12 18 V21",
        routing: "M4 6 H9 L15 18 H20 M4 18 H9 L11.5 13 M14 6 H20",
        rack: "M3 5 H21 V19 H3 Z M3 10 H21 M3 14 H21 M6 7.5 H8 M6 12 H8 M6 16.5 H8",
        amp: "M5 3 H19 V21 H5 Z M9 15 A3 3 0 1 0 15 15 A3 3 0 1 0 9 15 M11 7 H13",
        meter: "M5 21 V9 M10 21 V4 M15 21 V12 M20 21 V7",
        score: "M12 3 L14.6 9 L21 9.6 L16.2 14 L17.6 20.4 L12 17 L6.4 20.4 L7.8 14 L3 9.6 L9.4 9 Z",
        gauge: "M4 17 A8 8 0 1 1 20 17 M12 13 L16 8",
        sine: "M2 12 C5 4 7 4 9 12 C11 20 13 20 15 12 C17 4 19 4 22 12",
        phase: "M12 3 C20 3 20 21 12 21 C4 21 4 3 12 3 Z M3 12 H21",
        ripple: "M8 8 A6 6 0 0 0 8 16 M5 5 A10 10 0 0 0 5 19 M13 12 H21",
        play: "M7 4 L19 12 L7 20 Z",
        radio: "M8.5 8.5 A5 5 0 0 0 8.5 15.5 M15.5 8.5 A5 5 0 0 1 15.5 15.5 M5.5 5.5 A9.5 9.5 0 0 0 5.5 18.5 M18.5 5.5 A9.5 9.5 0 0 1 18.5 18.5 M11 12 H13",
        chip: "M7 7 H17 V17 H7 Z M10 10 H14 V14 H10 Z M9 3 V7 M15 3 V7 M9 17 V21 M15 17 V21 M3 9 H7 M3 15 H7 M17 9 H21 M17 15 H21",
        chat: "M4 5 H20 V16 H10 L5 20 V16 H4 Z M8 10.5 H8.5 M12 10.5 H12.5 M16 10.5 H16.5",
        bolt: "M13 2 L4 14 H11 L10 22 L20 9 H13 Z",
        tools: "M4 20 L13 11 M14 4 A5 5 0 0 0 20 10 L17 10 L14 7 Z",
        plugins: "M12 2 L21 7 V17 L12 22 L3 17 V7 Z M12 22 V12 M21 7 L12 12 L3 7",
        samples: "M4 8 A8 3 0 1 0 20 8 A8 3 0 1 0 4 8 M4 8 V16 A8 3 0 0 0 20 16 V8",
        card: "M3 7 H21 V17 H3 Z M7 17 V20 M11 17 V20 M15 17 V20 M7 10 H11 V14 H7 Z",
        bluetooth: "M7 7 L17 17 L12 22 V2 L17 7 L7 17",
        network: "M9 3 H15 V9 H9 Z M12 9 V15 M5 21 V15 H19 V21",
        cloud: "M7 18 H17 A4 4 0 0 0 17 10 A6 6 0 0 0 6 11 A3.5 3.5 0 0 0 7 18 Z",
        cascade: "M9 15 L15 9 M7 11 L5 13 A3.5 3.5 0 0 0 11 19 L13 17 M17 13 L19 11 A3.5 3.5 0 0 0 13 5 L11 7",
        list: "M4 6 H20 M4 12 H20 M4 18 H13 M16 16 L18 18 L22 14",
        sliders: "M4 6 H20 M4 12 H20 M4 18 H20 M9 4 V8 M15 10 V14 M7 16 V20",
        user: "M8 8 A4 4 0 1 0 16 8 A4 4 0 1 0 8 8 M4 21 C4 16 8 14 12 14 C16 14 20 16 20 21",
        key: "M3 15 A4 4 0 1 0 11 15 A4 4 0 1 0 3 15 M10 12 L20 2 M16 6 L19 9",
        badge: "M12 2 L15 5 H19 V9 L22 12 L19 15 V19 H15 L12 22 L9 19 H5 V15 L2 12 L5 9 V5 H9 Z M9 12 L11 14 L15 10",
        admin: "M12 3 L20 6 V12 C20 17 16 20 12 21 C8 20 4 17 4 12 V6 Z",
        target: "M12 3 V7 M12 17 V21 M3 12 H7 M17 12 H21 M7 12 A5 5 0 1 0 17 12 A5 5 0 1 0 7 12",
        help: "M3 12 A9 9 0 1 0 21 12 A9 9 0 1 0 3 12 M9.5 9.5 A2.5 2.5 0 1 1 13 11.8 C12.3 12.3 12 13 12 14 M12 17 V17.5",
        flag: "M5 21 V4 H17 L15 8 L17 12 H5",
        pulse: "M3 12 H7 L10 5 L14 19 L17 12 H21",
        store: "M5 8 H19 L18 21 H6 Z M9 8 V6 A3 3 0 0 1 15 6 V8",
        update: "M19 12 A7 7 0 1 1 16.5 6.6 M19 4 V8 H15 M12 8 V14 M9.5 11.5 L12 14 L14.5 11.5",   // a turning arrow with a download arrow inside
        streaming: "M10 14 L14 10 M8 12 L6 14 A3.5 3.5 0 0 0 11 19 L13 17 M16 12 L18 10 A3.5 3.5 0 0 0 13 5 L11 7",   // two chain links
        vst: "M4 4 H20 V20 H4 Z M8 8 V16 M12 8 V16 M16 8 V16 M6 11 H10 M10 14 H14 M14 10 H18",
        extension: "M12 2 L21 7 V17 L12 22 L3 17 V7 Z M9 12 H15 M12 9 V15",
        plus: "M12 5 V19 M5 12 H19",
        reverb: "M3 16 Q7 8 12 12 T21 8 M3 21 Q8 14 12 17 T21 13",
        drive: "M12 3 C16 8 18 11 18 15 A6 6 0 0 1 6 15 C6 12 8 10 9 8 C10 11 11 12 12 12 C12 9 11 6 12 3 Z",
        search: "M4 11 A7 7 0 1 0 18 11 A7 7 0 1 0 4 11 M16 16 L21 21",
        folder: "M3 6 H9 L11 8 H21 V19 H3 Z",
        check: "M5 12 L10 17 L19 7",
        refresh: "M20 11 A8 8 0 1 0 18 17 M20 5 V11 H14",
        lock: "M6 11 H18 V21 H6 Z M8 11 V7 A4 4 0 0 1 16 7 V11",
        // 1.27: the ASSIGN page (a grid with a slash: groups that silence), the strip kinds and the MONITOR section
        assign: "M4 4 H10 V10 H4 Z M14 4 H20 V10 H14 Z M4 14 H10 V20 H4 Z M14 14 H20 V20 H14 Z M3 21 L21 3",
        mic: "M9 3 H15 V12 A3 3 0 0 1 9 12 Z M6 11 A6 6 0 0 0 18 11 M12 17 V21 M8 21 H16",
        inst: "M15 3 L21 9 M13 5 L19 11 M17 7 L9 15 M9 15 A4 4 0 1 1 5 19 A3 3 0 0 1 9 15",
        phones: "M4 15 A8 8 0 0 1 20 15 M4 14 H7 V20 H4 Z M17 14 H20 V20 H17 Z",
        monitor: "M4 9 H8 L13 5 V19 L8 15 H4 Z M16 9 A4 4 0 0 1 16 15 M18 6 A8 8 0 0 1 18 18",
        talk: "M4 5 H20 V15 H11 L7 19 V15 H4 Z M8 9 H16 M8 12 H13"
    })
}
