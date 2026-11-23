// NOTIFICATIONS: in-app toasts (bottom right, a few seconds each) and a history panel (the NOTICES button in the header). The events:
// what's new (the signed feature feed), update ready, clipping (a strip or the master over 0 dBFS), device lost / connected, CPU overload
// (load over 90 % or new dropouts; at most one every 30 s) and the audio loop stopped. The phone and browser remotes show the same events
// (remote_page.html, android Notices.kt), worked out from the same status.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AmixOS

Item {
    id: nc
    objectName: "noticeCenter"
    property int unread: 0
    property var history: []          // [{t, kind, text}], newest first, at most 50
    property bool clipOn: false
    property real cpuAt: 0
    property int xrunsSeen: -1
    property int tickN: 0
    property string devSeen: engine.outputDeviceName
    readonly property bool panelOpen: panel.visible
    function kindColor(k) { return k === "clip" || k === "lost" || k === "cpu" || k === "stopped" ? Theme.danger : k === "connected" ? Theme.emerald : k === "update" || k === "new" ? Theme.cyan : Theme.amber }
    function push(kind, text) {
        if (history.length > 0 && history[0].text === text && Date.now() - history[0].t.getTime() < 10000) return   // the same notice again within 10 s: once
        const h = history.slice(); h.unshift({ t: new Date(), kind: kind, text: text }); if (h.length > 50) h.length = 50; history = h; unread++
        toasts.insert(0, { kind: kind, text: text }); while (toasts.count > 3) toasts.remove(toasts.count - 1); toastT.restart()
    }
    function toggle() { if (panel.visible) panel.close(); else { panel.open(); unread = 0 } }
    ListModel { id: toasts }
    Timer { id: toastT; interval: 4500; onTriggered: { if (toasts.count > 0) toasts.remove(toasts.count - 1); if (toasts.count > 0) restart() } }
    // the events
    Connections { target: engine
        function onStateChanged() {
            const d = engine.outputDeviceName
            if (d !== nc.devSeen) { if (nc.devSeen !== "" && d === "") nc.push("lost", i18n.t("Device lost: ", i18n.lang) + nc.devSeen); else if (d !== "") nc.push("connected", i18n.t("Device connected: ", i18n.lang) + d); nc.devSeen = d }
        }
    }
    property bool wasRunning: engine.running
    readonly property bool runningNow: engine.running
    onRunningNowChanged: { if (wasRunning && !runningNow) push("stopped", i18n.t("The audio loop stopped", i18n.lang)); wasRunning = runningNow }
    Connections { target: engine
        function onMetersChanged() {
            if (++nc.tickN % 6) return   // five times a second is enough for a notice (the meters themselves stay at 30)
            let c = !!engine.telv("masterClip", 0)
            for (let i = 0, n = Math.min(engine.strips, 64); i < n && !c; ++i) c = !!engine.meterAt(i).clip
            if (c && !nc.clipOn) nc.push("clip", i18n.t("Clipping: a channel or the master went over 0 dBFS", i18n.lang))
            nc.clipOn = c
            const load = engine.telv("loadPct", 0) || 0, xr = engine.telv("xruns", 0) || 0, now = Date.now()
            if ((load >= 90 || (nc.xrunsSeen >= 0 && xr > nc.xrunsSeen)) && now - nc.cpuAt > 30000) { nc.cpuAt = now; nc.push("cpu", i18n.t("CPU overload: dropouts (load ", i18n.lang) + Math.round(load) + " %)") }
            nc.xrunsSeen = xr
        }
    }
    readonly property int newFeatures: updater.newFeatures
    onNewFeaturesChanged: if (newFeatures > 0) push("new", i18n.t("What's new: ", i18n.lang) + newFeatures + i18n.t(" new features", i18n.lang))
    readonly property string updState: updater.state
    onUpdStateChanged: if (updState === "available" || updState === "downloaded") push("update", i18n.t("Update ready: ", i18n.lang) + updater.latest)
    Component.onCompleted: if (updater.newFeatures > 0) push("new", i18n.t("What's new: ", i18n.lang) + updater.newFeatures + i18n.t(" new features", i18n.lang))
    // the toasts
    Column { id: toastCol; objectName: "noticeToasts"; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 16; spacing: 6; z: 90
        Repeater { model: toasts
            Rectangle { width: Math.min(360, nc.width - 32); height: tt.implicitHeight + 16; radius: 8; color: Theme.cardSolid; border.color: nc.kindColor(model.kind); border.width: 1.5
                Rectangle { width: 4; height: parent.height - 10; y: 5; x: 5; radius: 2; color: nc.kindColor(model.kind) }
                Text { id: tt; x: 16; width: parent.width - 24; anchors.verticalCenter: parent.verticalCenter; text: model.text; color: Theme.ink; wrapMode: Text.WordWrap; font.family: Theme.mono; font.pixelSize: 10; font.bold: true }
                MouseArea { anchors.fill: parent; onClicked: { toasts.remove(index); nc.toggle() } } } } }
    // the history
    Popup { id: panel; objectName: "noticePanel"; x: nc.width - width - 12; y: 52; width: Math.min(380, nc.width - 24); height: Math.min(420, nc.height - 80); padding: 10
        background: Rectangle { radius: 8; color: Theme.cardSolid; border.color: Theme.line }
        contentItem: ColumnLayout { spacing: 6
            RowLayout { Layout.fillWidth: true
                Text { text: i18n.t("NOTIFICATIONS", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 2; Layout.fillWidth: true }
                Rectangle { implicitWidth: clT.implicitWidth + 14; implicitHeight: 20; radius: 6; color: Theme.ov(0.08); border.color: Theme.line
                    Text { id: clT; anchors.centerIn: parent; text: i18n.t("CLEAR", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                    MouseArea { anchors.fill: parent; onClicked: { nc.history = []; nc.unread = 0 } } } }
            Text { visible: nc.history.length === 0; text: i18n.t("No notifications", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 }
            ListView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 4; model: nc.history
                delegate: RowLayout { width: ListView.view.width; spacing: 6
                    Rectangle { implicitWidth: 6; implicitHeight: 6; radius: 3; color: nc.kindColor(modelData.kind) }
                    Text { text: Qt.formatTime(modelData.t, "HH:mm:ss"); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                    Text { Layout.fillWidth: true; text: modelData.text; color: Theme.ink; wrapMode: Text.WordWrap; font.family: Theme.mono; font.pixelSize: 9 } } } } }
}
