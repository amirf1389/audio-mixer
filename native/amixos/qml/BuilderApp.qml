// AmF X1 PLUG-IN BUILDER as its own program: the builder page in a window with the product bar and the language menu.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AmixOS

ApplicationWindow {
    id: win
    objectName: "builderWindow"
    visible: true; width: 1440; height: 900; minimumWidth: 1100; minimumHeight: 640
    title: "AmF X1 Plug-in Builder · AMFB Audio"
    color: Theme.obsidian
    LayoutMirroring.enabled: i18n.rtl; LayoutMirroring.childrenInherit: true
    header: Rectangle {
        height: 48; color: Theme.chrome; border.color: Theme.lineSoft
        RowLayout { anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 12
            Text { text: "AMF X1"; color: Theme.orange; font.family: Theme.mono; font.pixelSize: 18; font.weight: Font.Black; font.letterSpacing: 4 }
            Text { text: i18n.t("PLUG-IN BUILDER", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 12; font.bold: true; font.letterSpacing: 3 }
            Text { text: i18n.t("by AMFB Audio · design VST3 plug-ins from AMFB blocks, for any DAW", i18n.lang); color: Theme.dim; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideRight }
            ComboBox { id: langBox; Layout.preferredWidth: 180; textRole: "name"; model: i18n.languages
                currentIndex: { const l = i18n.languages; for (let k = 0; k < l.length; ++k) if (l[k].code === i18n.lang) return k; return 0 }
                onActivated: function (k) { i18n.setLang(i18n.languages[k].code) } } } }
    BuilderPage { anchors.fill: parent }
}
