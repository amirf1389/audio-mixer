// ADMIN: behind the PIN. Edition (PRO shows the operator pages, CONSUMER hides them), licence summary, backups (now / restore / autosave),
// audit log export and clear, data folder, and the honest state of updates: this build verifies a signed update.json but does not flash
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
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 24; radius: 4; color: on ? onColor : Qt.rgba(1, 1, 1, 0.08); border.color: Theme.lineSoft
        Text { id: bl; anchors.centerIn: parent; text: label; color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    component Card: Rectangle { default property alias content: body.data; Layout.fillWidth: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; implicitHeight: body.implicitHeight + 24; ColumnLayout { id: body; anchors.fill: parent; anchors.margins: 12; spacing: 8 } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle { Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 8
                Text { text: "ADMIN:"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Text { text: access.adminMode ? "AUTHENTICATED (PIN)" : "LOCKED"; color: access.adminMode ? Theme.emerald : Theme.danger; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                Item { Layout.fillWidth: true }
                Text { text: access.message; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideLeft; Layout.maximumWidth: 480 }
                Btn { visible: access.adminMode; label: "LEAVE"; onClicked: access.leaveAdmin() } } }
        // locked view
        Rectangle { visible: !access.adminMode; Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
            ColumnLayout { anchors.centerIn: parent; spacing: 10
                Head { text: "ENTER THE ADMIN PIN"; Layout.alignment: Qt.AlignHCenter }
                PinPad { Layout.alignment: Qt.AlignHCenter; hint: "ADMIN PIN"; onEntered: function (pin) { access.enterAdmin(pin) } }
                Text { visible: access.pinIsDefault; text: "The default PIN 1234 is still active: change it on ACCOUNT."; color: Theme.danger; font.family: Theme.mono; font.pixelSize: 7; Layout.alignment: Qt.AlignHCenter } } }
        Flickable { visible: access.adminMode; Layout.fillWidth: true; Layout.fillHeight: true; contentHeight: grid.implicitHeight; clip: true; boundsBehavior: Flickable.StopAtBounds
            GridLayout { id: grid; width: parent.width; columns: 2; columnSpacing: 8; rowSpacing: 8
                Card { Head { text: "EDITION & LICENCE" }
                    RowLayout { spacing: 6
                        Btn { label: "PRO (operator pages shown)"; on: !access.consumerEdition; onColor: Theme.cyan; onClicked: access.consumerEdition = false }
                        Btn { label: "CONSUMER (hidden)"; on: access.consumerEdition; onColor: Theme.amber; onClicked: access.consumerEdition = true } }
                    Note { text: "CONSUMER hides ACCOUNT, ACCESS, ADMIN and DIAGNOSTICS from the rail for everyone who is not in ADMIN mode. The plan itself (" + page.lic.planName + ", " + engine.licensedChannels + " channels, " + page.lic.state + ") is on LICENSE; keys are bound to this machine (" + (page.lic.machine ? page.lic.machine : "—") + ")." } }
                Card { Head { text: "BACKUP & RECOVERY" }
                    RowLayout { spacing: 6
                        Btn { label: "BACKUP NOW"; on: true; onColor: Theme.cyan; onClicked: settings.exportConsole("") }
                        Btn { label: "RESTORE BACKUP"; onClicked: settings.importConsole("") }
                        Btn { label: "RESTORE AUTOSAVE"; on: settings.autosave.exists; onClicked: settings.restoreAutosave() }
                        Btn { label: "DATA FOLDER"; onClicked: settings.openDataFolder() } }
                    Note { text: (settings.autosave.exists ? "Autosave " + settings.autosave.at + " (" + settings.autosave.bytes + " bytes), every 60 s. " : "No autosave yet. ") + "BACKUP NOW writes ~/amixos-console.json (console, scenes, presets; never the licence). " + settings.message } }
                Card { Head { text: "AUDIT LOG" }
                    RowLayout { spacing: 6
                        Btn { label: "EXPORT CSV"; on: true; onColor: Theme.cyan; onClicked: access.exportAudit() }
                        Btn { label: "CLEAR LOG"; onColor: Theme.danger; onClicked: access.clearAudit() }
                        Text { text: access.audit.length + " events kept (last 300 shown on ACCOUNT)"; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 } }
                    Note { text: "Role switches, locks, PIN changes, permission edits and admin actions, with time and role, in ~/.amixos/audit.log." } }
                Card { Head { text: "UPDATES" }
                    Note { text: "Version " + appVersion + ". The console does not download or flash anything by itself: updates come as a signed installer from the releases page, and the installer's update.json is verified against the vendor's public key before anything is applied (the same check the daemon runs). There is no OTA firmware, no A/B partition and no hardware vault in this build; the browser demo showed those as mock-ups." } }
                Card { Layout.columnSpan: 2; Head { text: "MAINTENANCE" }
                    RowLayout { spacing: 6
                        Btn { label: "LOCK CONSOLE"; onColor: Theme.danger; onClicked: access.lock() }
                        Btn { label: "RESTART ENGINE"; onClicked: { engine.stop(); settings.startLastDevice() } }
                        Text { text: "SAFE MODE and FACTORY RESET are on DIAGNOSTICS; CALIBRATE has the loopback and noise tests."; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } } }
            } }
    }
}
