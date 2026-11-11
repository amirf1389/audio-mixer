// LICENSE: what this copy runs as (BASIC 8 ch / PRO 16 / STUDIO 32 / MASTER), the machine code, paste a key to CHECK it (nothing stored) and
// ACTIVATE it, deactivate, and a ready-made licence request to send the vendor. Keys and edition tokens are verified offline with the vendor's
// PUBLIC key compiled into the program (fingerprint shown); the private key never leaves the vendor. A paid edition (edition-token.json next to
// the program) activates by itself on first start and is bound to this computer.
import QtQuick
import AmixOS   // this module: brings the Theme singleton (a plain directory import would see Theme as an ordinary type)
import QtQuick.Controls
import QtQuick.Layouts

Flickable {
    id: page
    objectName: "licensePage"
    contentHeight: col.implicitHeight + 24; clip: true
    readonly property var lic: engine.license
    property var check: null
    property string msg: ""
    property bool msgOk: false
    function dateOf(ms) { return ms > 0 ? new Date(ms).toISOString().slice(0, 10) : "" }
    ColumnLayout {
        id: col; width: parent.width - 20; x: 10; y: 10; spacing: 10
        Panel {
            Layout.fillWidth: true
            title: i18n.t("LICENSE  ·  ", i18n.lang) + page.lic.planName
            subtitle: page.lic.state === "active" ? (page.lic.source === "edition" ? page.lic.editionName + " · serial " + page.lic.serial + " · bound to this computer" : "Key " + page.lic.id + (page.lic.name ? " for " + page.lic.name : "") + (page.lic.boundToThisPc ? " · bound to this computer" : " · any computer") + (page.lic.expires > 0 ? " · until " + page.dateOf(page.lic.expires) : " · no expiry"))
                    : page.lic.state === "expired" ? "The stored key has expired: this copy runs BASIC. " + page.lic.message
                    : page.lic.state === "invalid" ? "The stored key is not valid here: this copy runs BASIC. " + page.lic.message
                    : "No key: this copy runs BASIC with 8 channels. Strips 9-32 are shown but locked."
            RowLayout { spacing: 8
                Repeater { model: [["BASIC", 8, "basic"], ["PRO", 16, "pro"], ["STUDIO", 32, "studio"], ["MASTER", 32, "master"]]
                    delegate: Rectangle { implicitWidth: 120; implicitHeight: 54; radius: 6; color: page.lic.plan === modelData[2] ? Qt.rgba(0.984, 0.749, 0.141, 0.18) : Theme.ov(0.04); border.color: page.lic.plan === modelData[2] ? Theme.amber : Theme.lineSoft
                        ColumnLayout { anchors.centerIn: parent; spacing: 2
                            Text { Layout.alignment: Qt.AlignHCenter; text: modelData[0]; color: page.lic.plan === modelData[2] ? Theme.amber : Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 2 }
                            Text { Layout.alignment: Qt.AlignHCenter; text: modelData[1] + i18n.t(" channels", i18n.lang) + (modelData[2] === "master" ? i18n.t(" · all access", i18n.lang) : modelData[2] === "studio" ? i18n.t(" · studio", i18n.lang) : modelData[2] === "pro" ? i18n.t(" · plugins, OTA", i18n.lang) : ""); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7 } } } } }
            RowLayout { Layout.fillWidth: true; spacing: 10
                Text { text: i18n.t("MACHINE CODE", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                TextInput { text: page.lic.machine; readOnly: true; selectByMouse: true; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 12; font.bold: true; font.letterSpacing: 1 }
                Text { text: i18n.t("give this to the vendor for a key bound to this computer (it is a hash: not secret, not personal)", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; Layout.fillWidth: true; elide: Text.ElideRight } }
        }
        Panel {
            Layout.fillWidth: true
            title: i18n.t("ENTER A KEY OR EDITION TOKEN", i18n.lang)
            subtitle: "AMIX1.… licence key from the vendor's Key Generator. CHECK tells you if it is genuine and what it changes; nothing is stored until ACTIVATE."
            Rectangle { Layout.fillWidth: true; implicitHeight: 64; radius: 6; color: Theme.deep; border.color: keyIn.activeFocus ? Theme.cyan : Theme.lineSoft
                Flickable { anchors.fill: parent; anchors.margins: 6; contentHeight: keyIn.implicitHeight; clip: true
                    TextEdit { id: keyIn; width: parent.width; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; wrapMode: TextEdit.WrapAnywhere; selectByMouse: true
                        Text { anchors.fill: parent; text: i18n.t("AMIX1.eyJ2IjoxLCJwbGFuIjoicHJvIi…", i18n.lang); color: Theme.dim; font: parent.font; visible: !parent.text.length } } } }
            RowLayout { spacing: 6
                Rectangle { implicitWidth: 90; implicitHeight: 26; radius: 5; color: Qt.rgba(0, 0.95, 1, 0.18); border.color: Theme.cyan; Text { anchors.centerIn: parent; text: i18n.t("CHECK", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { page.check = engine.checkKey(keyIn.text); page.msg = page.check.message; page.msgOk = page.check.valid } } }
                Rectangle { implicitWidth: 110; implicitHeight: 26; radius: 5; color: Theme.amber; opacity: keyIn.text.length ? 1 : 0.4; Text { anchors.centerIn: parent; text: i18n.t("ACTIVATE", i18n.lang); color: "#000"; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; enabled: keyIn.text.length > 0; onClicked: { const r = engine.activateKey(keyIn.text); page.msg = r.message; page.msgOk = r.ok; if (r.ok) { keyIn.text = ""; page.check = null } } } }
                Rectangle { visible: page.lic.source === "key"; implicitWidth: 110; implicitHeight: 26; radius: 5; color: Theme.ov(0.08); border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: i18n.t("DEACTIVATE", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { engine.deactivateKey(); page.msg = "The key was removed from this computer. It can be activated again, here or on another computer within its seats."; page.msgOk = true } } }
                Rectangle { implicitWidth: 90; implicitHeight: 26; radius: 5; color: Theme.ov(0.08); border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: i18n.t("RE-CHECK", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { engine.refreshLicense(); page.msg = "Licence re-read."; page.msgOk = true } } } }
            Text { visible: page.msg.length > 0; text: page.msg; color: page.msgOk ? Theme.emerald : Theme.danger; font.family: Theme.mono; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { visible: page.check && page.check.valid; text: page.check ? (i18n.t("Plan ", i18n.lang) + page.check.planName + " · " + page.check.channels + i18n.t(" channels · ", i18n.lang) + (page.check.boundToThisPc ? i18n.t("bound to this computer", i18n.lang) : i18n.t("any computer", i18n.lang)) + (page.check.expires > 0 ? i18n.t(" · until ", i18n.lang) + page.dateOf(page.check.expires) : i18n.t(" · no expiry", i18n.lang)) + (page.check.name ? " · " + page.check.name : "")) : ""; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
        }
        Panel {
            Layout.fillWidth: true
            title: i18n.t("AUTO LICENCE  ·  ", i18n.lang) + (engine.autoLicense ? i18n.t("ON", i18n.lang) : i18n.t("OFF", i18n.lang))
            subtitle: "The console asks and listens; it never makes keys. AUTO REQUEST writes " + engine.autoLicenseRequestPath + " for the vendor. When the vendor's key file (*.amixkey, *.key or key*.txt) lands in the data folder or Downloads, it is verified against the vendor's public key and activated at once."
            RowLayout { spacing: 6
                Rectangle { implicitWidth: 110; implicitHeight: 26; radius: 5; color: engine.autoLicense ? Theme.emerald : Theme.ov(0.08); border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: engine.autoLicense ? i18n.t("AUTO ON", i18n.lang) : i18n.t("AUTO OFF", i18n.lang); color: engine.autoLicense ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.autoLicense = !engine.autoLicense } }
                Rectangle { implicitWidth: 130; implicitHeight: 26; radius: 5; color: Qt.rgba(0, 0.95, 1, 0.18); border.color: Theme.cyan; Text { anchors.centerIn: parent; text: i18n.t("AUTO REQUEST", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { const pth = engine.writeLicenseRequest(reqPlan.plan, reqName.text, reqMail.text); if (pth) { reqOut.text = engine.licenseRequest(reqPlan.plan, reqName.text, reqMail.text); engine.autoLicense = true } } } }
                Rectangle { implicitWidth: 100; implicitHeight: 26; radius: 5; color: Theme.ov(0.08); border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: i18n.t("LOOK NOW", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.scanForKeys() } }
                Text { Layout.fillWidth: true; text: engine.autoLicenseStatus; color: engine.autoLicenseStatus.indexOf("activated") === 0 || engine.autoLicenseStatus === "active" ? Theme.emerald : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideRight } }
        }
        Panel {
            Layout.fillWidth: true
            title: i18n.t("REQUEST A KEY", i18n.lang)
            subtitle: "Copy this request and send it to the vendor (e-mail or WhatsApp). It holds the machine code, the plan wanted and your name: nothing secret."
            RowLayout { spacing: 6
                Text { text: i18n.t("PLAN", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                Repeater { model: ["pro", "studio", "master"]; delegate: Rectangle { implicitWidth: 70; implicitHeight: 20; radius: 4; color: reqPlan.plan === modelData ? Theme.amber : Theme.ov(0.06); Text { anchors.centerIn: parent; text: modelData.toUpperCase(); color: reqPlan.plan === modelData ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: reqPlan.plan = modelData } } }
                Item { id: reqPlan; property string plan: "pro" }
                Field { id: reqName; placeholderText: i18n.t("your name", i18n.lang); Layout.preferredWidth: 160; font.family: Theme.mono; font.pixelSize: 9 }
                Field { id: reqMail; placeholderText: i18n.t("e-mail", i18n.lang); Layout.preferredWidth: 180; font.family: Theme.mono; font.pixelSize: 9 }
                Rectangle { implicitWidth: 120; implicitHeight: 26; radius: 5; color: Qt.rgba(0, 0.95, 1, 0.18); border.color: Theme.cyan; Text { anchors.centerIn: parent; text: i18n.t("MAKE REQUEST", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { reqOut.text = engine.licenseRequest(reqPlan.plan, reqName.text, reqMail.text); reqOut.selectAll() } } } }
            TextEdit { id: reqOut; Layout.fillWidth: true; readOnly: true; selectByMouse: true; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; wrapMode: TextEdit.WrapAnywhere; visible: text.length > 0 }
        }
        Panel {
            Layout.fillWidth: true
            title: i18n.t("HOW IT IS CHECKED", i18n.lang)
            subtitle: "Offline, in this program, with the vendor's public key " + engine.vendorKeyFingerprint() + " (ECDSA P-256 / SHA-256). The Key Generator that signs keys stays on the vendor's computer and is protected by a one-time code (TOTP) there."
            Text { text: i18n.t("A key can be bound to one machine code, or valid on any computer within its seats. An edition token (Pro / Studio Master installer) activates once and is recorded in ~/.amixos/edition.json for this computer only. Nothing here phones home.", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
    }
}
