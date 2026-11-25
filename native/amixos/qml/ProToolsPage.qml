// PRO TOOLS (PRO, STUDIO, MASTER): MULTITRACK RECORDER (the chosen channels and the master bus, one WAV or FLAC file per track, into a
// timestamped session folder), ADVANCED ANALYZER (goniometer / phase scope, correlation, loudness history) and SESSION NOTES with a
// marker list. BASIC sees each card locked with UPGRADE. The analyzer draws only while the page is shown, a few times a second, from the
// engine's existing scope and loudness taps (no extra audio work).
import QtQuick
import AmixOS
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "proToolsPage"
    readonly property bool pro: proTools.allowed(engine.edition) || proTools.preview
    property var pairs: []
    property real corr: 0
    property string sessionDir: ""
    component Card: Rectangle { property string title; property string objName; default property alias body: inner.data; objectName: objName
        Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 240; radius: 8; color: Theme.ov(0.04); border.color: Theme.lineSoft
        Text { id: ct; x: 12; y: 10; text: i18n.t(parent.title, i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; font.letterSpacing: 2 }
        Item { id: inner; anchors.fill: parent; anchors.topMargin: 30; anchors.margins: 12; visible: page.pro }
        Rectangle { objectName: parent.objName + "Locked"; anchors.fill: parent; anchors.topMargin: 28; visible: !page.pro; color: Qt.rgba(0, 0, 0, 0.25); radius: 8
            Column { anchors.centerIn: parent; spacing: 8
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: i18n.t("LOCKED", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 12; font.bold: true }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: i18n.t("PRO, STUDIO and MASTER have this tool.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                Rectangle { objectName: "proUpgradeBtn"; anchors.horizontalCenter: parent.horizontalCenter; implicitWidth: ut.implicitWidth + 22; width: implicitWidth; height: 24; radius: 7; color: "transparent"; border.color: Theme.amber
                    Text { id: ut; anchors.centerIn: parent; text: i18n.t("UPGRADE", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Window.window.current = "renew" } } } } }
    Timer { interval: 200; repeat: true; running: page.visible && page.pro && engine.uiAwake !== false
        onTriggered: { page.pairs = engine.lissajous(256); page.corr = proTools.correlationOf(page.pairs); const t = engine.telemetry; proTools.pushLoudness(t.momentary ?? -120, t.shortTerm ?? -120); gonio.requestPaint(); hist.requestPaint() } }
    ScrollView { anchors.fill: parent; anchors.margins: 12; contentWidth: availableWidth; clip: true
        GridLayout { width: parent.width; columns: width > 1100 ? 3 : 1; rowSpacing: 10; columnSpacing: 10
            // MULTITRACK RECORDER
            Card { title: "MULTITRACK RECORDER"; objName: "proRecorder"
                ColumnLayout { anchors.fill: parent; spacing: 6
                    Text { text: i18n.t("Tracks: pick the channels; each is its own file.", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Flow { Layout.fillWidth: true; spacing: 3
                        Repeater { model: Math.min(32, engine.licensedChannels); delegate: Rectangle { objectName: "proArm" + index; width: 30; height: 20; radius: 5; readonly property bool on: { engine.recordMessage; engine.recordFolder; return engine.armed(index) }
                            color: on ? Theme.danger : Theme.ov(0.08); Text { anchors.centerIn: parent; text: index + 1; color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                            MouseArea { anchors.fill: parent; enabled: !engine.recording; onClicked: engine.setArmed(index, !parent.on) } } } }
                    RowLayout { spacing: 6
                        Button { objectName: "proRecMaster"; text: engine.recordMaster ? i18n.t("MASTER BUS: ON", i18n.lang) : i18n.t("MASTER BUS: OFF", i18n.lang); enabled: !engine.recording; onClicked: engine.recordMaster = !engine.recordMaster }
                        Button { objectName: "proRecWav"; text: "WAV"; highlighted: engine.recordFormat !== 4; enabled: !engine.recording; onClicked: engine.recordFormat = 0 }
                        Button { objectName: "proRecFlac"; text: "FLAC"; highlighted: engine.recordFormat === 4; enabled: !engine.recording; onClicked: engine.recordFormat = 4 } }
                    RowLayout { spacing: 6
                        Button { objectName: "proRecStart"; text: engine.recording ? i18n.t("STOP", i18n.lang) : i18n.t("RECORD SESSION", i18n.lang)
                            onClicked: { if (engine.recording) { engine.stopRecording(); proTools.saveTo(page.sessionDir); return }
                                const base = engine.recordFolder; const dir = proTools.makeSessionFolder(base); if (!dir) return; page.sessionDir = dir; engine.recordFolder = dir; engine.startRecording(); engine.recordFolder = base } }
                        Button { text: i18n.t("MARK", i18n.lang); enabled: engine.recording; onClicked: proTools.addMarker(engine.recordSeconds, "") }
                        Text { text: engine.recording ? engine.recordSeconds.toFixed(1) + " s" : ""; color: Theme.danger; font.family: Theme.mono; font.pixelSize: 10; font.bold: true } }
                    Text { objectName: "proRecSession"; text: page.sessionDir ? i18n.t("SESSION ", i18n.lang) + page.sessionDir : i18n.t("Each take goes into its own timestamped session folder.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                    Text { text: engine.recordMessage; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Item { Layout.fillHeight: true } } }
            // ADVANCED ANALYZER
            Card { title: "ADVANCED ANALYZER"; objName: "proAnalyzer"
                ColumnLayout { anchors.fill: parent; spacing: 6
                    RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
                        Canvas { id: gonio; objectName: "proGonio"; Layout.preferredWidth: Math.min(parent.height, 200); Layout.preferredHeight: Layout.preferredWidth; renderStrategy: Canvas.Cooperative
                            onPaint: { const c = getContext("2d"); c.reset(); c.fillStyle = "#0a0b0d"; c.fillRect(0, 0, width, height); c.strokeStyle = "#2a2d33"; c.beginPath(); c.moveTo(width / 2, 0); c.lineTo(width / 2, height); c.moveTo(0, height / 2); c.lineTo(width, height / 2); c.stroke()
                                const g = proTools.goniometer(page.pairs); c.fillStyle = "#3ee6a8"; for (let i = 0; i + 1 < g.length; i += 2) c.fillRect(g[i] * width, g[i + 1] * height, 1.5, 1.5) } }
                        ColumnLayout { Layout.fillWidth: true; spacing: 4
                            Text { text: i18n.t("CORRELATION", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
                            Text { objectName: "proCorr"; text: (page.corr >= 0 ? "+" : "") + page.corr.toFixed(2); color: page.corr < 0 ? Theme.danger : page.corr < 0.3 ? Theme.amber : Theme.emerald; font.family: Theme.mono; font.pixelSize: 20; font.bold: true }
                            Rectangle { Layout.fillWidth: true; height: 8; radius: 4; color: Theme.ov(0.08); Rectangle { x: (page.corr + 1) / 2 * (parent.width - 6); width: 6; height: 8; radius: 3; color: Theme.ink } }
                            Text { text: i18n.t("-1 out of phase · 0 wide · +1 mono", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } } }
                    Text { text: i18n.t("LOUDNESS HISTORY (momentary, short-term LUFS)", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
                    Canvas { id: hist; objectName: "proLoudHistory"; Layout.fillWidth: true; Layout.preferredHeight: 70; renderStrategy: Canvas.Cooperative
                        onPaint: { const c = getContext("2d"); c.reset(); c.fillStyle = "#0a0b0d"; c.fillRect(0, 0, width, height); const h = proTools.loudHistory; const n = h.length / 2; if (n < 2) return
                            const y = v => height - (Math.max(-60, Math.min(0, v)) + 60) / 60 * height; for (let k = 0; k < 2; ++k) { c.strokeStyle = k ? "#f5b642" : "#4fb3ff"; c.beginPath(); for (let i = 0; i < n; ++i) { const px = i / 599 * width; if (i) c.lineTo(px, y(h[i * 2 + k])); else c.moveTo(px, y(h[k])) } c.stroke() } } }
                    Item { Layout.fillHeight: true } } }
            // SESSION NOTES / MARKERS
            Card { title: "SESSION NOTES"; objName: "proNotes"
                ColumnLayout { anchors.fill: parent; spacing: 6
                    RowLayout { spacing: 6; Layout.fillWidth: true
                        Field { id: mk; objectName: "proMarkerLabel"; Layout.fillWidth: true; placeholderText: i18n.t("marker name", i18n.lang) }
                        Button { objectName: "proAddMarker"; text: i18n.t("ADD MARKER", i18n.lang); onClicked: { proTools.addMarker(engine.recording ? engine.recordSeconds : 0, mk.text); mk.text = "" } } }
                    ListView { objectName: "proMarkerList"; Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 60; clip: true; model: proTools.markers
                        delegate: RowLayout { width: ListView.view.width; spacing: 6
                            Text { text: Number(modelData.t).toFixed(1) + " s"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 8; Layout.preferredWidth: 56 }
                            Text { text: modelData.label; color: Theme.ink; font.family: Theme.sans; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                            Text { text: "✕"; color: Theme.muted; font.pixelSize: 10; MouseArea { anchors.fill: parent; onClicked: proTools.removeMarker(modelData.id) } } } }
                    TextArea { objectName: "proNotesText"; Layout.fillWidth: true; Layout.preferredHeight: 70; placeholderText: i18n.t("Session notes", i18n.lang); text: proTools.notes; wrapMode: TextEdit.Wrap; onEditingFinished: proTools.notes = text }
                    Button { text: i18n.t("SAVE TO SESSION", i18n.lang); enabled: page.sessionDir !== ""; onClicked: proTools.saveTo(page.sessionDir) } } }
        }
    }
}
