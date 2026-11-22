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
    component Btn: Rectangle { property string label; property bool on: false; property color onColor: Theme.amber; signal clicked(); implicitWidth: Math.max(56, bl.implicitWidth + 16); implicitHeight: 24; radius: 8; gradient: Gradient { GradientStop { position: 0; color: on ? Theme.btnTop(onColor) : Theme.ov(0.11) } GradientStop { position: 1; color: on ? Theme.btnBottom(onColor) : Theme.ov(0.08) } } color: on ? onColor : Theme.ov(0.08); border.color: Theme.lineSoft
        Rectangle { anchors.fill: parent; anchors.margins: 1; radius: Math.max(0, parent.radius - 1); color: "transparent"; border.width: 1; border.color: Qt.rgba(1, 1, 1, parent.on ? 0.30 : 0.07) }
        Text { id: bl; anchors.centerIn: parent; text: i18n.t(label, i18n.lang); color: parent.on ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: parent.clicked() } }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        Rectangle { Layout.fillWidth: true; implicitHeight: 34; radius: 8; color: Theme.card; border.color: Theme.line
            RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 8
                Text { text: i18n.t("ACCOUNT:", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true; font.letterSpacing: 2 }
                Text { text: i18n.t("PROFILE: ", i18n.lang) + page.roleName(access.role); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                Text { text: access.surfaceLocked ? i18n.t("SURFACE LOCKED", i18n.lang) : i18n.t("SURFACE OPEN", i18n.lang); color: access.surfaceLocked ? Theme.danger : Theme.emerald; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                Item { Layout.fillWidth: true }
                Text { text: access.message; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideLeft; Layout.maximumWidth: 420 }
                Btn { label: access.surfaceLocked ? i18n.t("LOCKED", i18n.lang) : i18n.t("LOCK CONSOLE", i18n.lang); on: !access.surfaceLocked; onColor: Theme.danger; onClicked: if (!access.surfaceLocked) access.lock() } } }
                Btn { objectName: "pinAtStartBtn"; label: access.pinAtStart ? i18n.t("PIN AT START: ON", i18n.lang) : i18n.t("PIN AT START: OFF", i18n.lang); on: access.pinAtStart; onColor: Theme.amber; onClicked: access.pinAtStart = !access.pinAtStart }
        RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8
            // profiles
            Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 6
                    Head { text: i18n.t("SELECT ACTIVE OPERATOR PROFILE", i18n.lang) }
                    Repeater { model: access.roles; delegate: Rectangle { Layout.fillWidth: true; implicitHeight: 58; radius: 6; color: access.role === modelData.id ? Qt.rgba(0.98, 0.75, 0.14, 0.14) : Theme.ov(0.04); border.color: access.role === modelData.id ? Theme.amber : (page.wantRole === modelData.id ? Theme.cyan : Theme.lineSoft)
                        ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 2
                            RowLayout { Text { text: i18n.t(modelData.name ?? "", i18n.lang); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 10; font.bold: true } Item { Layout.fillWidth: true } }
                            Text { Layout.fillWidth: true; text: modelData.blurb; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap }
                            Text { text: page.permLine(page.mx[modelData.id]); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 } }
                        MouseArea { anchors.fill: parent; onClicked: { if (!access.setRole(modelData.id, "")) page.wantRole = modelData.id; else page.wantRole = "" } } } }
                    Text { visible: page.wantRole !== ""; text: i18n.t("Enter the PIN on the pad to switch to ", i18n.lang) + page.roleName(page.wantRole); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 8 }
                    Item { Layout.fillHeight: true }
                    MoreText { Layout.fillWidth: true; text: i18n.t("Switching down (fewer rights) is free; switching up asks for the PIN. The pages a profile may not use disappear from the rail; ACCESS shows and edits the matrix.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap } } }
            // pin
            Rectangle { Layout.preferredWidth: 230; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 8
                    Head { text: i18n.t("CONSOLE SURFACE PIN", i18n.lang) }
                    Text { visible: access.pinIsDefault; Layout.fillWidth: true; text: i18n.t("DEFAULT PIN 1234 IS STILL ACTIVE: set your own below before a show.", i18n.lang); color: Theme.danger; font.family: Theme.mono; font.pixelSize: 7; font.bold: true; wrapMode: Text.WordWrap }
                    PinPad { Layout.alignment: Qt.AlignHCenter; hint: page.wantRole !== "" ? i18n.t("PIN FOR ", i18n.lang) + page.roleName(page.wantRole) : (access.surfaceLocked ? i18n.t("PIN TO UNLOCK", i18n.lang) : i18n.t("PIN TO LOCK / CHECK", i18n.lang))
                        onEntered: function (pin) { if (page.wantRole !== "") { if (access.setRole(page.wantRole, pin)) page.wantRole = "" } else if (access.surfaceLocked) access.unlock(pin); else if (access.unlock(pin)) access.lock() } }
                    Text { text: access.failedPins > 0 ? access.failedPins + i18n.t(" wrong PIN(s) in a row", i18n.lang) : i18n.t("Tap digits, then ENTER. Locked: faders, mutes and solos do not react.", i18n.lang); color: access.failedPins > 0 ? Theme.danger : Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.lineSoft }
                    Text { text: i18n.t("CHANGE PIN", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; font.letterSpacing: 1 }
                    Field { id: oldPin; Layout.fillWidth: true; placeholderText: i18n.t("current PIN", i18n.lang); echoMode: TextInput.Password; inputMethodHints: Qt.ImhDigitsOnly; font.family: Theme.mono; font.pixelSize: 9 }
                    Field { id: newPin; Layout.fillWidth: true; placeholderText: i18n.t("new PIN (4-8 digits)", i18n.lang); echoMode: TextInput.Password; inputMethodHints: Qt.ImhDigitsOnly; font.family: Theme.mono; font.pixelSize: 9 }
                    Btn { label: i18n.t("SET PIN", i18n.lang); on: newPin.text.length >= 4; onColor: Theme.cyan; onClicked: if (access.setPin(oldPin.text, newPin.text)) { oldPin.text = ""; newPin.text = "" } }
                    Item { Layout.fillHeight: true } } }
            // audit
            Rectangle { Layout.preferredWidth: 420; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line; clip: true
                ColumnLayout { anchors.fill: parent; anchors.margins: 12; spacing: 6
                    RowLayout { Head { text: i18n.t("OPERATOR AUDIT TRAIL", i18n.lang) } Item { Layout.fillWidth: true } Btn { label: i18n.t("EXPORT CSV", i18n.lang); onClicked: access.exportAudit() } }
                    ListView { id: lv; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: access.audit; spacing: 2; onCountChanged: positionViewAtEnd()
                        delegate: RowLayout { width: lv.width; spacing: 6
                            Text { text: modelData.at.replace("T", " "); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7 }
                            Text { text: modelData.who; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 7; font.bold: true }
                            Text { Layout.fillWidth: true; text: i18n.t(modelData.text ?? "", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap } } }
                    Text { visible: access.audit.length === 0; text: i18n.t("No events yet. Role switches, locks, PIN changes and admin actions are written to ~/.amixos/audit.log.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 7; wrapMode: Text.WordWrap; Layout.fillWidth: true } } }
        }
    }
}
