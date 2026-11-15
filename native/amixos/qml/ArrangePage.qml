// ARRANGE: WAV files as clips on tracks along a timeline (bars at the tempo, or seconds). Tracks have gain, pan, M / S and a volume automation
// lane; markers mark positions; the transport plays through the master chain (start the engine on CONNECTIONS first); EXPORT renders the
// whole mix offline to a 24-bit WAV. Nothing is recorded here. Keys: Space play / stop, S split at the head, M marker, Delete remove the clip.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "arrangePage"
    property string folder: ""
    property var files: []
    property int dragId: 0
    property real dragX: 0
    readonly property int lane: 56
    readonly property int head: 26
    readonly property int hdrW: 190
    readonly property real pps: arrange.zoom          // pixels per second
    readonly property real beat: 60 / arrange.bpm
    function xOf(t) { return t * pps }
    function tOf(x) { return Math.max(0, x / pps) }
    function fmt(t) { var b = Math.floor(t / (beat * 4)) + 1, bt = Math.floor((t % (beat * 4)) / beat) + 1; return b + "." + bt + "  " + t.toFixed(2) + " s" }
    focus: true
    Keys.onPressed: function (e) { if (e.key === Qt.Key_Space) { arrange.playing ? arrange.stop() : arrange.play(-1); e.accepted = true } else if (e.key === Qt.Key_S) arrange.splitAtHead(); else if (e.key === Qt.Key_M) arrange.addMarker(); else if (e.key === Qt.Key_Delete || e.key === Qt.Key_Backspace) arrange.deleteSelected() }
    Connections { target: arrange; function onProjectChanged() { tl.requestPaint() } function onTransportChanged() { tl.requestPaint() } function onSelectionChanged() { tl.requestPaint() } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        // transport + settings
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 36; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Text { text: i18n.t("ARRANGE", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Rectangle { id: gb3; implicitWidth: 28; implicitHeight: 22; radius: 7; color: Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Qt.lighter(gb3.color, 1.25) } GradientStop { position: 1; color: Qt.darker(gb3.color, 1.12) } } Text { anchors.centerIn: parent; text: "⏮"; color: Theme.ink; font.pixelSize: 11 } MouseArea { anchors.fill: parent; onClicked: arrange.toStart() } }
                Rectangle { id: gb4; implicitWidth: 60; implicitHeight: 22; radius: 7; color: arrange.playing ? Theme.emerald : Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Qt.lighter(gb4.color, 1.25) } GradientStop { position: 1; color: Qt.darker(gb4.color, 1.12) } } Text { anchors.centerIn: parent; text: arrange.playing ? i18n.t("▶ PLAYING", i18n.lang) : i18n.t("▶ PLAY", i18n.lang); color: arrange.playing ? "#000" : Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: arrange.play(-1) } }
                Rectangle { id: gb5; implicitWidth: 40; implicitHeight: 22; radius: 7; color: Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Qt.lighter(gb5.color, 1.25) } GradientStop { position: 1; color: Qt.darker(gb5.color, 1.12) } } Text { anchors.centerIn: parent; text: "■"; color: Theme.ink; font.pixelSize: 11 } MouseArea { anchors.fill: parent; onClicked: arrange.stop() } }
                Rectangle { id: gb6; implicitWidth: Math.max(44, children[0].implicitWidth + 12); implicitHeight: 22; radius: 7; color: arrange.loop ? Theme.cyan : Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Qt.lighter(gb6.color, 1.25) } GradientStop { position: 1; color: Qt.darker(gb6.color, 1.12) } } Text { anchors.centerIn: parent; text: i18n.t("LOOP", i18n.lang); color: arrange.loop ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: arrange.loop = !arrange.loop } }
                Text { text: page.fmt(arrange.position); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; Layout.preferredWidth: 120 }
                Text { text: i18n.t("BPM", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                SpinBox { palette.base: Theme.deep; palette.text: Theme.ink; palette.button: Theme.cardRaised; palette.buttonText: Theme.ink; palette.highlight: Theme.orange; palette.mid: Theme.line; from: 20; to: 300; value: Math.round(arrange.bpm); onValueModified: arrange.bpm = value; implicitWidth: 90; implicitHeight: 24; font.pixelSize: 9 }
                Text { text: i18n.t("SNAP", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                Repeater { model: [[0, "OFF"], [1, "BAR"], [4, "1/4"], [8, "1/8"], [16, "1/16"]]; delegate: Rectangle { id: gb7; implicitWidth: 34; implicitHeight: 20; radius: 7; color: arrange.snap === modelData[0] ? Theme.amber : Theme.ov(0.06); gradient: Gradient { GradientStop { position: 0; color: Qt.lighter(gb7.color, 1.25) } GradientStop { position: 1; color: Qt.darker(gb7.color, 1.12) } } Text { anchors.centerIn: parent; text: modelData[1]; color: arrange.snap === modelData[0] ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } MouseArea { anchors.fill: parent; onClicked: arrange.snap = modelData[0] } } }
                Text { text: i18n.t("ZOOM", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                Slider { from: 5; to: 400; value: arrange.zoom; onMoved: arrange.zoom = value; implicitWidth: 100 }
                Item { Layout.fillWidth: true }
                Rectangle { id: gb8; implicitWidth: Math.max(70, children[0].implicitWidth + 12); implicitHeight: 22; radius: 7; color: Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Qt.lighter(gb8.color, 1.25) } GradientStop { position: 1; color: Qt.darker(gb8.color, 1.12) } } Text { anchors.centerIn: parent; text: i18n.t("+ TRACK", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: arrange.addTrack() } }
                Rectangle { id: gb9; implicitWidth: Math.max(70, children[0].implicitWidth + 12); implicitHeight: 22; radius: 7; color: Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Qt.lighter(gb9.color, 1.25) } GradientStop { position: 1; color: Qt.darker(gb9.color, 1.12) } } Text { anchors.centerIn: parent; text: i18n.t("MARKER", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: arrange.addMarker() } }
                Rectangle { id: gb10; implicitWidth: Math.max(60, children[0].implicitWidth + 12); implicitHeight: 22; radius: 7; color: Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Qt.lighter(gb10.color, 1.25) } GradientStop { position: 1; color: Qt.darker(gb10.color, 1.12) } } Text { anchors.centerIn: parent; text: i18n.t("SPLIT", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: arrange.splitAtHead() } }
                Rectangle { id: gb11; implicitWidth: Math.max(70, children[0].implicitWidth + 12); implicitHeight: 22; radius: 7; color: Theme.orange; gradient: Gradient { GradientStop { position: 0; color: Qt.lighter(gb11.color, 1.25) } GradientStop { position: 1; color: Qt.darker(gb11.color, 1.12) } } Text { anchors.centerIn: parent; text: i18n.t("EXPORT", i18n.lang); color: "#000"; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: arrange.exportWav("") } }
                Rectangle { id: gb12; implicitWidth: Math.max(44, children[0].implicitWidth + 12); implicitHeight: 22; radius: 7; color: Theme.ov(0.06); gradient: Gradient { GradientStop { position: 0; color: Qt.lighter(gb12.color, 1.25) } GradientStop { position: 1; color: Qt.darker(gb12.color, 1.12) } } border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: i18n.t("NEW", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: arrange.newProject() } }
            }
        }
        // the timeline: track headers on the left, a scrolling canvas on the right
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; clip: true
            Row {
                anchors.fill: parent; anchors.margins: 6
                Column {   // headers
                    width: page.hdrW; spacing: 0
                    Item { width: parent.width; height: page.head; Text { anchors.centerIn: parent; text: arrange.tracks.length + i18n.t(" TRACKS · ", i18n.lang) + arrange.clips.length + i18n.t(" CLIPS · END ", i18n.lang) + arrange.end.toFixed(1) + " s"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
                    Repeater { model: arrange.tracks
                        delegate: Rectangle { width: parent.width - 4; height: page.lane; color: Theme.ov(index % 2 ? 0.025 : 0.05); border.color: Theme.lineSoft
                            Rectangle { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 4; color: Theme.stripColors[modelData.color] }
                            ColumnLayout { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 4; anchors.topMargin: 3; spacing: 2
                                RowLayout { Layout.fillWidth: true; spacing: 3
                                    TextInput { Layout.fillWidth: true; text: i18n.t(modelData.name ?? "", i18n.lang); color: modelData.audible ? Theme.ink : Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true; selectByMouse: true; onEditingFinished: arrange.setTrack(modelData.id, "name", text) }
                                    Rectangle { implicitWidth: 16; implicitHeight: 14; radius: 2; color: modelData.mute ? Theme.danger : Theme.ov(0.08); Text { anchors.centerIn: parent; text: "M"; color: modelData.mute ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } MouseArea { anchors.fill: parent; onClicked: arrange.setTrack(modelData.id, "mute", !modelData.mute) } }
                                    Rectangle { implicitWidth: 16; implicitHeight: 14; radius: 2; color: modelData.solo ? Theme.amber : Theme.ov(0.08); Text { anchors.centerIn: parent; text: "S"; color: modelData.solo ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 7; font.bold: true } MouseArea { anchors.fill: parent; onClicked: arrange.setTrack(modelData.id, "solo", !modelData.solo) } }
                                    Rectangle { implicitWidth: 14; implicitHeight: 14; radius: 2; color: Theme.ov(0.06); Text { anchors.centerIn: parent; text: "×"; color: Theme.muted; font.pixelSize: 9 } MouseArea { anchors.fill: parent; onClicked: arrange.removeTrack(modelData.id) } } }
                                RowLayout { Layout.fillWidth: true; spacing: 4
                                    Text { text: "G"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } Slider { Layout.fillWidth: true; from: -60; to: 6; value: modelData.db; onMoved: arrange.setTrack(modelData.id, "db", value); implicitHeight: 14 } Text { text: Theme.dbText(modelData.db); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 7; Layout.preferredWidth: 30 } }
                                RowLayout { Layout.fillWidth: true; spacing: 4
                                    Text { text: "P"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } Slider { Layout.fillWidth: true; from: -1; to: 1; value: modelData.pan; onMoved: arrange.setTrack(modelData.id, "pan", value); implicitHeight: 14 } Text { text: modelData.pan === 0 ? "C" : (modelData.pan < 0 ? "L" + Math.round(-modelData.pan * 100) : "R" + Math.round(modelData.pan * 100)); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; Layout.preferredWidth: 30 }
                                    Rectangle { implicitWidth: Math.max(22, children[0].implicitWidth + 12); implicitHeight: 12; radius: 2; color: Theme.ov(0.06); Text { anchors.centerIn: parent; text: i18n.t("AUTO", i18n.lang); color: modelData.auto.length ? Theme.cyan : Theme.dim; font.family: Theme.mono; font.pixelSize: 5 } MouseArea { anchors.fill: parent; onClicked: arrange.clearAutomation(modelData.id) } } }
                            } } }
                }
                Flickable {
                    id: flick
                    width: parent.width - page.hdrW; height: parent.height
                    contentWidth: Math.max(width, page.xOf(arrange.end + 30)); contentHeight: height
                    flickableDirection: Flickable.HorizontalFlick; boundsBehavior: Flickable.StopAtBounds; clip: true
                    Canvas {
                        id: tl
                        width: flick.contentWidth; height: flick.height
                        onPaint: {
                            const ctx = getContext("2d"); ctx.reset();
                            const w = width, h = height, bar = page.beat * 4, pxBar = page.xOf(bar), lane = page.lane, head = page.head;
                            ctx.fillStyle = Theme.chrome; ctx.fillRect(0, 0, w, h);
                            // lanes
                            for (let i = 0; i < arrange.tracks.length; i++) { ctx.fillStyle = i % 2 ? Theme.ov(0.02) : Theme.ov(0.04); ctx.fillRect(0, head + i * lane, w, lane - 1) }
                            // grid: bars (strong) and beats (faint) when there is room
                            const step = pxBar >= 24 ? 1 : pxBar >= 12 ? 2 : pxBar >= 6 ? 4 : 8;
                            ctx.font = "7px monospace"; ctx.textBaseline = "top";
                            for (let b = 0; b * pxBar < w; b += step) {
                                const x = b * pxBar; ctx.fillStyle = Theme.ov(0.14); ctx.fillRect(x, 0, 1, h); ctx.fillStyle = "#8b93a7"; ctx.fillText(String(b + 1), x + 3, 3);
                                if (pxBar >= 60) for (let k = 1; k < 4; k++) { ctx.fillStyle = Theme.ov(0.05); ctx.fillRect(x + k * pxBar / 4, head, 1, h - head) }
                            }
                            // clips
                            for (const c of arrange.clips) {
                                let row = -1; for (let i = 0; i < arrange.tracks.length; i++) if (arrange.tracks[i].id === c.track) row = i;
                                if (row < 0) continue;
                                const x0 = page.dragId === c.id ? page.dragX : page.xOf(c.start), x1 = x0 + page.xOf(c.end - c.start), y = head + row * lane + 3, hh = lane - 8, sel = arrange.selectedClip === c.id;
                                ctx.fillStyle = c.missing ? "rgba(255,77,94,0.35)" : (Theme.stripColors[arrange.tracks[row].color] + (sel ? "ee" : "99")); ctx.fillRect(x0, y, Math.max(2, x1 - x0), hh);
                                ctx.strokeStyle = sel ? "#ffffff" : "rgba(0,0,0,0.5)"; ctx.lineWidth = sel ? 1.5 : 1; ctx.strokeRect(x0 + 0.5, y + 0.5, Math.max(2, x1 - x0) - 1, hh - 1);
                                ctx.save(); ctx.beginPath(); ctx.rect(x0, y, Math.max(2, x1 - x0), hh); ctx.clip();   // the label and the waveform stay inside the clip
                                ctx.fillStyle = "#000"; ctx.font = "bold 8px monospace"; ctx.fillText((c.missing ? "MISSING  " : "") + c.name + (c.db ? "  " + c.db.toFixed(1) + " dB" : ""), x0 + 4, y + 3);
                                // a stylised waveform: bars at the gain, no sample data needed on the UI thread
                                ctx.fillStyle = "rgba(0,0,0,0.35)"; for (let px = x0 + 2; px < x1 - 2; px += 3) { const a = (0.25 + 0.5 * Math.abs(Math.sin(px * 0.37 + c.id))) * (hh - 16) / 2; ctx.fillRect(px, y + hh / 2 + 5 - a / 2, 2, a) }
                                ctx.restore();
                            }
                            // automation lanes (dB -60..+6 mapped over the lane)
                            for (let i = 0; i < arrange.tracks.length; i++) {
                                const t = arrange.tracks[i]; if (!t.auto.length) continue; const y0 = head + i * lane, yOf = v => y0 + lane - 4 - (v + 60) / 66 * (lane - 8);
                                ctx.strokeStyle = "#4cc3ff"; ctx.lineWidth = 1.5; ctx.beginPath(); ctx.moveTo(0, yOf(t.auto[0].v));
                                for (const p of t.auto) ctx.lineTo(page.xOf(p.t), yOf(p.v)); ctx.lineTo(w, yOf(t.auto[t.auto.length - 1].v)); ctx.stroke();
                                ctx.fillStyle = "#4cc3ff"; for (const p of t.auto) { ctx.beginPath(); ctx.arc(page.xOf(p.t), yOf(p.v), 3, 0, 6.283); ctx.fill() }
                            }
                            // markers
                            ctx.font = "7px monospace"; for (const m of arrange.markers) { const x = page.xOf(m.t); ctx.fillStyle = "#ffd93d"; ctx.fillRect(x, 0, 1, h); ctx.fillRect(x, 0, 36, 9); ctx.fillStyle = "#000"; ctx.fillText(m.name.slice(0, 7), x + 2, 1) }
                            // playhead
                            const px = page.xOf(arrange.position); ctx.fillStyle = "#ff4d5e"; ctx.fillRect(px - 0.5, 0, 1.5, h); ctx.beginPath(); ctx.moveTo(px - 5, 0); ctx.lineTo(px + 5, 0); ctx.lineTo(px, 7); ctx.fill();
                        }
                        MouseArea {
                            anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.RightButton
                            property var hit: null
                            function clipAt(x, y) { for (const c of arrange.clips) { let row = -1; for (let i = 0; i < arrange.tracks.length; i++) if (arrange.tracks[i].id === c.track) row = i; if (row < 0) continue; const x0 = page.xOf(c.start), x1 = page.xOf(c.end), y0 = page.head + row * page.lane; if (x >= x0 && x <= x1 && y >= y0 && y < y0 + page.lane) return c } return null }
                            onPressed: function (m) {
                                page.forceActiveFocus();
                                if (m.y < page.head) { arrange.seek(page.snapTime(page.tOf(m.x))); return }
                                const c = clipAt(m.x, m.y);
                                if (m.button === Qt.RightButton) {   // right click on a lane: an automation point at that level
                                    const row = Math.floor((m.y - page.head) / page.lane); if (row >= 0 && row < arrange.tracks.length) { const v = -60 + (1 - (m.y - page.head - row * page.lane - 4) / (page.lane - 8)) * 66; arrange.addAutomation(arrange.tracks[row].id, page.snapTime(page.tOf(m.x)), Math.max(-60, Math.min(6, v))) }
                                    return }
                                if (c) { arrange.selectedClip = c.id; hit = { id: c.id, dx: m.x - page.xOf(c.start) }; page.dragId = c.id; page.dragX = page.xOf(c.start) } else { arrange.selectedClip = 0; arrange.seek(page.snapTime(page.tOf(m.x))) }
                                tl.requestPaint()
                            }
                            onPositionChanged: function (m) { if (hit && (m.buttons & Qt.LeftButton)) { page.dragX = Math.max(0, m.x - hit.dx); tl.requestPaint() } }
                            onReleased: function (m) { if (hit) { const row = Math.floor((m.y - page.head) / page.lane); arrange.moveClip(hit.id, page.tOf(page.dragX)); if (row >= 0 && row < arrange.tracks.length) arrange.moveClipToTrack(hit.id, arrange.tracks[row].id) } hit = null; page.dragId = 0; tl.requestPaint() }
                            onDoubleClicked: function (m) { const c = clipAt(m.x, m.y); if (c) arrange.seek(c.start) }
                        }
                    }
                }
            }
        }
        // import + status
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 92; radius: 8; color: Theme.card; border.color: Theme.line
            ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 4
                RowLayout { Layout.fillWidth: true; spacing: 6
                    Text { text: i18n.t("IMPORT WAV", i18n.lang); color: Theme.emerald; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                    Field { id: folderIn; Layout.fillWidth: true; placeholderText: i18n.t("folder of audio files: WAV, FLAC, AIFF, M4A, MP3 ... (empty = your Music folder); or one file path", i18n.lang); font.family: Theme.mono; font.pixelSize: 9; implicitHeight: 24; onAccepted: page.scan() }
                    Rectangle { id: gb13; implicitWidth: Math.max(60, children[0].implicitWidth + 12); implicitHeight: 24; radius: 7; color: Qt.rgba(0, 0.95, 1, 0.18); gradient: Gradient { GradientStop { position: 0; color: Qt.lighter(gb13.color, 1.25) } GradientStop { position: 1; color: Qt.darker(gb13.color, 1.12) } } border.color: Theme.cyan; Text { anchors.centerIn: parent; text: i18n.t("SCAN", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: page.scan() } }
                    Text { text: i18n.t("Selected clip: ", i18n.lang) + (arrange.selectedClip ? "#" + arrange.selectedClip : i18n.t("none", i18n.lang)) + i18n.t("  ·  S split · M marker · Del remove · right-click a lane = automation point", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
                Flow { Layout.fillWidth: true; spacing: 4
                    Repeater { model: page.files; delegate: Rectangle { id: gb14; implicitWidth: fl.implicitWidth + 12; implicitHeight: 18; radius: 7; color: Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Qt.lighter(gb14.color, 1.25) } GradientStop { position: 1; color: Qt.darker(gb14.color, 1.12) } } border.color: Theme.lineSoft
                        Text { id: fl; anchors.centerIn: parent; text: modelData.split("/").pop(); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 7 } MouseArea { anchors.fill: parent; onClicked: arrange.importFile(modelData, 0) } } }
                    Text { visible: page.files.length === 0; text: i18n.t("No files listed yet. Type a folder or a .wav path and press SCAN; click a file to place it on the last track at the playhead (snapped).", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
                Text { text: arrange.message; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; Layout.fillWidth: true; elide: Text.ElideRight }
            }
        }
    }
    function scan() { const p = folderIn.text.trim(); if (/\.(wav|flac|aiff?|aifc|m4a|aac|mp4|mp3|wma|ogg|oga|opus|caf)$/i.test(p)) { arrange.importFile(p, 0); return } page.files = arrange.wavFilesIn(p) }
}
