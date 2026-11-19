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
                    Text { visible: newGroup; text: i18n.t(p.group, i18n.lang); color: Theme.groupColor(p.group); opacity: 0.85; font.family: Theme.mono; font.pixelSize: 7; font.bold: true; font.letterSpacing: 2; leftPadding: 10; topPadding: index === 0 ? 0 : 10; bottomPadding: 2 }
                    Rectangle {
                        id: btn
                        readonly property bool allowed: access.canPage(p.id)
                        visible: allowed; width: col.width - 10; x: 5; height: allowed ? 50 : 0; radius: 8
                        readonly property color tint: Theme.groupColor(p.group)
                        readonly property bool lit: rail.current === p.id
                        color: "transparent"
                        border.color: lit ? tint : "transparent"
                        // the page open now: a glow in its section's colour, brighter at the top
                        Rectangle { anchors.fill: parent; radius: parent.radius; visible: parent.lit
                            gradient: Gradient { GradientStop { position: 0; color: Qt.rgba(btn.tint.r, btn.tint.g, btn.tint.b, 0.30) } GradientStop { position: 1; color: Qt.rgba(btn.tint.r, btn.tint.g, btn.tint.b, 0.08) } } }
                        Rectangle { visible: parent.lit; width: 3; height: parent.height - 16; radius: 2; anchors.verticalCenter: parent.verticalCenter; x: -3; color: parent.tint }
                        Rectangle { anchors.fill: parent; radius: parent.radius; visible: !parent.lit && ra.containsMouse; color: Qt.rgba(btn.tint.r, btn.tint.g, btn.tint.b, 0.10) }
                        Column {
                            anchors.centerIn: parent; spacing: 3
                            Item { anchors.horizontalCenter: parent.horizontalCenter; width: 20; height: 20; opacity: p.ready ? 1 : 0.55
                                Icon { id: rIc; anchors.fill: parent; name: p.id; color: btn.lit ? Theme.harm(Theme.groupColor(p.group)) : (p.ready ? Qt.rgba(Theme.groupColor(p.group).r, Theme.groupColor(p.group).g, Theme.groupColor(p.group).b, 0.85) : Theme.dim) }
                                Text { visible: !rIc.has; anchors.centerIn: parent; text: p.icon; color: p.ready ? Theme.groupColor(p.group) : Theme.dim; font.pixelSize: 16 } }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: i18n.t(p.text, i18n.lang); color: rail.current === p.id ? Theme.ink : (p.ready ? Theme.muted : Theme.dim); font.family: Theme.mono; font.pixelSize: 7; font.bold: true; width: col.width - 14; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
                        }
                        // A NEW VERSION: a dot on SOFTWARE UPDATE until that page has been opened
                        Rectangle { objectName: "updateBadge"; visible: p.id === "update" && updater.noticeBadge; width: 10; height: 10; radius: 5; color: Theme.orange; border.color: Theme.deep; border.width: 1; anchors.top: parent.top; anchors.right: parent.right; anchors.topMargin: 6; anchors.rightMargin: 12 }
                        MouseArea { id: ra; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: rail.picked(p.id) }
                    }
                }
            }
        }
    }
}
