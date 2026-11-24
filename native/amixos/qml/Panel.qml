// A flat tile: a header bar with the title, then the content
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Layouts

Rectangle {
    id: panel
    property string title: ""
    property string subtitle: ""
    property bool more: false   // COMPACT: the explanation under the title opens with a click on the header (the (i) mark)
    default property alias content: body.data
    color: Theme.card
    radius: Theme.radius
    border.color: Theme.fluid ? Theme.glassEdge : Theme.line
    gradient: Theme.fluid ? sheen : null   // FLUID GLASS: a soft light at the top edge, the translucent card below (no blur)
    Gradient { id: sheen; GradientStop { position: 0; color: Theme.glassTop } GradientStop { position: 0.35; color: Theme.card } }
    Behavior on border.color { enabled: Theme.motion; ColorAnimation { duration: Theme.motionMs } }
    border.width: 1
    implicitHeight: col.implicitHeight + 16
    implicitWidth: 320
    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6
        Rectangle {   // the section's header bar: its title, and what it is for under it
            visible: panel.title.length > 0
            Layout.fillWidth: true; implicitHeight: Math.max(28, headCol.implicitHeight + 8); radius: Theme.radiusS; color: Theme.head
            ColumnLayout { id: headCol; anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.leftMargin: 8; anchors.rightMargin: 24; spacing: 2
                Text { text: i18n.t(panel.title, i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1; elide: Text.ElideRight; Layout.fillWidth: true }
                Text { visible: panel.more && panel.subtitle.length > 0; text: i18n.t(panel.subtitle, i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            }
            Rectangle {   // (i): there is more to read
                visible: panel.subtitle.length > 0; anchors.right: parent.right; anchors.rightMargin: 6; anchors.top: parent.top; anchors.topMargin: 7
                width: 14; height: 14; radius: 7; color: panel.more ? Theme.amber : "transparent"; border.color: panel.more ? Theme.amber : Theme.muted
                Text { anchors.centerIn: parent; text: "i"; color: panel.more ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } }
            MouseArea { anchors.fill: parent; enabled: panel.subtitle.length > 0; cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor; onClicked: panel.more = !panel.more }
        }
        ColumnLayout { id: body; Layout.fillWidth: true; spacing: 8 }
    }
}
