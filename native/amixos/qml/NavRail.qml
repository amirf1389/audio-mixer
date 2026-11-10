// The page rail on the left (the old #main-nav): groups, one button per page, the current page lit in orange; pages not yet ported are dimmed
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: rail
    property var pages: []
    property string current: "home"
    signal picked(string id)
    color: Theme.chrome
    width: 76
    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.lineSoft }
    Flickable {
        anchors.fill: parent
        contentHeight: col.implicitHeight + 16
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: col
            width: parent.width
            topPadding: 8
            Repeater {
                model: rail.pages
                delegate: Column {
                    width: col.width
                    property var p: modelData
                    property bool newGroup: index === 0 || rail.pages[index - 1].group !== p.group
                    Text { visible: newGroup; text: i18n.t(p.group, i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.bold: true; font.letterSpacing: 2; leftPadding: 10; topPadding: index === 0 ? 0 : 10; bottomPadding: 2 }
                    Rectangle {
                        readonly property bool allowed: access.canPage(p.id)
                        visible: allowed; width: col.width - 10; x: 5; height: allowed ? 50 : 0; radius: 8
                        color: rail.current === p.id ? Qt.rgba(0.918, 0.345, 0.047, 0.16) : "transparent"
                        border.color: rail.current === p.id ? Theme.orange : "transparent"
                        Column {
                            anchors.centerIn: parent; spacing: 3
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: p.icon; color: p.ready ? Theme.groupColor(p.group) : Theme.dim; font.pixelSize: 16; opacity: p.ready ? 1 : 0.55 }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: i18n.t(p.text, i18n.lang); color: rail.current === p.id ? Theme.ink : (p.ready ? Theme.muted : Theme.dim); font.family: Theme.mono; font.pixelSize: 7; font.bold: true; width: col.width - 14; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
                        }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: rail.picked(p.id) }
                    }
                }
            }
        }
    }
}
