// ADMIN: behind the PIN. Edition (PRO shows the operator pages, CONSUMER hides them), licence summary, backups (now / restore / autosave),
// audit log export and clear, data folder, and the honest state of updates: this build verifies a signed updater.json but does not flash
// anything; the browser build's KNOX vault, A/B partitions and OTA firmware were mock-ups and are not ported.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "adminPage"
    readonly property var lic: engine.license
    component Head: Text { color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
    component Note: Text { Layout.fillWidth: true; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 24; radius: 8; gradient: Gradient { GradientStop { position: 0; color: on ? Qt.lighter(onColor, 1.22) : Theme.ov(0.12) } GradientStop { position: 1; color: on ? Qt.darker(onColor, 1.15) : Theme.ov(0.035) } } color: on ? onColor : Theme.ov(0.08); border.color: Theme.lineSoft
        Rectangle { anchors.fill: parent; anchors.margins: 1; radius: Math.max(0, parent.radius - 1); color: "transparent"; border.width: 1; border.color: Qt.rgba(1, 1, 1, parent.on ? 0.30 : 0.07) }
        Text { id: bl; anchors.centerIn: parent; text: i18n.t(label, i18n.lang); color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    component Card: Rectangle { default property alias content: body.data; Layout.fillWidth: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; implicitHeight: body.implicitHeight + 24; ColumnLayout { id: body; anchors.fill: parent; anchors.margins: 12; spacing: 8 } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle { Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 8
                Text { text: i18n.t("ADMIN:", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Text { text: access.adminMode ? i18n.t("AUTHENTICATED (PIN)", i18n.lang) : i18n.t("LOCKED", i18n.lang); color: access.adminMode ? Theme.emerald : Theme.danger; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                Item { Layout.fillWidth: true }
                Text { text: access.message; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideLeft; Layout.maximumWidth: 480 }
                Btn { visible: access.adminMode; label: i18n.t("LEAVE", i18n.lang); onClicked: access.leaveAdmin() } } }
        // locked view
        Rectangle { visible: !access.adminMode; Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
            ColumnLayout { anchors.centerIn: parent; spacing: 10
                Head { text: i18n.t("ENTER THE ADMIN PIN", i18n.lang); Layout.alignment: Qt.AlignHCenter }
                PinPad { Layout.alignment: Qt.AlignHCenter; hint: i18n.t("ADMIN PIN", i18n.lang); onEntered: function (pin) { access.enterAdmin(pin) } }
                Text { visible: access.pinIsDefault; text: i18n.t("The default PIN 1234 is still active: change it on ACCOUNT.", i18n.lang); color: Theme.danger; font.family: Theme.mono; font.pixelSize: 7; Layout.alignment: Qt.AlignHCenter } } }
        Flickable { visible: access.adminMode; Layout.fillWidth: true; Layout.fillHeight: true; contentHeight: grid.implicitHeight; clip: true; boundsBehavior: Flickable.StopAtBounds
            GridLayout { id: grid; width: parent.width; columns: 2; columnSpacing: 8; rowSpacing: 8
                Card { Head { text: i18n.t("EDITION & LICENCE", i18n.lang) }
                    RowLayout { spacing: 6
                        Btn { label: i18n.t("PRO (operator pages shown)", i18n.lang); on: !access.consumerEdition; onColor: Theme.cyan; onClicked: access.consumerEdition = false }
                        Btn { label: i18n.t("CONSUMER (hidden)", i18n.lang); on: access.consumerEdition; onColor: Theme.amber; onClicked: access.consumerEdition = true } }
                    Note { text: i18n.t("CONSUMER hides ACCOUNT, ACCESS, ADMIN and DIAGNOSTICS from the rail for everyone who is not in ADMIN mode. The plan itself (", i18n.lang) + page.lic.planName + ", " + engine.licensedChannels + i18n.t(" channels, ", i18n.lang) + page.lic.state + i18n.t(") is on LICENSE; keys are bound to this machine (", i18n.lang) + (page.lic.machine ? page.lic.machine : "—") + ")." } }
                Card { Head { text: i18n.t("BACKUP & RECOVERY", i18n.lang) }
                    RowLayout { spacing: 6
                        Btn { label: i18n.t("BACKUP NOW", i18n.lang); on: true; onColor: Theme.cyan; onClicked: settings.exportConsole("") }
                        Btn { label: i18n.t("RESTORE BACKUP", i18n.lang); onClicked: settings.importConsole("") }
                        Btn { label: i18n.t("RESTORE AUTOSAVE", i18n.lang); on: settings.autosave.exists; onClicked: settings.restoreAutosave() }
                        Btn { label: i18n.t("DATA FOLDER", i18n.lang); onClicked: settings.openDataFolder() } }
                    Note { text: (settings.autosave.exists ? i18n.t("Autosave ", i18n.lang) + settings.autosave.at + " (" + settings.autosave.bytes + i18n.t(" bytes), every 60 s. ", i18n.lang) : i18n.t("No autosave yet. ", i18n.lang)) + i18n.t("BACKUP NOW writes ~/amixos-console.json (console, scenes, presets; never the licence). ", i18n.lang) + settings.message } }
                Card { Head { text: i18n.t("AUDIT LOG", i18n.lang) }
                    RowLayout { spacing: 6
                        Btn { label: i18n.t("EXPORT CSV", i18n.lang); on: true; onColor: Theme.cyan; onClicked: access.exportAudit() }
                        Btn { label: i18n.t("CLEAR LOG", i18n.lang); onColor: Theme.danger; onClicked: access.clearAudit() }
                        Text { text: access.audit.length + i18n.t(" events kept (last 300 shown on ACCOUNT)", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } }
                    Note { text: i18n.t("Role switches, locks, PIN changes, permission edits and admin actions, with time and role, in ~/.amixos/audit.log.", i18n.lang) } }
                Card { Head { text: i18n.t("UPDATES", i18n.lang) }
                    RowLayout { spacing: 6
                        Btn { label: i18n.t("CHECK NOW", i18n.lang); on: updater.state !== "checking"; onColor: Theme.cyan; onClicked: updater.check() }
                        Btn { visible: updater.state === "available" && updater.file.url !== undefined; label: i18n.t("DOWNLOAD ", i18n.lang) + (updater.file.name || ""); on: true; onClicked: updater.download() }
                        Btn { visible: updater.downloaded !== ""; label: i18n.t("OPEN DOWNLOAD FOLDER", i18n.lang); onClicked: updater.openDownloadFolder() }
                        Btn { label: updater.checkAtStart ? i18n.t("AT START: ON", i18n.lang) : i18n.t("AT START: OFF", i18n.lang); on: updater.checkAtStart; onColor: Theme.emerald; onClicked: updater.checkAtStart = !updater.checkAtStart } }
                    Note { text: i18n.t("Version ", i18n.lang) + updater.current + " · " + updater.state.toUpperCase() + (updater.lastCheck ? i18n.t(" · last check ", i18n.lang) + updater.lastCheck : "") + ". " + updater.message + i18n.t(" Signed manifest from ", i18n.lang) + updater.url + i18n.t("; a download is sha256-checked and saved under ~/.amixos/downloads; the console never runs installers itself. No OTA firmware, no A/B partition, no hardware vault in this build.", i18n.lang) } }
                Card { Layout.columnSpan: 2; Head { text: i18n.t("MAINTENANCE", i18n.lang) }
                    RowLayout { spacing: 6
                        Btn { label: i18n.t("LOCK CONSOLE", i18n.lang); onColor: Theme.danger; onClicked: access.lock() }
                        Btn { label: i18n.t("RESTART ENGINE", i18n.lang); onClicked: { engine.stop(); settings.startLastDevice() } }
                        Text { text: i18n.t("SAFE MODE and FACTORY RESET are on DIAGNOSTICS; CALIBRATE has the loopback and noise tests.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } } }
            } }
    }
}
