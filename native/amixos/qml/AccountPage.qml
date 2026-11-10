// ACCOUNT: the operator profile (FOH MASTER, MONITOR TECH, BROADCAST, GUEST), the console surface PIN lock, and the audit trail.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "accountPage"
    property string wantRole: ""
    readonly property var mx: access.matrix
    function permLine(row) { var a = [], d = []; for (var p = 0; p < access.perms.length; ++p) { if (row && row[access.perms[p]]) a.push(access.perms[p].toUpperCase()); else d.push(access.perms[p].toUpperCase()) } return (a.length ? "ALLOWED: " + a.join(" · ") : "") + (d.length ? "   LOCKED: " + d.join(" · ") : "") }
    function roleName(id) { for (let i = 0; i < access.roles.length; ++i) if (access.roles[i].id === id) return access.roles[i].name; return id }
    component Head: Text { color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.5 }
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 24; radius: 4; color: on ? onColor : Theme.ov(0.08); border.color: Theme.lineSoft
        Text { id: bl; anchors.centerIn: parent; text: label; color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle { Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 8
                Text { text: "ACCOUNT:"; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Text { text: "PROFILE: " + page.roleName(access.role); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                Text { text: access.surfaceLocked ? "SURFACE LOCKED" : "SURFACE OPEN"; color: access.surfaceLocked ? Theme.danger : Theme.emerald; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                Item { Layout.fillWidth: true }
                Text { text: access.message; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideLeft; Layout.maximumWidth: 420 }
                Btn { label: access.surfaceLocked ? "LOCKED" : "LOCK CONSOLE"; on: !access.surfaceLocked; onColor: Theme.danger; onClicked: if (!access.surfaceLocked) access.lock() } } }
        RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            // profiles
            Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 6
                    Head { text: "SELECT ACTIVE OPERATOR PROFILE" }
                    Repeater { model: access.roles; delegate: Rectangle { Layout.fillWidth: true; implicitHeight: 58; radius: 6; color: access.role === modelData.id ? Qt.rgba(0.98, 0.75, 0.14, 0.14) : Theme.ov(0.04); border.color: access.role === modelData.id ? Theme.amber : (page.wantRole === modelData.id ? Theme.cyan : Theme.lineSoft)
                        ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 2
                            RowLayout { Text { text: modelData.name; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true } Item { Layout.fillWidth: true } }
                            Text { Layout.fillWidth: true; text: modelData.blurb; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }
                            Text { text: page.permLine(page.mx[modelData.id]); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
                        MouseArea { anchors.fill: parent; onClicked: { if (!access.setRole(modelData.id, "")) page.wantRole = modelData.id; else page.wantRole = "" } } } }
                    Text { visible: page.wantRole !== ""; text: "Enter the PIN on the pad to switch to " + page.roleName(page.wantRole); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8 }
                    Item { Layout.fillHeight: true }
                    Text { Layout.fillWidth: true; text: "Switching down (fewer rights) is free; switching up asks for the PIN. The pages a profile may not use disappear from the rail; ACCESS shows and edits the matrix."; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap } } }
            // pin
            Rectangle { Layout.preferredWidth: 230; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 8
                    Head { text: "CONSOLE SURFACE PIN" }
                    Text { visible: access.pinIsDefault; Layout.fillWidth: true; text: "DEFAULT PIN 1234 IS STILL ACTIVE: set your own below before a show."; color: Theme.danger; font.family: Theme.mono; font.pixelSize: 7; font.bold: true; wrapMode: Text.WordWrap }
                    PinPad { Layout.alignment: Qt.AlignHCenter; hint: page.wantRole !== "" ? "PIN FOR " + page.roleName(page.wantRole) : (access.surfaceLocked ? "PIN TO UNLOCK" : "PIN TO LOCK / CHECK")
                        onEntered: function (pin) { if (page.wantRole !== "") { if (access.setRole(page.wantRole, pin)) page.wantRole = "" } else if (access.surfaceLocked) access.unlock(pin); else if (access.unlock(pin)) access.lock() } }
                    Text { text: access.failedPins > 0 ? access.failedPins + " wrong PIN(s) in a row" : "Tap digits, then ENTER. Locked: faders, mutes and solos do not react."; color: access.failedPins > 0 ? Theme.danger : Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.lineSoft }
                    Text { text: "CHANGE PIN"; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.letterSpacing: 1 }
                    TextField { id: oldPin; Layout.fillWidth: true; placeholderText: "current PIN"; echoMode: TextInput.Password; inputMethodHints: Qt.ImhDigitsOnly; font.family: Theme.mono; font.pixelSize: 9 }
                    TextField { id: newPin; Layout.fillWidth: true; placeholderText: "new PIN (4-8 digits)"; echoMode: TextInput.Password; inputMethodHints: Qt.ImhDigitsOnly; font.family: Theme.mono; font.pixelSize: 9 }
                    Btn { label: "SET PIN"; on: newPin.text.length >= 4; onColor: Theme.cyan; onClicked: if (access.setPin(oldPin.text, newPin.text)) { oldPin.text = ""; newPin.text = "" } }
                    Item { Layout.fillHeight: true } } }
            // audit
            Rectangle { Layout.preferredWidth: 420; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; clip: true
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 6
                    RowLayout { Head { text: "OPERATOR AUDIT TRAIL" } Item { Layout.fillWidth: true } Btn { label: "EXPORT CSV"; onClicked: access.exportAudit() } }
                    ListView { id: lv; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: access.audit; spacing: 2; onCountChanged: positionViewAtEnd()
                        delegate: RowLayout { width: lv.width; spacing: 6
                            Text { text: modelData.at.replace("T", " "); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
                            Text { text: modelData.who; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 7; font.bold: true }
                            Text { Layout.fillWidth: true; text: modelData.text; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap } } }
                    Text { visible: access.audit.length === 0; text: "No events yet. Role switches, locks, PIN changes and admin actions are written to ~/.amixos/audit.log."; color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap; Layout.fillWidth: true } } }
        }
    }
}
