#include <tuple>
#include <chrono>
#include <thread>
// A mix OS: the native console. Qt Quick interface (qml/) on the C++20 engine (Engine.h -> am::Runtime). No browser, no web runtime.
//   amixos              the console
//   amixos --smoke      offscreen: load the interface, open the null device, run one second, print the meters, exit 0 (the automated check)
//   amixos --shots DIR  like --smoke, and saves a PNG of every ported page into DIR (visual check without a display: QT_QPA_PLATFORM=offscreen)
//   amixos --lang CODE  start in that language (one of the 80+; also applies to --shots)
#include <QGuiApplication>
#ifdef _WIN32
#include <objbase.h>
#endif
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QPalette>
#include <QDir>
#include <QTimer>
#include <cmath>
#include <cstdio>

#include <QElapsedTimer>
#include <QThread>
#include <QUdpSocket>
#include <QtEndian>
#include <QFileInfo>
#include <QFile>
#include <vector>
#include "Arrange.h"
#include "Settings.h"
#include "Access.h"
#include "Osc.h"
#include "Media.h"
#include "Samples.h"
#include "paths/paths.hpp"
#include "samples/samples.hpp"
#include <QHostAddress>
#include <QSettings>
#include <QIcon>
#include "Update.h"
#include "Measure.h"
#include "Remote.h"
#include "Net.h"
#include "measure/measure.hpp"
#include "Engine.h"
#include "license/license.hpp"
#include "I18n.h"

// real time passes (processEvents alone returns at once when the queue is empty)
static void waitMs(int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(5); } }

