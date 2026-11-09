// The design language of the AmF X1 console, carried over: obsidian background, glass panels, WING orange rail, amber accents, mono type.
pragma Singleton
import QtQuick

QtObject {
    readonly property color obsidian: "#040507"
    readonly property color card: "#0f141c"
    readonly property color cardRaised: "#141b26"
    readonly property color line: Qt.rgba(1, 1, 1, 0.14)
    readonly property color lineSoft: Qt.rgba(1, 1, 1, 0.06)
    readonly property color ink: "#f1f5f9"
    readonly property color muted: "#94a3b8"
    readonly property color dim: "#64748b"
    readonly property color orange: "#ea580c"
    readonly property color orangeGlow: Qt.rgba(1, 0.416, 0, 0.45)
    readonly property color amber: "#fbbf24"
    readonly property color cyan: "#00f2ff"
    readonly property color emerald: "#10b981"
    readonly property color danger: "#ff2a5f"
    readonly property color violet: "#8b5cf6"
    readonly property color pink: "#f472b6"
    readonly property string mono: "JetBrains Mono, DejaVu Sans Mono, Consolas, monospace"
    readonly property string sans: "Inter, Segoe UI, Roboto, sans-serif"
    readonly property int radius: 10
    readonly property var stripColors: ["#f59e0b", "#22d3ee", "#a78bfa", "#34d399", "#f472b6", "#fb923c", "#60a5fa", "#facc15"]
    function groupColor(g) {
        switch (g) {
        case "MIX": return amber; case "ROUTE": return "#818cf8"; case "METER": return emerald; case "SOURCE": return cyan;
        case "DEVICE": return "#f59e0b"; case "SYSTEM": return "#a1a1aa"; case "HELP": return "#38bdf8"; default: return orange;
        }
    }
    function dbText(v) { return v <= -119 ? "-inf" : (v > 0 ? "+" : "") + v.toFixed(1); }
}
