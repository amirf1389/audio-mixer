// AI ASSISTANT: ask in plain words; the answer comes from the console's own live state (the device, the meters, loudness, the SOUND
// SCORE and its parts, Windows' mute, the inputs) and ends with one-click actions. It runs on this computer: no account, no key, nothing
// is sent anywhere. Each answer says what was read, what it means and what to do.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AmixOS

Item {
    id: page
    objectName: "assistantPage"
    signal go(string id)
    function t(s) { return i18n.t(s, i18n.lang) }
    function tel(k, d) { const v = engine.telv(k, engine.meterTick); return v === undefined ? d : v }
    function db(v) { return v <= -119 ? "-inf" : Number(v).toFixed(1) }
    property var chat: []           // [{who: "you" | "ai", text, actions: [{label, run}]}]
    readonly property var quick: ["Why is there no sound?", "How does my mix sound?", "Is it too loud for streaming?", "It distorts", "Make the vocal clearer", "Feedback / howling", "Set up a sub and tops", "Too much delay (latency)", "What can you do?"]

    function say(who, text, actions) { const c = page.chat.slice(); c.push({ who: who, text: text, actions: actions || [] }); page.chat = c; Qt.callLater(function () { list.positionViewAtEnd() }) }
    function act(label, fn) { return { label: label, run: fn } }
    function nav(label, id) { return act(label, function () { page.go(id) }) }

    // THE ANSWERS: one function per topic, each reading the live state
    function noSound() {
        const a = [], why = [];
        if (!engine.running) { why.push(page.t("No sound card is running.")); a.push(act("AUTO START", function () { engine.autoStart(0); page.say("ai", engine.running ? page.t("Started: ") + engine.status : page.t("AUTO START found nothing it could open: pick a device on CONNECTIONS.")) })); a.push(nav("CONNECTIONS", "io")) }
        if (engine.masterDb <= -60) { why.push(page.t("The MASTER fader is at -inf.")); a.push(act("RAISE MASTER TO 0 dB", function () { engine.setMaster(0); page.say("ai", page.t("Master at 0 dB.")) })) }
        if (engine.windowsMuted) why.push(page.t("Windows' own sound output is muted."));
        if (engine.inputMutedBySystem()) { why.push(page.t("The inputs deliver only silence: Windows may block the microphone (Settings → Privacy → Microphone) or the input is not plugged.")); a.push(nav("CONNECTIONS", "io")) }
        if (engine.running && page.tel("masterPeakDb", -120) <= -119 && why.length === 0) { why.push(page.t("Everything is open but nothing reaches the master: check that a strip is up and not muted, and that its input is routed.")); a.push(nav("MIXER", "mixer")); a.push(nav("ROUTING", "routing")) }
        if (why.length === 0) why.push(page.t("The master is playing at") + " " + page.db(page.tel("masterPeakDb", -120)) + " dBFS. " + page.t("If you still hear nothing, check the outputs on ROUTING and the speakers' own power and volume."));
        page.say("ai", why.join("\n"), a)
    }
    function score() {
        const s = engine.soundScore();
        if (!s.valid) { page.say("ai", page.t("There is nothing to judge yet: play something through the master."), [nav("FULL SCORE", "score")]); return }
        const weak = (s.parts || []).filter(function (p) { return p.max > 0 && p.points < p.max }).sort(function (x, y) { return x.points / x.max - y.points / y.max });
        let txt = page.t("SOUND SCORE") + " " + Math.round(s.total) + " / 100, " + page.t("grade") + " " + s.grade + ".";
        for (let i = 0; i < Math.min(3, weak.length); ++i) txt += "\n• " + page.t(weak[i].name) + " " + Math.round(weak[i].points) + "/" + weak[i].max + (weak[i].tip ? ": " + page.t(weak[i].tip) : "");
        if (weak.length === 0) txt += " " + page.t("Every part is at its best.");
        page.say("ai", txt, [nav("FULL SCORE", "score"), act("MASTER ANALYZER", function () { if (typeof root !== "undefined" && root.openAnalyzer) root.openAnalyzer() }), nav("MASTERING", "master")])
    }
    function loud() {
        const i = page.tel("integrated", -120), st = page.tel("shortTerm", -120), tp = page.tel("truePeak", -120);
        if (st <= -119) { page.say("ai", page.t("No programme yet: play something for a few seconds, then ask again.")); return }
        const ref = i > -119 ? i : st, diff = ref - (-14);
        let txt = page.t("Loudness") + " " + page.db(ref) + " LUFS (" + page.t("short-term") + " " + page.db(st) + "), " + page.t("true peak") + " " + page.db(tp) + " dBTP. ";
        txt += Math.abs(diff) < 1.5 ? page.t("That is right for streaming (about -14 LUFS).") : diff > 0 ? page.t("About") + " " + diff.toFixed(1) + " LU " + page.t("louder than streaming services play: they will turn it down, and the limiter is working hard.") : page.t("About") + " " + (-diff).toFixed(1) + " LU " + page.t("quieter than streaming level: raise it on MASTERING.");
        if (tp > -1) txt += " " + page.t("The true peak is above -1 dBTP: encoders (MP3, AAC) may distort it.");
        page.say("ai", txt, [act("LIMITER CEILING -1 dBTP", function () { engine.setMasterLimiter(true, -1, -1); page.say("ai", page.t("The master limiter now stops at -1 dB.")) }), nav("LOUDNESS", "lufs"), nav("MASTERING", "master")])
    }
    function distort() {
        const tp = page.tel("truePeak", -120), pk = page.tel("masterPeakDb", -120);
        let txt = page.t("Master peak") + " " + page.db(pk) + " dBFS, " + page.t("true peak") + " " + page.db(tp) + " dBTP. ";
        txt += pk > -0.5 ? page.t("The master is at full scale: lower the loudest strips or the master, and keep the limiter on.") : page.t("The master is not clipping: the distortion is earlier, usually an input set too hot. Watch the IN meter of each strip and lower its INPUT TRIM until it stays out of the red.");
        page.say("ai", txt, [act("LIMITER ON", function () { engine.setMasterLimiter(true, -1, -1); page.say("ai", page.t("Master limiter on, ceiling -1 dB.")) }), act("CLEAR CLIPS", function () { engine.clearClips() }), nav("MIXER", "mixer")])
    }
    function vocal() { page.say("ai", page.t("For a clearer voice: a high-pass at 80-100 Hz removes rumble; a gentle cut around 250-400 Hz removes boxiness; 2-3 dB around 3-5 kHz brings presence; a compressor at 3:1 keeps it steady; a de-esser tames the S sounds. Open the vocal strip's TONE page, or put the VOCAL preset on it."), [nav("TONE (EQ)", "dsp"), nav("COMPRESSOR", "dyn"), nav("EFFECTS", "fx")]) }
    function feedback() { page.say("ai", page.t("Howling is a frequency going round from the speaker into the microphone. Lower the microphone's gain, keep the speakers in front of the microphones, then find the ringing frequency on RTA EQ and cut it narrowly (or put the ANTI-FEEDBACK plug-in on the microphone strip)."), [nav("RTA EQ", "rtaeq"), nav("PLUGINS", "plugins")]) }
    function amp() { page.say("ai", page.t("On AMP / SYSTEM give the sub output the SUB preset and the main outputs TOPS: both cross at 100 Hz with Linkwitz-Riley and add up flat. Delay the tops or the sub by the distance between them (2.9 ms per metre), and set each limiter below the amplifier's clip point."), [nav("AMP / SYSTEM", "amp"), nav("ROUTING", "routing")]) }
    function latency() {
        const io = engine.ioInfo();
        page.say("ai", engine.running ? page.t("The buffer is") + " " + Number(io.blockMs || 0).toFixed(1) + " ms " + page.t("on") + " " + (io.backend || "") + ". " + page.t("For live monitoring use ASIO (or WASAPI exclusive) and a 128 or 256 frame block; raise it again if you hear clicks.") : page.t("Nothing runs yet: start a sound card first."), [nav("SOUND CARD", "asio")])
    }
    function help() { page.say("ai", page.t("I read the console live and answer about: no sound, how the mix sounds (SOUND SCORE), loudness for streaming, distortion, a clearer vocal, feedback, speakers and subwoofers (AMP / SYSTEM), latency, and analog or digital sound. Every answer ends with buttons that do the fix. I run on this computer: nothing is sent anywhere."), []) }
    function analog() { page.say("ai", page.t("DIGITAL leaves the mix exactly as computed; ANALOG rounds the peaks softly on the master and every channel, the way an analogue desk does."), [act("ANALOG", function () { engine.dspMode = "analog"; page.say("ai", page.t("DSP MODE: ANALOG.")) }), act("DIGITAL", function () { engine.dspMode = "digital"; page.say("ai", page.t("DSP MODE: DIGITAL.")) })]) }

    function ask(q) {
        q = (q || "").trim(); if (!q) return; page.say("you", q); const s = q.toLowerCase();
        const has = function () { for (let i = 0; i < arguments.length; ++i) if (s.indexOf(arguments[i]) >= 0) return true; return false };
        if (has("no sound", "silent", "can't hear", "cant hear", "nothing", "not hear", "no audio", "mute")) noSound();
        else if (has("loud", "lufs", "stream", "youtube", "spotify", "level")) loud();
        else if (has("distort", "clip", "crack", "red", "overload")) distort();
        else if (has("vocal", "voice", "speech", "singer", "clear")) vocal();
        else if (has("feedback", "howl", "whistl", "ring")) feedback();
        else if (has("amp", "speaker", "sub", "crossover", "tops", "system")) amp();
        else if (has("latency", "delay", "lag", "buffer", "late")) latency();
        else if (has("analog", "analogue", "warm", "digital")) analog();
        else if (has("score", "sound", "mix", "better", "improve", "good", "quality")) score();
        else if (has("help", "what can", "how do")) help();
        else { page.say("ai", page.t("I did not catch that. Here is the console now:") + " " + engine.status + (engine.running ? " · " + page.t("master") + " " + page.db(page.tel("masterPeakDb", -120)) + " dBFS" : ""), [act("WHAT CAN YOU DO?", help)]) }
    }
    function greet() { page.say("ai", page.t("Hello. Ask me about the sound in plain words, or pick a question below. I look at the console live and offer the fix."), []) }
    Component.onCompleted: greet()

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 16; spacing: 10
        RowLayout { Layout.fillWidth: true; spacing: 10
            Text { text: page.t("AI ASSISTANT"); color: Theme.ink; font.family: Theme.mono; font.pixelSize: 16; font.bold: true; font.letterSpacing: 3 }
            Text { text: page.t("reads the console live · runs on this computer · nothing is sent"); color: Theme.dim; font.family: Theme.mono; font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideRight }
            Rectangle { implicitWidth: clT.implicitWidth + 16; implicitHeight: 22; radius: 4; color: Theme.ov(0.06); border.color: Theme.lineSoft
                Text { id: clT; anchors.centerIn: parent; text: page.t("CLEAR"); color: Theme.muted; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                MouseArea { anchors.fill: parent; onClicked: { page.chat = []; page.greet() } } } }
        Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; radius: Theme.radius; color: Theme.card; border.color: Theme.line
            ListView { id: list; objectName: "assistantList"; anchors.fill: parent; anchors.margins: 12; spacing: 10; clip: true; model: page.chat
                delegate: Item { width: list.width; height: bubble.height
                    readonly property bool mine: modelData.who === "you"
                    Rectangle { id: bubble; anchors.right: parent.mine ? parent.right : undefined; width: Math.min(list.width * 0.78, Math.max(bt.implicitWidth, acts.implicitWidth) + 24); height: bc.implicitHeight + 18; radius: 8
                        color: parent.mine ? Qt.rgba(0.13, 0.83, 0.93, 0.12) : Theme.ov(0.05); border.color: parent.mine ? Theme.cyan : Theme.lineSoft
                        Column { id: bc; x: 12; y: 9; width: parent.width - 24; spacing: 8
                            Text { text: parent.parent.parent.mine ? page.t("YOU") : "✦ " + page.t("ASSISTANT"); color: parent.parent.parent.mine ? Theme.cyan : Theme.orange; font.family: Theme.mono; font.pixelSize: 8; font.bold: true }
                            Text { id: bt; width: parent.width; text: modelData.text; color: Theme.ink; font.family: Theme.sans; font.pixelSize: 12; wrapMode: Text.WordWrap }
                            Flow { id: acts; width: parent.width; spacing: 6; visible: modelData.actions.length > 0
                                Repeater { model: modelData.actions
                                    delegate: Rectangle { implicitWidth: at.implicitWidth + 16; implicitHeight: 24; radius: 4; color: Theme.ov(0.06); border.color: Theme.orange
                                        Text { id: at; anchors.centerIn: parent; text: page.t(modelData.label); color: Theme.orange; font.family: Theme.mono; font.pixelSize: 9; font.bold: true }
                                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: modelData.run() } } } } } } } } }
        Flow { Layout.fillWidth: true; spacing: 6
            Repeater { model: page.quick
                delegate: Rectangle { implicitWidth: qt.implicitWidth + 16; implicitHeight: 24; radius: 12; color: qm.containsMouse ? Theme.ov(0.1) : Theme.ov(0.05); border.color: Theme.lineSoft
                    Text { id: qt; anchors.centerIn: parent; text: page.t(modelData); color: Theme.ink; font.family: Theme.sans; font.pixelSize: 10 }
                    MouseArea { id: qm; objectName: "assistantQuick_" + index; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: page.ask(modelData) } } } }
        RowLayout { Layout.fillWidth: true; spacing: 8
            TextField { id: input; objectName: "assistantInput"; Layout.fillWidth: true; implicitHeight: 34; font.family: Theme.sans; font.pixelSize: 12; placeholderText: page.t("Ask about the sound…")
                onAccepted: { page.ask(text); text = "" } }
            Rectangle { implicitWidth: 80; implicitHeight: 34; radius: 6; color: Theme.orange
                Text { anchors.centerIn: parent; text: page.t("ASK"); color: "#000"; font.family: Theme.mono; font.pixelSize: 10; font.bold: true }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { page.ask(input.text); input.text = "" } } } }
    }
}
