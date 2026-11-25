// STREAMING LINKS: paste a link to YouTube, Spotify, TIDAL, Deezer or TikTok. The console says what it is (service, kind), keeps it in a
// list with your own name for it, and OPEN starts it in the service's own app or the browser. The sound is the service's: to hear it on a
// channel, pick that app under INPUT: APP SOUND on the mixer. The console does not download, record or decode what a link points to.
import QtQuick
import AmixOS
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "streamingPage"
    readonly property var hue: ({ youtube: "#ff4d4d", spotify: "#1ed760", tidal: "#e6e6e6", deezer: "#a238ff", tiktok: "#25f4ee" })
    property var probe: ({ ok: false })
    Component.onCompleted: streaming.active = true
    Component.onDestruction: streaming.active = false
    function go(id) { Window.window.current = id }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(64, bl.implicitWidth + 18); implicitHeight: 26; radius: 9
        color: on ? onColor : (bh.hovered ? Theme.ov(0.16) : Theme.ov(0.09)); border.width: 1; border.color: on || bh.hovered ? onColor : Theme.line
        Text { id: bl; anchors.centerIn: parent; text: i18n.t(parent.label, i18n.lang); color: parent.on ? "#0b0f14" : Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 0.6 }
        HoverHandler { id: bh; cursorShape: Qt.PointingHandCursor } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    Flickable {
        anchors.fill: parent; anchors.margins: 12; contentWidth: width; contentHeight: col.implicitHeight; clip: true; flickableDirection: Flickable.VerticalFlick
        ColumnLayout {
            id: col; width: parent.width; spacing: 10
            Panel { Layout.fillWidth: true; title: "NOW PLAYING"
                subtitle: "Official APIs only: a service's own sign-in (OAuth with PKCE, your client id) or the computer's MEDIA SESSION. Nothing is downloaded, recorded or captured."
                RowLayout { Layout.fillWidth: true; spacing: 10
                    ColumnLayout { Layout.fillWidth: true; spacing: 2
                        Text { objectName: "npTitle"; Layout.fillWidth: true; elide: Text.ElideRight; text: streaming.now.any ? streaming.now.title : i18n.t("Nothing playing", i18n.lang); color: Theme.ink; font.family: Theme.sans; font.pixelSize: 15; font.bold: true }
                        Text { Layout.fillWidth: true; elide: Text.ElideRight; text: streaming.now.any ? [streaming.now.artist, streaming.now.album, streaming.now.service].filter(function(x) { return !!x }).join("  ·  ") : i18n.t("Play something in a music app or a browser.", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 }
                        Text { text: i18n.t("SOURCE", i18n.lang) + ": " + (streaming.now.source || i18n.t("media session", i18n.lang)); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 } }
                    Btn { objectName: "npPrev"; label: "PREVIOUS"; onColor: Theme.cyan; onClicked: streaming.command("previous") }
                    Btn { objectName: "npToggle"; label: "PLAY / PAUSE"; on: !!streaming.now.playing; onColor: Theme.emerald; onClicked: streaming.command("toggle") }
                    Btn { objectName: "npNext"; label: "NEXT"; onColor: Theme.cyan; onClicked: streaming.command("next") } }
                Text { visible: streaming.message.length > 0; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: i18n.t(streaming.message, i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9 } }
            Panel { Layout.fillWidth: true; title: "SERVICES"
                subtitle: streaming.sealed ? i18n.t("Sign-in tokens are sealed by Windows (DPAPI).", i18n.lang) : i18n.t("This system's key store is not used yet: a sign-in lasts until the console closes.", i18n.lang)
                Repeater { model: streaming.providers
                    delegate: ColumnLayout { Layout.fillWidth: true; spacing: 4
                        RowLayout { Layout.fillWidth: true; spacing: 8
                            Text { Layout.preferredWidth: 100; text: modelData.name; color: page.hue[modelData.id] || Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true }
                            Text { Layout.fillWidth: true; elide: Text.ElideRight; text: i18n.t(modelData.note, i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                            Field { visible: modelData.login; Layout.preferredWidth: 200; text: modelData.clientId; placeholderText: i18n.t("client id", i18n.lang); onEditingFinished: streaming.setClientId(modelData.id, text) }
                            Btn { visible: modelData.login; label: modelData.signedIn ? "SIGN OUT" : "SIGN IN"; on: modelData.signedIn; onColor: Theme.emerald; onClicked: modelData.signedIn ? streaming.signOut(modelData.id) : streaming.signIn(modelData.id) } }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: modelData.setup.map(function(x, i) { return (i + 1) + ". " + i18n.t(x, i18n.lang) }).join("\n"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; lineHeight: 1.2 } } } }
            Panel { Layout.fillWidth: true; title: "STREAMING LINKS"
                subtitle: "Paste a link to YouTube, Spotify, TIDAL, Deezer or TikTok. OPEN plays it in the service's own app or the browser."
                RowLayout { Layout.fillWidth: true; spacing: 8
                    Field { id: urlIn; objectName: "linkUrl"; Layout.fillWidth: true; placeholderText: i18n.t("paste a link here", i18n.lang); onTextChanged: page.probe = text.length ? links.check(text) : ({ ok: false }) }
                    Field { id: nameIn; objectName: "linkName"; Layout.preferredWidth: 220; placeholderText: i18n.t("your name for it (optional)", i18n.lang) }
                    Btn { objectName: "linkAdd"; label: "ADD"; on: !!page.probe.ok; onColor: Theme.emerald; onClicked: { if (links.add(urlIn.text, nameIn.text)) { urlIn.text = ""; nameIn.text = "" } } } }
                RowLayout { Layout.fillWidth: true; spacing: 8; visible: urlIn.text.length > 0
                    Rectangle { width: 10; height: 10; radius: 5; color: page.probe.ok ? (page.hue[page.probe.service] || Theme.emerald) : Theme.danger }
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; color: page.probe.ok ? Theme.ink : Theme.danger
                        text: page.probe.ok ? page.probe.serviceName + "  ·  " + i18n.t(page.probe.kind, i18n.lang).toUpperCase() + "  ·  " + page.probe.id : i18n.t(page.probe.error || "not a link", i18n.lang) } }
                Text { objectName: "linkMessage"; visible: links.message.length > 0; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: i18n.t(links.message, i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9 } }
            Panel { Layout.fillWidth: true; title: "YOUR LINKS"; subtitle: links.list.length + " / 200"
                Text { visible: links.list.length === 0; text: i18n.t("No links yet: paste one above.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 10 }
                Repeater { model: links.list
                    delegate: Rectangle { objectName: "linkRow" + index; Layout.fillWidth: true; implicitHeight: 46; radius: 6; color: rowH.hovered ? Theme.ov(0.09) : Theme.ov(0.05); border.color: rowH.hovered ? (page.hue[modelData.service] || Theme.line) : Theme.line
                        HoverHandler { id: rowH }
                        Rectangle { x: 1; y: 5; width: 4; height: parent.height - 10; radius: 2; color: page.hue[modelData.service] || Theme.muted }
                        RowLayout { anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 10; spacing: 10
                            Rectangle { implicitWidth: 92; implicitHeight: 22; radius: 5; color: "transparent"; border.color: page.hue[modelData.service] || Theme.line
                                Text { anchors.centerIn: parent; text: modelData.serviceName; color: page.hue[modelData.service] || Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } }
                            ColumnLayout { Layout.fillWidth: true; spacing: 1
                                Text { Layout.fillWidth: true; elide: Text.ElideRight; text: modelData.name.length ? modelData.name : i18n.t(modelData.kind, i18n.lang).toUpperCase() + "  " + modelData.id; color: Theme.ink; font.family: Theme.sans; font.pixelSize: 12; font.bold: true }
                                Text { Layout.fillWidth: true; elide: Text.ElideMiddle; text: i18n.t(modelData.kind, i18n.lang).toUpperCase() + "  ·  " + modelData.web; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } }
                            Btn { objectName: "linkOpenApp" + index; visible: modelData.app.length > 0; label: "OPEN IN APP"; on: true; onColor: page.hue[modelData.service] || Theme.cyan; onClicked: links.open(index, true) }
                            Btn { objectName: "linkOpen" + index; label: "OPEN IN BROWSER"; onColor: Theme.cyan; onClicked: links.open(index, false) }
                            Btn { objectName: "linkRemove" + index; label: "REMOVE"; onColor: Theme.danger; onClicked: links.remove(index) } } } } }
            Panel { Layout.fillWidth: true; title: "HOW THE SOUND GETS INTO THE MIX"
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; lineHeight: 1.3
                    text: i18n.t("The service's own app (or the browser) plays the link. To hear it on a channel, choose that app under INPUT: APP SOUND on the mixer. The mixing console does not download, record or decode what a link points to, and some services do not allow their music to be mixed or broadcast: their terms decide, and music on a live stream is the account holder's responsibility.", i18n.lang) }
                RowLayout { spacing: 8
                    Btn { label: "OPEN THE MIXER"; onColor: Theme.amber; onClicked: page.go("mixer") }
                    Btn { label: "MASTERING PRESETS"; onColor: Theme.amber; onClicked: page.go("master") } } }
        }
    }
}
