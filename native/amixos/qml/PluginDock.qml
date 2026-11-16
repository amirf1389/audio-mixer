// THE PLUG-IN'S OWN INTERFACE, INSIDE THE CONSOLE: a panel in the main window (title bar with the plug-in and its channel, POP OUT and
// CLOSE) whose body is the plug-in's native editor, a child window of the console that the engine keeps over `body` (it moves with the
// panel and hides with it). Drag the title bar to move it. One plug-in at a time; opening another replaces it.
// Note: a native window is drawn above the interface, so nothing of the console should overlap the body while it is open.
import QtQuick
import QtQuick.Controls
import AmixOS

Rectangle {
    id: dock
    objectName: "pluginDock"
    property int strip: -1
    property int slot: -1
    property string title: ""
    property string note: ""
    property int ew: 640
    property int eh: 400
    visible: false
    z: 900
    width: Math.max(ew, 320) + 2; height: ew > 0 ? eh + bar.height + 2 : 0
    radius: 6; color: Theme.card; border.color: Theme.accent
    function openFor(strip_, slot_, title_) {
        strip = strip_; slot = slot_; title = title_; note = ""; visible = true
        if (x === 0 && y === 0 && parent) { x = Math.max(8, parent.width - width - 24); y = 70 }
        const r = engine.dockPluginEditor(strip_, slot_, body)
        if (!r.ok) { visible = false; note = r.note || ""; return r }
        ew = r.w; eh = r.h
        // keep the panel on screen
        if (parent) { x = Math.min(x, Math.max(0, parent.width - width)); y = Math.min(y, Math.max(0, parent.height - height)) }
        return r
    }
    function close() { engine.undockPluginEditor(); visible = false }
    Connections { target: engine; function onPluginDockChanged() { if (!engine.pluginDocked()) dock.visible = false } }

    Rectangle {
        id: bar; x: 1; y: 1; width: parent.width - 2; height: 30; radius: 5; color: Theme.cardRaised
        Text { x: 10; anchors.verticalCenter: parent.verticalCenter; width: parent.width - btns.width - 20; elide: Text.ElideRight
            text: dock.title + "  ·  CH " + (dock.strip + 1); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true }
        DragHandler { target: dock; xAxis.minimum: 0; yAxis.minimum: 0
            xAxis.maximum: dock.parent ? dock.parent.width - dock.width : 0; yAxis.maximum: dock.parent ? dock.parent.height - dock.height : 0 }
        Row { id: btns; anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter; spacing: 6
            Repeater { model: [{ k: "pop", t: "⇱ POP OUT" }, { k: "close", t: "✕" }]
                delegate: Rectangle { width: bt.implicitWidth + 14; height: 22; radius: 5; color: Theme.ov(0.08); border.color: Theme.line
                    Text { id: bt; anchors.centerIn: parent; text: i18n.t(modelData.t, i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: { const s = dock.strip, sl = dock.slot; dock.close(); if (modelData.k === "pop") engine.openPluginWindow(s, sl) } } } } } }
    // a menu or dialog of the console is open: the plug-in's native window (drawn above everything) steps aside so it is not hidden
    readonly property bool covered: { const o = Overlay.overlay; if (!o) return false; const ch = o.children; for (let i = 0; i < ch.length; ++i) if (ch[i].visible && ch[i].width > 0) return true; return false }
    // the plug-in's editor is placed over this item by the engine (hidden while covered)
    Item { id: body; objectName: "pluginDockBody"; x: 1; y: bar.height + 1; width: dock.ew; height: dock.eh; visible: !dock.covered }
    Text { anchors.centerIn: body; visible: dock.covered; text: i18n.t("The plug-in window comes back when the menu closes.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9 }
}
