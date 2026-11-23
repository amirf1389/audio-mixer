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
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 22; radius: 8; gradient: Gradient { GradientStop { position: 0; color: on ? Theme.btnTop(onColor) : Theme.ov(0.11) } GradientStop { position: 1; color: on ? Theme.btnBottom(onColor) : Theme.ov(0.08) } } color: on ? onColor : Theme.ov(0.08); border.color: Theme.lineSoft
        Rectangle { anchors.fill: parent; anchors.margins: 1; radius: Math.max(0, parent.radius - 1); color: "transparent"; border.width: 1; border.color: Qt.rgba(1, 1, 1, parent.on ? 0.30 : 0.07) }
        Text { id: bl; anchors.centerIn: parent; text: i18n.t(label, i18n.lang); color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    component Knob: RowLayout { property string label; property real from; property real to; property real value; property string unit; signal moved(real v); spacing: 8; Layout.fillWidth: true
        Text { text: i18n.t(label, i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; Layout.preferredWidth: 90 }
        MSlider { Layout.fillWidth: true; from: parent.from; to: parent.to; value: parent.value; onMoved: parent.moved(value) }
        Text { text: parent.value.toFixed(parent.to > 30 ? 0 : 2) + " " + unit; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; Layout.preferredWidth: 60 } }
    // A SHORT WINDOW (or a large interface zoom): the page scrolls instead of running past the bottom of the page area
    Flickable { id: pageScroll; objectName: "pageScroll"; anchors.fill: parent; contentWidth: width; contentHeight: Math.max(height, pageCol.implicitHeight + 20); clip: true
        boundsBehavior: Flickable.StopAtBounds; interactive: contentHeight > height + 1; ScrollIndicator.vertical: ScrollIndicator { }
    ColumnLayout {
        id: pageCol; x: 10; y: 10; width: pageScroll.width - 20; height: pageScroll.contentHeight - 20
        spacing: 8
        Rectangle { Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Text { text: page.t("360 AUDIO:"); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Text { text: (engine.speakerArray ? page.t("10 SPEAKERS in front on outputs 1-10 (ear level 1-5, above 6-8, below 9-10) · the binaural stage rests") : page.t("binaural placement for headphones · azimuth · elevation · distance · spread · IAMF elements (violet)")); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                Item { Layout.fillWidth: true }
                Btn { objectName: "front10Btn"; label: engine.speakerArray ? page.t("SPEAKERS: FRONT 180° · 10 ON") : page.t("SPEAKERS: FRONT 180° · 10"); on: engine.speakerArray; onColor: "#f472b6"; onClicked: engine.speakerArray = !engine.speakerArray }
                Btn { label: page.t("ALL OFF"); onClicked: { for (let i = 0; i < engine.strips; i++) { const s = engine.spatial(i); if (s.on) engine.setSpatial(i, false, s.az, s.el, s.dist, s.spread) } page.rev++ } } } }
        Rectangle {   // THE SCENE: cinema hall (10 fixed points + 10 travelling objects), head tracking, and a map of every object from above
            objectName: "spatialScene"; Layout.fillWidth: true; implicitHeight: 150; radius: 8; color: Theme.card; border.color: engine.cinemaHall ? Theme.emerald : Theme.line
            Connections { target: engine; function onSpatialMoved() { sceneMap.requestPaint(); page.rev++ } }
            RowLayout { anchors.fill: parent; anchors.margins: 10; spacing: 14
                ColumnLayout { Layout.preferredWidth: 420; Layout.maximumWidth: 420; spacing: 6
                    RowLayout { spacing: 6
                        Btn { objectName: "cinemaBtn"; label: engine.cinemaHall ? page.t("CINEMA HALL: ON") : page.t("CINEMA HALL"); on: engine.cinemaHall; onColor: Theme.emerald; onClicked: engine.cinemaHall = !engine.cinemaHall }
                        Text { text: engine.cinemaHall ? page.t("10 points · ") + engine.spatialMoving + page.t(" objects travelling · strips 1-20") : page.t("10 fixed points + 10 travelling objects on strips 1-20, hall reverb on FX 8"); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } }
                    RowLayout { spacing: 6
                        Btn { objectName: "headTrackBtn"; label: engine.headTrack ? page.t("HEAD TRACK: ON") : page.t("HEAD TRACK: OFF"); on: engine.headTrack; onColor: Theme.cyan; onClicked: engine.headTrack = !engine.headTrack }
                        Btn { label: page.t("FACE FRONT"); onClicked: { engine.headYaw = 0; engine.headPitch = 0 } }
                        Text { text: page.t("yaw ") + Math.round(engine.headYaw) + "°  ·  " + page.t("pitch ") + Math.round(engine.headPitch) + "°"; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } }
                    RowLayout { spacing: 6; Text { Layout.preferredWidth: 40; text: page.t("TURN"); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                        MSlider { Layout.fillWidth: true; from: -180; to: 180; value: engine.headYaw; onMoved: engine.headYaw = value } }
                    RowLayout { spacing: 6; Text { Layout.preferredWidth: 40; text: page.t("NOD"); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                        MSlider { Layout.fillWidth: true; from: -60; to: 60; value: engine.headPitch; onMoved: engine.headPitch = value } }
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: page.t("With HEAD TRACK on, the scene stays in the room while the head turns: drag on the map or use TURN and NOD. For headphones."); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
                Canvas { id: sceneMap; objectName: "sceneMap"; Layout.fillWidth: true; Layout.fillHeight: true
                    onPaint: { const g = getContext("2d"); g.clearRect(0, 0, width, height); const cx = width / 2, cy = height / 2, R = Math.min(width * 0.5, height) / 2 - 6, sx = (width / 2 - 8) / R;
                        g.strokeStyle = "rgba(255,255,255,0.10)"; g.lineWidth = 1; for (const f of [0.5, 1]) { g.beginPath(); g.ellipse(cx - R * f * sx, cy - R * f, 2 * R * f * sx, 2 * R * f); g.stroke() }
                        // the listener, and where the head points (up = the front of the room)
                        const hy = (engine.headTrack ? engine.headYaw : 0) * Math.PI / 180; g.strokeStyle = "#37c6e8"; g.lineWidth = 2; g.beginPath(); g.moveTo(cx, cy); g.lineTo(cx + Math.sin(hy) * 16, cy - Math.cos(hy) * 16); g.stroke(); g.fillStyle = "#37c6e8"; g.beginPath(); g.arc(cx, cy, 4, 0, 6.3); g.fill();
                        const sc = engine.spatialScene(); g.font = "8px monospace";
                        for (let i = 0; i < sc.length; ++i) { const o = sc[i], a = o.az * Math.PI / 180, d = Math.min(1, o.dist / 10) * Math.cos(o.el * Math.PI / 180), x = cx + Math.sin(a) * d * R * sx, y = cy - Math.cos(a) * d * R;
                            g.fillStyle = o.iamf ? "#9b7bff" : o.orbit !== 0 ? "#f5b524" : "#19b97f"; if (o.orbit !== 0) { g.beginPath(); g.arc(x, y, 3 + o.el / 30, 0, 6.3); g.fill() } else g.fillRect(x - 4, y - 4, 8, 8);
                            g.fillStyle = "rgba(255,255,255,0.6)"; g.fillText(o.strip + 1, x + 6, y + 3) } }
                    MouseArea { anchors.fill: parent; onPositionChanged: function (m) { if (pressed && engine.headTrack) engine.headYaw = Math.atan2(m.x - width / 2, height / 2 - m.y) * 180 / Math.PI } onPressed: function (m) { if (engine.headTrack) engine.headYaw = Math.atan2(m.x - width / 2, height / 2 - m.y) * 180 / Math.PI } } } } }
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
                        if (engine.speakerArray) { const L = engine.speakerLayout(); for (let k = 0; k < L.length; k++) { const sp = L[k], a = sp.az * Math.PI / 180, r = rad * 0.98 * Math.cos(sp.el * Math.PI / 180), x = cx + Math.sin(a) * r, y = cy - Math.cos(a) * r
                            ctx.fillStyle = sp.el > 0 ? "#f9a8d4" : sp.el < 0 ? "#be185d" : "#f472b6"; ctx.fillRect(x - 7, y - 5, 14, 10); ctx.fillStyle = "#000"; ctx.font = "bold 7px monospace"; ctx.fillText(sp.out, x - (sp.out > 9 ? 5 : 2.5), y + 3) } }   // the ten speakers: ear level pink, above light, below dark; the number is the output
                        for (let i = 0; i < engine.strips; i++) { const s = engine.spatial(i); if (!s.on && i !== page.sel) continue; const a = s.az * Math.PI / 180, r = Math.min(1, Math.log(1 + s.dist) / Math.log(11)) * rad * Math.cos(s.el * Math.PI / 180), x = cx + Math.sin(a) * r, y = cy - Math.cos(a) * r
                            ctx.fillStyle = i === page.sel ? Theme.orange : Theme.cyan; ctx.globalAlpha = s.on ? 1 : 0.4; ctx.beginPath(); ctx.arc(x, y, i === page.sel ? 9 : 6, 0, 2 * Math.PI); ctx.fill(); ctx.globalAlpha = 1; ctx.fillStyle = "#000"; ctx.font = "bold 8px monospace"; ctx.fillText(i + 1, x - (i > 8 ? 5 : 2.5), y + 3) } }
                    MouseArea { anchors.fill: parent
                        function place(mx, my) { const dx = mx - width / 2, dy = height / 2 - my, r = Math.min(1, Math.hypot(dx, dy) / dial.rad), c = page.cur; page.put(true, Math.round(Math.atan2(dx, dy) * 180 / Math.PI), 0, Math.max(0.3, Math.pow(11, r) - 1), c.spread) }
                        onPressed: function (m) { place(m.x, m.y) } onPositionChanged: function (m) { if (pressed) place(m.x, m.y) } } } }
            ColumnLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: chFlow.implicitHeight + 16; Layout.maximumHeight: 160; clip: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                    Flickable { id: chScroll; x: 8; y: 8; width: parent.width - 16; height: parent.height - 16; contentHeight: chFlow.implicitHeight; clip: true; boundsBehavior: Flickable.StopAtBounds; ScrollIndicator.vertical: ScrollIndicator { }   // 300 channels do not fit: the list scrolls
                    Flow { id: chFlow; width: chScroll.width; spacing: 4
                        Repeater { model: engine.strips; delegate: Rectangle { width: 74; height: 22; radius: 4; ToolTip.visible: chH.hovered; ToolTip.text: (index + 1) + "  " + ((page.strips[index] && page.strips[index].name) || ""); HoverHandler { id: chH } readonly property bool on: { page.rev; return engine.spatial(index).on }
                                readonly property bool iamf: { page.rev; return engine.spatial(index).iamf === true }
                                color: page.sel === index ? Theme.orange : on ? (iamf ? "#9b7bff" : Theme.cyan) : Theme.ov(0.08); border.color: Theme.lineSoft
                                Text { anchors.fill: parent; anchors.leftMargin: 4; anchors.rightMargin: 4; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight   // the channel number and as much of its name as fits (the full name on hover): "LIVE" twice told nothing
                                    text: (index + 1) + " " + ((page.strips[index] && page.strips[index].name) ? page.strips[index].name : "CH " + (index + 1)); color: (page.sel === index || parent.on) ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true }
                                MouseArea { anchors.fill: parent; onClicked: page.sel = index } } } } } }
                Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                    ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 10
                        RowLayout { spacing: 8
                            Text { text: "CH " + (page.sel + 1); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 12; font.bold: true }
                            Btn { label: page.cur.on ? page.t("360: ON") : page.t("360: OFF (PAN)"); on: page.cur.on; onColor: Theme.emerald; onClicked: page.put(!page.cur.on, page.cur.az, page.cur.el, page.cur.dist, page.cur.spread) }
                            Repeater { model: [["FRONT", 0], ["RIGHT", 90], ["BEHIND", 180], ["LEFT", -90]]; delegate: Btn { label: page.t(modelData[0]); onClicked: page.put(true, modelData[1], page.cur.el, page.cur.dist, page.cur.spread) } }
                            Item { Layout.fillWidth: true } }
                        // IAMF: the strip placed by an IAMF ELEMENT in its rack (the immersive format of the Eclipsa plug-ins); the controls here move its knobs
                        RowLayout { spacing: 8
                            Btn { objectName: "iamfBtn"; label: page.cur.iamf ? page.t("IAMF ELEMENT: ON") : page.t("USE IAMF ELEMENT"); on: page.cur.iamf === true; onColor: "#9b7bff"
                                onClicked: { if (page.cur.iamf) engine.setPlugin(page.sel, page.cur.iamfSlot, ""); else { const pl = engine.plugins(page.sel); for (let k = 0; k < pl.length; k++) if (!pl[k].kind) { engine.setPlugin(page.sel, k, "fx:iamfelement"); break } } page.rev++ } }
                            Btn { objectName: "iamfRender"; visible: page.cur.iamf === true; label: page.cur.render === "SPEAKERS" ? page.t("RENDER: SPEAKERS") : page.t("RENDER: BINAURAL"); on: true; onColor: Theme.cyan
                                onClicked: { engine.setPluginParam(page.sel, page.cur.iamfSlot, 4, page.cur.render === "SPEAKERS" ? 0.0 : 1.0); page.rev++ } }
                            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: page.cur.iamf ? page.t("Placed by the IAMF ELEMENT in rack slot ") + (page.cur.iamfSlot + 1) + page.t(": the dial and the sliders move its knobs, its knobs move the dial.") : page.t("IAMF: put an IAMF ELEMENT on this channel; put the IAMF RENDERER on the master bus for binaural monitoring and loudness."); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
                        Knob { label: page.t("AZIMUTH"); from: -180; to: 180; value: page.cur.az > 180 ? page.cur.az - 360 : page.cur.az; unit: "°"; onMoved: function (v) { page.put(true, v, page.cur.el, page.cur.dist, page.cur.spread) } }
                        Knob { label: page.t("ELEVATION"); from: -90; to: 90; value: page.cur.el; unit: "°"; onMoved: function (v) { page.put(true, page.cur.az, v, page.cur.dist, page.cur.spread) } }
                        Knob { label: page.t("DISTANCE"); from: 0.3; to: 10; value: page.cur.dist; unit: "m"; onMoved: function (v) { page.put(true, page.cur.az, page.cur.el, v, page.cur.spread) } }
                        Knob { label: page.t("SPREAD"); from: 0; to: 1; value: page.cur.spread; unit: ""; onMoved: function (v) { page.put(true, page.cur.az, page.cur.el, page.cur.dist, v) } }
                        Item { Layout.fillHeight: true }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7
                            text: page.t("Listen on headphones. The far ear hears the strip up to 0.66 ms later, quieter and duller (head shadow); behind adds a gentle low-pass; distance over 1 m lowers the level; SPREAD 1 removes the direction. Elevation narrows the sideways cues; this model has no pinna filter, so up and down sound alike. Sends and buses keep the strip's pan. Saved with the console and its scenes. Not an HRTF data set and not a licensed immersive format.") } } } } }
    } }
}
