// The console's slider: a thin dark track, the part before the handle filled in the page's colour, a round handle that grows a ring under
// the pointer. A stock Slider otherwise (from / to / value / onMoved ...), so a page can still give it its own background or handle.
import QtQuick
import QtQuick.Controls
import AmixOS

Slider {
    id: ms
    property color tint: Theme.accent
    implicitHeight: 20; implicitWidth: 120
    background: Rectangle {
        x: ms.leftPadding; y: ms.topPadding + ms.availableHeight / 2 - height / 2; width: ms.availableWidth; height: 4; radius: 2
        color: Theme.dark ? "#2a2e36" : "#cfd4dc"
        Rectangle { width: ms.visualPosition * parent.width; height: parent.height; radius: 2; color: ms.enabled ? ms.tint : Theme.dim }
    }
    handle: Rectangle {
        x: ms.leftPadding + ms.visualPosition * (ms.availableWidth - width); y: ms.topPadding + ms.availableHeight / 2 - height / 2
        width: 14; height: 14; radius: 7; color: ms.pressed ? ms.tint : (Theme.dark ? "#f1f5f9" : "#ffffff"); border.width: 2; border.color: ms.enabled ? ms.tint : Theme.dim
        Rectangle { anchors.centerIn: parent; width: 22; height: 22; radius: 11; z: -1; color: Qt.rgba(ms.tint.r, ms.tint.g, ms.tint.b, 0.25); visible: ms.hovered || ms.pressed }
    }
}
