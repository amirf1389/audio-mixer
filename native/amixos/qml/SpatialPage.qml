// 360 AUDIO: each strip placed around the listener's head for headphones. The dial is the view from above (front at the top); drag a dot to
// set the direction and the distance. Our own binaural model (time and level difference, head shadow, rear low-pass): no HRTF data set and
// no licensed format. A strip with 360 off pans as before.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "spatialPage"
    property int sel: 0
    property int rev: 0
    readonly property var cur: { rev; return engine.spatial(sel) }
    readonly property var strips: engine.stripState
    function t(s) { return i18n.t(s, i18n.lang) }
    function put(on, az, el, dist, spread) { engine.setSpatial(sel, on, az, el, dist, spread); rev++ }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 22; radius: 4; color: on ? onColor : Theme.ov(0.08); border.color: Theme.lineSoft
        Text { id: bl; anchors.centerIn: parent; text: label; color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    component Knob: RowLayout { property string label; property real from; property real to; property real value; property string unit; signal moved(real v); spacing: 8; Layout.fillWidth: true
        Text { text: label; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; Layout.preferredWidth: 90 }
        Slider { Layout.fillWidth: true; from: parent.from; to: parent.to; value: parent.value; onMoved: parent.moved(value) }
        Text { text: parent.value.toFixed(parent.to > 30 ? 0 : 2) + " " + unit; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; Layout.preferredWidth: 60 } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle { Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Text { text: page.t("360 AUDIO:"); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Text { text: page.t("binaural placement for headphones · azimuth · elevation · distance · spread"); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Item { Layout.fillWidth: true }
                Btn { label: page.t("ALL OFF"); onClicked: { for (let i = 0; i < 32; i++) { const s = engine.spatial(i); if (s.on) engine.setSpatial(i, false, s.az, s.el, s.dist, s.spread) } page.rev++ } } } }
        RowLayout { id: body; Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            Rectangle { Layout.fillHeight: true; Layout.preferredWidth: Math.max(260, Math.min(page.height - 70, page.width * 0.5)); Layout.minimumWidth: 260; radius: Theme.radius; color: Theme.chrome; border.color: Theme.line; clip: true
                Canvas { id: dial; anchors.fill: parent; anchors.margins: 10
                    readonly property real rad: Math.min(width, height) / 2 - 14
                    property int tick: page.rev; onTickChanged: requestPaint()
                    Connections { target: Theme; function onDarkChanged() { dial.requestPaint() } }
                    onPaint: { const ctx = getContext("2d"), cx = width / 2, cy = height / 2; ctx.clearRect(0, 0, width, height); ctx.strokeStyle = Theme.ov(0.14); ctx.lineWidth = 1
                        for (let k = 1; k <= 4; k++) { ctx.beginPath(); ctx.arc(cx, cy, rad * k / 4, 0, 2 * Math.PI); ctx.stroke() }
                        ctx.beginPath(); ctx.moveTo(cx - rad, cy); ctx.lineTo(cx + rad, cy); ctx.moveTo(cx, cy - rad); ctx.lineTo(cx, cy + rad); ctx.stroke()
                        ctx.fillStyle = Theme.dim; ctx.font = "9px monospace"; ctx.fillText(page.t("FRONT"), cx - 14, cy - rad - 3); ctx.fillText(page.t("BEHIND"), cx - 16, cy + rad + 11); ctx.fillText("L", cx - rad - 10, cy + 3); ctx.fillText("R", cx + rad + 4, cy + 3)
                        ctx.fillStyle = Theme.ov(0.5); ctx.beginPath(); ctx.arc(cx, cy, 7, 0, 2 * Math.PI); ctx.fill(); ctx.beginPath(); ctx.moveTo(cx - 4, cy - 6); ctx.lineTo(cx, cy - 12); ctx.lineTo(cx + 4, cy - 6); ctx.fill()
                        for (let i = 0; i < 32; i++) { const s = engine.spatial(i); if (!s.on && i !== page.sel) continue; const a = s.az * Math.PI / 180, r = Math.min(1, Math.log(1 + s.dist) / Math.log(11)) * rad * Math.cos(s.el * Math.PI / 180), x = cx + Math.sin(a) * r, y = cy - Math.cos(a) * r
                            ctx.fillStyle = i === page.sel ? Theme.orange : Theme.cyan; ctx.globalAlpha = s.on ? 1 : 0.4; ctx.beginPath(); ctx.arc(x, y, i === page.sel ? 9 : 6, 0, 2 * Math.PI); ctx.fill(); ctx.globalAlpha = 1; ctx.fillStyle = "#000"; ctx.font = "bold 8px monospace"; ctx.fillText(i + 1, x - (i > 8 ? 5 : 2.5), y + 3) } }
                    MouseArea { anchors.fill: parent
                        function place(mx, my) { const dx = mx - width / 2, dy = height / 2 - my, r = Math.min(1, Math.hypot(dx, dy) / dial.rad), c = page.cur; page.put(true, Math.round(Math.atan2(dx, dy) * 180 / Math.PI), 0, Math.max(0.3, Math.pow(11, r) - 1), c.spread) }
                        onPressed: function (m) { place(m.x, m.y) } onPositionChanged: function (m) { if (pressed) place(m.x, m.y) } } } }
            ColumnLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 92; Layout.maximumHeight: 92; clip: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                    Flow { id: chFlow; x: 8; y: 8; width: parent.width - 16; spacing: 4
                        Repeater { model: 32; delegate: Rectangle { width: 42; height: 22; radius: 4; readonly property bool on: { page.rev; return engine.spatial(index).on }
                                color: page.sel === index ? Theme.orange : on ? Theme.cyan : Theme.ov(0.08); border.color: Theme.lineSoft
                                Text { anchors.centerIn: parent; text: (page.strips[index] && page.strips[index].name) ? page.strips[index].name.substring(0, 5) : "CH " + (index + 1); color: (page.sel === index || parent.on) ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true }
                                MouseArea { anchors.fill: parent; onClicked: page.sel = index } } } } }
                Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                    ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 10
                        RowLayout { spacing: 8
                            Text { text: "CH " + (page.sel + 1); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 12; font.bold: true }
                            Btn { label: page.cur.on ? page.t("360: ON") : page.t("360: OFF (PAN)"); on: page.cur.on; onColor: Theme.emerald; onClicked: page.put(!page.cur.on, page.cur.az, page.cur.el, page.cur.dist, page.cur.spread) }
                            Repeater { model: [["FRONT", 0], ["RIGHT", 90], ["BEHIND", 180], ["LEFT", -90]]; delegate: Btn { label: page.t(modelData[0]); onClicked: page.put(true, modelData[1], page.cur.el, page.cur.dist, page.cur.spread) } }
                            Item { Layout.fillWidth: true } }
                        Knob { label: page.t("AZIMUTH"); from: -180; to: 180; value: page.cur.az > 180 ? page.cur.az - 360 : page.cur.az; unit: "°"; onMoved: function (v) { page.put(true, v, page.cur.el, page.cur.dist, page.cur.spread) } }
                        Knob { label: page.t("ELEVATION"); from: -90; to: 90; value: page.cur.el; unit: "°"; onMoved: function (v) { page.put(true, page.cur.az, v, page.cur.dist, page.cur.spread) } }
                        Knob { label: page.t("DISTANCE"); from: 0.3; to: 10; value: page.cur.dist; unit: "m"; onMoved: function (v) { page.put(true, page.cur.az, page.cur.el, v, page.cur.spread) } }
                        Knob { label: page.t("SPREAD"); from: 0; to: 1; value: page.cur.spread; unit: ""; onMoved: function (v) { page.put(true, page.cur.az, page.cur.el, page.cur.dist, v) } }
                        Item { Layout.fillHeight: true }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7
                            text: page.t("Listen on headphones. The far ear hears the strip up to 0.66 ms later, quieter and duller (head shadow); behind adds a gentle low-pass; distance over 1 m lowers the level; SPREAD 1 removes the direction. Elevation narrows the sideways cues; this model has no pinna filter, so up and down sound alike. Sends and buses keep the strip's pan. Saved with the console and its scenes. Not an HRTF data set and not a licensed immersive format.") } } } } }
    }
}
