// The design language of the AmF X1 console: obsidian background, glass panels, WING orange rail, amber accents, mono type. Two modes:
// dark (the stage look) and light (daylight / print), switched by `dark` (Main.qml binds it to settings.darkTheme). Every colour is an
// explicit #RRGGBB or Qt.rgba; the pages never hard-code a background, they take deep / card / chrome and the ov() overlays from here.
pragma Singleton
import QtQuick

QtObject {
    property bool dark: true
    readonly property color obsidian: dark ? "#040507" : "#f3f5f9"      // the window
    readonly property color deep: dark ? "#05070a" : "#e6eaf1"          // meters, canvases, fader tracks, text fields
    // GLASS: the header, the rail and the panels let the AURORA colour behind them through; cardSolid is for menus and popups over a page
    property bool glass: true
    property color accent: orange                                       // the colour of the section open now (Main.qml sets it)
    readonly property color chrome: glass ? (dark ? Qt.rgba(0.027, 0.039, 0.063, 0.62) : Qt.rgba(1, 1, 1, 0.70)) : (dark ? "#070a10" : "#ffffff")        // the header bar
    readonly property color card: glass ? (dark ? Qt.rgba(0.047, 0.063, 0.094, 0.64) : Qt.rgba(1, 1, 1, 0.78)) : (dark ? "#0f141c" : "#ffffff")
    readonly property color cardSolid: dark ? "#0f141c" : "#ffffff"
    readonly property color cardRaised: dark ? "#141b26" : "#eef1f6"
    readonly property color line: dark ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(0, 0, 0, 0.14)
    readonly property color lineSoft: dark ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(0, 0, 0, 0.07)
    readonly property color ink: dark ? "#f1f5f9" : "#0f172a"
    readonly property color muted: dark ? "#94a3b8" : "#475569"
    readonly property color dim: dark ? "#64748b" : "#64748b"
    readonly property color orange: "#ea580c"
    readonly property color orangeGlow: Qt.rgba(1, 0.416, 0, 0.45)
    readonly property color amber: dark ? "#fbbf24" : "#d97706"
    readonly property color cyan: dark ? "#00f2ff" : "#0e7490"
    readonly property color emerald: dark ? "#10b981" : "#047857"
    readonly property color danger: dark ? "#ff2a5f" : "#dc2626"
    readonly property color violet: dark ? "#8b5cf6" : "#6d28d9"
    readonly property color pink: dark ? "#f472b6" : "#db2777"
    readonly property string product: "AmF X1"
    readonly property string vendor: "AMFB Audio"
    readonly property string mono: "JetBrains Mono, DejaVu Sans Mono, Consolas, monospace"
    readonly property string sans: "Inter, Segoe UI, Roboto, sans-serif"
    readonly property int radius: 10
    readonly property var stripColors: ["#f59e0b", "#22d3ee", "#a78bfa", "#34d399", "#f472b6", "#fb923c", "#60a5fa", "#facc15"]
    // an overlay a pages puts on a panel: white on the dark theme, black on the light one, the same strength
    // BUTTON COLOUR HARMONY: every button colour passes through here, so that all of them share one saturation and lightness band
    // (no neon next to pastel) and the gradient only shifts gently. Glass (translucent) colours stay glass.
    function harm(c) { if (c.a < 0.6) return c; const h = c.hslHue < 0 ? 0 : c.hslHue, sat = c.hslHue < 0 ? 0 : Math.min(c.hslSaturation, dark ? 0.58 : 0.55);
        return Qt.hsla(h, sat, Math.max(dark ? 0.46 : 0.40, Math.min(dark ? 0.58 : 0.52, c.hslLightness)), c.a) }
    function btnTop(c) { if (c.a < 0.6) return Qt.rgba(c.r, c.g, c.b, Math.min(1, c.a * 1.35)); const h = harm(c); return Qt.hsla(Math.max(0, h.hslHue), Math.max(0, h.hslSaturation), Math.min(0.66, h.hslLightness + 0.05), h.a) }
    function btnBottom(c) { if (c.a < 0.6) return Qt.rgba(c.r, c.g, c.b, c.a * 0.55); const h = harm(c); return Qt.hsla(Math.max(0, h.hslHue), Math.max(0, h.hslSaturation), Math.max(0.30, h.hslLightness - 0.06), h.a) }
    function ov(a) { return dark ? Qt.rgba(1, 1, 1, a) : Qt.rgba(0, 0, 0, a); }
    function groupColor(g) {
        switch (g) {
        case "MIX": return amber; case "ROUTE": return dark ? "#818cf8" : "#4f46e5"; case "METER": return emerald; case "SOURCE": return cyan;
        case "DEVICE": return "#f59e0b"; case "SYSTEM": return dark ? "#a1a1aa" : "#52525b"; case "HELP": return dark ? "#38bdf8" : "#0369a1"; default: return orange;
        }
    }
    // device kinds (engine: classifyDevice): a sign and a word for the lists and the header
    function kindIcon(k) { switch (k) { case "headphones": return "🎧"; case "headset": return "🎧"; case "speakers": return "🔊"; case "microphone": return "🎤"; case "interface": return "🎛"; case "bluetooth": return "ᛒ"; case "digital": return "⎚"; case "line": return "⇄"; case "app": return "▶"; case "wrapper": return "◇"; default: return "◌" } }
    function kindName(k) { switch (k) { case "headphones": return "HEADPHONES"; case "headset": return "HEADSET"; case "speakers": return "SPEAKERS"; case "microphone": return "MICROPHONE"; case "interface": return "AUDIO INTERFACE"; case "bluetooth": return "BLUETOOTH"; case "digital": return "DIGITAL OUT"; case "line": return "LINE"; case "app": return "APP AUDIO"; case "wrapper": return "ASIO WRAPPER"; default: return "DEVICE" } }
    function dbText(v) { return v <= -119 ? "-inf" : (v > 0 ? "+" : "") + v.toFixed(1); }
}
