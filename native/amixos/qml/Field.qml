// A text field in the console's own colours (the style's field is white on some platforms).
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls

TextField {
    id: f
    implicitHeight: 26
    color: Theme.ink; placeholderTextColor: Theme.dim; selectionColor: Theme.orange; selectedTextColor: "#000"; selectByMouse: true
    font.family: Theme.mono; font.pixelSize: 9; leftPadding: 8; rightPadding: 8
    background: Rectangle { radius: 5; color: Theme.deep; border.color: f.activeFocus ? Theme.amber : Theme.line }
}