// Every Qt / QML message also goes to <data>/console.log (the console has no terminal on Windows): QML errors, missing imports, the render
// backend. The last 500 lines are kept; --software draws with the CPU raster renderer when the graphics driver refuses D3D11 / OpenGL.
static QFile* g_log = nullptr;
static void logToFile(QtMsgType t, const QMessageLogContext&, const QString& m) {
  static const char* kind[] = {"debug", "warning", "critical", "fatal", "info"}; const QByteArray line = QDateTime::currentDateTime().toString("HH:mm:ss.zzz ").toUtf8() + kind[int(t) < 5 ? int(t) : 0] + ": " + m.toUtf8() + "\n";
  std::fputs(line.constData(), stderr); if (g_log && g_log->isOpen()) { g_log->write(line); g_log->flush(); }
}
int main(int argc, char* argv[]) {
#ifdef _WIN32
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // COM for the main thread before Qt starts: the ASIO driver objects (CoCreateInstance) and the WASAPI enumerator are created from here
#endif
  for (int i = 1; i < argc; ++i) { const QByteArray a(argv[i]); if (a == "--software") QQuickWindow::setGraphicsApi(QSGRendererInterface::Software); else if (a == "--opengl") QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL); }
  QGuiApplication app(argc, argv);
  // One explicit palette for every stock control (Slider, ComboBox, TextField, Menu, Dialog, ScrollBar, ToolTip): the Fusion style draws
  // them from the application palette, which Windows and macOS hand over in their own colours. The values are Theme.qml's (every one an
  // explicit #RRGGBB), for the dark and the light look; applied now and again whenever SETTINGS flips the theme.
  auto applyPalette = [](bool dark) {
    QPalette pal;
    const QColor window(dark ? "#040507" : "#f3f5f9"), card(dark ? "#0f141c" : "#ffffff"), raised(dark ? "#141b26" : "#eef1f6"), ink(dark ? "#f1f5f9" : "#0f172a"), dim("#64748b"), amber(dark ? "#fbbf24" : "#d97706"), line(dark ? "#242c3a" : "#cfd5df");
    pal.setColor(QPalette::Window, window); pal.setColor(QPalette::WindowText, ink); pal.setColor(QPalette::Base, card); pal.setColor(QPalette::AlternateBase, raised); pal.setColor(QPalette::Text, ink);
    pal.setColor(QPalette::Button, raised); pal.setColor(QPalette::ButtonText, ink); pal.setColor(QPalette::Highlight, amber); pal.setColor(QPalette::HighlightedText, QColor(dark ? "#000000" : "#ffffff"));
    pal.setColor(QPalette::ToolTipBase, raised); pal.setColor(QPalette::ToolTipText, ink); pal.setColor(QPalette::PlaceholderText, dim); pal.setColor(QPalette::Mid, line); pal.setColor(QPalette::Dark, window); pal.setColor(QPalette::Light, line); pal.setColor(QPalette::Midlight, line); pal.setColor(QPalette::Shadow, window);
    pal.setColor(QPalette::Disabled, QPalette::Text, dim); pal.setColor(QPalette::Disabled, QPalette::ButtonText, dim); pal.setColor(QPalette::Disabled, QPalette::WindowText, dim); pal.setColor(QPalette::Link, QColor(dark ? "#00f2ff" : "#0e7490"));
    QGuiApplication::setPalette(pal);
  };
  app.setOrganizationName("AMFB Audio");
  am::lic::setProgramDir(QCoreApplication::applicationDirPath().toStdString());   // edition-token.json next to the program (paid editions)
  app.setOrganizationDomain("amfb.audio");
  app.setApplicationName("A mix OS"); app.setApplicationDisplayName("AmF X1");
  app.setApplicationVersion(QStringLiteral(AMIX_VERSION));
  app.setWindowIcon(QIcon(QStringLiteral(":/qt/qml/AmixOS/icons/amfx1.png")));   // the mixer logo in the title bar and the task bar
  QQuickStyle::setStyle("Fusion");   // the one stock style that draws from the application palette (set dark below); Basic ignores the palette and paints light grey boxes on Windows

  bool smoke = false; QString shots, lang;
  const QString smokeWav = QDir::tempPath() + "/amixos-smoke-tone.wav", smokeOut = QDir::tempPath() + "/amixos-smoke-export.wav";   // used by the smoke and by its end-of-run lambda: must outlive both
  for (int i = 1; i < argc; ++i) { const QString a = QString::fromLocal8Bit(argv[i]); if (a == "--smoke") smoke = true; else if (a == "--shots" && i + 1 < argc) { smoke = true; shots = QString::fromLocal8Bit(argv[++i]); } else if (a == "--lang" && i + 1 < argc) lang = QString::fromLocal8Bit(argv[++i]); }

  am::paths::setProgramDir(QCoreApplication::applicationDirPath().toStdString());
  if (!smoke) { QFile* lf = new QFile(QString::fromStdString(am::paths::file("console.log"))); if (lf->size() > 256 * 1024) lf->remove(); if (lf->open(QIODevice::Append | QIODevice::Text)) { g_log = lf; qInstallMessageHandler(logToFile); qInfo("AmF X1 %s starting: %s, Qt %s, render %s", AMIX_VERSION, am::paths::osName(), qVersion(), QQuickWindow::graphicsApi() == QSGRendererInterface::Software ? "software" : "auto"); } }
  Engine engine; I18n i18n; Arrange arrange(&engine); Settings settings(&engine); applyPalette(settings.darkTheme()); QObject::connect(&settings, &Settings::changed, &app, [&] { static bool last = settings.darkTheme(); if (last != settings.darkTheme()) { last = settings.darkTheme(); applyPalette(last); } }); if (smoke) settings.finishSetup(); Access access(&engine); Osc osc(&engine); Media media(&engine); Samples samples(&engine); Update update(QStringLiteral(AMIX_VERSION)); Measure measure(&engine); Remote remote(&engine); Net net(&engine);
  if (!lang.isEmpty()) i18n.setLang(lang);   // --lang fa: start in that language (also for --shots)
  QQmlApplicationEngine qml;
  qml.rootContext()->setContextProperty("engine", &engine);
  qml.rootContext()->setContextProperty("arrange", &arrange);
  qml.rootContext()->setContextProperty("settings", &settings);
  qml.rootContext()->setContextProperty("access", &access);
  qml.rootContext()->setContextProperty("osc", &osc);
  qml.rootContext()->setContextProperty("media", &media);
  qml.rootContext()->setContextProperty("samples", &samples);
  qml.rootContext()->setContextProperty("updater", &update);
  QObject::connect(&update, &Update::restarting, &app, [&] { settings.sessionClosed(); engine.stop(); QTimer::singleShot(1800, &app, &QCoreApplication::quit); });   // INSTALL AND RESTART: save, release the sound card, show the notice, quit; the setup starts the console again
  qml.rootContext()->setContextProperty("measure", &measure);
  qml.rootContext()->setContextProperty("remote", &remote);
  qml.rootContext()->setContextProperty("net", &net);
  QObject::connect(&remote, &Remote::stopped, &app, [&] { if (!engine.running()) settings.startLastDevice(); });   // the remote service has let the sound card go: the console takes it back
  QObject::connect(&app, &QGuiApplication::aboutToQuit, &settings, &Settings::sessionClosed);
  QObject::connect(&app, &QGuiApplication::aboutToQuit, &engine, &Engine::saveNow);   // the console is written before the window goes, whatever happens to the process afterwards
  QObject::connect(&app, &QGuiApplication::aboutToQuit, &engine, &Engine::restoreSystemSound);   // a Windows mute set by the fader at -inf is taken back on exit
  if (!smoke && update.checkAtStart() && settings.setupDone()) QTimer::singleShot(3000, &update, &Update::check);
  qml.rootContext()->setContextProperty("i18n", &i18n);
  qml.rootContext()->setContextProperty("appVersion", QStringLiteral(AMIX_VERSION));
  qml.rootContext()->setContextProperty("noBoot", smoke);
  QObject::connect(&qml, &QQmlApplicationEngine::objectCreationFailed, &app, [] { std::fprintf(stderr, "amixos: the interface could not be created\n"); QCoreApplication::exit(2); }, Qt::QueuedConnection);
  qml.addImportPath(QStringLiteral("qrc:/qt/qml"));   // the console's own module (AmixOS, with the Theme singleton) lives in the resources: Qt 6.4 on Windows does not search there by itself, and every Theme.* colour came out undefined (black on white)
  qml.load(QUrl(QStringLiteral("qrc:/qt/qml/AmixOS/qml/Main.qml")));   // Qt 6.4 (loadFromModule is 6.5+)
  if (!smoke && settings.autoStart()) QTimer::singleShot(300, &app, [&] { settings.startLastDevice(); });   // the remembered device, once the interface is up
  // AUTO AUDIO INTERFACE: when an interface is plugged in (hot-plug) and the engine is idle, start it (the remembered device if it is the one, else AUTO DETECT)
  QObject::connect(&engine, &Engine::hotplug, &app, [&](bool added, const QString& what) { if (!added || engine.running() || !settings.autoStart()) return; QTimer::singleShot(1500, &app, [&] { if (!engine.running()) settings.startLastDevice(); }); qInfo("hot-plug: %s", qPrintable(what)); });
  if (!smoke) engine.setHotplugWatch(true);
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
      if (mpk < -20 || spk > -100 || !arrange.playing()) { std::fprintf(stderr, "amixos --smoke: arrangement not audible on the master (master %.1f dBFS, strip %.1f, playing %d, engine running %d: %s)\n", mpk, spk, arrange.playing(), engine.running(), qPrintable(engine.status())); return 19; }
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
    { // ACOUSTICS offline: the sweep at -6 dB and 5 ms late, as a WAV -> a flat response at -6 dB, the delay found, room modes, auto EQ runs
      const am::measure::Sweep sw = am::measure::makeSweep(48000, 20, 20000, 2, 0.5); std::vector<float> rec(sw.signal.size() + 240, 0.f); for (std::size_t i = 0; i < sw.signal.size(); ++i) rec[i + 240] = 0.5f * sw.signal[i];
      const QString mw = QDir::tempPath() + "/amixos-smoke-sweep.wav"; std::string merr; if (!am::arr::writeWav24(mw.toStdString(), rec, rec, 48000, merr) || !measure.importSweep(mw, 2, 20, 20000, 24)) { std::fprintf(stderr, "amixos --smoke: acoustics import failed: %s\n", qPrintable(measure.message())); return 60; }
      const QVariantList mf = measure.result().value("freq").toList(), mm = measure.result().value("mag").toList(); double at1k = -999; for (int i = 0; i < mf.size(); ++i) if (std::fabs(mf[i].toDouble() - 1000) < 20) at1k = mm[i].toDouble();
      if (std::fabs(at1k + 6.0) > 1.0 || std::fabs(measure.decay().value("delayMs").toDouble() - 5.0) > 0.2) { std::fprintf(stderr, "amixos --smoke: acoustics reads %.2f dB at 1 kHz (want -6), delay %.2f ms (want 5)\n", at1k, measure.decay().value("delayMs").toDouble()); return 61; }
      if (measure.room(5, 4, 2.7, 0.5, 1, 1, 1.2, 2.5, 3, 1.2).value("modes").toList().isEmpty() || !measure.autoEq("house", 0, 4, 20, 400, 8) || measure.waterfall(12).value("rows").toList().isEmpty()) { std::fprintf(stderr, "amixos --smoke: acoustics room / auto EQ / waterfall failed\n"); return 62; }
      QFile::remove(mw);
      engine.setSpatial(5, true, 90, 0, 2, 0); if (!engine.spatial(5).value("on").toBool() || engine.spatial(5).value("az").toDouble() != 90) { std::fprintf(stderr, "amixos --smoke: 360 audio state did not take\n"); return 63; } engine.setSpatial(5, false, 0, 0, 1, 0);
      std::fprintf(stderr, "amixos --smoke: acoustics offline sweep %.2f dB at 1 kHz, delay 5 ms, room modes, auto EQ; 360 audio state OK\n", at1k); }
    { // MASTER CHAIN: the program EQ is its own rack (strip -1), not CH 1's EQ; a mastering preset switches the plug-in on
      const int before = engine.eqBand(0, 5).value("type").toInt(); engine.setEq(-1, 5, 1, 1000, 1.41, 4);
      if (engine.eqBand(-1, 5).value("gainDb").toDouble() != 4 || engine.eqBand(0, 5).value("type").toInt() != before) { std::fprintf(stderr, "amixos --smoke: the program EQ is not separate from the channel EQ\n"); return 64; }
      engine.applyMasteringPreset("STREAMING"); if (!engine.mastering().value("on").toBool() || engine.mastering().value("preset").toString() != "STREAMING") { std::fprintf(stderr, "amixos --smoke: mastering preset did not apply\n"); return 65; }
      engine.setMasterLimiter(true, -3, 250); { const QVariantMap lm = engine.masterLimiter(); if (!lm.value("on").toBool() || lm.value("ceiling").toDouble() != -3 || lm.value("release").toDouble() != 250) { std::fprintf(stderr, "amixos --smoke: limiter ceiling / release did not take\n"); return 66; } }
      engine.setMasterLimiter(true, -0.5); if (engine.masterLimiter().value("release").toDouble() != 250) { std::fprintf(stderr, "amixos --smoke: the two-value limiter call lost the release\n"); return 67; } engine.setMasterLimiter(true, -0.5, 100);
      { QVariantMap mods; mods["deessOn"] = true; mods["bassMonoOn"] = true; mods["exciterOn"] = true; engine.setMastering(mods); const QVariantMap mm = engine.mastering(); if (!mm.value("deessOn").toBool() || !mm.value("bassMonoOn").toBool() || !mm.value("exciterOn").toBool()) { std::fprintf(stderr, "amixos --smoke: mastering modules did not switch\n"); return 68; } mods["deessOn"] = false; mods["bassMonoOn"] = false; mods["exciterOn"] = false; engine.setMastering(mods); }
      QVariantMap off; off["on"] = false; engine.setMastering(off); engine.setEq(-1, 5, 0, 1000, 1.41, 0); }
    { // AUTO UPDATE: nothing downloaded = nothing to install; coming back on the new version says UPDATED once; coming back on the old one says it did not complete
      if (update.installAndRestart() || update.canInstall()) { std::fprintf(stderr, "amixos --smoke: install and restart must refuse without a verified download\n"); return 70; }
      { QSettings qs; qs.setValue("update/installing", QStringLiteral("0.0.1")); qs.sync(); } { Update u2(QStringLiteral(AMIX_VERSION)); if (u2.justUpdated() != QStringLiteral(AMIX_VERSION) || !u2.failedUpdate().isEmpty()) { std::fprintf(stderr, "amixos --smoke: the update was not recognised after the restart\n"); return 71; } }
      { QSettings qs; if (qs.contains("update/installing")) { std::fprintf(stderr, "amixos --smoke: the pending-update mark was not cleared\n"); return 72; } qs.setValue("update/installing", QStringLiteral("999.0")); qs.sync(); } { Update u3(QStringLiteral(AMIX_VERSION)); if (u3.failedUpdate() != "999.0" || !u3.justUpdated().isEmpty()) { std::fprintf(stderr, "amixos --smoke: a failed update was not reported\n"); return 73; } } }
    { QElapsedTimer gt; gt.start(); const bool r = engine.guarded([] { std::this_thread::sleep_for(std::chrono::milliseconds(600)); return true; }, 120); if (r || gt.elapsed() > 450) { std::fprintf(stderr, "amixos --smoke: a stuck driver call was not left behind (%lld ms)\n", static_cast<long long>(gt.elapsed())); return 74; }
      if (!engine.guarded([] { return true; }, 2000)) { std::fprintf(stderr, "amixos --smoke: a quick guarded call failed\n"); return 75; } }
    { // EQUALIZER APO round trip on the program EQ; the signature sound preset
      engine.setEq(-1, 0, 2, 80, 0.7, 3); engine.setEq(-1, 1, 1, 2500, 2, -4); const QString apo = engine.exportApo(-1, QDir::tempPath() + "/amixos-smoke-apo.txt"); engine.setEq(-1, 0, 0, 31.5, 1.41, 0); engine.setEq(-1, 1, 0, 63, 1.41, 0);
      if (apo.isEmpty() || engine.importApo(-1, apo) != 2 || engine.eqBand(-1, 0).value("type").toInt() != 2 || std::fabs(engine.eqBand(-1, 1).value("gainDb").toDouble() + 4) > 0.05 || std::fabs(engine.eqBand(-1, 1).value("freq").toDouble() - 2500) > 1) { std::fprintf(stderr, "amixos --smoke: Equalizer APO export / import round trip failed\n"); return 76; }
      QFile::remove(apo); engine.setEq(-1, 0, 0, 31.5, 1.41, 0); engine.setEq(-1, 1, 0, 63, 1.41, 0);
      engine.applyMasteringPreset("AMFB SIGNATURE"); if (engine.mastering().value("preset").toString() != "AMFB SIGNATURE" || !engine.mastering().value("bassMonoOn").toBool()) { std::fprintf(stderr, "amixos --smoke: signature preset\n"); return 77; } QVariantMap so; so["on"] = false; so["preset"] = ""; so["bassMonoOn"] = false; so["exciterOn"] = false; engine.setMastering(so); }
    { // WINDOWS SOUND SYNC on a stand-in for the system volume: -inf mutes it, raising the fader restores what it was, sync off leaves it alone, exit takes the mute back
      bool sysMute = false; int sets = 0; engine.setSystemMuteBackend([&](bool& m) { m = sysMute; return true; }, [&](bool m) { sysMute = m; ++sets; return true; });
      const double keep = engine.masterDb(); engine.setWindowsMuteSync(true); engine.setMaster(-60); if (!sysMute || !engine.windowsMuted()) { std::fprintf(stderr, "amixos --smoke: the fader at -inf did not mute the system sound\n"); return 78; }
      engine.setMaster(-10); if (sysMute || engine.windowsMuted()) { std::fprintf(stderr, "amixos --smoke: raising the fader did not un-mute the system sound\n"); return 79; }
      sysMute = true; engine.syncSystemSound(); engine.setMaster(-60); engine.setMaster(0); if (!sysMute) { std::fprintf(stderr, "amixos --smoke: a mute the person had set in Windows was not put back\n"); return 80; } sysMute = false; engine.syncSystemSound();
      engine.setWindowsMuteSync(false); engine.setMaster(-60); if (sysMute) { std::fprintf(stderr, "amixos --smoke: sync off still muted the system sound\n"); return 81; }
      engine.setWindowsMuteSync(true); if (!sysMute) { std::fprintf(stderr, "amixos --smoke: switching sync on at -inf did not mute\n"); return 82; } engine.restoreSystemSound(); if (sysMute) { std::fprintf(stderr, "amixos --smoke: exit did not take the mute back\n"); return 83; }
      engine.setMaster(keep); engine.setSystemMuteBackend([](bool&) { return false; }, [](bool) { return false; }); engine.syncSystemSound(); }
    { // the ten effects in the rack of any channel: listed, loaded into a slot, a knob moves, bypass, cleared; a late channel too
      int fxn = 0; for (const QVariant& v : engine.pluginCatalog()) if (v.toMap().value("fx").toBool()) ++fxn; if (fxn != 17) { std::fprintf(stderr, "amixos --smoke: %d effects in the rack catalogue (want 17)\n", fxn); return 84; }
      for (const int ch : {6, 7}) { int slot = 0; for (const QVariant& v : engine.pluginCatalog()) { const QVariantMap m = v.toMap(); if (!m.value("fx").toBool()) continue; engine.setPlugin(ch, slot % 10, m.value("kind").toString()); const QVariantMap pl = engine.plugins(ch)[slot % 10].toMap();
          if (pl.value("pending").toBool() || pl.value("format").toString() != "FX" || engine.pluginParams(ch, slot % 10).size() < 4) { std::fprintf(stderr, "amixos --smoke: effect %s did not load into CH %d slot %d\n", qPrintable(m.value("kind").toString()), ch + 1, slot + 1); return 85; } ++slot; }
        engine.setPluginParam(ch, 0, 0, 0.9); engine.setPluginBypass(ch, 0, true); if (!engine.plugins(ch)[0].toMap().value("bypass").toBool()) { std::fprintf(stderr, "amixos --smoke: effect bypass\n"); return 86; }
        for (int k = 0; k < 16; ++k) engine.setPlugin(ch, k, ""); if (engine.pluginCount(ch) != 0) { std::fprintf(stderr, "amixos --smoke: effects not cleared\n"); return 87; } } }
    { (void)remote.addresses(); if (remote.running()) { std::fprintf(stderr, "amixos --smoke: the remote service must be off until asked\n"); return 88; } }
    { // rack presets: the plate reverb's factory presets set its knobs in a rack slot; the wrong effect's preset is refused
      const QVariantList rp = engine.rackPresets("fx:reverb"); engine.setPlugin(6, 0, "fx:reverb"); const double before = engine.pluginParams(6, 0)[0].toMap().value("value").toDouble();
      bool moved = false; for (const QVariant& v : rp) { if (!engine.applyRackPreset(6, 0, v.toMap().value("id").toInt())) { std::fprintf(stderr, "amixos --smoke: rack preset refused\n"); return 89; } if (std::fabs(engine.pluginParams(6, 0)[0].toMap().value("value").toDouble() - before) > 0.01) { moved = true; break; } }
      const QVariantList dp = engine.rackPresets("fx:delay"); if (rp.size() < 50 || !moved || dp.isEmpty() || engine.applyRackPreset(6, 0, dp[0].toMap().value("id").toInt()) || !engine.rackPresets("fx:tubeamp").isEmpty()) { std::fprintf(stderr, "amixos --smoke: rack presets (%d reverb, moved %d)\n", int(rp.size()), moved); return 90; }
      engine.setPlugin(6, 0, ""); }
    if (!measure.chime()) { std::fprintf(stderr, "amixos --smoke: the boot chord did not start on a running engine\n"); return 69; } measure.cancel();
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
    { media.clear(); if (media.add(smokeWav) != 1 || media.playlist().size() != 1 || !media.playlist()[0].toMap()["ok"].toBool()) { std::fprintf(stderr, "amixos --smoke: media add: %s\n", qPrintable(media.message())); return 110; }
      if (!media.play(0) || !media.playing()) { std::fprintf(stderr, "amixos --smoke: media play\n"); return 111; } waitMs(400);
      if (media.position() < 0.2 || engine.telemetry()["masterPeakDb"].toDouble() < -40) { std::fprintf(stderr, "amixos --smoke: media not heard on the master (pos %.2f, master %.1f dBFS)\n", media.position(), engine.telemetry()["masterPeakDb"].toDouble()); return 112; }
      media.stop(); if (media.playing() || media.position() != 0) { std::fprintf(stderr, "amixos --smoke: media stop\n"); return 113; }
      const QVariantMap t = engine.telemetry(); if (!t.contains("loadPct") || t["blocks"].toULongLong() == 0 || t["maxBlockUs"].toDouble() <= 0) { std::fprintf(stderr, "amixos --smoke: profiler telemetry\n"); return 114; }
      engine.setOutputBits(16); engine.setDcBlock(true); if (engine.outputBits() != 16 || !engine.dcBlock()) { std::fprintf(stderr, "amixos --smoke: output stage settings\n"); return 115; } engine.setOutputBits(0); media.clear(); }
    // SAMPLES: the bank is rendered, a pad plays into the master, stops; EXPORT writes 100 WAVs. FX PRESETS: 300 listed, one applied loads the kind.
    { if (!samples.ready() || samples.list().size() != 100) { std::fprintf(stderr, "amixos --smoke: samples bank\n"); return 120; }
      if (!samples.play(1) || !samples.play(7)) { std::fprintf(stderr, "amixos --smoke: sample trigger\n"); return 121; } waitMs(200);
      if (engine.telemetry()["masterPeakDb"].toDouble() < -40) { std::fprintf(stderr, "amixos --smoke: sample not heard on the master (%.1f dBFS)\n", engine.telemetry()["masterPeakDb"].toDouble()); return 122; } samples.stopAll();
      const QString sdir = QDir::tempPath() + "/amixos-smoke-samples"; QDir(sdir).removeRecursively(); std::string serr; if (!am::samples::writeAll(sdir.toStdString(), 48000, serr) || QDir(sdir).entryList({"*.wav"}, QDir::Files).size() != 100) { std::fprintf(stderr, "amixos --smoke: sample export: %s\n", serr.c_str()); return 123; } QDir(sdir).removeRecursively();
      if (engine.fxPresets().size() != 300 || !engine.applyFxPreset(8, 1) || engine.busFx(8)["kind"].toString() != "reverb" || engine.busFxPreset(8) != 1) { std::fprintf(stderr, "amixos --smoke: fx presets\n"); return 124; }
      if (!engine.applyFxPreset(9, 120) || engine.busFx(9)["kind"].toString() != "delay" || engine.applyFxPreset(9, 999)) { std::fprintf(stderr, "amixos --smoke: fx preset kinds\n"); return 125; } engine.setBusFx(8, ""); engine.setBusFx(9, "");
      if (engine.busState().size() != 16 || engine.busState()[0].toMap()["kind"] != "aux" || engine.busState()[8].toMap()["kind"] != "fx") { std::fprintf(stderr, "amixos --smoke: 16 buses (8 AUX + 8 FX)\n"); return 126; }
      // EQ PRESETS: 20 slots, the factory ten locked and named (AmiX W Shape first), apply changes the response, SAVE / CLEAR on a custom slot, locked slots refuse SAVE
      { const QVariantList ep = engine.eqPresets(); if (ep.size() != 20 || ep[0].toMap()["name"] != "AmiX W Shape" || ep[1].toMap()["name"] != "AmiX Golden EQ" || !ep[0].toMap()["locked"].toBool() || ep[10].toMap()["locked"].toBool()) { std::fprintf(stderr, "amixos --smoke: eq preset list\n"); return 140; }
        if (!engine.applyEqPreset(1, 3) || engine.stripEqPreset(3) != 1 || std::fabs(engine.responseCurve(3, 8)[0].toMap()["db"].toDouble()) < 0.5) { std::fprintf(stderr, "amixos --smoke: apply AmiX W Shape\n"); return 141; }
        if (engine.saveEqPreset(1, "x", 3) || !engine.saveEqPreset(11, "Smoke Curve", 3) || engine.eqPresets()[10].toMap()["name"] != "Smoke Curve" || engine.eqPresets()[10].toMap()["empty"].toBool()) { std::fprintf(stderr, "amixos --smoke: save eq preset\n"); return 142; }
        engine.setEq(3, 0, 0, 100, 1, 0); if (engine.stripEqPreset(3) != 0) { std::fprintf(stderr, "amixos --smoke: an edit must clear the preset mark\n"); return 143; }
        if (!engine.applyEqPreset(11, 4) || engine.stripEqPreset(4) != 11 || !engine.clearEqPreset(11) || !engine.eqPresets()[10].toMap()["empty"].toBool() || engine.stripEqPreset(4) != 0) { std::fprintf(stderr, "amixos --smoke: apply / clear custom eq preset\n"); return 144; }
        for (int b = 0; b < 10; ++b) { engine.setEq(3, b, 0, 1000, 1, 0); engine.setEq(4, b, 0, 1000, 1, 0); } engine.setCrossover(3, 0, 0, 80, 0, 16000); engine.setCrossover(4, 0, 0, 80, 0, 16000); }
      // RTA EQ custom slots: ten, save / clear round trip, gains clamped to +-12
      { if (engine.geqSlots().size() != 10 || !engine.geqSlots()[2].toMap()["empty"].toBool()) { std::fprintf(stderr, "amixos --smoke: geq slots\n"); return 150; }
        QVariantList g; for (int b = 0; b < 10; ++b) g << (b - 4.0) * 5; if (!engine.saveGeqSlot(3, "Smoke", g) || engine.geqSlots()[2].toMap()["name"] != "Smoke" || engine.geqSlots()[2].toMap()["gains"].toList()[0].toDouble() != -12.0 || engine.geqSlots()[2].toMap()["gains"].toList()[4].toDouble() != 0.0) { std::fprintf(stderr, "amixos --smoke: geq save\n"); return 151; }
        if (!engine.clearGeqSlot(3) || !engine.geqSlots()[2].toMap()["empty"].toBool() || engine.saveGeqSlot(11, "x", g)) { std::fprintf(stderr, "amixos --smoke: geq clear\n"); return 152; } }
      // DRIVER FALLBACK policy round trip (kept in the driver lock file) and the Bluetooth filter (no Bluetooth device on this machine: an empty list, no crash)
      { engine.setDriverFallback("dsound"); if (engine.driverFallback() != "dsound") { std::fprintf(stderr, "amixos --smoke: driver fallback\n"); return 160; } engine.setDriverFallback("all"); if (engine.driverFallback() != "all") { std::fprintf(stderr, "amixos --smoke: driver fallback reset\n"); return 161; }
        for (const QVariant& v : engine.bluetoothDevices()) if (!v.toMap()["bluetooth"].toBool()) { std::fprintf(stderr, "amixos --smoke: bluetooth filter\n"); return 162; } }
      // UM2 MACHINE: a Behringer UM2 with its own ASIO driver + Windows endpoints + DirectSound + Realtek onboard: AUTO DETECT must pick the UM2's
      // ASIO, the plan must walk ASIO -> WASAPI exclusive -> WASAPI shared -> DirectSound on the UM2's endpoints, auto start with nothing remembered
      // must try it; hot-plug: without the UM2 the detection picks Realtek, with it the UM2; the running device vanishing stops the engine with a note.
      { auto dev = [](const char* b, const char* id, const char* name, int in, int out, bool dIn = false, bool dOut = false) { QVariantMap m; m["backend"] = b; m["id"] = id; m["name"] = name; m["inChannels"] = in; m["outChannels"] = out; m["defaultRate"] = 48000.0; m["defaultIn"] = dIn; m["defaultOut"] = dOut; return QVariant(m); };
        const QVariantList pc = {dev("asio", "Realtek ASIO", "Realtek ASIO", 2, 2), dev("wasapi", "{r-out}", "Speakers (Realtek High Definition Audio)", 0, 2, false, true), dev("wasapi", "{r-in}", "Microphone (Realtek High Definition Audio)", 2, 0, true, false), dev("dsound", "{ds-r}", "Speakers (Realtek High Definition Audio)", 0, 2, false, true), dev("null", "null", "Null device (timed, no hardware)", 2, 2)};
        QVariantList um2 = pc; um2.prepend(dev("asio", "BEHRINGER USB AUDIO", "BEHRINGER USB AUDIO", 2, 2)); um2 << dev("wasapi", "{um2-in}", "Microphone (USB Audio CODEC)", 2, 0) << dev("wasapi", "{um2-out}", "Speakers (USB Audio CODEC)", 0, 2) << dev("dsound", "{ds-um2}", "Speakers (USB Audio CODEC)", 0, 2) << dev("dsound", "{ds-um2-in}", "Microphone (USB Audio CODEC)", 2, 0);
        const QVariantList realDevices = engine.devices();
        engine.simulateDevices(pc); const QVariantMap d0 = engine.autoDetect();
        if (d0["backend"] != "wasapi" && d0["backend"] != "asio") { std::fprintf(stderr, "amixos --smoke: UM2 machine without the UM2: detection picked %s\n", qPrintable(d0["name"].toString())); return 170; }
        if (d0["name"].toString().contains("BEHRINGER")) { std::fprintf(stderr, "amixos --smoke: UM2 detected while unplugged\n"); return 170; }
        engine.simulateDevices(um2); const QVariantMap d1 = engine.autoDetect();
        if (d1["backend"] != "asio" || d1["name"] != "BEHRINGER USB AUDIO" || d1["in"] != "BEHRINGER USB AUDIO" || d1["out"] != "BEHRINGER USB AUDIO") { std::fprintf(stderr, "amixos --smoke: UM2 not picked: %s / %s (%s)\n", qPrintable(d1["backend"].toString()), qPrintable(d1["name"].toString()), qPrintable(d1["why"].toString())); return 171; }
        const QVariantList plan = engine.driverPlan("asio", "BEHRINGER USB AUDIO", "BEHRINGER USB AUDIO");
        if (plan.size() != 4 || plan[0].toMap()["mode"] != "asio" || plan[1].toMap()["mode"] != "wasapi-exclusive" || plan[1].toMap()["in"] != "{um2-in}" || plan[1].toMap()["out"] != "{um2-out}" || plan[2].toMap()["mode"] != "wasapi-shared" || plan[3].toMap()["mode"] != "dsound" || plan[3].toMap()["out"] != "{ds-um2}") { std::fprintf(stderr, "amixos --smoke: UM2 driver plan has %d steps\n", int(plan.size())); for (const QVariant& p : plan) std::fprintf(stderr, "   %s in=%s out=%s why=%s\n", qPrintable(p.toMap()["mode"].toString()), qPrintable(p.toMap()["in"].toString()), qPrintable(p.toMap()["out"].toString()), qPrintable(p.toMap()["why"].toString())); return 172; }
        engine.setDriverFallback("dsound"); const QVariantList plan2 = engine.driverPlan("asio", "BEHRINGER USB AUDIO", "BEHRINGER USB AUDIO"); engine.setDriverFallback("all");
        if (plan2.size() != 2 || plan2[1].toMap()["mode"] != "dsound") { std::fprintf(stderr, "amixos --smoke: UM2 plan with DirectSound-only fallback has %d steps\n", int(plan2.size())); return 173; }
        int plugged = 0; QObject::connect(&engine, &Engine::hotplug, &app, [&](bool added, const QString& w) { if (added && w.contains("BEHRINGER")) ++plugged; });
        if (engine.inputLabel(0).isEmpty()) { std::fprintf(stderr, "amixos --smoke: input label\n"); return 174; }
        std::printf("amixos --smoke: UM2 machine OK: detect -> %s (%s); plan %s -> %s (%s / %s) -> %s -> %s (%s); DirectSound-only -> %d steps\n", qPrintable(d1["name"].toString()), qPrintable(d1["why"].toString()), qPrintable(plan[0].toMap()["mode"].toString()), qPrintable(plan[1].toMap()["mode"].toString()), qPrintable(plan[1].toMap()["in"].toString()), qPrintable(plan[1].toMap()["out"].toString()), qPrintable(plan[2].toMap()["mode"].toString()), qPrintable(plan[3].toMap()["mode"].toString()), qPrintable(plan[3].toMap()["out"].toString()), int(plan2.size()));
        { // a laptop with no ASIO: the pick is an output endpoint, and it must come with an input (it used to start with 0 inputs); a headset pairs with its own microphone; kinds are named
          const QVariantList lap = {dev("wasapi", "{spk}", "Speakers (Realtek High Definition Audio)", 0, 2, false, true), dev("wasapi", "{mic}", "Microphone Array (Realtek High Definition Audio)", 2, 0, true, false), dev("wasapi", "{hp}", "Headphones (Realtek High Definition Audio)", 0, 2), dev("wasapi", "{bt}", "Headphones (WH-1000XM4 Stereo)", 0, 2), dev("dsound", "{ds-spk}", "Speakers (Realtek High Definition Audio)", 0, 2, false, true), dev("dsound", "{ds-mic}", "Microphone Array (Realtek High Definition Audio)", 2, 0, true, false)};
          engine.simulateDevices(lap); const QVariantMap l = engine.autoDetect(); QStringList kinds; for (const QVariant& v : engine.devices()) kinds << v.toMap()["kind"].toString();
          if (l["out"] != "{spk}" || l["in"] != "{mic}" || l["inChannels"].toInt() != 2 || l["inKind"] != "microphone" || l["outKind"] != "speakers") { std::fprintf(stderr, "amixos --smoke: laptop detection: out %s in %s (%d in, %s / %s)\n", qPrintable(l["out"].toString()), qPrintable(l["in"].toString()), l["inChannels"].toInt(), qPrintable(l["inKind"].toString()), qPrintable(l["outKind"].toString())); return 175; }
          if (kinds != QStringList({"speakers", "microphone", "headphones", "bluetooth", "speakers", "microphone"})) { std::fprintf(stderr, "amixos --smoke: device kinds: %s\n", qPrintable(kinds.join(", "))); return 176; }
          std::printf("amixos --smoke: laptop without ASIO: %s + %s (%d in / %d out); kinds %s\n", qPrintable(l["outName"].toString()), qPrintable(l["inName"].toString()), l["inChannels"].toInt(), l["outChannels"].toInt(), qPrintable(kinds.join(", "))); }
        { // BLUETOOTH START: a refusal is followed by the next way in (own rate, 48 kHz, without the hands-free microphone, the other Windows path), and the note says what was tried
          const QVariantList bt = {dev("dsound", "{ds-bt}", "Headphones (WH-1000XM4 Stereo)", 0, 2), dev("dsound", "{ds-btmic}", "Headset (WH-1000XM4 Hands-Free)", 1, 0), dev("wasapi", "{w-bt}", "Headphones (WH-1000XM4 Stereo)", 0, 2)};
          engine.simulateDevices(bt); const bool started = engine.openBluetooth("dsound", "{ds-btmic}", "{ds-bt}", 512); const QString note = engine.bluetoothNote();
          if (started || !note.contains("without the hands-free microphone") || !note.contains("on WASAPI instead")) { std::fprintf(stderr, "amixos --smoke: Bluetooth start did not walk its fallbacks: %s\n", qPrintable(note)); return 177; }
          if (engine.openBluetooth("dsound", "-", "{gone}", 512) || !engine.bluetoothNote().contains("RESCAN")) { std::fprintf(stderr, "amixos --smoke: a vanished Bluetooth device was not reported\n"); return 178; }
          if (!engine.open("null", "", "", 48000, 256, 2, 2)) { std::fprintf(stderr, "amixos --smoke: the null device did not reopen after the Bluetooth check\n"); return 179; } }
        engine.simulateDevices(realDevices); }
      { // CHANNEL EQ (3 bands of its own) apart from the MIC EQ (ten bands, a rack item): one does not touch the other
        engine.setGraphicEq(4, QVariantList{3.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -2.0}, true); engine.setChannelEq(4, 4.0, -3.0, 2500.0, 2.0, true);
        const QVariantMap c3 = engine.channelEq(4); const QVariantList g = engine.graphicEq(4);
        if (std::fabs(c3["low"].toDouble() - 4) > 1e-6 || std::fabs(c3["mid"].toDouble() + 3) > 1e-6 || std::fabs(c3["midFreq"].toDouble() - 2500) > 1e-6 || std::fabs(c3["high"].toDouble() - 2) > 1e-6 || !c3["on"].toBool() || std::fabs(g[0].toDouble() - 3) > 1e-6 || std::fabs(g[9].toDouble() + 2) > 1e-6) { std::fprintf(stderr, "amixos --smoke: channel EQ / MIC EQ are not separate\n"); return 195; }
        bool hasMic = false, hasCh = false; for (const QVariant& v : engine.pluginCatalog()) { const QString k = v.toMap()["kind"].toString(); hasMic = hasMic || k == "miceq"; hasCh = hasCh || k == "eq"; }
        engine.setPlugin(4, 0, "miceq"); engine.setPlugin(4, 0, ""); const QVariantList g2 = engine.graphicEq(4);
        if (!hasMic || !hasCh || engine.eqBand(4, 0)["type"].toInt() != 0 || std::fabs(g2[0].toDouble() - 3) > 1e-6 || !engine.channelEq(4)["on"].toBool()) { std::fprintf(stderr, "amixos --smoke: MIC EQ rack item (removing it must switch the ten bands off, keep their gains and leave the channel EQ on)\n"); return 196; }
        engine.setChannelEq(4, 0, 0, 1000, 0, false); engine.setGraphicEq(4, QVariantList{0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, false); if (engine.channelEq(4)["on"].toBool()) { std::fprintf(stderr, "amixos --smoke: channel EQ flat\n"); return 197; } }
      { // PLUG-INS COME BACK: a VST3, a VST2 and a built-in effect with moved knobs and a bypass survive (1) save / load of the console file,
        // (2) the sound card being restarted and (3) the program being closed and started again
        QString k3, k2; for (const QVariant& v : engine.vstCatalog()) { const QVariantMap m = v.toMap(); const QString k = m["kind"].toString(); if (k3.isEmpty() && k.startsWith("vst3:")) k3 = k; if (k2.isEmpty() && k.startsWith("vst2:")) k2 = k; }
        const int S = 20; engine.setPlugin(S, 0, k3); engine.setPlugin(S, 1, k2); engine.setPlugin(S, 2, "fx:tubeamp");
        engine.setPluginParam(S, 0, engine.pluginParams(S, 0)[0].toMap()["id"].toInt(), 0.8); engine.setPluginParam(S, 1, engine.pluginParams(S, 1)[0].toMap()["id"].toInt(), 0.3);
        engine.setPluginParam(S, 2, engine.pluginParams(S, 2)[0].toMap()["id"].toInt(), 0.9); engine.setPluginBypass(S, 1, true);
        auto rackOk = [&](Engine& e, const char* when) -> bool {
          const QVariantList pl = e.plugins(S); const double want[3] = {0.8, 0.3, 0.9}; const QString kinds[3] = {k3, k2, QStringLiteral("fx:tubeamp")};
          for (int k = 0; k < 3; ++k) { const QVariantMap m = pl[k].toMap(); const QVariantList pp = e.pluginParams(S, k);
            if (m["kind"].toString() != kinds[k] || m["pending"].toBool() || pp.isEmpty() || std::fabs(pp[0].toMap()["value"].toDouble() - want[k]) > 0.02 || m["bypass"].toBool() != (k == 1)) {
              std::fprintf(stderr, "amixos --smoke: plug-in slot %d did not come back %s: kind %s pending %d value %.3f (want %.1f) bypass %d error '%s'\n", k + 1, when, qPrintable(m["kind"].toString().left(40)), int(m["pending"].toBool()), pp.isEmpty() ? -1.0 : pp[0].toMap()["value"].toDouble(), want[k], int(m["bypass"].toBool()), qPrintable(e.pluginError(S, k))); return false; } }
          return e.pluginCount(S) == 3; };
        if (k3.isEmpty() || k2.isEmpty() || !rackOk(engine, "right after insert")) return 199;
        const QString file = QDir::tempPath() + "/amixos-smoke-rack.json"; if (settings.exportConsole(file).isEmpty()) return 200;
        for (int k = 0; k < 3; ++k) engine.setPlugin(S, k, ""); if (engine.pluginCount(S) != 0 || !settings.importConsole(file) || !rackOk(engine, "after loading the console file")) return 201;
        engine.stop(); if (!engine.open("null", "", "", 48000, 256, 2, 2) || !rackOk(engine, "after the sound card restarted")) return 202;
        engine.saveNow(); { Engine again; if (!rackOk(again, "after closing and starting the program")) return 203; }
        { // a plug-in whose file is not there waits in its slot, keeps its settings, and comes back when the file does
          QString mod = k2.mid(5); const int bar = mod.indexOf('|'); if (bar > 0) mod = mod.left(bar); const QString away = mod + ".away";
          if (QFile::exists(mod) && QFile::rename(mod, away)) {
            engine.stop(); const bool opened = engine.open("null", "", "", 48000, 256, 2, 2); const QVariantMap waiting = engine.plugins(S)[1].toMap();
            const bool waited = opened && waiting["pending"].toBool() && waiting["kind"].toString() == k2; QFile::rename(away, mod);
            const int came = waited ? engine.retryPlugins() : -1; if (came < 1 || !rackOk(engine, "after its file came back")) { std::fprintf(stderr, "amixos --smoke: a missing plug-in did not wait and come back (waited %d, came back %d, error %s)\n", int(waited), came, qPrintable(engine.pluginError(S, 1))); return 204; } } }
        { QSettings().remove("console/state"); engine.setGain(S, -7.5); QElapsedTimer st; st.start(); while (st.elapsed() < 2600) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
          if (!QSettings().value("console/state").toString().contains("-7.5")) { std::fprintf(stderr, "amixos --smoke: the console was not written by itself after a change\n"); return 205; } engine.setGain(S, 0); }
        for (int k = 0; k < 3; ++k) engine.setPlugin(S, k, ""); engine.saveNow(); QFile::remove(file); }
      { // A MIXER ON ITS ASIO DRIVER: found by name in the device list; ASSIGN puts device input n on strip n and names the strips
        const QVariantList real = engine.devices(); QVariantList rig = real;
        { QVariantMap d; d["backend"] = "asio"; d["id"] = "X-USB ASIO Driver"; d["name"] = "X-USB ASIO Driver"; d["inChannels"] = 32; d["outChannels"] = 32; d["defaultRate"] = 48000.0; d["kind"] = "interface"; rig << d; }
        engine.simulateDevices(rig); const QVariantList mx = engine.mixersFound();
        if (mx.size() != 1 || mx[0].toMap()["maker"].toString() != "Behringer" || mx[0].toMap()["inChannels"].toInt() != 32 || mx[0].toMap()["running"].toBool()) { std::fprintf(stderr, "amixos --smoke: the mixer was not found in the device list\n"); return 206; }
        const QString n0 = engine.stripState()[0].toMap()["name"].toString(), n1 = engine.stripState()[1].toMap()["name"].toString(); engine.setInputRoute(0, 1); engine.setInputRoute(5, 0);
        const int got = engine.assignMixerChannels("X32", true);   // the null device has 2 inputs: strips 1-2 get them, the others none
        if (got != engine.deviceInputs() || engine.inputRoutes()[0].toInt() != 0 || engine.inputRoutes()[1].toInt() != 1 || engine.inputRoutes()[5].toInt() != -1 || engine.stripState()[1].toMap()["name"].toString() != "X32 02") { std::fprintf(stderr, "amixos --smoke: mixer channel assignment (%d)\n", got); return 207; }
        engine.resetRouting(); engine.setName(0, n0); engine.setName(1, n1); engine.simulateDevices(real); if (!engine.mixersFound().isEmpty()) return 208; }
      { // CLOSE NOISE: the gate lands 6 dB above what the channel carries now; a silent channel is left alone
        engine.setNullTestInput(true); engine.stop(); engine.open("null", "", "", 48000, 256, 2, 2); QElapsedTimer t; t.start(); while (t.elapsed() < 400) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);   // the null device now delivers a -12 dBFS tone on inputs 1-2
        engine.setGate(0, false, -50, -80, 1, 50, 100); const double thr = engine.closeNoise(0); const QVariantMap ch = engine.channelParams(0);
        if (thr > -1 || thr < -30.01 || !ch["gateOn"].toBool() || std::fabs(ch["gateThresholdDb"].toDouble() - thr) > 1e-6) { std::fprintf(stderr, "amixos --smoke: CLOSE NOISE did not set the gate (%.1f)\n", thr); return 209; }
        engine.setGate(0, false, -50, -80, 1, 50, 100); if (engine.closeNoise(30) != 0) { std::fprintf(stderr, "amixos --smoke: CLOSE NOISE on a silent channel must do nothing\n"); return 210; } engine.setNullTestInput(false); engine.stop(); engine.open("null", "", "", 48000, 256, 2, 2);
        if (!settings.setupDone()) { std::fprintf(stderr, "amixos --smoke: the factory setup must not be asked for without a factory reset\n"); return 211; } }
      { // MASTER MUTE comes back to where the fader was; LIMIT CHANNELS switches every channel limiter; AUTO -INF pulls down the strips without an input
        engine.setMaster(-4.5); engine.setMasterMuted(true); const bool down = engine.masterDb() <= -59.9; engine.setMasterMuted(false);
        if (!down || std::fabs(engine.masterDb() + 4.5) > 1e-6 || engine.masterMuted()) { std::fprintf(stderr, "amixos --smoke: master mute\n"); return 212; } engine.setMaster(0);
        engine.limitAllChannels(true); const bool allOn = engine.allChannelsLimited(); engine.limitAllChannels(false); if (!allOn || engine.allChannelsLimited() || engine.channelParams(7)["limiterOn"].toBool()) { std::fprintf(stderr, "amixos --smoke: limit channels\n"); return 213; }
        engine.resetRouting(); const int pulled = engine.infUnused();   // the null device has 2 inputs: strips 3-32 have none
        if (pulled != 30 || engine.stripState()[0].toMap()["db"].toDouble() < -100 || engine.stripState()[9].toMap()["db"].toDouble() > -59.9 || engine.infUnused() != 0) { std::fprintf(stderr, "amixos --smoke: AUTO -INF pulled %d\n", pulled); return 214; }
        for (int i = 2; i < 32; ++i) engine.setGain(i, 0);
        bool sync = false, fbk = false; for (const QVariant& v : engine.pluginCatalog()) { sync = sync || v.toMap()["kind"].toString() == "fx:syncdelay"; fbk = fbk || v.toMap()["kind"].toString() == "fx:antifeedback"; } if (!sync || !fbk) { std::fprintf(stderr, "amixos --smoke: SYNC DELAY / FEEDBACK KILLER are not in the rack list\n"); return 215; }
        engine.setPlugin(22, 0, "fx:antifeedback"); const QVariantList fp = engine.pluginParams(22, 0); const bool fbOk = fp.size() >= 4 && !engine.plugins(22)[0].toMap()["pending"].toBool() && engine.pluginParamText(22, 0, fp[3].toMap()["id"].toInt(), 1.0) == "LIVE"; engine.setPlugin(22, 0, "");
        if (!fbOk) { std::fprintf(stderr, "amixos --smoke: FEEDBACK KILLER did not load in a rack slot\n"); return 216; } }
      { // 360 SCENE: the cinema hall puts 10 fixed points and 10 travelling objects on strips 1-20; the travelling ones move by themselves; a turned head
        // shifts what is heard, not where the objects are; switching the hall off stops and clears it
        engine.setCinemaHall(true); const QVariantList sc0 = engine.spatialScene(); const double az10 = engine.spatial(10)["az"].toDouble(), az0 = engine.spatial(0)["az"].toDouble();
        if (sc0.size() != 20 || engine.spatialMoving() != 10 || !engine.spatial(19)["on"].toBool() || engine.busFx(15)["kind"].toString() != "hall") { std::fprintf(stderr, "amixos --smoke: cinema hall scene (%d objects, %d moving)\n", int(sc0.size()), engine.spatialMoving()); return 217; }
        QElapsedTimer ct; ct.start(); while (ct.elapsed() < 500) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (std::fabs(engine.spatial(10)["az"].toDouble() - az10) < 3 || std::fabs(engine.spatial(0)["az"].toDouble() - az0) > 1e-9) { std::fprintf(stderr, "amixos --smoke: the travelling objects did not move / a fixed point moved\n"); return 218; }
        engine.setHeadTrack(true); engine.setHeadYaw(90); const QVariantMap c = engine.spatialScene()[1].toMap();   // the centre point, head turned right: heard from the left
        if (std::fabs(c["az"].toDouble()) > 1e-9 || std::fabs(c["heardAz"].toDouble() + 90) > 1e-6) { std::fprintf(stderr, "amixos --smoke: head tracking (heard at %.1f)\n", c["heardAz"].toDouble()); return 219; }
        engine.setHeadYaw(0); engine.setHeadTrack(false); engine.setCinemaHall(false);
        if (engine.spatialMoving() != 0 || !engine.spatialScene().isEmpty() || engine.spatial(3)["on"].toBool()) { std::fprintf(stderr, "amixos --smoke: cinema hall off\n"); return 220; } engine.setBusFx(15, ""); }
      { // STEREO PAIR: the partner gets the same processing and rack, panned opposite; while linked, rack changes follow. AUTO SELECT narrows auto detect.
        const int A = 24, B = 25; engine.setLink(A, false); engine.setPan(A, -0.6); engine.setChannelEq(A, 3, -2, 1800, 1.5, true); engine.setCompressor(A, true, -22, 3, 6, 10, 120, 2); engine.setPlugin(A, 0, "fx:tubeamp");
        engine.setPluginParam(A, 0, engine.pluginParams(A, 0)[0].toMap()["id"].toInt(), 0.7); if (!engine.matchStereoPair(A)) return 221;
        const QVariantMap pb = engine.channelParams(B); const QVariantList rb = engine.plugins(B);
        if (std::fabs(engine.channelEq(B)["mid"].toDouble() + 2) > 1e-6 || !pb["compOn"].toBool() || std::fabs(pb["compThresholdDb"].toDouble() + 22) > 1e-6 || rb[0].toMap()["kind"].toString() != "fx:tubeamp" || std::fabs(engine.pluginParams(B, 0)[0].toMap()["value"].toDouble() - 0.7) > 0.02 || std::fabs(engine.stripState()[B].toMap()["pan"].toDouble() - 0.6) > 1e-6) { std::fprintf(stderr, "amixos --smoke: stereo pair match\n"); return 222; }
        engine.setLink(A, true); engine.setPlugin(A, 1, "fx:syncdelay"); engine.setPluginBypass(A, 0, true); engine.setPluginParam(A, 1, engine.pluginParams(A, 1)[2].toMap()["id"].toInt(), 0.9);
        if (engine.plugins(B)[1].toMap()["kind"].toString() != "fx:syncdelay" || !engine.plugins(B)[0].toMap()["bypass"].toBool() || std::fabs(engine.pluginParams(B, 1)[2].toMap()["value"].toDouble() - 0.9) > 0.02) { std::fprintf(stderr, "amixos --smoke: a linked pair did not keep the same rack\n"); return 223; }
        engine.setPlugin(A, 0, ""); engine.setPlugin(A, 1, ""); if (engine.pluginCount(B) != 0) { std::fprintf(stderr, "amixos --smoke: removing a plug-in on a linked pair\n"); return 224; }
        engine.setLink(A, false); engine.resetStrip(A); engine.resetStrip(B);
        const QVariantList real = engine.devices(); QVariantList rig;
        for (const auto& [bk, id, nm, kind] : {std::tuple<const char*, const char*, const char*, const char*>{"asio", "Focusrite USB ASIO", "Focusrite USB ASIO", "interface"}, {"wasapi", "{w-out}", "Speakers (Realtek High Definition Audio)", "speakers"}, {"dsound", "{d-out}", "Speakers (USB Audio)", "speakers"}, {"dsound", "{bt}", "Headphones (WH-1000XM4 Stereo)", "bluetooth"}}) { QVariantMap d; d["backend"] = bk; d["id"] = id; d["name"] = nm; d["kind"] = kind; d["inChannels"] = QString::fromLatin1(bk) == "asio" ? 2 : 0; d["outChannels"] = 2; d["defaultOut"] = QString::fromLatin1(bk) == "wasapi"; d["defaultRate"] = 48000.0; rig << d; }
        engine.simulateDevices(rig); const QString keep = engine.autoPrefer(); QStringList got;
        for (const char* pf : {"best", "asio", "wasapi", "dsound", "bluetooth"}) { engine.setAutoPrefer(pf); got << engine.autoDetect()["id"].toString(); }
        rig.removeLast(); engine.simulateDevices(rig); engine.setAutoPrefer("bluetooth"); const QVariantMap fb = engine.autoDetect(); engine.setAutoPrefer(keep); engine.simulateDevices(real);
        if (got != QStringList{"Focusrite USB ASIO", "Focusrite USB ASIO", "{w-out}", "{d-out}", "{bt}"} || fb["id"].toString() != "Focusrite USB ASIO" || !fb["why"].toString().contains("no BLUETOOTH device")) { std::fprintf(stderr, "amixos --smoke: auto select picked %s / fallback %s\n", qPrintable(got.join(", ")), qPrintable(fb["id"].toString())); return 225; } }
      { // VST FOLDER: the console's own plug-in folder is created and saved among the scanned folders, once
        const QStringList before = settings.vstDirs(); const QString vf = settings.vstUserFolder(); settings.vstUserFolder();
        if (vf.isEmpty() || !QDir(vf).exists() || settings.vstDirs().count(vf) != 1 || QSettings().value("vst3/dirs").toStringList().count(vf) != 1) { std::fprintf(stderr, "amixos --smoke: the VST folder was not created and saved\n"); return 198; }
        if (!before.contains(vf)) settings.removeVstDir(vf); }
      { // MIC SWEEP: the measured strip is muted in the mix while the sweep plays and comes back afterwards; the recording is described (level, noise, input)
        engine.setMute(0, false); if (!measure.runSweep(0, 0.5, 100, 8000, -12, 6) || !engine.stripState()[0].toMap()["mute"].toBool()) { std::fprintf(stderr, "amixos --smoke: the sweep did not start or the measured strip was not muted\n"); return 192; }
        QElapsedTimer mt; mt.start(); while (measure.state() == "measuring" && mt.elapsed() < 8000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        const QVariantMap mi = measure.info();
        if (measure.state() != "done" || engine.stripState()[0].toMap()["mute"].toBool() || mi["lines"].toStringList().size() < 2 || !mi.contains("snrDb") || mi["input"].toString() != "input 1") { std::fprintf(stderr, "amixos --smoke: sweep info (%s, %d lines)\n", qPrintable(measure.state()), int(mi["lines"].toStringList().size())); return 193; }
        if (!engine.openRouting()) { std::fprintf(stderr, "amixos --smoke: open ASIO routing must be on by default\n"); return 194; } }
      { // NETWORK AUDIO + MIDI: everything off at start; a stream to this machine comes back on a network input and reaches a strip; MIDI moves a fader and learns a control
        if (net.txOn() || net.rxOn() || net.midiNetOn() || net.discoverOn()) { std::fprintf(stderr, "amixos --smoke: network audio must be off until asked\n"); return 181; }
        if (net.transports().size() < 5 || !net.transports()[0].toMap()["available"].toBool()) { std::fprintf(stderr, "amixos --smoke: transports list\n"); return 182; }
        const int keepFirst = net.rxFirstStrip(); const QString keepRx = net.rxAddress(), keepTx = net.txAddress(); const int keepRxPort = net.rxPort(), keepTxPort = net.txPort();
        net.setRxAddress("127.0.0.1"); net.setRxPort(47420); net.setRxChannels(2); net.setRxBits(24); net.setRxRate(48000); net.setRxLatency(20); net.setTxAddress("127.0.0.1"); net.setTxPort(47420); net.setTxChannels(2); net.setTxBits(24);
        if (!net.startReceive() || !net.startSend()) { std::fprintf(stderr, "amixos --smoke: network stream start: %s\n", qPrintable(net.message())); return 183; }
        net.setRxFirstStrip(7); if (engine.inputRoutes()[6].toInt() != 256 || engine.inputRoutes()[7].toInt() != 257) { std::fprintf(stderr, "amixos --smoke: network inputs not patched\n"); return 184; }
        QElapsedTimer nt; nt.start(); while (nt.elapsed() < 600) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        const QVariantMap ns = net.stats();
        if (ns["txPackets"].toULongLong() < 200 || ns["rxPackets"].toULongLong() < 200 || ns["rxLost"].toULongLong() != 0 || !ns["rxLive"].toBool()) { std::fprintf(stderr, "amixos --smoke: network stream did not flow (tx %llu rx %llu)\n", ns["txPackets"].toULongLong(), ns["rxPackets"].toULongLong()); return 185; }
        net.stopReceive(); net.stopSend(); if (net.txOn() || net.rxOn() || engine.inputRoutes()[6].toInt() != 6) { std::fprintf(stderr, "amixos --smoke: network stream stop / strips back to their inputs\n"); return 186; }
        net.setRxFirstStrip(keepFirst); net.setRxAddress(keepRx); net.setTxAddress(keepTx); net.setRxPort(keepRxPort); net.setTxPort(keepTxPort);
        const double before = engine.stripState()[2].toMap()["db"].toDouble(); net.midiIn(0xb2, 7, 100); if (std::fabs(engine.stripState()[2].toMap()["db"].toDouble()) > 1e-6) { std::fprintf(stderr, "amixos --smoke: MIDI fader\n"); return 187; }
        const bool m0 = engine.stripState()[5].toMap()["mute"].toBool(); net.midiIn(0x90, 5, 100); if (engine.stripState()[5].toMap()["mute"].toBool() == m0) { std::fprintf(stderr, "amixos --smoke: MIDI mute\n"); return 188; } net.midiIn(0x90, 5, 100);
        const QString keepMap = QSettings().value("net/midiMap").toString(); net.learn("fader", 3); net.midiIn(0xb0, 44, 1); net.midiIn(0xb0, 44, 127);
        if (net.midiLearning() || net.midiLearned() < 1 || std::fabs(engine.stripState()[3].toMap()["db"].toDouble() - 10) > 1e-6) { std::fprintf(stderr, "amixos --smoke: MIDI learn\n"); return 189; }
        net.clearLearned(); QSettings().setValue("net/midiMap", keepMap); engine.setGain(2, before); engine.setGain(3, 0);
        if (!net.setMidiNet(true)) { net.setMidiNetPort(47430); if (!net.setMidiNet(true)) { std::fprintf(stderr, "amixos --smoke: network MIDI: %s\n", qPrintable(net.message())); return 190; } } net.setMidiNet(false); }
      // NETWORK: the interface list holds the loopback card; the allowlist refuses a foreign address and admits a listed one
      bool loop = false; for (const QVariant& v : osc.interfaces()) if (v.toMap()["kind"] == "loopback") loop = true; if (!loop) { std::fprintf(stderr, "amixos --smoke: network interfaces\n"); return 127; }
      osc.setAllowedIps("10.1.2.3, 192.168.5.0/24"); if (osc.ipAllowed(QHostAddress("10.9.9.9")) || !osc.ipAllowed(QHostAddress("10.1.2.3")) || !osc.ipAllowed(QHostAddress("192.168.5.77")) || !osc.ipAllowed(QHostAddress::LocalHost)) { std::fprintf(stderr, "amixos --smoke: osc allowed list\n"); return 128; } osc.setAllowedIps("");
      if (osc.remoteAddress() != "127.0.0.1:" + QString::number(osc.port())) { std::fprintf(stderr, "amixos --smoke: osc remote address %s\n", qPrintable(osc.remoteAddress())); return 129; }
      // AUTO LICENCE: a request file is written; a key file holding a bad key is seen and refused (never activated); the vendor's real key would be accepted the same way
      const QString req = engine.writeLicenseRequest("pro", "Smoke", "smoke@example.invalid"); if (req.isEmpty() || !QFileInfo::exists(req)) { std::fprintf(stderr, "amixos --smoke: key request file\n"); return 130; }
      { QFile kf(QString::fromStdString(am::paths::dataDir()) + "/smoke.amixkey"); kf.open(QIODevice::WriteOnly); kf.write("AMIX1.not-a-real-key.xx"); kf.close(); engine.setAutoLicense(true); const int tried = engine.scanForKeys(); kf.remove(); QFile::remove(req);
        if (tried < 1 || engine.license()["source"].toString() == "key") { std::fprintf(stderr, "amixos --smoke: auto licence scan (%d tried, %s)\n", tried, qPrintable(engine.autoLicenseStatus())); return 131; } } }
    { update.setUrl("http://example.com/update.json"); if (update.state() != "error") { std::fprintf(stderr, "amixos --smoke: a non-project update URL was accepted\n"); return 123; }
      QFile mf(QCoreApplication::applicationDirPath() + "/../../releases/update.json"); if (!mf.exists()) mf.setFileName(QStringLiteral(AMIX_SOURCE_DIR) + "/../../releases/update.json");
      if (mf.open(QIODevice::ReadOnly)) { const QString env = QString::fromUtf8(mf.readAll());
        // releases/update.json is still signed by the previous vendor key (re-sign it with scripts/make-update.js): the console must say so, not accept it
        const bool ok = update.applyManifest(env); if (ok && update.state() != "available" && update.state() != "upToDate") { std::fprintf(stderr, "amixos --smoke: manifest accepted but state %s\n", qPrintable(update.state())); return 120; }
        if (!ok && (update.state() != "error" || !update.message().contains("signature"))) { std::fprintf(stderr, "amixos --smoke: manifest rejected for the wrong reason: %s\n", qPrintable(update.message())); return 120; }
        std::printf("amixos --smoke: manifest %s (%s)\n", ok ? "verified" : "rejected", qPrintable(update.message()));
        QString bad = env; bad.replace("\"version\":\"", "\"version\":\"9"); if (update.applyManifest(bad) || update.state() != "error") { std::fprintf(stderr, "amixos --smoke: a tampered manifest was accepted\n"); return 121; }
        if (Update::compareVersions("1.2.10", "1.2.9") <= 0 || Update::compareVersions("0.1.0", "0.1.0") != 0) { std::fprintf(stderr, "amixos --smoke: version compare\n"); return 122; } }
      else std::printf("amixos --smoke: releases/update.json not found next to the build; manifest check skipped\n");
      const bool wasDone = settings.setupDone(); settings.rerunSetup(); if (settings.setupDone()) { std::fprintf(stderr, "amixos --smoke: rerunSetup\n"); return 124; } settings.finishSetup(); if (!settings.setupDone()) { std::fprintf(stderr, "amixos --smoke: finishSetup\n"); return 125; } (void)wasDone; }
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
    { if (!access.can("faders") || !access.canPage("admin")) { std::fprintf(stderr, "amixos --smoke: FOH MASTER must see everything\n"); return 90; }
      access.lock(); if (!access.surfaceLocked() || access.can("faders")) { std::fprintf(stderr, "amixos --smoke: surface lock\n"); return 91; }
      if (access.unlock("0000") || !access.unlock(access.pinIsDefault() ? "1234" : "1234")) { std::fprintf(stderr, "amixos --smoke: PIN check (default 1234 expected on a fresh profile)\n"); return 92; }
      if (!access.setRole("guest", "") || access.canPage("routing") || access.can("dsp") || !access.can("faders")) { std::fprintf(stderr, "amixos --smoke: GUEST gating\n"); return 93; }
      if (access.setRole("master", "9999") || !access.setRole("master", "1234")) { std::fprintf(stderr, "amixos --smoke: switching up needs the PIN\n"); return 94; }
      access.setPerm("guest", "dsp", true); if (access.matrix()["guest"].toMap()["dsp"].toBool()) { std::fprintf(stderr, "amixos --smoke: matrix edit without ADMIN mode must fail\n"); return 95; }
      if (!access.enterAdmin("1234")) { std::fprintf(stderr, "amixos --smoke: admin PIN\n"); return 96; } access.setPerm("guest", "dsp", true); if (!access.matrix()["guest"].toMap()["dsp"].toBool()) { std::fprintf(stderr, "amixos --smoke: matrix edit\n"); return 97; } access.resetMatrix(); access.leaveAdmin();
      if (access.audit().size() < 5) { std::fprintf(stderr, "amixos --smoke: audit log\n"); return 98; } }
    { engine.setAutomix(0, true, 2.0); engine.setAutomix(1, true, 1.0); if (!engine.stripState()[0].toMap()["automix"].toBool() || std::fabs(engine.stripState()[0].toMap()["amWeight"].toDouble() - 2.0) > 1e-9) { std::fprintf(stderr, "amixos --smoke: automix state\n"); return 100; }
      engine.setAutoLevelOn(true); engine.setAutoLevelTarget(-18); if (!engine.autoLevelOn() || std::fabs(engine.autoLevelTarget() + 18) > 1e-9) { std::fprintf(stderr, "amixos --smoke: auto level props\n"); return 101; }
      waitMs(300); if (!engine.meters()[0].toMap().contains("amGainDb") || !engine.telemetry().contains("autoLevelDb")) { std::fprintf(stderr, "amixos --smoke: automix / auto level telemetry\n"); return 102; }
      engine.setAutoLevelOn(false); engine.setAutomix(0, false, 1); engine.setAutomix(1, false, 1);
      // OSC: a /ch/5/fader -12 packet through the parser, then one over UDP to the server on 127.0.0.1
      auto pkt = [](const QByteArray& addr, float v) { QByteArray o = addr; o.append('\0'); while (o.size() % 4) o.append('\0'); o += QByteArray(",f\0\0", 4); quint32 b; std::memcpy(&b, &v, 4); char be[4]; qToBigEndian(b, be); o.append(be, 4); return o; };
      if (!osc.handle(pkt("/ch/5/fader", -12.f)) || std::fabs(engine.stripState()[4].toMap()["db"].toDouble() + 12) > 1e-6) { std::fprintf(stderr, "amixos --smoke: OSC parse\n"); return 103; }
      osc.setAllowLan(false); osc.setPort(18000); osc.setEnabled(true); if (!osc.enabled()) { std::fprintf(stderr, "amixos --smoke: OSC bind: %s\n", qPrintable(osc.status())); return 104; }
      QUdpSocket client; client.writeDatagram(pkt("/ch/6/fader", -7.f), QHostAddress::LocalHost, 18000); waitMs(300);
      if (std::fabs(engine.stripState()[5].toMap()["db"].toDouble() + 7) > 1e-6 || osc.rx() < 1) { std::fprintf(stderr, "amixos --smoke: OSC over UDP did not arrive (rx %d)\n", osc.rx()); return 105; }
      osc.setEnabled(false); engine.setGain(4, 0); engine.setGain(5, 0); }
    engine.setInputRoute(3, 0); engine.setOutputRoute(0, 1);   // the routing matrix: CH 4 reads input 1, OUT 1 carries MASTER L
    if (engine.inputRoutes()[3].toInt() != 0 || engine.outputRoutes()[0].toInt() != 1) { std::fprintf(stderr, "amixos --smoke: routing did not take\n"); return 11; }
    engine.resetRouting(); if (engine.inputRoutes()[3].toInt() != 3 || engine.outputRoutes()[0].toInt() != 3) { std::fprintf(stderr, "amixos --smoke: routing defaults wrong\n"); return 12; }
    QTimer::singleShot(1200, &app, [&] {
      if (!shots.isEmpty()) {   // one picture per ported page
        QDir().mkpath(shots);
        auto* win = qobject_cast<QQuickWindow*>(qml.rootObjects().first());
        engine.setNullTestInput(true); engine.open("null", "", "", 48000, 256, 2, 2); waitMs(300);   // the null device with the -12 dBFS test input: meters, fader IN / OUT columns and VU faces show a signal on the shots
        { const QVariantList mm = engine.meters(); const double in0 = mm[0].toMap()["inDb"].toDouble(), out0 = mm[0].toMap()["peakDb"].toDouble(), in2 = mm[2].toMap()["inDb"].toDouble();
          if (std::fabs(in0 + 12.0) > 1.0 || std::fabs(out0 - (in0 + engine.stripState()[0].toMap()["db"].toDouble())) > 1.5 || in2 > -100) { std::fprintf(stderr, "amixos --shots: real-time meters wrong: strip 1 in %.1f (want -12) out %.1f (want in + fader %.1f), strip 3 in %.1f (want silence)\n", in0, out0, engine.stripState()[0].toMap()["db"].toDouble(), in2); QCoreApplication::exit(9); return; }
          const int was2 = engine.inputRoutes()[2].toInt(); engine.setInputRoute(2, 0); waitMs(250); const double in2b = engine.meters()[2].toMap()["inDb"].toDouble(); engine.setInputRoute(2, was2); if (std::fabs(in2b + 12.0) > 1.0) { std::fprintf(stderr, "amixos --shots: input routing: strip 3 on input 1 reads %.1f dBFS (want -12)\n", in2b); QCoreApplication::exit(9); return; }
          if (engine.inputLabel(0).isEmpty() || !engine.inputLabel(0).endsWith("IN 1")) { std::fprintf(stderr, "amixos --shots: input label %s\n", qPrintable(engine.inputLabel(0))); QCoreApplication::exit(9); return; }
          std::printf("amixos --shots: real-time meters OK: in1 %.1f dBFS, out1 %.1f dBFS (fader %.1f), strip 3 silent then %.1f on input 1; label \"%s\"\n", in0, out0, engine.stripState()[0].toMap()["db"].toDouble(), in2b, qPrintable(engine.inputLabel(0))); }
        if (engine.meters().size() > 0 && engine.meters()[0].toMap()["inDb"].toDouble() < -20) { std::fprintf(stderr, "amixos --shots: the null test input is not reaching the strips (%.1f dBFS)\n", engine.meters()[0].toMap()["inDb"].toDouble()); QCoreApplication::exit(8); return; }
        if (QObject* wiz = win->findChild<QObject*>("setupPage")) {   // the factory setup, every step
          win->setProperty("forceSetup", true);
          wiz->setProperty("step", 1); wiz->setProperty("termsAlert", true); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50); win->grabWindow().save(shots + QStringLiteral("/setup-2-alert.png")); wiz->setProperty("termsAlert", false);
          for (int step = 0; step < 7; ++step) { wiz->setProperty("step", step); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50); win->grabWindow().save(shots + QStringLiteral("/setup-%1.png").arg(step + 1)); std::printf("amixos --shots: setup-%d\n", step + 1); }
          win->setProperty("forceSetup", false); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
          { win->setProperty("current", "mixer"); for (int k = 0; k < 10; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);   // the plug-in editor on a tube amp and on the hall reverb
            if (QObject* edw = win->findChild<QObject*>("plugEd")) { for (const auto& [kind, name, file] : {std::tuple<const char*, const char*, const char*>{"fx:tubeamp", "TUBE AMP", "plugin-editor-tubeamp.png"}, {"fx:hall", "HALL REVERB", "plugin-editor-hall.png"}, {"fx:reverb", "PLATE REVERB", "plugin-editor-plate.png"}}) {
                engine.setPlugin(0, 8, kind); QMetaObject::invokeMethod(edw, "openFor", Q_ARG(QVariant, 0), Q_ARG(QVariant, 8), Q_ARG(QVariant, QString::fromLatin1(kind)), Q_ARG(QVariant, QString::fromLatin1(name)));
                { QElapsedTimer bt; bt.start(); while (bt.elapsed() < 500) QCoreApplication::processEvents(QEventLoop::AllEvents, 20); } win->grabWindow().save(shots + "/" + file); QMetaObject::invokeMethod(edw, "close"); }
              engine.setPlugin(0, 8, ""); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50); }
            else { std::fprintf(stderr, "amixos --shots: the plug-in editor was not found\n"); } }
          win->setProperty("bootOn", true); { QElapsedTimer bt; bt.start(); while (bt.elapsed() < 1700) QCoreApplication::processEvents(QEventLoop::AllEvents, 20); } win->grabWindow().save(shots + QStringLiteral("/boot-splash.png")); win->setProperty("bootOn", false); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        for (const char* id : {"home", "mixer", "dsp", "dyn", "gate", "bus", "dca", "scenes", "presets", "routing", "renew", "arrange", "io", "meter", "lufs", "sys", "fx", "osc", "phase", "acoustics", "surround", "master", "rtaeq", "miceq", "live", "tools", "calib", "boot", "help", "acct", "access", "admin", "ai", "plugins", "network", "media", "asio", "samples", "rtaeq", "bluetooth"}) {
          if (QString::fromLatin1(id) == "rtaeq") { QVariantList golden; for (double g : {1.0, 5.0, 3.0, 3.0, 4.0, 2.0, 2.0, 3.0, 3.0, 1.0}) golden << g; engine.setGraphicEq(0, golden, true); }   // the beta's Golden EQ on the shot
          win->setProperty("current", QString::fromLatin1(id)); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
          const QImage img = win->grabWindow(); img.save(shots + "/" + id + ".png"); std::printf("amixos --shots: %s %dx%d\n", id, img.width(), img.height());
          if (QString::fromLatin1(id) == "admin") {   // the same page with the PIN entered
            access.enterAdmin("1234"); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50); win->grabWindow().save(shots + "/admin-open.png"); access.leaveAdmin(); std::printf("amixos --shots: admin-open\n"); }
          if (QString::fromLatin1(id) == "mixer") {   // FILE / VIEW / INFO and the About box
            { QVariant r; QMetaObject::invokeMethod(win, "menuSelfTest", Q_RETURN_ARG(QVariant, r)); const QStringList badMenu = r.toStringList(); waitMs(200);
              if (!badMenu.isEmpty()) std::fprintf(stderr, "amixos --shots: FAILED menu entries: %s\n", qPrintable(badMenu.join(" | "))); else std::printf("amixos --shots: every FILE / EDIT / VIEW / TOOLS / HELP entry does its job\n");
              if (!win->findChild<QObject*>("menuAbout") || !win->findChild<QObject*>("menuEdit") || !win->findChild<QObject*>("menuTools")) std::fprintf(stderr, "amixos --shots: FAILED the EDIT / TOOLS / ABOUT buttons are missing\n");
              win->setProperty("current", QStringLiteral("mixer")); waitMs(200); }
            if (QObject* ve = win->findChild<QObject*>("volumeEnhancer")) {   // AMFB VOLUME ENHANCER: a preset loaded through its own button path, then the panel on the shot
              const QVariantMap keepM = engine.mastering(), keepL = engine.masterLimiter(); const double keepW = engine.width();
              QMetaObject::invokeMethod(ve, "open"); QMetaObject::invokeMethod(ve, "load", Q_ARG(QVariant, 2)); waitMs(500);
              const QVariantMap mm = engine.mastering();
              if (!mm["on"].toBool() || std::fabs(mm["gain"].toDouble() - 6) > 1e-6 || std::fabs(mm["mid"].toDouble() + 0.5) > 1e-6 || std::fabs(mm["compMix"].toDouble() - 0.7) > 1e-6 || !mm["bassMonoOn"].toBool() || !engine.masterLimiter()["on"].toBool() || std::fabs(engine.width() - 1.1) > 1e-6) std::fprintf(stderr, "amixos --shots: FAILED the volume enhancer preset did not reach the master chain\n");
              win->grabWindow().save(shots + "/volume-enhancer.png"); std::printf("amixos --shots: volume-enhancer\n");
              QMetaObject::invokeMethod(ve, "load", Q_ARG(QVariant, 0)); QMetaObject::invokeMethod(ve, "close"); QVariantMap back = keepM; engine.setMastering(back); engine.setMasterLimiter(keepL["on"].toBool(), keepL["ceiling"].toDouble()); engine.setWidth(keepW); waitMs(200);
            } else std::fprintf(stderr, "amixos --shots: FAILED the volume enhancer is missing\n");
            if (QObject* ma = win->findChild<QObject*>("masterAnalyzer")) { QMetaObject::invokeMethod(ma, "open"); waitMs(2600);
              if (ma->property("history").toList().size() < 6 || ma->property("columns").toList().size() < 10) std::fprintf(stderr, "amixos --shots: FAILED the master analyzer collected no history / spectrogram\n");
              ma->setProperty("targetIndex", 3); waitMs(300); win->grabWindow().save(shots + "/master-analyzer.png"); std::printf("amixos --shots: master-analyzer\n");
              QMetaObject::invokeMethod(ma, "clearAll"); if (!ma->property("history").toList().isEmpty()) std::fprintf(stderr, "amixos --shots: FAILED analyzer CLEAR\n"); ma->setProperty("targetIndex", 0); QMetaObject::invokeMethod(ma, "close"); waitMs(200);
            } else std::fprintf(stderr, "amixos --shots: FAILED the master analyzer is missing\n");
            if (!win->findChild<QObject*>("menuFile") || !win->findChild<QObject*>("menuView") || !win->findChild<QObject*>("menuInfo")) std::fprintf(stderr, "amixos --shots: FAILED the FILE / VIEW / INFO menus are missing\n");
            if (QObject* ab = win->findChild<QObject*>("aboutPop")) { QMetaObject::invokeMethod(ab, "open"); waitMs(400); win->grabWindow().save(shots + "/menu-about.png"); QMetaObject::invokeMethod(ab, "close"); waitMs(200); std::printf("amixos --shots: menu-about\n"); } }
          if (QString::fromLatin1(id) == "acoustics") { measure.runSweep(0, 0.5, 100, 8000, -12, 12); QElapsedTimer mt; mt.start(); while (measure.state() == "measuring" && mt.elapsed() < 8000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20); waitMs(300); win->grabWindow().save(shots + "/acoustics-sweep-info.png"); std::printf("amixos --shots: acoustics-sweep-info\n"); }
          if (QString::fromLatin1(id) == "dsp") { engine.setChannelEq(engine.selectedStrip(), 4.0, -3.0, 2500.0, 2.0, true); waitMs(300); win->grabWindow().save(shots + "/channel-eq-3band.png"); engine.setChannelEq(engine.selectedStrip(), 0, 0, 1000, 0, false); std::printf("amixos --shots: channel-eq-3band\n");
            if (!win->findChild<QObject*>("channelEq3")) std::fprintf(stderr, "amixos --shots: FAILED the 3-band channel EQ is missing\n");
            if (QObject* tp = win->findChild<QObject*>("tonePage")) { engine.setGraphicEq(engine.selectedStrip(), QVariantList{3.0, 0.0, -2.0, 0.0, 0.0, 4.0, 0.0, 0.0, -3.0, 1.0}, true); tp->setProperty("graphic", true); waitMs(400); win->grabWindow().save(shots + "/tone-mic-eq-faders.png"); tp->setProperty("graphic", false); engine.setGraphicEq(engine.selectedStrip(), QVariantList{0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, false); std::printf("amixos --shots: tone-mic-eq-faders\n"); } }
          if (QString::fromLatin1(id) == "surround") { engine.setCinemaHall(true); engine.setHeadTrack(true); engine.setHeadYaw(35); waitMs(700); win->grabWindow().save(shots + "/360-cinema-hall.png"); std::printf("amixos --shots: 360-cinema-hall\n");
            if (!win->findChild<QObject*>("sceneMap")) std::fprintf(stderr, "amixos --shots: FAILED the 360 scene map is missing\n"); engine.setHeadYaw(0); engine.setHeadTrack(false); engine.setCinemaHall(false); engine.setBusFx(15, ""); }
          if (QString::fromLatin1(id) == "rtaeq") { QObject* rp = nullptr; int shown = 0; for (QObject* o : win->findChildren<QObject*>("rtaEqPage")) if (o->property("visible").toBool()) { rp = o; ++shown; }   // the page on screen (an earlier one may still be on its way out)
            if (shown != 1) std::fprintf(stderr, "amixos --shots: FAILED %d RTA EQ pages on screen\n", shown); if (rp) {   // the sliders of the MASTER EQ: after a move the page shows the new value (it used to snap back)
            const QVariantList keep = rp->property("gains").toList(); QMetaObject::invokeMethod(rp, "setBand", Q_ARG(QVariant, 3), Q_ARG(QVariant, 6.0)); waitMs(120);
            const double g3 = rp->property("gains").toList().value(3).toDouble(), now = rp->property("bandNow").toMap()["gainDb"].toDouble();
            QMetaObject::invokeMethod(rp, "setParam", Q_ARG(QVariant, 1), Q_ARG(QVariant, 250.0), Q_ARG(QVariant, 2.5), Q_ARG(QVariant, -4.0)); waitMs(120);
            const QVariantMap bn = rp->property("bandNow").toMap(); const double g3b = rp->property("gains").toList().value(3).toDouble();
            if (!rp->property("program").toBool() || std::fabs(g3 - 6) > 1e-6 || std::fabs(now - 6) > 1e-6 || std::fabs(bn["gainDb"].toDouble() + 4) > 1e-6 || std::fabs(bn["q"].toDouble() - 2.5) > 1e-6 || std::fabs(g3b + 4) > 1e-6 || std::fabs(engine.eqBand(-1, 3)["gainDb"].toDouble() + 4) > 1e-6)
              std::fprintf(stderr, "amixos --shots: FAILED the RTA EQ sliders do not follow on the master EQ (fader %.1f, band %.1f, then %.1f q %.2f; program %d strip %d band %d engine %.1f visible %d)\n", g3, now, bn["gainDb"].toDouble(), bn["q"].toDouble(), int(rp->property("program").toBool()), rp->property("strip").toInt(), rp->property("band").toInt(), engine.eqBand(-1, 3)["gainDb"].toDouble(), int(rp->property("visible").toBool()));
            else std::printf("amixos --shots: RTA EQ sliders follow on the master EQ\n");
            QMetaObject::invokeMethod(rp, "apply", Q_ARG(QVariant, QVariant(keep))); waitMs(80); } }
          if (QString::fromLatin1(id) == "network") { if (QObject* np = win->findChild<QObject*>("networkPage")) {   // the AUDIO + MIDI view with a stream running to this machine and back
            const QString rxA = net.rxAddress(), txA = net.txAddress(); const int rxP = net.rxPort(), txP = net.txPort(), first = net.rxFirstStrip();
            net.setRxAddress("127.0.0.1"); net.setRxPort(47422); net.setTxAddress("127.0.0.1"); net.setTxPort(47422); net.startReceive(); net.startSend(); net.setRxFirstStrip(7); net.midiIn(0xb1, 7, 96);
            np->setProperty("view", 1); waitMs(900); win->grabWindow().save(shots + "/network-audio-midi.png"); std::printf("amixos --shots: network-audio-midi\n");
            if (!win->findChild<QObject*>("aoipPanel")) { std::fprintf(stderr, "amixos --shots: FAILED the AUDIO + MIDI panel is missing\n"); }
            net.stopSend(); net.stopReceive(); net.setRxFirstStrip(first); net.setRxAddress(rxA); net.setTxAddress(txA); net.setRxPort(rxP); net.setTxPort(txP); engine.setGain(1, 0); np->setProperty("view", 0); } }
          if (QString::fromLatin1(id) == "fx") { if (QObject* fp = win->findChild<QObject*>("effectsPage")) { fp->setProperty("browse", true); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50); win->grabWindow().save(shots + "/fx-presets.png"); fp->setProperty("browse", false); std::printf("amixos --shots: fx-presets\n"); } }
          if (QString::fromLatin1(id) == "dsp") { if (QObject* tp = win->findChild<QObject*>("tonePage")) { tp->setProperty("rta", true); waitMs(500); win->grabWindow().save(shots + "/tone-rta.png"); tp->setProperty("rta", false);
            engine.applyEqPreset(1, 0); tp->setProperty("contentY", 400); waitMs(300); win->grabWindow().save(shots + "/tone-presets.png"); tp->setProperty("contentY", 0); for (int b = 0; b < 10; ++b) engine.setEq(0, b, 0, 1000, 1, 0); engine.setCrossover(0, 0, 0, 80, 0, 16000); std::printf("amixos --shots: tone-rta, tone-presets\n"); } }
          if (QString::fromLatin1(id) == "rtaeq") { if (QObject* rp = win->findChild<QObject*>("rtaEqPage")) { for (const char* v : {"faders", "both"}) { rp->setProperty("view", QString::fromLatin1(v)); waitMs(250); win->grabWindow().save(shots + "/rtaeq-" + v + ".png"); } rp->setProperty("view", "diamond"); std::printf("amixos --shots: rtaeq-faders, rtaeq-both\n"); } engine.setGraphicEq(0, QVariantList(), false); }
          if (QString::fromLatin1(id) == "routing") {
            if (QObject* rp = win->findChild<QObject*>("routingPage")) { rp->setProperty("tab", 1); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50); win->grabWindow().save(shots + "/routing-outputs.png"); std::printf("amixos --shots: routing-outputs\n"); }
          }
          if (QString::fromLatin1(id) == "mixer") {   // the same page with the analogue VU faces
            if (QObject* mixer = win->findChild<QObject*>("mixerPage")) {
              mixer->setProperty("rackCompact", true); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50); win->grabWindow().save(shots + "/mixer-compact.png"); mixer->setProperty("rackCompact", false); std::printf("amixos --shots: mixer-compact\n");
              if (QObject* pop = mixer->findChild<QObject*>("mixerQuick")) { QMetaObject::invokeMethod(pop, "open"); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50); win->grabWindow().save(shots + "/mixer-settings.png"); QMetaObject::invokeMethod(pop, "close"); std::printf("amixos --shots: mixer-settings\n"); }
              mixer->setProperty("sendView", 1); waitMs(400); win->grabWindow().save(shots + "/mixer-aux.png"); mixer->setProperty("sendView", 2); waitMs(400); win->grabWindow().save(shots + "/mixer-fx.png"); mixer->setProperty("sendView", 0); std::printf("amixos --shots: mixer-aux, mixer-fx\n");
              mixer->setProperty("vuMode", true); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50); win->grabWindow().save(shots + "/mixer-vu.png"); mixer->setProperty("vuMode", false); std::printf("amixos --shots: mixer-vu\n"); }
          }
        }
      }
      if (!shots.isEmpty()) {   // the light theme on three pages
        auto* lw = qobject_cast<QQuickWindow*>(qml.rootObjects().first()); settings.setDarkTheme(false); for (const char* id : {"home", "mixer", "dsp"}) { lw->setProperty("current", QString::fromLatin1(id)); waitMs(350); lw->grabWindow().save(shots + "/" + id + "-light.png"); } settings.setDarkTheme(true); waitMs(200); std::printf("amixos --shots: home-light, mixer-light, dsp-light\n"); }
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
