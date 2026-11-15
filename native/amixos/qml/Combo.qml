// A drop-down in the console's own colours (the style's box is light on some platforms): explicit background, text and list.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls

ComboBox {
    id: c
    implicitHeight: 24
    font.family: Theme.mono; font.pixelSize: 9
    background: Rectangle { radius: 5; color: Theme.cardRaised; border.color: c.activeFocus ? Theme.amber : Theme.line }
    contentItem: Text { leftPadding: 8; rightPadding: 24; text: i18n.t(c.displayText, i18n.lang); color: Theme.ink; font: c.font; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
    indicator: Text { x: c.width - width - 8; anchors.verticalCenter: parent.verticalCenter; text: "▾"; color: Theme.muted; font.pixelSize: 10 }
    popup: Popup { y: c.height; width: c.width; implicitHeight: Math.min(contentItem.implicitHeight + 2, 360); padding: 1
        background: Rectangle { radius: 5; color: Theme.cardSolid; border.color: Theme.line }
        contentItem: ListView { clip: true; implicitHeight: contentHeight; model: c.popup.visible ? c.delegateModel : null; currentIndex: c.highlightedIndex; ScrollIndicator.vertical: ScrollIndicator {} } }
    delegate: ItemDelegate { width: c.width; height: 24; highlighted: c.highlightedIndex === index
        contentItem: Text { text: i18n.t(String(c.textRole ? (Array.isArray(c.model) ? modelData[c.textRole] : model[c.textRole]) : modelData), i18n.lang); color: Theme.ink; font: c.font; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        background: Rectangle { color: highlighted ? Theme.ov(0.12) : "transparent" } }
}
