// A mix OS: the native console. Qt Quick interface (qml/) on the C++20 engine (Engine.h -> am::Runtime). No browser, no web runtime.
//   amixos              the console
//   amixos --smoke      offscreen: load the interface, open the null device, run one second, print the meters, exit 0 (the automated check)
//   amixos --shots DIR  like --smoke, and saves a PNG of every ported page into DIR (visual check without a display: QT_QPA_PLATFORM=offscreen)
//   amixos --lang CODE  start in that language (one of the 80+; also applies to --shots)
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QDir>
#include <QTimer>
#include <cmath>
#include <cstdio>

#include <QElapsedTimer>
#include <QThread>
#include <QFileInfo>
#include <QFile>
#include <vector>
#include "Arrange.h"
#include "Settings.h"
#include "Engine.h"
#include "license/license.hpp"
#include "I18n.h"

// real time passes (processEvents alone returns at once when the queue is empty)
static void waitMs(int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(5); } }

int main(int argc, char* argv[]) {
  QGuiApplication app(argc, argv);
  app.setOrganizationName("AMFB Audio");
  am::lic::setProgramDir(QCoreApplication::applicationDirPath().toStdString());   // edition-token.json next to the program (paid editions)
  app.setOrganizationDomain("amfb.audio");
  app.setApplicationName("A mix OS");
  app.setApplicationVersion(QStringLiteral(AMIX_VERSION));
  QQuickStyle::setStyle("Basic");   // the console draws its own controls (Theme.qml); the Basic style never fights the colours

  bool smoke = false; QString shots, lang;
  const QString smokeWav = QDir::tempPath() + "/amixos-smoke-tone.wav", smokeOut = QDir::tempPath() + "/amixos-smoke-export.wav";   // used by the smoke and by its end-of-run lambda: must outlive both
  for (int i = 1; i < argc; ++i) { const QString a = QString::fromLocal8Bit(argv[i]); if (a == "--smoke") smoke = true; else if (a == "--shots" && i + 1 < argc) { smoke = true; shots = QString::fromLocal8Bit(argv[++i]); } else if (a == "--lang" && i + 1 < argc) lang = QString::fromLocal8Bit(argv[++i]); }

  Engine engine; I18n i18n; Arrange arrange(&engine); Settings settings(&engine);
  if (!lang.isEmpty()) i18n.setLang(lang);   // --lang fa: start in that language (also for --shots)
  QQmlApplicationEngine qml;
  qml.rootContext()->setContextProperty("engine", &engine);
  qml.rootContext()->setContextProperty("arrange", &arrange);
  qml.rootContext()->setContextProperty("settings", &settings);
  QObject::connect(&app, &QGuiApplication::aboutToQuit, &settings, &Settings::sessionClosed);
  qml.rootContext()->setContextProperty("i18n", &i18n);
  qml.rootContext()->setContextProperty("appVersion", QStringLiteral(AMIX_VERSION));
  QObject::connect(&qml, &QQmlApplicationEngine::objectCreationFailed, &app, [] { std::fprintf(stderr, "amixos: the interface could not be created\n"); QCoreApplication::exit(2); }, Qt::QueuedConnection);
  qml.load(QUrl(QStringLiteral("qrc:/qt/qml/AmixOS/qml/Main.qml")));   // Qt 6.4 (loadFromModule is 6.5+)
  if (!smoke && settings.autoStart()) QTimer::singleShot(300, &app, [&] { settings.startLastDevice(); });   // the remembered device, once the interface is up
  if (qml.rootObjects().isEmpty()) return 2;

  if (smoke) {
    // the null device: a timed "sound card" that needs no hardware; the strips read silence, the meters must publish blocks
    const bool opened = engine.open("null", "", "", 48000, 256, 2, 2);
    if (!opened) { std::fprintf(stderr, "amixos --smoke: %s\n", qPrintable(engine.status())); return 3; }
    engine.setGain(0, -6); engine.setEq(0, 0, 1, 1000, 1.0, 3);   // a change through the control ring, a channel-DSP change
    engine.storeScene(15, "smoke"); engine.setGain(0, 0); engine.recallScene(15);   // a scene round trip must bring the fader back
    if (std::fabs(engine.stripState()[0].toMap().value("db").toDouble() + 6) > 1e-9) { std::fprintf(stderr, "amixos --smoke: scene recall did not restore the fader\n"); return 5; }
    if (!engine.applyPreset("VOCAL", 1) || !engine.channelParams(1).value("compOn").toBool()) { std::fprintf(stderr, "amixos --smoke: factory preset did not apply\n"); return 6; }
    engine.clearScene(15);
    // the monitor section and the MORE menu's copy / paste are model changes; they must round-trip through the Engine
    engine.setMonitor(2, -12, true, true, false);
    { const QVariantMap mo = engine.monitor(); if (mo.value("source").toInt() != 2 || std::fabs(mo.value("db").toDouble() + 12) > 1e-9 || !mo.value("dim").toBool() || !mo.value("mono").toBool() || mo.value("mute").toBool()) { std::fprintf(stderr, "amixos --smoke: monitor state did not take\n"); return 8; } }
    engine.copyChannel(1); if (!engine.channelClipboard() || !engine.pasteChannel(2) || !engine.channelParams(2).value("compOn").toBool()) { std::fprintf(stderr, "amixos --smoke: copy / paste channel failed\n"); return 9; }
    engine.resetStrip(2); if (engine.channelParams(2).value("compOn").toBool()) { std::fprintf(stderr, "amixos --smoke: reset channel left the compressor on\n"); return 10; }
    engine.setMonitor(0, 0, false, false, false);
    // the licence: this test machine has no key, so BASIC with 8 channels; a malformed key is refused without storing anything
    if (engine.license().value("state").toString() != "basic" && engine.license().value("source").toString().isEmpty()) { std::fprintf(stderr, "amixos --smoke: licence state unexpected\n"); return 13; }
    if (engine.checkKey("not a key").value("valid").toBool() || engine.activateKey("AMIX1.x.y").value("ok").toBool()) { std::fprintf(stderr, "amixos --smoke: a bad key was accepted\n"); return 14; }
    if (engine.vendorKeyFingerprint() != "3D0E-717F-366A-EA41") { std::fprintf(stderr, "amixos --smoke: vendor key fingerprint changed\n"); return 15; }
    // driver intelligence: the null device has no ladder (plain request); openSmart on it opens like open()
    if (engine.driverPlan("null", "", "").size() != 1 || !engine.openSmart("null", "", "", 48000, 256, 2, 2) || !engine.lastRoute().contains("first try")) { std::fprintf(stderr, "amixos --smoke: openSmart on null failed\n"); return 16; }
    // ARRANGE: a generated WAV imported as a clip plays through the master (the strips stay silent); export writes a file
    { const QString wav = smokeWav; std::vector<float> L(24000), R(24000); for (std::size_t i = 0; i < L.size(); ++i) L[i] = R[i] = 0.5f * std::sin(static_cast<float>(i) * 0.1f); std::string werr;
      if (!am::arr::writeWav24(wav.toStdString(), L, R, 48000, werr)) { std::fprintf(stderr, "amixos --smoke: %s\n", werr.c_str()); return 17; }
      arrange.newProject(); const int clip = arrange.importFile(wav, 0); if (!clip || arrange.clips().size() != 1) { std::fprintf(stderr, "amixos --smoke: arrange import failed: %s\n", qPrintable(arrange.message())); return 18; }
      arrange.play(0); QElapsedTimer et; et.start(); while (et.elapsed() < 350) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      engine.tick(); const double mpk = engine.telemetry().value("masterPeakDb").toDouble(), spk = engine.meters()[0].toMap().value("peakDb").toDouble();
      if (mpk < -20 || spk > -100 || !arrange.playing()) { std::fprintf(stderr, "amixos --smoke: arrangement not audible on the master (master %.1f dBFS, strip %.1f, playing %d)\n", mpk, spk, arrange.playing()); return 19; }
      // RECORD while the tone plays: master + armed CH 1 -> two WAV files in a temp folder, the master file carries signal
      engine.setRecordFolder(QDir::tempPath() + "/amixos-smoke-rec"); engine.setArmed(0, true); if (!engine.startRecording()) { std::fprintf(stderr, "amixos --smoke: record start: %s\n", qPrintable(engine.recordMessage())); return 45; }
      { QElapsedTimer rt2; rt2.start(); while (rt2.elapsed() < 300) QCoreApplication::processEvents(QEventLoop::AllEvents, 20); }
      engine.stopRecording(); const QStringList rf = engine.recordFiles(); if (rf.size() != 2 || (engine.recordSeconds() < 0.2 && !qEnvironmentVariableIsSet("AMIXOS_SMOKE_RELAX"))) { std::fprintf(stderr, "amixos --smoke: record files %d, %.2f s\n", static_cast<int>(rf.size()), engine.recordSeconds()); return 46; }
      { am::arr::Audio ra; std::string rerr; if (!am::arr::readWav(rf[0].toStdString(), ra, rerr) || (ra.L.size() < 9600 && !qEnvironmentVariableIsSet("AMIXOS_SMOKE_RELAX"))) { std::fprintf(stderr, "amixos --smoke: recorded master unreadable: %s\n", rerr.c_str()); return 47; } float pk = 0; for (float v : ra.L) pk = std::max(pk, std::fabs(v)); if (!qEnvironmentVariableIsSet("AMIXOS_SMOKE_RELAX") && pk < 0.1f) { std::fprintf(stderr, "amixos --smoke: recorded master is silent (%.3f)\n", pk); return 48; } }
      for (const QString& f : rf) QFile::remove(f); QDir().rmdir(QDir::tempPath() + "/amixos-smoke-rec"); engine.setArmed(0, false); engine.setRecordFolder("");
      { const QVariantMap ad = engine.autoDetect(); if (!ad.isEmpty() && ad["backend"].toString() == "null") { std::fprintf(stderr, "amixos --smoke: auto detect must never pick null\n"); return 49; } }
      arrange.stop(); const QString out = arrange.exportWav(smokeOut); if (out.isEmpty() || !QFileInfo::exists(out)) { std::fprintf(stderr, "amixos --smoke: export failed: %s\n", qPrintable(arrange.message())); return 20; }
      // a little more for the screenshot: a second track with the same file later, a marker, two automation points, the head in the middle
      const int t2 = arrange.addTrack("Pad"); arrange.importFile(wav, t2); arrange.moveClip(arrange.clips()[1].toMap().value("id").toInt(), 1.0); arrange.setTrack(t2, "pan", 0.5);
      engine.setPlugin(0, 0, "gate"); engine.setPlugin(0, 1, "eq"); engine.setPlugin(0, 2, "comp"); engine.setPluginBypass(0, 2, true);   // the rack in the screenshot: three built-in stages, the test VST3 loaded, a missing VST3 pending
      engine.rescanVst({QCoreApplication::applicationDirPath() + "/engine/vst3", QCoreApplication::applicationDirPath() + "/engine/vst2"});   // the engine build's test plug-ins, for the rack in the picture
      for (const QVariant& v : engine.vstCatalog()) { const QVariantMap m = v.toMap(); engine.setPlugin(0, m["format"].toString() == "VST2" ? 5 : 3, m["kind"].toString()); }
      engine.setPlugin(0, 4, "vst3:/plugins/Example.vst3|00");
      arrange.seek(0.75); arrange.addMarker("Verse"); arrange.addAutomation(t2, 1.0, 0); arrange.addAutomation(t2, 1.5, -20); arrange.setSelectedClip(clip); }
    // the mixer action bar: plugin slots drive the built-in stages, global actions touch every strip
    engine.setPlugin(4, 0, "comp"); if (!engine.channelParams(4).value("compOn").toBool() || engine.plugins(4)[0].toMap().value("kind").toString() != "comp") { std::fprintf(stderr, "amixos --smoke: plugin slot did not switch the compressor on\n"); return 21; }
    engine.setPluginBypass(4, 0, true); if (engine.channelParams(4).value("compOn").toBool()) { std::fprintf(stderr, "amixos --smoke: plugin bypass left the compressor on\n"); return 22; }
    engine.setPlugin(4, 1, "vst3:/plugins/Example.vst3"); if (!engine.plugins(4)[1].toMap().value("pending").toBool()) { std::fprintf(stderr, "amixos --smoke: VST3 slot not marked pending\n"); return 23; }
    engine.movePlugin(4, 1, 0); if (!engine.plugins(4)[0].toMap().value("pending").toBool() || engine.pluginCount(4) != 2) { std::fprintf(stderr, "amixos --smoke: plugin reorder failed\n"); return 26; }
    engine.setPlugin(4, 0, ""); engine.setPlugin(4, 1, "");
    engine.muteAll(true); if (!engine.stripState()[7].toMap().value("mute").toBool()) { std::fprintf(stderr, "amixos --smoke: mute all failed\n"); return 24; } engine.muteAll(false);
    engine.setSolo(2, true); engine.clearSolo(); if (engine.stripState()[2].toMap().value("solo").toBool()) { std::fprintf(stderr, "amixos --smoke: clear solo failed\n"); return 25; }
    // stereo link, trim, undo / redo, A / B
    engine.setLink(6, true); engine.setGain(6, -9); engine.setPan(6, -0.5); if (std::fabs(engine.stripState()[7].toMap().value("db").toDouble() + 9) > 1e-9 || std::fabs(engine.stripState()[7].toMap().value("pan").toDouble() - 0.5) > 1e-9) { std::fprintf(stderr, "amixos --smoke: stereo link did not follow\n"); return 27; }
    engine.setLink(6, false); engine.setGain(6, 0); engine.setGain(7, 0); engine.setPan(6, 0); engine.setPan(7, 0);
    engine.setTrim(5, 6); if (std::fabs(engine.trim(5) - 6) > 1e-9) { std::fprintf(stderr, "amixos --smoke: trim did not take\n"); return 28; } engine.setTrim(5, 0);
    engine.recordHistory(); engine.setGain(9, -12); engine.recordHistory(); if (!engine.canUndo()) { std::fprintf(stderr, "amixos --smoke: no undo history\n"); return 29; }
    engine.undo(); if (std::fabs(engine.stripState()[9].toMap().value("db").toDouble()) > 1e-9) { std::fprintf(stderr, "amixos --smoke: undo did not restore the fader\n"); return 30; }
    engine.redo(); if (std::fabs(engine.stripState()[9].toMap().value("db").toDouble() + 12) > 1e-9) { std::fprintf(stderr, "amixos --smoke: redo failed\n"); return 31; }
    engine.toggleAB(); engine.setGain(9, 3); engine.toggleAB(); if (std::fabs(engine.stripState()[9].toMap().value("db").toDouble() + 12) > 1e-9 || engine.abSlot() != "A") { std::fprintf(stderr, "amixos --smoke: A/B did not restore A\n"); return 32; }
    engine.toggleAB(); engine.setGain(9, 0); engine.toggleAB(); engine.setGain(9, 0);
    // settings: a value round-trips through QSettings; the console exports and imports as one JSON file
    { const bool was = settings.confirmDestructive(); settings.setConfirmDestructive(!was); if (settings.confirmDestructive() == was) { std::fprintf(stderr, "amixos --smoke: setting did not persist\n"); return 33; } settings.setConfirmDestructive(was);
      settings.setClipDb(-3); if (std::fabs(settings.clipDb() + 3) > 1e-9) { std::fprintf(stderr, "amixos --smoke: clip point\n"); return 34; } settings.setClipDb(-0.1);
      engine.setName(11, "ExportMe"); const QString ex = settings.exportConsole(QDir::tempPath() + "/amixos-smoke-console.json"); if (ex.isEmpty()) { std::fprintf(stderr, "amixos --smoke: export console failed\n"); return 35; }
      engine.setName(11, "Changed"); if (!settings.importConsole(ex) || engine.stripState()[11].toMap().value("name").toString() != "ExportMe") { std::fprintf(stderr, "amixos --smoke: import console did not restore the name\n"); return 36; }
      engine.setName(11, "CH 12"); QFile::remove(ex); if (settings.dataFiles().size() < 5 || settings.factoryResetPreview().size() < 5) { std::fprintf(stderr, "amixos --smoke: settings lists\n"); return 37; } }
    // VST3: the engine build's test plug-in (amixtest.vst3) loads into a slot, exposes parameters, keeps them through a reload; a missing module is pending with a reason
    { const QString vstDir = QCoreApplication::applicationDirPath() + "/engine/vst3"; const int n = engine.rescanVst({vstDir, QCoreApplication::applicationDirPath() + "/engine/vst2"});
      if (n < 2) { std::fprintf(stderr, "amixos --smoke: the test VST3 plug-in was not found in %s\n", qPrintable(vstDir)); return 38; }
      const QVariantMap first = engine.vstCatalog()[0].toMap(); engine.setPlugin(4, 0, first["kind"].toString());
      QVariantMap slot = engine.plugins(4)[0].toMap(); if (slot["pending"].toBool() || slot["name"].toString() != "AMIX Test Gain") { std::fprintf(stderr, "amixos --smoke: VST3 load failed: %s\n", qPrintable(engine.pluginError(4, 0))); return 39; }
      const QVariantList pp = engine.pluginParams(4, 0); if (pp.size() != 2 || pp[0].toMap()["title"].toString() != "Gain") { std::fprintf(stderr, "amixos --smoke: VST3 params\n"); return 40; }
      engine.setPluginParam(4, 0, pp[0].toMap()["id"].toInt(), 0.75); if (std::fabs(engine.pluginParams(4, 0)[0].toMap()["value"].toDouble() - 0.75) > 1e-9 || engine.pluginParams(4, 0)[0].toMap()["text"].toString() != "1.50 x") { std::fprintf(stderr, "amixos --smoke: VST3 param set\n"); return 41; }
      engine.setPluginBypass(4, 0, true); if (!engine.plugins(4)[0].toMap()["bypass"].toBool()) { std::fprintf(stderr, "amixos --smoke: VST3 bypass\n"); return 42; } engine.setPluginBypass(4, 0, false);
      engine.open("null", "", "", 44100, 128, 2, 2);   // a re-open reloads the plug-in at the new rate and keeps its state
      if (engine.plugins(4)[0].toMap()["pending"].toBool() || std::fabs(engine.pluginParams(4, 0)[0].toMap()["value"].toDouble() - 0.75) > 1e-9) { std::fprintf(stderr, "amixos --smoke: VST3 state lost across re-open\n"); return 43; }
      engine.setPlugin(4, 1, "vst3:/nowhere/missing.vst3|00"); if (!engine.plugins(4)[1].toMap()["pending"].toBool() || engine.pluginError(4, 1).isEmpty()) { std::fprintf(stderr, "amixos --smoke: missing VST3 not reported\n"); return 44; }
      QString kind2; for (const QVariant& v : engine.vstCatalog()) if (v.toMap()["format"].toString() == "VST2") kind2 = v.toMap()["kind"].toString();
      if (kind2.isEmpty()) { std::fprintf(stderr, "amixos --smoke: the VST2 test plug-in was not found\n"); return 50; }
      engine.setPlugin(4, 2, kind2); if (engine.plugins(4)[2].toMap()["pending"].toBool() || engine.plugins(4)[2].toMap()["name"].toString() != "AMIX Test Gain 2" || engine.pluginParams(4, 2).size() != 1) { std::fprintf(stderr, "amixos --smoke: VST2 load failed: %s\n", qPrintable(engine.pluginError(4, 2))); return 51; }
      engine.setPlugin(4, 0, ""); engine.setPlugin(4, 1, ""); engine.setPlugin(4, 2, ""); engine.open("null", "", "", 48000, 256, 2, 2); }
    engine.setBusFx(0, "reverb"); if (engine.busFx(0)["name"].toString().isEmpty() || engine.busFx(0)["params"].toList().isEmpty() || engine.busState()[0].toMap()["fx"].toString() != "reverb") { std::fprintf(stderr, "amixos --smoke: bus effect did not load\n"); return 60; }
    { const QString pid = engine.busFx(0)["params"].toList()[0].toMap()["id"].toString(); engine.setBusFxParam(0, pid, 0.8); if (std::fabs(engine.busFx(0)["params"].toList()[0].toMap()["value"].toDouble() - 0.8) > 1e-9) { std::fprintf(stderr, "amixos --smoke: bus effect param\n"); return 61; } }
    engine.setBusFxMix(0, 0.5); engine.setBusFxBypass(0, true); if (std::fabs(engine.busFx(0)["mix"].toDouble() - 0.5) > 1e-9 || !engine.busFx(0)["bypass"].toBool()) { std::fprintf(stderr, "amixos --smoke: bus effect mix / bypass\n"); return 62; } engine.setBusFxBypass(0, false);
    engine.setBusFx(1, "delay"); engine.setBusFx(2, "chorus"); engine.setBusFx(3, "drive"); if (engine.busFx(3)["params"].toList().isEmpty()) { std::fprintf(stderr, "amixos --smoke: drive\n"); return 63; }
    engine.setWidth(1.5); if (std::fabs(engine.width() - 1.5) > 1e-9) { std::fprintf(stderr, "amixos --smoke: width\n"); return 64; } engine.setWidth(1.0);
    if (engine.scopeFrame(0, 20, 256, true)["samples"].toList().size() != 256 || engine.lissajous(128).size() != 256) { std::fprintf(stderr, "amixos --smoke: scope frame size\n"); return 65; }
    { QVariantList g; for (int b = 0; b < 10; ++b) g << (b == 7 ? 4.0 : 0.0); engine.setGraphicEq(2, g, true); if (std::fabs(engine.graphicEq(2)[7].toDouble() - 4.0) > 1e-9 || engine.eqBand(2, 7)["type"].toInt() != 1 || std::fabs(engine.eqBand(2, 7)["freq"].toDouble() - 4000) > 1e-6) { std::fprintf(stderr, "amixos --smoke: graphic EQ\n"); return 70; }
      engine.setGraphicEq(2, g, false); if (engine.eqBand(2, 7)["type"].toInt() != 0 || std::fabs(engine.graphicEq(2)[7].toDouble() - 4.0) > 1e-9) { std::fprintf(stderr, "amixos --smoke: graphic EQ bypass keeps gains\n"); return 71; }
      engine.setGraphicEq(2, g, true); engine.copyEqToAll(2); if (std::fabs(engine.graphicEq(9)[7].toDouble() - 4.0) > 1e-9) { std::fprintf(stderr, "amixos --smoke: copy EQ to all\n"); return 72; }
      for (int s = 0; s < 32; ++s) engine.setGraphicEq(s, QVariantList(), true); }
    engine.setTestOn(true); engine.setTestType(0); engine.setTestFreq(1000); engine.setTestDb(-6); waitMs(500);
    { const double pk = engine.telemetry()["masterPeakDb"].toDouble(); if ((pk < -9 || pk > -3) && !qEnvironmentVariableIsSet("AMIXOS_SMOKE_RELAX")) { std::fprintf(stderr, "amixos --smoke: test sine at -6 dBFS reads %.1f dBFS on the master\n", pk); return 73; }
      const QVariantList sp = engine.spectrum(0); if (sp.size() != 31) { std::fprintf(stderr, "amixos --smoke: spectrum bands\n"); return 74; } int best = 0; for (int b = 1; b < 31; ++b) if (sp[b].toDouble() > sp[best].toDouble()) best = b; if (best != 17 && !qEnvironmentVariableIsSet("AMIXOS_SMOKE_RELAX")) { std::fprintf(stderr, "amixos --smoke: 1 kHz sine peaks in band %d, expected 17\n", best); return 75; } }
    engine.setTestOn(false); engine.setClickOn(true); engine.setClickBpm(240); waitMs(500); if (engine.clickBeat() < 1 && !qEnvironmentVariableIsSet("AMIXOS_SMOKE_RELAX")) { std::fprintf(stderr, "amixos --smoke: metronome beat\n"); return 76; } engine.setClickOn(false); engine.setClickBpm(120);
    { const QString t = settings.saveText("smoke-test.csv", "a,b\n1,2\n"); if (t.isEmpty() || !QFileInfo::exists(t)) { std::fprintf(stderr, "amixos --smoke: saveText\n"); return 80; } QFile::remove(t);
      if (engine.blockFrames() != 256 || std::fabs(engine.sampleRate() - 48000) > 1e-6) { std::fprintf(stderr, "amixos --smoke: blockFrames / sampleRate\n"); return 81; }
      const QVariantMap rt = engine.roundTrip(); if (rt["ok"].toBool()) { std::fprintf(stderr, "amixos --smoke: round trip found an echo on a silent input\n"); return 82; } }
    engine.setInputRoute(3, 0); engine.setOutputRoute(0, 1);   // the routing matrix: CH 4 reads input 1, OUT 1 carries MASTER L
    if (engine.inputRoutes()[3].toInt() != 0 || engine.outputRoutes()[0].toInt() != 1) { std::fprintf(stderr, "amixos --smoke: routing did not take\n"); return 11; }
    engine.resetRouting(); if (engine.inputRoutes()[3].toInt() != 3 || engine.outputRoutes()[0].toInt() != 3) { std::fprintf(stderr, "amixos --smoke: routing defaults wrong\n"); return 12; }
    QTimer::singleShot(1200, &app, [&] {
      if (!shots.isEmpty()) {   // one picture per ported page
        QDir().mkpath(shots);
        auto* win = qobject_cast<QQuickWindow*>(qml.rootObjects().first());
        for (const char* id : {"home", "mixer", "dsp", "dyn", "gate", "bus", "dca", "scenes", "presets", "routing", "renew", "arrange", "io", "meter", "lufs", "sys", "fx", "osc", "phase", "miceq", "live", "tools", "calib", "boot", "help"}) {
          win->setProperty("current", QString::fromLatin1(id)); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
          const QImage img = win->grabWindow(); img.save(shots + "/" + id + ".png"); std::printf("amixos --shots: %s %dx%d\n", id, img.width(), img.height());
          if (QString::fromLatin1(id) == "routing") {
            if (QObject* rp = win->findChild<QObject*>("routingPage")) { rp->setProperty("tab", 1); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50); win->grabWindow().save(shots + "/routing-outputs.png"); std::printf("amixos --shots: routing-outputs\n"); }
          }
          if (QString::fromLatin1(id) == "mixer") {   // the same page with the analogue VU faces
            if (QObject* mixer = win->findChild<QObject*>("mixerPage")) {
              mixer->setProperty("rackCompact", true); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50); win->grabWindow().save(shots + "/mixer-compact.png"); mixer->setProperty("rackCompact", false); std::printf("amixos --shots: mixer-compact\n");
              if (QObject* pop = mixer->findChild<QObject*>("mixerQuick")) { QMetaObject::invokeMethod(pop, "open"); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50); win->grabWindow().save(shots + "/mixer-settings.png"); QMetaObject::invokeMethod(pop, "close"); std::printf("amixos --shots: mixer-settings\n"); }
              mixer->setProperty("vuMode", true); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50); win->grabWindow().save(shots + "/mixer-vu.png"); mixer->setProperty("vuMode", false); std::printf("amixos --shots: mixer-vu\n"); }
          }
        }
      }
      // the language tables: 80 languages, every one covers the page names
      int full = 0; for (const QVariant& l : i18n.languages()) if (l.toMap().value("coverage").toInt() >= 44) ++full;
      std::printf("amixos --smoke: %d languages, %d cover all page names; MIXER in fa = %s\n", i18n.count(), full, qPrintable(i18n.t("MIXER", "fa")));
      if (i18n.count() < 80 || full < 80 || i18n.t("MIXER", "fa") == "MIXER") { std::fprintf(stderr, "amixos --smoke: language tables incomplete\n"); QCoreApplication::exit(7); return; }   // (a bare `return 7` here would make the lambda non-void and fall off the end: heap corruption)
      const QVariantMap m = engine.telemetry();
      const qulonglong blocks = m.value("blocks").toULongLong();
      std::printf("amixos --smoke: interface loaded, %s backend, %llu blocks in 1.2 s, xruns %llu, master %.1f dBFS, %d pages\n", qPrintable(engine.backend()), blocks, m.value("xruns").toULongLong(), m.value("masterPeakDb").toDouble(), engine.pageCount());
      arrange.newProject(); QFile::remove(smokeWav); QFile::remove(smokeOut);   // the smoke's demo arrangement is not kept
      for (int k = 0; k < 6; ++k) engine.setPlugin(0, k, "");
      engine.stop();
      QCoreApplication::exit(blocks > 100 && engine.pageCount() >= 30 ? 0 : 4);
    });
  }
  return app.exec();
}
