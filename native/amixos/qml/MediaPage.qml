// STREAM & MEDIA: a WAV playlist played into the master (next to the ARRANGE player): add files or a folder, transport, seek, player gain,
// loop, auto-next, peak. The browser build's NOW PLAYING (Spotify, YouTube, a shared tab) has no counterpart: this build plays files.
// MP3 / FLAC decoding is not built in; convert to WAV (the ARRANGE export writes 24-bit WAV).
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "mediaPage"
    property var appPanel: null
    function mmss(s) { const m = Math.floor(s / 60), r = Math.floor(s % 60); return m + ":" + (r < 10 ? "0" : "") + r }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 26; radius: 4; color: on ? onColor : Theme.ov(0.08); border.color: Theme.lineSoft
        Text { id: bl; anchors.centerIn: parent; text: i18n.t(label, i18n.lang); color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle { Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Text { text: i18n.t("STREAM & MEDIA:", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Text { text: media.playlist.length + i18n.t(" in the playlist · ", i18n.lang) + media.message; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideRight; Layout.fillWidth: true }
                Text { text: media.playing ? i18n.t("PLAYING", i18n.lang) : i18n.t("STOPPED", i18n.lang); color: media.playing ? Theme.emerald : Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } } }
        // APP AUDIO: what another program plays (a music player, YouTube / YouTube Music in a browser, Spotify, Apple Music, TIDAL, Deezer, Roon ...)
        // taken into the mixer as an input, through WASAPI loopback of the output it plays on. Secure by construction: nothing is decoded,
        // unlocked or logged in; the console only reads what Windows already mixes for the speakers. The apps keep their own accounts and DRM.
        Rectangle { Layout.fillWidth: true; implicitHeight: appCol.implicitHeight + 24; radius: Theme.radius; color: Theme.card; border.color: Theme.line
            property var loops: { const l = []; for (let i = 0; i < engine.devices.length; ++i) if (String(engine.devices[i].id).indexOf("loopback:") === 0) l.push(engine.devices[i]); return l }
            property int pick: -1
            ColumnLayout { id: appCol; anchors.fill: parent; anchors.margins: 12; spacing: 6
                RowLayout { Layout.fillWidth: true; spacing: 8
                    Text { text: i18n.t("APP AUDIO  ·  PLAYERS & STREAMING INTO THE MIXER", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
                    Item { Layout.fillWidth: true }
                    Text { text: parent.parent.parent.loops.length ? parent.parent.parent.loops.length + i18n.t(" output", i18n.lang) + (parent.parent.parent.loops.length === 1 ? "" : "s") + i18n.t(" can be captured", i18n.lang) : (engine.backends.indexOf("wasapi") >= 0 ? i18n.t("no output endpoint seen: RESCAN on CONNECTIONS", i18n.lang) : i18n.t("Windows only (WASAPI loopback)", i18n.lang)); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7
                    text: i18n.t("Play in any app: Windows Media Player, YouTube / YouTube Music, Spotify, Apple Music, TIDAL, Deezer, Roon, Qobuz, Amazon Music, a browser tab. Pick the output that app plays on and CAPTURE: the console opens WASAPI in loopback mode on that output and feeds the mix into the strips, from where it goes to EQ, dynamics, effects, recording and the ASIO / WASAPI / DirectSound output you chose. Secure mode: no account, no decoding, no DRM, nothing leaves this computer; the apps keep playing exactly as before.", i18n.lang) }
                Flow { Layout.fillWidth: true; spacing: 4
                    Repeater { model: parent.parent.parent.loops
                        delegate: Btn { label: String(modelData.name).replace("App audio: what plays on ", "⟲ "); on: appPanel.pick === index; onColor: Theme.cyan; onClicked: appPanel.pick = index } } }
                RowLayout { spacing: 6
                    Btn { label: i18n.t("▶ CAPTURE INTO THE MIXER", i18n.lang); on: appPanel.pick >= 0; onColor: Theme.emerald
                        onClicked: { if (appPanel.pick < 0) return; const d = appPanel.loops[appPanel.pick]; const last = settings.lastDevice; const out = last && last.backend === "wasapi" && last.out ? last.out : ""; const ok = engine.open("wasapi", d.id, out, settings.defaultRate || 48000, settings.defaultFrames || 256, Math.min(32, d.inChannels || 2), 2); if (ok) settings.rememberDevice("wasapi", d.id, out, settings.defaultRate || 48000, settings.defaultFrames || 256, Math.min(32, d.inChannels || 2), 2) } }
                    Btn { label: i18n.t("⏹ STOP", i18n.lang); visible: engine.running; onClicked: engine.stop() }
                    Text { Layout.fillWidth: true; text: engine.running ? engine.status : i18n.t("The captured app lands on CH 1-2 (its left / right); route it on ROUTING. To hear it, the output must be a different device than the one captured, or you get a loop.", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap } } }
            Component.onCompleted: appPanel = this
        }
        // transport
        Rectangle { Layout.fillWidth: true; implicitHeight: 118; radius: Theme.radius; color: Theme.card; border.color: Theme.line
            ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 8
                RowLayout { spacing: 6
                    Btn { label: "⏮"; onClicked: media.previous() }
                    Btn { label: media.playing ? i18n.t("⏸ PAUSE", i18n.lang) : i18n.t("▶ PLAY", i18n.lang); on: true; onColor: media.playing ? Theme.amber : Theme.emerald; onClicked: media.playing ? media.pause() : media.play(-1) }
                    Btn { label: i18n.t("⏹ STOP", i18n.lang); onClicked: media.stop() }
                    Btn { label: "⏭"; onClicked: media.next() }
                    Btn { label: i18n.t("LOOP", i18n.lang); on: media.loop; onColor: Theme.cyan; onClicked: media.loop = !media.loop }
                    Btn { label: i18n.t("AUTO NEXT", i18n.lang); on: media.autoNext; onColor: Theme.cyan; onClicked: media.autoNext = !media.autoNext }
                    Item { Layout.fillWidth: true }
                    Text { text: (media.current >= 0 && media.playlist[media.current] ? media.playlist[media.current].name : i18n.t("nothing loaded", i18n.lang)); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; elide: Text.ElideMiddle; Layout.maximumWidth: 500 } }
                RowLayout { spacing: 8; Layout.fillWidth: true
                    Text { text: page.mmss(media.position); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9 }
                    Slider { Layout.fillWidth: true; from: 0; to: Math.max(0.01, media.duration); value: media.position; onMoved: media.seek(value) }
                    Text { text: page.mmss(media.duration); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 } }
                RowLayout { spacing: 8; Layout.fillWidth: true
                    Text { text: i18n.t("PLAYER GAIN", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                    Slider { Layout.fillWidth: true; from: -60; to: 12; stepSize: 0.5; value: media.gainDb; onMoved: media.gainDb = value }
                    Text { Layout.preferredWidth: 60; text: media.gainDb.toFixed(1) + i18n.t(" dB", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; horizontalAlignment: Text.AlignRight }
                    MeterBar { Layout.preferredWidth: 160; implicitHeight: 10; peakDb: media.peakDb }
                    Text { Layout.preferredWidth: 56; text: media.peakDb > -119 ? media.peakDb.toFixed(1) + i18n.t(" dBFS", i18n.lang) : "—"; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8; horizontalAlignment: Text.AlignRight } } } }
        // playlist
        Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; clip: true
            ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 6
                RowLayout { spacing: 6; Layout.fillWidth: true
                    Text { text: i18n.t("PLAYLIST", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
                    Field { id: pathIn; Layout.fillWidth: true; placeholderText: i18n.t("an audio file (WAV, FLAC, AIFF, M4A, MP3 ...) or a folder of them", i18n.lang); font.family: Theme.mono; font.pixelSize: 9; implicitHeight: 26; onAccepted: { media.add(text); text = "" } }
                    Btn { label: i18n.t("ADD", i18n.lang); on: true; onClicked: { media.add(pathIn.text); pathIn.text = "" } }
                    Btn { label: i18n.t("CLEAR", i18n.lang); onClicked: media.clear() } }
                ListView { id: lv; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 3; model: media.playlist
                    delegate: Rectangle { width: lv.width; height: 36; radius: 6; color: index === media.current ? Qt.rgba(0.98, 0.75, 0.14, 0.14) : Theme.ov(0.03); border.color: index === media.current ? Theme.amber : (modelData.ok ? Theme.lineSoft : Theme.danger)
                        RowLayout { anchors.fill: parent; anchors.margins: 8; spacing: 10
                            Text { text: (index + 1) + "."; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                            ColumnLayout { Layout.fillWidth: true; spacing: 1
                                Text { text: i18n.t(modelData.name ?? "", i18n.lang); color: modelData.ok ? Theme.ink : Theme.danger; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                Text { text: modelData.ok ? modelData.path : modelData.error + "  ·  " + modelData.path; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; elide: Text.ElideMiddle; Layout.fillWidth: true } }
                            Text { text: modelData.seconds > 0 ? page.mmss(modelData.seconds) : ""; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                            Btn { label: i18n.t("PLAY", i18n.lang); on: true; onColor: Theme.emerald; onClicked: media.play(index) }
                            Btn { label: i18n.t("REMOVE", i18n.lang); onClicked: media.remove(index) } }
                        MouseArea { anchors.fill: parent; z: -1; onClicked: media.select(index) } }
                    Text { visible: media.playlist.length === 0; anchors.centerIn: parent; text: i18n.t("Empty. Type the path of a WAV or a folder and press ADD. The player feeds the master before its fader; mix it with LEVELS and the master.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; width: parent.width - 40; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap } }
                Text { Layout.fillWidth: true; text: i18n.t("WAV, FLAC and AIFF are read by the console itself; M4A, MP3, WMA and ALAC by Windows; OGG, Opus, WavPack, APE, DSD, AC-3, DTS and more by the FFmpeg decoders installed with it. Other rates are resampled to the engine rate. The SOUND CARD page has DC removal and dither for the output, so the player never needs the OS mixer.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap } } }
    }
}
