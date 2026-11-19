// A TOOLBAR ROW THAT NEVER CUTS A BUTTON: when the window is too narrow for all of it, the row scrolls sideways (drag, wheel, or the
// "›" at its right end), and a fade says there is more. With room for everything it is an ordinary row.
import QtQuick
import QtQuick.Layouts
import AmixOS

Item {
    id: root
    default property alias content: row.data
    property alias spacing: row.spacing
    readonly property bool more: fl.contentWidth > fl.width + 1 && !fl.atXEnd
    implicitHeight: row.implicitHeight; implicitWidth: row.implicitWidth
    Flickable {
        id: fl; anchors.fill: parent; clip: true; contentWidth: Math.max(width, row.implicitWidth); contentHeight: height
        flickableDirection: Flickable.HorizontalFlick; boundsBehavior: Flickable.StopAtBounds; interactive: contentWidth > width + 1
        RowLayout { id: row; width: fl.contentWidth; height: fl.height; spacing: 6 }
        WheelHandler { enabled: fl.interactive; onWheel: (e) => { fl.contentX = Math.max(0, Math.min(fl.contentWidth - fl.width, fl.contentX - (e.angleDelta.y + e.angleDelta.x) * 0.5)) } }
    }
    Rectangle { z: 5; visible: root.more; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 30
        gradient: Gradient { orientation: Gradient.Horizontal; GradientStop { position: 0; color: "transparent" } GradientStop { position: 0.6; color: Theme.card } }
        Text { anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter; text: "›"; color: Theme.orange; font.pixelSize: 16; font.bold: true }
        MouseArea { objectName: "scrollHintArea"; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: fl.contentX = Math.max(0, fl.contentWidth - fl.width) } }
}
