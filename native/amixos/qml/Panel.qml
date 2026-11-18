// A flat tile: a header bar with the title, then the content
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Layouts

Rectangle {
    id: panel
    property string title: ""
    property string subtitle: ""
    default property alias content: body.data
    color: Theme.card
    radius: 6
    border.color: Theme.line
    border.width: 1
    implicitHeight: col.implicitHeight + 24
    implicitWidth: 320
    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        Rectangle {   // the section's header bar: its title, and what it is for under it
            visible: panel.title.length > 0
            Layout.fillWidth: true; implicitHeight: headCol.implicitHeight + 10; radius: 4; color: Theme.head
            ColumnLayout { id: headCol; anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 2
                Text { text: i18n.t(panel.title, i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 12; font.bold: true; font.letterSpacing: 2 }
                Text { visible: panel.subtitle.length > 0; text: i18n.t(panel.subtitle, i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            }
        }
        ColumnLayout { id: body; Layout.fillWidth: true; spacing: 8 }
    }
}
