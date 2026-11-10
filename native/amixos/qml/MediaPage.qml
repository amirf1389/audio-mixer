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
    function mmss(s) { const m = Math.floor(s / 60), r = Math.floor(s % 60); return m + ":" + (r < 10 ? "0" : "") + r }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 26; radius: 4; color: on ? onColor : Qt.rgba(1, 1, 1, 0.08); border.color: Theme.lineSoft
        Text { id: bl; anchors.centerIn: parent; text: label; color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle { Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 6
                Text { text: "STREAM & MEDIA:"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Text { text: media.playlist.length + " in the playlist · " + media.message; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideRight; Layout.fillWidth: true }
                Text { text: media.playing ? "PLAYING" : "STOPPED"; color: media.playing ? Theme.emerald : Theme.dim; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } } }
        // transport
        Rectangle { Layout.fillWidth: true; implicitHeight: 118; radius: Theme.radius; color: Theme.card; border.color: Theme.line
            ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 8
                RowLayout { spacing: 6
                    Btn { label: "⏮"; onClicked: media.previous() }
                    Btn { label: media.playing ? "⏸ PAUSE" : "▶ PLAY"; on: true; onColor: media.playing ? Theme.amber : Theme.emerald; onClicked: media.playing ? media.pause() : media.play(-1) }
                    Btn { label: "⏹ STOP"; onClicked: media.stop() }
                    Btn { label: "⏭"; onClicked: media.next() }
                    Btn { label: "LOOP"; on: media.loop; onColor: Theme.cyan; onClicked: media.loop = !media.loop }
                    Btn { label: "AUTO NEXT"; on: media.autoNext; onColor: Theme.cyan; onClicked: media.autoNext = !media.autoNext }
                    Item { Layout.fillWidth: true }
                    Text { text: (media.current >= 0 && media.playlist[media.current] ? media.playlist[media.current].name : "nothing loaded"); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; elide: Text.ElideMiddle; Layout.maximumWidth: 500 } }
                RowLayout { spacing: 8; Layout.fillWidth: true
                    Text { text: page.mmss(media.position); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9 }
                    Slider { Layout.fillWidth: true; from: 0; to: Math.max(0.01, media.duration); value: media.position; onMoved: media.seek(value) }
                    Text { text: page.mmss(media.duration); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 } }
                RowLayout { spacing: 8; Layout.fillWidth: true
                    Text { text: "PLAYER GAIN"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                    Slider { Layout.fillWidth: true; from: -60; to: 12; stepSize: 0.5; value: media.gainDb; onMoved: media.gainDb = value }
                    Text { Layout.preferredWidth: 60; text: media.gainDb.toFixed(1) + " dB"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; horizontalAlignment: Text.AlignRight }
                    MeterBar { Layout.preferredWidth: 160; implicitHeight: 10; peakDb: media.peakDb }
                    Text { Layout.preferredWidth: 56; text: media.peakDb > -119 ? media.peakDb.toFixed(1) + " dBFS" : "—"; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8; horizontalAlignment: Text.AlignRight } } } }
        // playlist
        Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; clip: true
            ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 6
                RowLayout { spacing: 6; Layout.fillWidth: true
                    Text { text: "PLAYLIST"; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
                    TextField { id: pathIn; Layout.fillWidth: true; placeholderText: "path of a .wav file or a folder of WAVs"; font.family: Theme.mono; font.pixelSize: 9; implicitHeight: 26; onAccepted: { media.add(text); text = "" } }
                    Btn { label: "ADD"; on: true; onClicked: { media.add(pathIn.text); pathIn.text = "" } }
                    Btn { label: "CLEAR"; onClicked: media.clear() } }
                ListView { id: lv; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 3; model: media.playlist
                    delegate: Rectangle { width: lv.width; height: 36; radius: 6; color: index === media.current ? Qt.rgba(0.98, 0.75, 0.14, 0.14) : Qt.rgba(1, 1, 1, 0.03); border.color: index === media.current ? Theme.amber : (modelData.ok ? Theme.lineSoft : Theme.danger)
                        RowLayout { anchors.fill: parent; anchors.margins: 8; spacing: 10
                            Text { text: (index + 1) + "."; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                            ColumnLayout { Layout.fillWidth: true; spacing: 1
                                Text { text: modelData.name; color: modelData.ok ? Theme.ink : Theme.danger; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                Text { text: modelData.ok ? modelData.path : modelData.error + "  ·  " + modelData.path; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; elide: Text.ElideMiddle; Layout.fillWidth: true } }
                            Text { text: modelData.seconds > 0 ? page.mmss(modelData.seconds) : ""; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                            Btn { label: "PLAY"; on: true; onColor: Theme.emerald; onClicked: media.play(index) }
                            Btn { label: "REMOVE"; onClicked: media.remove(index) } }
                        MouseArea { anchors.fill: parent; z: -1; onClicked: media.select(index) } }
                    Text { visible: media.playlist.length === 0; anchors.centerIn: parent; text: "Empty. Type the path of a WAV or a folder and press ADD. The player feeds the master before its fader; mix it with LEVELS and the master."; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; width: parent.width - 40; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap } }
                Text { Layout.fillWidth: true; text: "WAV only (8 / 16 / 24 / 32-bit PCM and float); other rates are resampled to the engine rate. MP3 / FLAC decoding is not built in. The SOUND CARD page has DC removal and dither for the output, so the player never needs the OS mixer."; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap } } }
    }
}
