// The design language of the AmF X1 console: obsidian background, glass panels, WING orange rail, amber accents, mono type. Two modes:
// dark (the stage look) and light (daylight / print), switched by `dark` (Main.qml binds it to settings.darkTheme). Every colour is an
// explicit #RRGGBB or Qt.rgba; the pages never hard-code a background, they take deep / card / chrome and the ov() overlays from here.
pragma Singleton
import QtQuick

QtObject {
    property bool dark: true
    // COLOUR THEMES (SETTINGS > GENERAL > Colour theme): the console's own graphite, or CHOCOLATE (base #66370A), OCEAN, FOREST, PLUM. Each
    // is a dark family whose surfaces are lighter / darker tones of one base colour; ink, muted and dim keep WCAG AA (4.5:1) on every surface.
    // The light look (white) ignores the scheme. Faders, meters and panels take these surfaces, so they follow the theme.
    property string scheme: ""
    readonly property var schemes: ({
        chocolate: { obsidian: "#1e1003", deep: "#170c02", chrome: "#2a1604", card: "#2e1905", cardRaised: "#3f2207", head: "#66370A", ink: "#fdf3e7", muted: "#e3c9a8", dim: "#d0ad85", accent: "#e8a050" },
        ocean:     { obsidian: "#06141f", deep: "#040e16", chrome: "#081a28", card: "#0b2233", cardRaised: "#123247", head: "#0b3d5c", ink: "#eef7fc", muted: "#a9cde0", dim: "#86afc6", accent: "#3cc8db" },
        forest:    { obsidian: "#0a140d", deep: "#070f09", chrome: "#0f1d14", card: "#13241a", cardRaised: "#1b3324", head: "#1f4d2b", ink: "#f0f8f1", muted: "#b4d4bb", dim: "#9cc2a3", accent: "#34d399" },
        plum:      { obsidian: "#160b17", deep: "#100711", chrome: "#1c0f1e", card: "#241326", cardRaised: "#321b35", head: "#4a1f4d", ink: "#fbf0fc", muted: "#dcbfe0", dim: "#b896bc", accent: "#c084fc" } })
    readonly property var pal: dark && schemes[scheme] !== undefined ? schemes[scheme] : null
    readonly property color obsidian: pal ? pal.obsidian : dark ? "#0d0f12" : "#e8ebf0"      // the window
    readonly property color deep: pal ? pal.deep : dark ? "#0a0b0d" : "#dfe3ea"          // meters, canvases, fader tracks, text fields
    // GLASS: the header, the rail and the panels let the AURORA colour behind them through; cardSolid is for menus and popups over a page
    property bool glass: false   // FLAT: every page is flat graphite tiles on a flat desk (true brings the translucent panels over the aurora back)
    property color accent: orange                                       // the colour of the section open now (Main.qml sets it)
    readonly property color chrome: pal ? pal.chrome : glass ? (dark ? Qt.rgba(0.027, 0.039, 0.063, 0.62) : Qt.rgba(1, 1, 1, 0.70)) : (dark ? "#121418" : "#ffffff")        // the header bar
    readonly property color card: pal ? pal.card : glass ? (dark ? Qt.rgba(0.047, 0.063, 0.094, 0.64) : Qt.rgba(1, 1, 1, 0.78)) : (dark ? "#16181c" : "#f4f6f9")
    readonly property color cardSolid: pal ? pal.card : dark ? "#16181c" : "#ffffff"
    readonly property color cardRaised: pal ? pal.cardRaised : dark ? "#1d2026" : "#eef1f6"
    readonly property color head: pal ? pal.head : dark ? "#262a31" : "#d5dae2"            // the header bar of a section
    readonly property color line: dark ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(0, 0, 0, 0.18)
    readonly property color lineSoft: dark ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(0, 0, 0, 0.09)
    readonly property color ink: pal ? pal.ink : dark ? "#f1f5f9" : "#0f172a"
    readonly property color muted: pal ? pal.muted : dark ? "#94a3b8" : "#3f4b5c"
    readonly property color dim: pal ? pal.dim : dark ? "#7b8aa0" : "#526074"       // AA (4.5:1) on the cards in both looks
    // MALAYSIA: the colours of songket cloth and the hibiscus (bunga raya). Gold is the console's own colour; red warns.
    readonly property color songket: dark ? "#e8b84a" : "#8f6508"        // the woven gold thread
    readonly property color bunga: dark ? "#f0304f" : "#d11a3c"          // hibiscus red
    readonly property color nila: dark ? "#3b5bdb" : "#1e3a8a"           // indigo, the ground of the cloth
    readonly property color orange: pal ? pal.accent : dark ? "#e8a93a" : "#8f6508"          // the console's accent: songket gold
    readonly property color orangeGlow: Qt.rgba(0.91, 0.72, 0.29, 0.45)
    readonly property color amber: dark ? "#fbbf24" : "#9a5504"
    readonly property color cyan: dark ? "#3cc8db" : "#0e7490"   // calmer than pure neon: the most used accent
    readonly property color emerald: dark ? "#10b981" : "#047857"
    readonly property color danger: dark ? "#f0304f" : "#d11a3c"
    readonly property color violet: dark ? "#8b5cf6" : "#6d28d9"
    readonly property color pink: dark ? "#f472b6" : "#b02a5f"
    readonly property string product: "AmF X1"
    readonly property string vendor: "AMFB Audio"
    readonly property string mono: "JetBrains Mono, DejaVu Sans Mono, Consolas, monospace"
    readonly property string sans: "Inter, Segoe UI, Roboto, sans-serif"
    readonly property int radius: 10
    readonly property var stripColors: ["#f59e0b", "#22d3ee", "#a78bfa", "#34d399", "#f472b6", "#fb923c", "#60a5fa", "#facc15"]
    // an overlay a pages puts on a panel: white on the dark theme, black on the light one, the same strength
    // BUTTON COLOUR HARMONY: every button colour passes through here, so that all of them share one saturation and lightness band
    // (no neon next to pastel) and the gradient only shifts gently. Glass (translucent) colours stay glass.
    function harm(c) { c = Qt.lighter(c, 1.0); if (c.a < 0.6) return c; const h = c.hslHue < 0 ? 0 : c.hslHue, sat = c.hslHue < 0 ? 0 : Math.min(c.hslSaturation, dark ? 0.58 : 0.55);
        return Qt.hsla(h, sat, Math.max(dark ? 0.46 : 0.40, Math.min(dark ? 0.58 : 0.52, c.hslLightness)), c.a) }
    function btnTop(c) { c = Qt.lighter(c, 1.0); if (c.a < 0.6) return Qt.rgba(c.r, c.g, c.b, Math.min(1, c.a * 1.1)); const h = harm(c); return Qt.hsla(Math.max(0, h.hslHue), Math.max(0, h.hslSaturation), Math.min(0.64, h.hslLightness + 0.02), h.a) }
    function btnBottom(c) { c = Qt.lighter(c, 1.0); if (c.a < 0.6) return Qt.rgba(c.r, c.g, c.b, c.a * 0.9); const h = harm(c); return Qt.hsla(Math.max(0, h.hslHue), Math.max(0, h.hslSaturation), Math.max(0.30, h.hslLightness - 0.02), h.a) }
    function ov(a) { return dark ? Qt.rgba(1, 1, 1, a) : Qt.rgba(0, 0, 0, a); }
    function groupColor(g) {
        switch (g) {
        case "MIX": return amber; case "ROUTE": return dark ? "#818cf8" : "#4f46e5"; case "METER": return emerald; case "SOURCE": return cyan;
        case "DEVICE": return "#f59e0b"; case "SYSTEM": return dark ? "#a1a1aa" : "#52525b"; case "HELP": return dark ? "#38bdf8" : "#0369a1"; default: return orange;
        }
    }
    // device kinds (engine: classifyDevice): a sign and a word for the lists and the header
    function kindIcon(k) { switch (k) { case "headphones": return "◖◗"; case "headset": return "◖◗"; case "speakers": return "◢"; case "microphone": return "⏚"; case "interface": return "☷"; case "bluetooth": return "ᛒ"; case "digital": return "⎚"; case "line": return "⇄"; case "app": return "▶"; case "wrapper": return "◇"; default: return "◌" } }
    // SPEAKERS ON A DIGITAL CABLE: an output Windows calls S/PDIF or HDMI is where the speakers / amplifier / soundbar are: shown as SPEAKERS,
    // with the cable after it (kindNote); a digital INPUT stays DIGITAL
    function outKindName(k) { return k === "digital" ? "SPEAKERS" : kindName(k) }
    function kindNote(k, name) { if (k !== "digital") return ""; const n = (name || "").toLowerCase(); return /hdmi|displayport|display audio|nvidia|amd high|intel\(r\) display/.test(n) ? " · HDMI" : " · S/PDIF" }
    function kindName(k) { switch (k) { case "headphones": return "HEADPHONES"; case "headset": return "HEADSET"; case "speakers": return "SPEAKERS"; case "microphone": return "MICROPHONE"; case "interface": return "AUDIO INTERFACE"; case "bluetooth": return "BLUETOOTH"; case "digital": return "DIGITAL OUT"; case "line": return "LINE"; case "app": return "APP AUDIO"; case "wrapper": return "ASIO WRAPPER"; default: return "DEVICE" } }
    function dbText(v) { return v <= -119 ? "-inf" : (v > 0 ? "+" : "") + v.toFixed(1); }
}
