// A glass panel (the .glass-panel.wing-bezel card of the old interface): title row + content
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Layouts

Rectangle {
    id: panel
    property string title: ""
    property string subtitle: ""
    default property alias content: body.data
    color: Theme.card
    radius: Theme.radius
    border.color: Theme.line
    border.width: 1
    implicitHeight: col.implicitHeight + 24
    implicitWidth: 320
    Rectangle { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.margins: 1; width: 3; radius: 2; color: Theme.orange; opacity: 0.85 }
    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        RowLayout {
            visible: panel.title.length > 0
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 2
                Text { text: panel.title; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 12; font.bold: true; font.letterSpacing: 1.5 }
                Text { visible: panel.subtitle.length > 0; text: panel.subtitle; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            }
        }
        ColumnLayout { id: body; Layout.fillWidth: true; spacing: 8 }
    }
}
