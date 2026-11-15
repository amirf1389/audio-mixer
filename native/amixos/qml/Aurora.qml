// AURORA: the living colour behind every page. Four soft lights drift slowly across the window: one in the colour of the section
// open now (MIX amber, ROUTE indigo, METER green, SOURCE cyan ...), the others violet, cyan and pink; the glass panels on top let
// them through. Slow (half a minute per drift); each light is painted once and only moved, so it costs almost nothing; VIEW → Colour motion stops them (they stay, still) for slow
// computers. The light look uses them more faintly.
import QtQuick
import AmixOS

Item {
    id: au
    property bool motion: true
    property color accent: Theme.orange
    clip: true
    readonly property real s: Math.max(width, height)
    readonly property real k: Theme.dark ? 1.0 : 0.55       // the light look: fainter
    Rectangle { anchors.fill: parent; color: Theme.obsidian }
    component Blob: Canvas {
        id: b
        property color tint: "white"; property real size: 600; property real strength: 0.22
        property real bx: 0.5; property real by: 0.5            // home, 0..1 of the window
        property real fx: bx; property real fy: by              // where it is now
        property real dx: 0.2; property real dy: 0.15; property int period: 30000
        width: size; height: size; x: fx * au.width - size / 2; y: fy * au.height - size / 2
        renderTarget: Canvas.Image; renderStrategy: Canvas.Immediate
        onTintChanged: requestPaint(); onSizeChanged: requestPaint(); onStrengthChanged: requestPaint()
        Connections { target: Theme; function onDarkChanged() { b.requestPaint() } }
        onPaint: { const c = getContext("2d"); c.reset(); if (size <= 0) return; const r = size / 2, a = strength * au.k;
            const g = c.createRadialGradient(r, r, 0, r, r, r);
            g.addColorStop(0.0, Qt.rgba(tint.r, tint.g, tint.b, a)); g.addColorStop(0.45, Qt.rgba(tint.r, tint.g, tint.b, a * 0.45)); g.addColorStop(1.0, Qt.rgba(tint.r, tint.g, tint.b, 0));
            c.fillStyle = g; c.fillRect(0, 0, size, size) }
        SequentialAnimation on fx { running: au.motion && au.visible; loops: Animation.Infinite
            NumberAnimation { from: b.bx; to: b.bx + b.dx; duration: b.period; easing.type: Easing.InOutSine } NumberAnimation { from: b.bx + b.dx; to: b.bx; duration: b.period; easing.type: Easing.InOutSine } }
        SequentialAnimation on fy { running: au.motion && au.visible; loops: Animation.Infinite
            NumberAnimation { from: b.by; to: b.by + b.dy; duration: b.period * 1.3; easing.type: Easing.InOutSine } NumberAnimation { from: b.by + b.dy; to: b.by; duration: b.period * 1.3; easing.type: Easing.InOutSine } }
    }
    Blob { tint: au.accent; size: au.s * 0.95; strength: 0.46; bx: 0.18; by: 0.22; dx: 0.22; dy: 0.18; period: 26000 }
    Blob { tint: Theme.violet; size: au.s * 0.85; strength: 0.36; bx: 0.82; by: 0.18; dx: -0.18; dy: 0.25; period: 33000 }
    Blob { tint: Theme.cyan; size: au.s * 0.75; strength: 0.28; bx: 0.70; by: 0.85; dx: -0.25; dy: -0.15; period: 29000 }
    Blob { tint: Theme.pink; size: au.s * 0.65; strength: 0.26; bx: 0.25; by: 0.90; dx: 0.20; dy: -0.22; period: 37000 }
    // a fine grain of light at the top: the stage wash
    Rectangle { anchors.left: parent.left; anchors.right: parent.right; height: parent.height * 0.5
        gradient: Gradient { GradientStop { position: 0; color: Qt.rgba(au.accent.r, au.accent.g, au.accent.b, 0.07 * au.k) } GradientStop { position: 1; color: "transparent" } } }
}
