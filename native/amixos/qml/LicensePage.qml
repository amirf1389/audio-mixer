// LICENSE: what this copy runs as (BASIC 10 ch / PRO 20 / STUDIO 32 / MASTER 32 / ULTIMATE 300, +30 per CHANNEL EXTEND pack), the machine code, paste a key to CHECK it (nothing stored) and
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
    readonly property string generatorUrl: "http://127.0.0.1:8799/?plan=&name=&email=&machine=" + encodeURIComponent(page.lic.machine || "")
    readonly property bool autoFailed: { const t = engine.autoLicenseStatus; return t !== "" && t !== "active" && t !== "off" && t.indexOf("activated") !== 0 && t.indexOf("watching") !== 0 && t.indexOf("request") !== 0 && t.indexOf("cannot") !== 0 }
    function dateOf(ms) { return ms > 0 ? new Date(ms).toISOString().slice(0, 10) : "" }
    ColumnLayout {
        id: col; width: parent.width - 20; x: 10; y: 10; spacing: 10
        Panel {
            Layout.fillWidth: true
            title: i18n.t("LICENSE  ·  ", i18n.lang) + page.lic.planName
            subtitle: page.lic.state === "active" ? (page.lic.source === "edition" ? page.lic.editionName + " · serial " + page.lic.serial + " · bound to this computer" : "Key " + page.lic.id + (page.lic.name ? " for " + page.lic.name : "") + (page.lic.boundToThisPc ? " · bound to this computer" : " · any computer") + (page.lic.expires > 0 ? " · until " + page.dateOf(page.lic.expires) : " · no expiry"))
                    : page.lic.state === "expired" ? "The stored key has expired: this copy runs BASIC. " + page.lic.message
                    : page.lic.state === "invalid" ? "The stored key is not valid here: this copy runs BASIC. " + page.lic.message
                    : "No key: this copy runs BASIC with 10 channels. Strips 11-32 are shown but locked."
            RowLayout { spacing: 8
                Repeater { model: [["BASIC", 10, "basic"], ["PRO", 20, "pro"], ["STUDIO", 32, "studio"], ["MASTER", 32, "master"], ["ULTIMATE", 300, "ultimate"]]
                    delegate: Rectangle { implicitWidth: 120; implicitHeight: 54; radius: 6; color: page.lic.plan === modelData[2] ? Qt.rgba(0.984, 0.749, 0.141, 0.18) : Theme.ov(0.04); border.color: page.lic.plan === modelData[2] ? Theme.amber : Theme.lineSoft
                        ColumnLayout { anchors.centerIn: parent; spacing: 2
                            Text { Layout.alignment: Qt.AlignHCenter; text: modelData[0]; color: page.lic.plan === modelData[2] ? Theme.amber : Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 2 }
                            Text { Layout.alignment: Qt.AlignHCenter; text: modelData[1] + i18n.t(" channels", i18n.lang) + (modelData[2] === "ultimate" ? i18n.t(" · the big console", i18n.lang) : modelData[2] === "master" ? i18n.t(" · all access", i18n.lang) : modelData[2] === "studio" ? i18n.t(" · studio", i18n.lang) : modelData[2] === "pro" ? i18n.t(" · plugins, OTA", i18n.lang) : ""); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7 } } } }
                // CHANNEL EXTEND: signed packs of 30 channels on PRO / STUDIO / MASTER (the count this computer holds and what they add now)
                Rectangle { objectName: "extendTile"; readonly property int packs: page.lic.extendPacks || 0; implicitWidth: 150; implicitHeight: 54; radius: 6; color: packs > 0 ? Qt.rgba(0.235, 0.784, 0.859, 0.16) : Theme.ov(0.04); border.color: packs > 0 ? Theme.cyan : Theme.lineSoft
                    ColumnLayout { anchors.centerIn: parent; spacing: 2
                        Text { Layout.alignment: Qt.AlignHCenter; text: i18n.t("EXTEND", i18n.lang) + (parent.parent.packs > 0 ? "  ×" + parent.parent.packs : ""); color: parent.parent.packs > 0 ? Theme.cyan : Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true; font.letterSpacing: 2 }
                        Text { Layout.alignment: Qt.AlignHCenter; text: parent.parent.packs > 0 ? "+" + (page.lic.extendChannels || 0) + i18n.t(" channels · ", i18n.lang) + engine.licensedChannels + i18n.t(" in all", i18n.lang) : i18n.t("+30 channels per pack", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7 } } } }
            RowLayout { Layout.fillWidth: true; spacing: 10
                Text { text: i18n.t("MACHINE CODE", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                TextInput { text: page.lic.machine; readOnly: true; selectByMouse: true; color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 12; font.bold: true; font.letterSpacing: 1 }
                Text { text: i18n.t("give this to the vendor for a key bound to this computer (it is a hash: not secret, not personal)", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 7; Layout.fillWidth: true; elide: Text.ElideRight } }
        }
        Panel {
            Layout.fillWidth: true
            title: i18n.t("ENTER A KEY OR EDITION TOKEN", i18n.lang)
            subtitle: i18n.t("AMIX1.… licence key or AMXX1.… channel extend key from the vendor's Key Generator. CHECK tells you if it is genuine and what it changes; nothing is stored until ACTIVATE.", i18n.lang)
            Rectangle { Layout.fillWidth: true; implicitHeight: 64; radius: 6; color: Theme.deep; border.color: keyIn.activeFocus ? Theme.cyan : Theme.lineSoft
                Flickable { anchors.fill: parent; anchors.margins: 6; contentHeight: keyIn.implicitHeight; clip: true
                    TextEdit { id: keyIn; width: parent.width; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 9; wrapMode: TextEdit.WrapAnywhere; selectByMouse: true
                        Text { anchors.fill: parent; text: i18n.t("AMIX1.eyJ2IjoxLCJwbGFuIjoicHJvIi…", i18n.lang); color: Theme.dim; font: parent.font; visible: !parent.text.length } } } }
            // PRODUCT KEY: five groups of five characters. It is traded at the licence server for the full key, which is then checked here as always.
            Rectangle { id: pkBox; objectName: "productKeyBox"; Layout.fillWidth: true; implicitHeight: pkCol.implicitHeight + 16; radius: 6; color: Theme.ov(0.03); border.color: Theme.violet
                readonly property bool looksLikeOne: keyIn.text.replace(/[^0-9A-Za-z]/g, "").length === 25 || keyIn.text.trim().indexOf("AMIX1.") === 0 || keyIn.text.trim().indexOf("AMFXE1.") === 0
                Connections { target: updater; function onActivated(a) { const r = engine.storeActivation(a); pkNote.text = r.ok ? i18n.t("Activated on this computer.", i18n.lang) : r.message }
                    function onRedeemed(key) { keyIn.text = key; const r = engine.activateKey(key); page.check = engine.checkKey(key); pkNote.text = r.ok ? i18n.t("Activated.", i18n.lang) : r.message } }
                ColumnLayout { id: pkCol; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8; spacing: 6
                    RowLayout { spacing: 8
                        Text { text: i18n.t("PRODUCT KEY", i18n.lang); color: Theme.violet; font.family: Theme.mono; font.pixelSize: 10; font.bold: true; font.letterSpacing: 1.5 }
                        Text { Layout.fillWidth: true; text: i18n.t("Product key (XXXXX-XXXXX-XXXXX-XXXXX-XXXXX), licence key or edition token: type it above and press ACTIVATE ONLINE. It is bound to this computer; after that it works offline for the whole licence term.", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideRight }
                        Rectangle { id: gb47; objectName: "productKeyBtn"; implicitWidth: pkT.implicitWidth + 22; implicitHeight: 26; radius: 7; color: pkBox.looksLikeOne ? Theme.violet : "transparent"; gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb47.color) } GradientStop { position: 1; color: Theme.btnBottom(gb47.color) } } border.color: Theme.violet; opacity: pkBox.looksLikeOne ? 1 : 0.5
                            Text { id: pkT; anchors.centerIn: parent; text: updater.redeemState === "asking" ? i18n.t("ASKING THE SERVER…", i18n.lang) : i18n.t("ACTIVATE ONLINE", i18n.lang); color: pkBox.looksLikeOne ? "#fff" : Theme.violet; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                            MouseArea { anchors.fill: parent; enabled: pkBox.looksLikeOne; onClicked: { pkNote.text = ""; updater.activateOnline(keyIn.text, engine.license.machine) } } }
                        Rectangle { id: gb48; objectName: "trialBtn"; implicitWidth: trT.implicitWidth + 22; implicitHeight: 26; radius: 7; color: "transparent"; gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb48.color) } GradientStop { position: 1; color: Theme.btnBottom(gb48.color) } } border.color: Theme.cyan
                            Text { id: trT; anchors.centerIn: parent; text: i18n.t("START 30-DAY TRIAL", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                            MouseArea { anchors.fill: parent; onClicked: { pkNote.text = ""; updater.startTrial(engine.license.machine) } } } }
                    RowLayout { spacing: 8
                        Text { text: i18n.t("LICENCE SERVER", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
                        Field { objectName: "licServer"; Layout.fillWidth: true; implicitHeight: 24; text: updater.licenseServer; placeholderText: "https://…"; font.family: Theme.mono; font.pixelSize: 8; selectByMouse: true; onEditingFinished: updater.licenseServer = text }
                        // MY ACCOUNT ONLINE: the customer page of the licence server (management/customer): plan, end date, computers, free one, downloads
                        Rectangle { id: gbAcc; objectName: "accountBtn"; readonly property bool ok: /^https:\/\//.test(updater.licenseServer); implicitWidth: accT.implicitWidth + 22; implicitHeight: 24; radius: 7; color: "transparent"; border.color: Theme.cyan; opacity: ok ? 1 : 0.4
                            Text { id: accT; anchors.centerIn: parent; text: i18n.t("MY ACCOUNT ONLINE", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                            MouseArea { anchors.fill: parent; enabled: gbAcc.ok; cursorShape: Qt.PointingHandCursor; onClicked: Qt.openUrlExternally(updater.licenseServer.replace(/\/+$/, "") + "/account/") } }
                        // MANAGE LICENCE ONLINE: the licence site of the same server (/licence/), opened with this computer's machine code and plan filled in
                        Rectangle { id: gbMan; objectName: "manageLicenceBtn"; readonly property string link: { void updater.licenseServer; return updater.manageLicenceUrl(page.lic.machine || "", page.lic.plan || "") }   /* re-read when the server setting changes */; implicitWidth: manT.implicitWidth + 22; implicitHeight: 24; radius: 7; color: "transparent"; border.color: Theme.amber; opacity: link !== "" ? 1 : 0.4
                            Text { id: manT; anchors.centerIn: parent; text: i18n.t("MANAGE LICENCE ONLINE", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                            MouseArea { anchors.fill: parent; enabled: gbMan.link !== ""; cursorShape: Qt.PointingHandCursor; onClicked: Qt.openUrlExternally(gbMan.link) } } }
                    Text { id: pkNote; visible: text !== "" || (updater.redeemState !== "" && updater.redeemState !== "asking" && updater.redeemState !== "done"); Layout.fillWidth: true; wrapMode: Text.WordWrap; text: ""; color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9
                        Binding on text { when: updater.redeemState !== "" && updater.redeemState !== "asking" && updater.redeemState !== "done"; value: updater.redeemState } } } }
            RowLayout { spacing: 6
                Rectangle { id: gb49; implicitWidth: Math.max(90, children[0].implicitWidth + 12); implicitHeight: 26; radius: 7; color: Qt.rgba(0.235, 0.784, 0.859, 0.18); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb49.color) } GradientStop { position: 1; color: Theme.btnBottom(gb49.color) } } border.color: Theme.cyan; Text { anchors.centerIn: parent; text: i18n.t("CHECK", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { page.check = engine.checkKey(keyIn.text); page.msg = page.check.message; page.msgOk = page.check.valid } } }
                Rectangle { id: gb50; implicitWidth: Math.max(110, children[0].implicitWidth + 12); implicitHeight: 26; radius: 7; color: Theme.amber; gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb50.color) } GradientStop { position: 1; color: Theme.btnBottom(gb50.color) } } opacity: keyIn.text.length ? 1 : 0.4; Text { anchors.centerIn: parent; text: i18n.t("ACTIVATE", i18n.lang); color: "#000"; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; enabled: keyIn.text.length > 0; onClicked: { const r = engine.activateKey(keyIn.text); page.msg = r.message; page.msgOk = r.ok; if (r.ok) { keyIn.text = ""; page.check = null } } } }
                Rectangle { id: gb51; visible: page.lic.source === "key"; implicitWidth: 110; implicitHeight: 26; radius: 7; color: Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb51.color) } GradientStop { position: 1; color: Theme.btnBottom(gb51.color) } } border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: i18n.t("DEACTIVATE", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { engine.deactivateKey(); page.msg = "The key was removed from this computer. It can be activated again, here or on another computer within its seats."; page.msgOk = true } } }
                Rectangle { id: gb52; implicitWidth: Math.max(90, children[0].implicitWidth + 12); implicitHeight: 26; radius: 7; color: Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb52.color) } GradientStop { position: 1; color: Theme.btnBottom(gb52.color) } } border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: i18n.t("RE-CHECK", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { engine.refreshLicense(); page.msg = "Licence re-read."; page.msgOk = true } } }
                Rectangle { id: gbExt; objectName: "removeExtendBtn"; visible: (page.lic.extendPacks || 0) > 0; implicitWidth: Math.max(150, children[0].implicitWidth + 12); implicitHeight: 26; radius: 7; color: Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gbExt.color) } GradientStop { position: 1; color: Theme.btnBottom(gbExt.color) } } border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: i18n.t("REMOVE EXTEND PACKS", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { engine.removeExtendPacks(); page.check = null; page.msg = "Extend packs removed from this computer."; page.msgOk = true } } } }
            Text { visible: page.msg.length > 0; text: page.msg; color: page.msgOk ? Theme.emerald : Theme.danger; font.family: Theme.mono; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { visible: page.check && page.check.valid; text: page.check ? (i18n.t("Plan ", i18n.lang) + page.check.planName + " · " + page.check.channels + i18n.t(" channels · ", i18n.lang) + (page.check.boundToThisPc ? i18n.t("bound to this computer", i18n.lang) : i18n.t("any computer", i18n.lang)) + (page.check.expires > 0 ? i18n.t(" · until ", i18n.lang) + page.dateOf(page.check.expires) : i18n.t(" · no expiry", i18n.lang)) + (page.check.name ? " · " + page.check.name : "")) : ""; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8 }
        }
        Panel {
            Layout.fillWidth: true
            title: i18n.t("AUTO LICENCE  ·  ", i18n.lang) + (engine.autoLicense ? i18n.t("ON", i18n.lang) : i18n.t("OFF", i18n.lang))
            subtitle: "The console asks and listens; it never makes keys. AUTO REQUEST writes " + engine.autoLicenseRequestPath + " for the vendor. When the vendor's key file (*.amixkey, *.key or key*.txt) lands in the data folder or Downloads, it is verified against the vendor's public key and activated at once."
            RowLayout { spacing: 6
                Rectangle { id: gb53; implicitWidth: 110; implicitHeight: 26; radius: 7; color: engine.autoLicense ? Theme.emerald : Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb53.color) } GradientStop { position: 1; color: Theme.btnBottom(gb53.color) } } border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: engine.autoLicense ? i18n.t("AUTO ON", i18n.lang) : i18n.t("AUTO OFF", i18n.lang); color: engine.autoLicense ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.autoLicense = !engine.autoLicense } }
                Rectangle { id: gb54; implicitWidth: Math.max(130, children[0].implicitWidth + 12); implicitHeight: 26; radius: 7; color: Qt.rgba(0.235, 0.784, 0.859, 0.18); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb54.color) } GradientStop { position: 1; color: Theme.btnBottom(gb54.color) } } border.color: Theme.cyan; Text { anchors.centerIn: parent; text: i18n.t("AUTO REQUEST", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { const pth = engine.writeLicenseRequest(reqPlan.plan, reqName.text, reqMail.text); if (pth) { reqOut.text = engine.licenseRequest(reqPlan.plan, reqName.text, reqMail.text); engine.autoLicense = true } } } }
                Rectangle { id: gb55; implicitWidth: Math.max(100, children[0].implicitWidth + 12); implicitHeight: 26; radius: 7; color: Theme.ov(0.08); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb55.color) } GradientStop { position: 1; color: Theme.btnBottom(gb55.color) } } border.color: Theme.lineSoft; Text { anchors.centerIn: parent; text: i18n.t("LOOK NOW", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: engine.scanForKeys() } }
                Text { Layout.fillWidth: true; text: engine.autoLicenseStatus; color: engine.autoLicenseStatus.indexOf("activated") === 0 || engine.autoLicenseStatus === "active" ? Theme.emerald : page.autoFailed ? Theme.danger : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; wrapMode: Text.WordWrap } }
            // OPEN KEY GENERATOR: the vendor's local generator (scripts/license-generator.js on 127.0.0.1:8799), prefilled with this machine code. Its
            // SEND TO AMF X1 ON THIS PC writes the key file into the data folder; the scan below picks it up within ~2 s while this page is open.
            RowLayout { spacing: 8
                Rectangle { id: gbGen; objectName: "openGeneratorBtn"; implicitWidth: Math.max(150, genT.implicitWidth + 22); implicitHeight: 26; radius: 7; color: "transparent"; border.color: Theme.amber
                    Text { id: genT; anchors.centerIn: parent; text: i18n.t("OPEN KEY GENERATOR", i18n.lang); color: Theme.amber; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Qt.openUrlExternally(page.generatorUrl) } }
                Text { text: i18n.t("VENDOR KEY", i18n.lang) + "  " + engine.vendorKeyFingerprint(); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                Text { Layout.fillWidth: true; text: i18n.t("Must match the fingerprint on the generator page.", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; elide: Text.ElideRight } }
            Timer { objectName: "keyRescan"; interval: 2000; repeat: true; running: page.visible && engine.autoLicense && !(page.lic.state === "active" && page.lic.source === "key"); onTriggered: engine.scanForKeys() }
            Connections { target: Qt.application; function onStateChanged() { if (Qt.application.state === Qt.ApplicationActive && page.visible && engine.autoLicense) engine.scanForKeys() } }
        }
        Panel {
            Layout.fillWidth: true
            title: i18n.t("REQUEST A KEY", i18n.lang)
            subtitle: i18n.t("Copy this request and send it to the vendor (e-mail or WhatsApp). It holds the machine code, the plan wanted and your name: nothing secret.", i18n.lang)
            RowLayout { spacing: 6
                Text { text: i18n.t("PLAN", i18n.lang); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 8 }
                Repeater { model: ["pro", "studio", "master", "ultimate"]; delegate: Rectangle { id: gb56; implicitWidth: 70; implicitHeight: 20; radius: 7; color: reqPlan.plan === modelData ? Theme.amber : Theme.ov(0.06); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb56.color) } GradientStop { position: 1; color: Theme.btnBottom(gb56.color) } } Text { anchors.centerIn: parent; text: modelData.toUpperCase(); color: reqPlan.plan === modelData ? "#000" : Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true } MouseArea { anchors.fill: parent; onClicked: reqPlan.plan = modelData } } }
                Item { id: reqPlan; property string plan: "pro" }
                Field { id: reqName; placeholderText: i18n.t("your name", i18n.lang); Layout.preferredWidth: 160; font.family: Theme.mono; font.pixelSize: 9 }
                Field { id: reqMail; placeholderText: i18n.t("e-mail", i18n.lang); Layout.preferredWidth: 180; font.family: Theme.mono; font.pixelSize: 9 }
                Rectangle { id: gb57; implicitWidth: Math.max(120, children[0].implicitWidth + 12); implicitHeight: 26; radius: 7; color: Qt.rgba(0.235, 0.784, 0.859, 0.18); gradient: Gradient { GradientStop { position: 0; color: Theme.btnTop(gb57.color) } GradientStop { position: 1; color: Theme.btnBottom(gb57.color) } } border.color: Theme.cyan; Text { anchors.centerIn: parent; text: i18n.t("MAKE REQUEST", i18n.lang); color: Theme.cyan; font.family: Theme.mono; font.pixelSize: 9; font.bold: true } MouseArea { anchors.fill: parent; onClicked: { reqOut.text = engine.licenseRequest(reqPlan.plan, reqName.text, reqMail.text); reqOut.selectAll() } } } }
            TextEdit { id: reqOut; Layout.fillWidth: true; readOnly: true; selectByMouse: true; color: Theme.ink; font.family: Theme.mono; font.pixelSize: 8; wrapMode: TextEdit.WrapAnywhere; visible: text.length > 0 }
        }
        Panel {
            Layout.fillWidth: true
            title: i18n.t("HOW IT IS CHECKED", i18n.lang)
            subtitle: "Offline, in this program, with the vendor's public key " + engine.vendorKeyFingerprint() + " (ECDSA P-256 / SHA-256). The Key Generator that signs keys stays on the vendor's computer and is protected by a one-time code (TOTP) there."
            MoreText { text: i18n.t("A key can be bound to one machine code, or valid on any computer within its seats. An edition token (Pro / Studio Master installer) activates once and is recorded in ~/.amixos/edition.json for this computer only. Nothing here phones home.", i18n.lang); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
    }
}
