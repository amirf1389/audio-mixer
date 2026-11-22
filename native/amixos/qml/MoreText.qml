// COMPACT: a long explanation shows as one line (ending in "…"); a click opens it in full and closes it again
import QtQuick

Text {
    id: mt
    property bool open: false
    wrapMode: Text.WordWrap
    maximumLineCount: open ? 1000 : 1
    elide: Text.ElideRight
    MouseArea { anchors.fill: parent; cursorShape: mt.truncated || mt.open ? Qt.PointingHandCursor : Qt.ArrowCursor; enabled: mt.truncated || mt.open; onClicked: mt.open = !mt.open }
}
