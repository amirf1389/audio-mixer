// TOP STATUS (1.27): the page title and the console's state at a glance, in the header: the surface LOCK, the sample rate and the clock,
// scene LOAD / SAVE and the network (LIVE SYNC to the phones and browsers). Every key is 44 px high (touch screens).
import QtQuick
import AmixOS
import QtQuick.Layouts

RowLayout {
    id: ts
    objectName: "topStatus"
    property string title: ""
    property bool compact: false
    signal go(string id)
    signal said(string text)
    spacing: 8
    component Chip: Rectangle {
        id: c
        property string icon: ""; property string label; property color hue: Theme.muted; property bool lit: false; signal hit()
        implicitWidth: Math.max(44, row.implicitWidth + 24); implicitHeight: 44; radius: Theme.radiusS
        readonly property color face: lit ? Qt.rgba(hue.r, hue.g, hue.b, 0.22) : Theme.ov(0.05)
        gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(c.face) } GradientStop { position: 1; color: Theme.btnBottom(c.face) } }
        border.color: lit ? hue : Theme.lineSoft
        Row { id: row; anchors.centerIn: parent; spacing: 8
            Icon { visible: c.icon !== "" && has; name: c.icon; color: c.lit ? c.hue : Theme.muted; width: 16; height: 16; anchors.verticalCenter: parent.verticalCenter }
            Text { visible: c.label !== ""; text: c.label; color: c.lit ? c.hue : Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; anchors.verticalCenter: parent.verticalCenter } }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: c.hit() }
    }
    Text { objectName: "pageTitle"; text: ts.title; color: Theme.accent; font.family: Theme.mono; font.pixelSize: 13; font.bold: true; font.letterSpacing: 2; elide: Text.ElideRight; Layout.maximumWidth: 200 }
    Chip { objectName: "topLock"; icon: "lock"; label: ts.compact ? "" : (access.surfaceLocked ? i18n.t("LOCKED", i18n.lang) : i18n.t("LOCK", i18n.lang)); lit: access.surfaceLocked; hue: Theme.amber
        onHit: { if (access.surfaceLocked) ts.go("access"); else { access.lock(); ts.said(i18n.t("Surface locked: faders, mutes and solos wait for the PIN.", i18n.lang)) } } }
    Chip { objectName: "topClock"; label: engine.running && engine.sampleRate > 0 ? (engine.sampleRate / 1000).toFixed(engine.sampleRate % 1000 ? 1 : 0) + " kHz · " + i18n.t("INT", i18n.lang) : i18n.t("NO CLOCK", i18n.lang)
        lit: engine.running; hue: Theme.emerald; onHit: ts.go("asio") }
    Chip { objectName: "topSceneLoad"; icon: "flag"; label: i18n.t("LOAD", i18n.lang); onHit: ts.go("scenes") }
    Chip { objectName: "topSceneSave"; label: i18n.t("SAVE", i18n.lang); hue: Theme.cyan
        onHit: { engine.storeScene(0, i18n.t("QUICK SAVE", i18n.lang)); ts.said(i18n.t("Scene 1 saved (QUICK SAVE).", i18n.lang)) } }
    Chip { objectName: "topNet"; icon: "network"; label: ts.compact ? "" : (remote.running ? (remote.remotes > 0 ? remote.remotes + i18n.t(" LINKED", i18n.lang) : i18n.t("ONLINE", i18n.lang)) : i18n.t("OFFLINE", i18n.lang))
        lit: remote.running; hue: Theme.cyan; onHit: ts.go("network") }
}
