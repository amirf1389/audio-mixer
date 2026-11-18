// SOFTWARE UPDATE: one page for keeping the console current, laid out the way a system's own update page is. At the top: the state in one
// line (up to date, an update is ready, checking ...) with CHECK FOR UPDATES; under it the update that is waiting with its notes and
// DOWNLOAD / INSTALL AND RESTART; the optional plug-ins of the signed list; the history of releases. Below: SYSTEM INFORMATION, read only:
// the computer, its firmware (BIOS / UEFI), the operating system and the installed sound drivers. The console updates only itself and its
// own plug-ins, from AMFB Audio's signed list; it never changes a driver, the firmware or Windows.
import QtQuick
import AmixOS
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "updatePage"
    readonly property string st: updater.state
    readonly property bool waiting: st === "available" || st === "downloading" || st === "downloaded"
    readonly property color tone: st === "error" ? Theme.danger : waiting ? Theme.amber : st === "upToDate" ? Theme.emerald : Theme.cyan
    readonly property var fw: engine.firmwareInfo()
    readonly property var sys: engine.systemInfo()
    readonly property var drivers: engine.soundDrivers()
    function headline() { return st === "checking" ? i18n.t("Checking for updates ...", i18n.lang) : st === "available" ? i18n.t("An update is ready", i18n.lang) : st === "downloading" ? i18n.t("Downloading the update ...", i18n.lang)
        : st === "downloaded" ? i18n.t("The update is downloaded and verified", i18n.lang) : st === "upToDate" ? i18n.t("You are up to date", i18n.lang) : st === "error" ? i18n.t("The update could not be checked", i18n.lang) : i18n.t("Press CHECK FOR UPDATES", i18n.lang) }
    function go(id) { Window.window.current = id }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(72, bl.implicitWidth + 22); implicitHeight: 30; radius: 10
        color: on ? onColor : (bh.hovered ? Theme.ov(0.16) : Theme.ov(0.09)); border.width: 1; border.color: on || bh.hovered ? onColor : Theme.line
        Text { id: bl; anchors.centerIn: parent; text: i18n.t(parent.label, i18n.lang); color: parent.on ? "#0b0f14" : Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 0.6 }
        HoverHandler { id: bh; cursorShape: Qt.PointingHandCursor } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    component Line: RowLayout { property string k; property string v; Layout.fillWidth: true; spacing: 10
        Text { text: parent.k; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; Layout.preferredWidth: 150 }
        Text { text: parent.v.length ? parent.v : i18n.t("not shown by this computer", i18n.lang); color: parent.v.length ? Theme.ink : Theme.dim; font.family: Theme.sans; font.pixelSize: 12; font.bold: parent.v.length > 0; wrapMode: Text.WordWrap; Layout.fillWidth: true } }
    Flickable {
        anchors.fill: parent; anchors.margins: 12; contentWidth: width; contentHeight: col.implicitHeight; clip: true; flickableDirection: Flickable.VerticalFlick
        ColumnLayout {
            id: col; width: parent.width; spacing: 10
            // THE STATE, in one line
            Rectangle { objectName: "updateHero"; Layout.fillWidth: true; implicitHeight: 112; radius: 8; color: Theme.card; border.width: 2; border.color: page.tone
                RowLayout { anchors.fill: parent; anchors.margins: 18; spacing: 18
                    Rectangle { implicitWidth: 64; implicitHeight: 64; radius: 32; color: Qt.rgba(page.tone.r, page.tone.g, page.tone.b, 0.16); border.width: 2; border.color: page.tone
                        Icon { anchors.centerIn: parent; width: 34; height: 34; name: page.st === "upToDate" ? "check" : "update"; color: page.tone
                            RotationAnimation on rotation { running: page.st === "checking" && page.visible; from: 0; to: 360; duration: 1400; loops: Animation.Infinite } } }
                    ColumnLayout { Layout.fillWidth: true; spacing: 4
                        Text { objectName: "updateHeadline"; text: page.headline(); color: Theme.ink; font.family: Theme.sans; font.pixelSize: 22; font.weight: Font.Black }
                        Text { Layout.fillWidth: true; elide: Text.ElideRight; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 10
                            text: Theme.product + " " + updater.current + (updater.latest !== "" && updater.latest !== updater.current ? "  →  " + updater.latest : "") + "  ·  " + i18n.t("last checked:", i18n.lang) + " " + (updater.lastCheck !== "" ? updater.lastCheck : i18n.t("never", i18n.lang)) }
                        Text { visible: updater.message.length > 0; Layout.fillWidth: true; elide: Text.ElideRight; text: i18n.t(updater.message, i18n.lang); color: page.st === "error" ? Theme.danger : Theme.muted; font.family: Theme.mono; font.pixelSize: 9 } }
                    Btn { objectName: "updateCheck"; label: page.st === "checking" ? "CHECKING" : "CHECK FOR UPDATES"; on: page.st !== "checking"; onColor: Theme.cyan; onClicked: updater.check() } }
                Rectangle { visible: page.st === "downloading"; anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 2; height: 4; radius: 2; color: Theme.amber; width: (parent.width - 4) * updater.progress / 100 } }
            // THE UPDATE THAT IS WAITING
            Panel { visible: page.waiting; Layout.fillWidth: true; title: i18n.t("UPDATE", i18n.lang) + " " + updater.latest; subtitle: updater.released !== "" ? i18n.t("Released", i18n.lang) + " " + updater.released + (updater.file.size ? "  ·  " + (updater.file.size / 1048576).toFixed(1) + " MB" : "") : ""
                Repeater { model: updater.notes; delegate: Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "•  " + modelData; color: Theme.ink; font.family: Theme.sans; font.pixelSize: 11 } }
                RowLayout { spacing: 8
                    Btn { objectName: "updateDownload"; visible: page.st === "available" && updater.file.url !== undefined; label: "DOWNLOAD"; on: true; onColor: Theme.amber; onClicked: updater.download() }
                    Text { visible: page.st === "downloading"; text: updater.progress + " %"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 12; font.bold: true }
                    Btn { objectName: "updateInstall"; visible: updater.canInstall; label: "INSTALL AND RESTART"; on: true; onColor: Theme.emerald; onClicked: updater.installAndRestart() }
                    Btn { visible: updater.downloaded !== ""; label: "OPEN DOWNLOAD FOLDER"; onColor: Theme.cyan; onClicked: updater.openDownloadFolder() } }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; text: i18n.t("The download is checked against the size and sha256 in AMFB Audio's signed list. Nothing installs until you press INSTALL AND RESTART.", i18n.lang) } }
            // SETTINGS OF THE UPDATE
            Panel { Layout.fillWidth: true; title: "UPDATE OPTIONS"
                RowLayout { spacing: 8
                    Btn { objectName: "updateAtStart"; label: updater.checkAtStart ? "CHECK AT START: ON" : "CHECK AT START: OFF"; on: updater.checkAtStart; onColor: Theme.emerald; onClicked: updater.checkAtStart = !updater.checkAtStart }
                    Btn { objectName: "updateAuto"; label: updater.autoDownload ? "DOWNLOAD BY ITSELF: ON" : "DOWNLOAD BY ITSELF: OFF"; on: updater.autoDownload; onColor: Theme.emerald; onClicked: updater.autoDownload = !updater.autoDownload }
                    Btn { label: "UPDATE FROM A USB STICK"; onColor: Theme.cyan; onClicked: page.go("engctl") }
                    Btn { label: "PLUG-IN DOWNLOADS" + (updater.plugins.length ? " (" + updater.plugins.length + ")" : ""); onColor: Theme.cyan; onClicked: page.go("store") } } }
            // HISTORY
            Panel { Layout.fillWidth: true; title: "UPDATE HISTORY"; subtitle: updater.features.length ? "" : "The list of releases appears after the first check."
                Repeater { model: updater.features.slice(0, 6)
                    delegate: ColumnLayout { Layout.fillWidth: true; spacing: 2
                        RowLayout { spacing: 8
                            Rectangle { implicitWidth: vT.implicitWidth + 14; implicitHeight: 18; radius: 9; color: modelData.version === updater.current ? Theme.emerald : Theme.ov(0.10)
                                Text { id: vT; anchors.centerIn: parent; text: modelData.version; color: modelData.version === updater.current ? "#0b0f14" : Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } }
                            Text { text: modelData.title + (modelData.date ? "  ·  " + modelData.date : ""); color: Theme.ink; font.family: Theme.sans; font.pixelSize: 12; font.bold: true }
                            Text { visible: modelData.version === updater.current; text: i18n.t("INSTALLED", i18n.lang); color: Theme.emerald; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } }
                        Repeater { model: (modelData.items || []).slice(0, 4); delegate: Text { Layout.fillWidth: true; Layout.leftMargin: 10; elide: Text.ElideRight; text: "•  " + modelData; color: Theme.muted; font.family: Theme.sans; font.pixelSize: 10 } } } } }
            // SYSTEM INFORMATION (read only)
            RowLayout { Layout.fillWidth: true; spacing: 10
                Panel { objectName: "sysInfoPanel"; Layout.fillWidth: true; Layout.preferredWidth: 100; Layout.alignment: Qt.AlignTop; title: "SYSTEM INFORMATION"; subtitle: "Read only: what this computer says about itself."
                    Line { k: i18n.t("COMPUTER", i18n.lang); v: (page.fw.maker + " " + page.fw.model).trim() }
                    Line { k: i18n.t("MAINBOARD", i18n.lang); v: page.fw.board }
                    Line { k: i18n.t("OPERATING SYSTEM", i18n.lang); v: page.fw.os }
                    Line { k: i18n.t("PROCESSOR", i18n.lang); v: page.sys.cpuName + "  ·  " + page.sys.logical + " " + i18n.t("threads", i18n.lang) + "  ·  " + page.sys.arch }
                    Line { k: i18n.t("MEMORY (RAM)", i18n.lang); v: (page.sys.ramTotalMb / 1024).toFixed(1) + " GB" }
                    Line { k: Theme.product.toUpperCase(); v: updater.current + "  ·  " + engine.edition } }
                Panel { objectName: "biosPanel"; Layout.fillWidth: true; Layout.preferredWidth: 100; Layout.alignment: Qt.AlignTop; title: "SYSTEM BIOS / UEFI"; subtitle: "Read only. The console never updates the firmware: that is done with the computer maker's own tool."
                    Line { k: i18n.t("MAKER", i18n.lang); v: page.fw.biosVendor }
                    Line { k: i18n.t("VERSION", i18n.lang); v: page.fw.biosVersion }
                    Line { k: i18n.t("DATE", i18n.lang); v: page.fw.biosDate } } }
            Panel { objectName: "driversPanel"; Layout.fillWidth: true; title: "SYSTEM DRIVERS · SOUND"; subtitle: "Read only: the sound drivers installed on this computer. The console never installs, updates or removes a driver; a newer one comes from the device maker or from Windows Update."
                Text { visible: page.drivers.length === 0; text: i18n.t("This computer does not list its sound drivers here.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 10 }
                Repeater { model: page.drivers
                    delegate: Rectangle { Layout.fillWidth: true; implicitHeight: 40; radius: 6; color: Theme.ov(0.05); border.color: Theme.line
                        RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 12
                            Icon { width: 18; height: 18; name: "card"; color: Theme.cyan }
                            ColumnLayout { Layout.fillWidth: true; spacing: 1
                                Text { Layout.fillWidth: true; elide: Text.ElideRight; text: modelData.name; color: Theme.ink; font.family: Theme.sans; font.pixelSize: 12; font.bold: true }
                                Text { Layout.fillWidth: true; elide: Text.ElideRight; text: modelData.provider; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } }
                            Text { text: modelData.version; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true }
                            Text { visible: modelData.date.length > 0; text: modelData.date; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9 } } } }
                RowLayout { spacing: 8
                    Btn { label: "SOUND CARD"; onColor: Theme.cyan; onClicked: page.go("asio") }
                    Btn { label: "CONNECTIONS"; onColor: Theme.cyan; onClicked: page.go("io") } } }
        }
    }
}
