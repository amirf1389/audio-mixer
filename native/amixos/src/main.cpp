#include <tuple>
#include <functional>
#include <memory>
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
#include <QQmlIncubationController>
#include "fx/fx_plugin.hpp"
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include "AutoZoom.h"
#include <QQuickItem>
#include <QPalette>
#include <QDir>
#include "arrange/arrange.hpp"
#include <QUrl>
#include <QTemporaryDir>
#include <QTimer>
#include <cmath>
#include <cstdio>

#include <QElapsedTimer>
#include <QThread>
#include <QUdpSocket>
#include <QTcpSocket>
#include <QtEndian>
#include <QFileInfo>
#include <QFile>
#include <ctime>
#include <QJsonArray>
#include <QMetaMethod>
#include <QMouseEvent>
#include <algorithm>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QJsonObject>
#include <QProcess>
#include <vector>
#include "Arrange.h"
#include "Settings.h"
#include "Access.h"
#include "Osc.h"
#include "Media.h"
#include "Samples.h"
#include "paths/paths.hpp"
#include "samples/samples.hpp"
#include "Links.h"
#include "Apps.h"
#include "SysNotify.h"
#include "AppWindows.h"
#include <QPainter>
#include "zip/zipstore.hpp"
#include <QHostAddress>
#include <QSettings>
#include <QIcon>
#include "Update.h"
#include "Measure.h"
#include "Remote.h"
#include "fx/fx.hpp"
#include "Net.h"
#include "Frame.h"
#include "measure/measure.hpp"
#include "Engine.h"
#include "WindowPlace.h"
#include <QCursor>
#include <QScreen>
#include "Graphics.h"
#include "license/license.hpp"
#include "I18n.h"

// real time passes (processEvents alone returns at once when the queue is empty)
// a real event loop (not processEvents): it also runs deferred deletes, as the running application does, so the tests see its memory
static void waitMs(int ms) { QEventLoop l; QTimer::singleShot(ms, &l, &QEventLoop::quit); l.exec(); }

// Every Qt / QML message also goes to <data>/console.log (the console has no terminal on Windows): QML errors, missing imports, the render
// backend. The last 500 lines are kept; --software draws with the CPU raster renderer when the graphics driver refuses D3D11 / OpenGL.
static QFile* g_log = nullptr;
static void logToFile(QtMsgType t, const QMessageLogContext&, const QString& m) {
  static const char* kind[] = {"debug", "warning", "critical", "fatal", "info"}; const QByteArray line = QDateTime::currentDateTime().toString("HH:mm:ss.zzz ").toUtf8() + kind[int(t) < 5 ? int(t) : 0] + ": " + m.toUtf8() + "\n";
  std::fputs(line.constData(), stderr); if (g_log && g_log->isOpen()) { g_log->write(line); g_log->flush(); }
}
// PAGE AUDIT (--bench with AMIX_BENCH_PAGES): every warning or error the interface logs, counted and kept with the page it came from
static int g_auditWarn = 0; static QStringList g_auditLines; static QString g_auditPage;
static int g_auditFontNotes = 0;   // see auditLog
static void auditLog(QtMsgType t, const QMessageLogContext&, const QString& m) {
  // the computer's fonts cannot shape a script (Qt: "OpenType support missing for <font>, script N"): the language menus show every language's
  // name in its own script, and a test machine without Bengali / Tamil / Khmer ... fonts says so once per font. Counted apart (the host's fonts,
  // not the console): Windows has fonts for all of them.
  if (t == QtWarningMsg && m.contains("OpenType support missing for")) { ++g_auditFontNotes; std::fputs((m + "\n").toLocal8Bit().constData(), stderr); return; }
  if (t == QtWarningMsg || t == QtCriticalMsg || t == QtFatalMsg) { ++g_auditWarn; if (g_auditLines.size() < 60) g_auditLines << ("[" + g_auditPage + "] " + m.left(220)); }
  std::fputs((m + "\n").toLocal8Bit().constData(), stderr);
}
#if defined(_WIN32) && defined(_WIN64)
char g_amixCrashPath[600] = {0};
// CRASH REPORT (Windows): an unhandled fault prints where it happened (module + offset of every frame, walked from the fault itself) to
// stderr and to <data>/crash.log, so a crash leaves something to go on instead of a silent exit. Offsets map to functions with the exe's symbols.
static LONG WINAPI amixCrashFilter(EXCEPTION_POINTERS* ep) {
  static volatile LONG once = 0; extern char g_amixCrashPath[600]; const char* path = g_amixCrashPath; if (InterlockedExchange(&once, 1)) return EXCEPTION_CONTINUE_SEARCH;
  FILE* outs[2] = {stderr, path[0] ? std::fopen(path, "a") : nullptr};
  auto say = [&](const char* fmt, void* a, const char* b, unsigned long long c) { for (FILE* f : outs) if (f) { std::fprintf(f, fmt, a, b, c); std::fflush(f); } };
  auto where = [&](void* addr, const char* tag) { HMODULE m = nullptr; char name[300] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCSTR>(addr), &m) && m) GetModuleFileNameA(m, name, sizeof name);
    const char* base = std::strrchr(name, '\\'); for (FILE* f : outs) if (f) { std::fprintf(f, "AmF X1 crash: %s %p  %s + 0x%llx\n", tag, addr, base ? base + 1 : name, m ? static_cast<unsigned long long>(static_cast<char*>(addr) - reinterpret_cast<char*>(m)) : 0ull); std::fflush(f); } };
  (void)say;
  for (FILE* f : outs) if (f) { std::fprintf(f, "AmF X1 crash: exception 0x%08lx, thread %lu, version %s (build %s)\n", ep->ExceptionRecord->ExceptionCode, GetCurrentThreadId(), AMIX_VERSION, AMIX_BUILD_STAMP); std::fflush(f); }
  where(ep->ExceptionRecord->ExceptionAddress, "at   ");
  CONTEXT c = *ep->ContextRecord;
  for (int k = 0; k < 40 && c.Rip; ++k) {
    DWORD64 imageBase = 0; PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(c.Rip, &imageBase, nullptr);
    if (!fn) { if (!c.Rsp) break; c.Rip = *reinterpret_cast<DWORD64*>(c.Rsp); c.Rsp += 8; }   // a leaf function: the return address is on top of the stack
    else { void* hd = nullptr; DWORD64 frame = 0; RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, c.Rip, fn, &c, &hd, &frame, nullptr); }
    if (!c.Rip) break; where(reinterpret_cast<void*>(c.Rip), "frame");
  }
  if (outs[1]) std::fclose(outs[1]);
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif
int main(int argc, char* argv[]) {
#ifdef _WIN32
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // COM for the main thread before Qt starts: the ASIO driver objects (CoCreateInstance) and the WASAPI enumerator are created from here
#endif
  QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);   // 125 % / 175 % drawn as such, per monitor (the manifest: per-monitor DPI awareness v2)
  // AUTO ZOOM (AutoZoom.h): before Qt starts, a zoom that fits the screen; Windows only (the screen and its scale are read here)
  QString zoomNote;
#ifdef _WIN32
  if (qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR")) {
    const int userPct = QSettings("AMFB Audio", "A mix OS").value("ui/zoom", 0).toInt();
    RECT wa{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);   // the process is DPI aware (its manifest): device pixels
    HDC dc = GetDC(nullptr); const double dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96; if (dc) ReleaseDC(nullptr, dc);
    const double os = dpi > 0 ? dpi / 96.0 : 1.0, lw = (wa.right - wa.left) / os, lh = (wa.bottom - wa.top) / os;
    // the screen's real size (from the monitor's own data): a 14-inch laptop gets a larger interface than a desk monitor with the same pixels
    HDC dc2 = GetDC(nullptr); const double mmW = dc2 ? GetDeviceCaps(dc2, HORZSIZE) : 0, mmH = dc2 ? GetDeviceCaps(dc2, VERTSIZE) : 0; if (dc2) ReleaseDC(nullptr, dc2);
    const double inches = mmW > 50 && mmH > 30 ? std::sqrt(mmW * mmW + mmH * mmH) / 25.4 : 0;
    const double z = autozoom::factor(lw, lh, userPct, inches);
    zoomNote = autozoom::describe(os, lw, lh, z) + (inches > 0 ? QString(", %1-inch screen").arg(inches, 0, 'f', 1) : QString()) + (userPct > 0 ? " (fixed in SETTINGS)" : userPct == autozoom::kFill ? " (RESPONSIVE)" : " (AUTO)");
    if (std::abs(z - 1.0) > 0.001) qputenv("QT_SCALE_FACTOR", QByteArray::number(z, 'f', 2));
  }
#endif
  QGuiApplication app(argc, argv);
  { bool testRun = false; for (int i = 1; i < argc; ++i) { const QByteArray a(argv[i]); if (a == "--smoke" || a == "--bench" || a == "--shots" || a == "--film" || a == "--big" || a == "--dump-presets") testRun = true; }
    Graphics::choose(testRun, app.arguments()); }   // GRAPHICS: the back end for this computer, before any window (Graphics.h)
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

  bool smoke = false, bench = false; QString shots, lang, film, filmScript, bigShots, dumpPresets;
  const QString smokeWav = QDir::tempPath() + "/amixos-smoke-tone.wav", smokeOut = QDir::tempPath() + "/amixos-smoke-export.wav";   // used by the smoke and by its end-of-run lambda: must outlive both
  for (int i = 1; i < argc; ++i) { const QString a = QString::fromLocal8Bit(argv[i]); if (a == "--smoke") smoke = true; else if (a == "--dump-presets" && i + 1 < argc) { smoke = true; dumpPresets = QString::fromLocal8Bit(argv[++i]); } else if (a == "--bench") { smoke = true; bench = true; } else if (a == "--shots" && i + 1 < argc) { smoke = true; shots = QString::fromLocal8Bit(argv[++i]); } else if (a == "--lang" && i + 1 < argc) lang = QString::fromLocal8Bit(argv[++i]); else if (a == "--film" && i + 2 < argc) { smoke = true; filmScript = QString::fromLocal8Bit(argv[++i]); film = QString::fromLocal8Bit(argv[++i]); }
#ifdef AMIXOS_TEST_LICENSE
    else if (a == "--big" && i + 1 < argc) { smoke = true; bigShots = QString::fromLocal8Bit(argv[++i]); }   // test builds: the 300-channel console, checked and photographed
#endif
  }
  { QSettings first; Settings::decideSetup(first, smoke || bench || !shots.isEmpty() || !film.isEmpty() || !bigShots.isEmpty()); }   // before anything else writes a setting

  am::paths::setProgramDir(QCoreApplication::applicationDirPath().toStdString());
#if defined(_WIN32) && defined(_WIN64)
  { const std::string cp = am::paths::file("crash.log"); std::snprintf(g_amixCrashPath, sizeof g_amixCrashPath, "%s", cp.c_str()); SetUnhandledExceptionFilter(amixCrashFilter); }
#endif
  if (smoke) std::setvbuf(stdout, nullptr, _IONBF, 0);   // the self-test prints as it goes: after a crash the last line is the step that was running
  if (!smoke) { QFile* lf = new QFile(QString::fromStdString(am::paths::file("console.log"))); if (lf->size() > 256 * 1024) lf->remove(); if (lf->open(QIODevice::Append | QIODevice::Text)) { g_log = lf; qInstallMessageHandler(logToFile); qInfo("AmF X1 %s starting: %s, Qt %s, render %s", AMIX_VERSION, am::paths::osName(), qVersion(), QQuickWindow::graphicsApi() == QSGRendererInterface::Software ? "software" : "auto"); if (!zoomNote.isEmpty()) qInfo("%s", qPrintable(zoomNote)); } }
  Engine engine; I18n i18n; Arrange arrange(&engine); Settings settings(&engine); if (engine.freshInstall()) { settings.factoryReset(); engine.markInstallSeen(); }   // NEW INSTALL: a full factory reset once, the licence kept
  if (!dumpPresets.isEmpty()) {   // PRESET PACK: the factory presets as the code has them (scripts/make-presets.js writes releases/presets/presets-pack.json from this)
    QFile f(dumpPresets); if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { std::fprintf(stderr, "amixos --dump-presets: cannot write %s\n", qPrintable(dumpPresets)); return 2; }
    f.write(QJsonDocument(engine.presetPack()).toJson(QJsonDocument::Indented)); return 0; }
  settings.setZoomNote(zoomNote); applyPalette(settings.darkTheme()); QObject::connect(&settings, &Settings::changed, &app, [&] { static bool last = settings.darkTheme(); if (last != settings.darkTheme()) { last = settings.darkTheme(); applyPalette(last); } }); if (smoke) settings.finishSetup(); Access access(&engine); Osc osc(&engine); Media media(&engine); Samples samples(&engine); Update update(QStringLiteral(AMIX_VERSION)); Measure measure(&engine); Remote remote(&engine); Net net(&engine); Frame frame(&engine); osc.setFrame(&frame); net.setFrame(&frame); frame.setOsc(&osc);
  // SAMPLE PADS while phones are linked: the console's engine is stopped and the remote service plays, so the pads are sent there
  samples.remotePlay = [&](int id, double gainDb) { return remote.send(QVariantMap{{"op", "sample"}, {"id", id}, {"gainDb", gainDb}}); };
  samples.remoteStop = [&] { remote.send(QVariantMap{{"op", "sample"}, {"stop", true}}); };
  samples.remoteSend = [&](const QVariantMap& cmd) { return remote.send(cmd); };   // CUSTOM PADS and SOUND EFFECTS while phones are linked
  engine.remoteInput = [&](const QString& in) { return remote.send(QVariantMap{{"op", "input"}, {"in", in}}); };   // INPUT while phones are linked
  engine.remoteInProcess = [&] { return remote.inProcess(); };
  if (!lang.isEmpty()) i18n.setLang(lang);   // --lang fa: start in that language (also for --shots)
  AppWindows appWindows;   // PROGRAM PICTURES for the app picker and the live program view (before the interface: it outlives it)
  Apps apps(&engine);   // before the interface, so it outlives it: every strip reads it (a page must never see it gone at exit)
  QQmlApplicationEngine qml;
  qml.rootContext()->setContextProperty("engine", &engine);
  qml.rootContext()->setContextProperty("frame", &frame);
  qml.rootContext()->setContextProperty("arrange", &arrange);
  qml.rootContext()->setContextProperty("settings", &settings);
  qml.rootContext()->setContextProperty("access", &access);
  qml.rootContext()->setContextProperty("osc", &osc);
  qml.rootContext()->setContextProperty("media", &media);
  const bool watchdogWas = engine.watchdog(); if (smoke || bench || !shots.isEmpty()) engine.setWatchdog(false);   // test runs: no automatic restarts under the other checks
  // ... and the SAVED choice stays what it was: a test run must not leave the watchdog switched off for the next start. LIVE MODE switches
  // the watchdog on and back (to "off", as it finds it here), which wrote "off" to the settings; it is put back now and again at the end.
  if (smoke || bench || !shots.isEmpty()) { QSettings().setValue("audio/watchdog", watchdogWas); QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [watchdogWas] { QSettings().setValue("audio/watchdog", watchdogWas); QSettings().sync(); }); }
  if (!shots.isEmpty()) settings.setSpatialEnabled(true);   // the screenshots show every page, the optional 360 AUDIO too
  Links links; links.dryRun = smoke || !shots.isEmpty(); qml.rootContext()->setContextProperty("links", &links);
  qml.rootContext()->setContextProperty("appWindows", &appWindows); qml.addImageProvider(QStringLiteral("appwin"), new AppWindowImages(&appWindows));   // the engine owns the provider
  qml.rootContext()->setContextProperty("apps", &apps);   // APP CAPTURE: several programs, each on its own strips
  qml.rootContext()->setContextProperty("samples", &samples);
  update.pluginDir = settings.vstUserFolder(); QObject::connect(&update, &Update::pluginInstalled, &settings, [&] { settings.rescanVst(); });   // PLUG-IN DOWNLOADS land in the console's own plug-in folder
  qml.rootContext()->setContextProperty("updater", &update);
  // A NEW VERSION, told by Windows too (once per version): a click brings the console to the front on SOFTWARE UPDATE
  QObject::connect(&update, &Update::notifySystem, &app, [&](const QString& title, const QString& text) {
    if (smoke) return;
    sysnotify::show(title, text, [&] { if (qml.rootObjects().isEmpty()) return; if (auto* w = qobject_cast<QWindow*>(qml.rootObjects().first())) { w->setProperty("current", "update"); if (w->windowState() & Qt::WindowMinimized) w->showNormal(); w->raise(); w->requestActivate(); } }); });
  QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [] { sysnotify::remove(); });
  QObject::connect(&update, &Update::revocations, &engine, [&](const QString& l) { engine.storeRevocations(l); });   // the newest revocation list the licence server signs
  QTimer::singleShot(4000, &update, [&] { update.fetchRevocations(); });   // once a start, quietly (nothing when no licence server is set)
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
  Graphics graphics; graphics.onMeterHz = [&](int hz) { engine.setMeterHz(hz); }; engine.setMeterHz(qEnvironmentVariableIsSet("AMIX_METER_HZ") ? qEnvironmentVariableIntValue("AMIX_METER_HZ") : graphics.startedWith() == QLatin1String("default") ? 30 : graphics.meterHz());   // AMIX_METER_HZ: the bench compares 30 / 60
  qml.rootContext()->setContextProperty("graphics", &graphics);
  qml.rootContext()->setContextProperty("appVersion", QStringLiteral(AMIX_VERSION));
  qml.rootContext()->setContextProperty("buildStamp", QStringLiteral(AMIX_BUILD_STAMP));   // when this build was made (UTC)
  qml.rootContext()->setContextProperty("noBoot", smoke);
  qml.rootContext()->setContextProperty("pageCacheOff", qEnvironmentVariable("AMIX_PAGE_CACHE") == "0");   // the page cache off: the before/after of the SWITCH bench
  qml.rootContext()->setContextProperty("asyncPages", !smoke || qEnvironmentVariableIsSet("AMIX_ASYNC_PAGES"));   // pages build without blocking the click (the tests: synchronous, unless asked)
  QObject::connect(&qml, &QQmlApplicationEngine::objectCreationFailed, &app, [] { std::fprintf(stderr, "amixos: the interface could not be created\n"); QCoreApplication::exit(2); }, Qt::QueuedConnection);
  qml.addImportPath(QStringLiteral("qrc:/qt/qml"));   // the console's own module (AmixOS, with the Theme singleton) lives in the resources: Qt 6.4 on Windows does not search there by itself, and every Theme.* colour came out undefined (black on white)
  // BLANK HUNT: pages built over several frames, as on a desktop (the offscreen platform has no frame-driven incubation: there an
  // asynchronous page is built at once, and a page not yet shown could never be seen)
  struct FrameIncubation : QQmlIncubationController { QTimer t; FrameIncubation() { t.setInterval(16); QObject::connect(&t, &QTimer::timeout, &t, [this] { incubateFor(4); }); t.start(); } };
  std::unique_ptr<FrameIncubation> frameIncubation; if (bench && qEnvironmentVariableIsSet("AMIX_BENCH_BLANK")) { frameIncubation = std::make_unique<FrameIncubation>(); qml.setIncubationController(frameIncubation.get()); }
  qml.load(QUrl(QStringLiteral("qrc:/qt/qml/AmixOS/qml/Main.qml")));   // Qt 6.4 (loadFromModule is 6.5+)
  if (!qml.rootObjects().isEmpty()) graphics.attach(qobject_cast<QQuickWindow*>(qml.rootObjects().first()));
  // THE WINDOW OPENS PLACED: built first (Main.qml starts hidden), then put centred on the work area of the screen under the mouse
  // (the taskbar left out, in that monitor's own scale), or back where it was closed when that place is still on a connected screen
  // (a monitor unplugged since: pulled onto a visible one), maximised when the designed size does not fit; only then shown
  if (auto* win = qml.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(qml.rootObjects().first())) {
    const bool offscreen = QGuiApplication::platformName() == QLatin1String("offscreen"), testRun = smoke || bench || !shots.isEmpty() || !film.isEmpty();
    const bool placeTest = qEnvironmentVariableIsSet("AMIX_PLACE_TEST");   // the placement checked on the offscreen platform with simulated screens (its configfile)
    const bool placeOff = qEnvironmentVariableIsSet("AMIX_PLACE_OFF");   // the window shown unplaced, as before (where the system puts a new window): the before / after of the placement check
    if ((!offscreen || placeTest) && !placeOff) {
      QScreen* home = QGuiApplication::screenAt(QCursor::pos()); if (!home) home = QGuiApplication::primaryScreen();
      if (home && win->screen() != home) win->setScreen(home);
      win->create(); const QMargins frame = win->frameMargins().isNull() ? QMargins(8, 31, 8, 8) : win->frameMargins();   // the frame is known once the native window exists
      QList<QRect> areas; for (QScreen* sc : QGuiApplication::screens()) areas << sc->availableGeometry();
      const QRect avail = home ? home->availableGeometry() : QRect(0, 0, 1920, 1040);
      QSettings st; const QRect saved = testRun ? QRect() : st.value("window/geometry").toRect(); const bool wasMax = !testRun && st.value("window/maximized", false).toBool();
      const QSize want(win->width(), win->height()), minimum(win->minimumWidth(), win->minimumHeight());
      const QRect r = winplace::choose(want, minimum, saved, areas, avail, frame);
      win->setGeometry(r);
      const bool tooBig = want.width() + 24 > avail.width() || want.height() + 48 > avail.height();
      if (wasMax || settings.uiZoom() == -1 || (!saved.isValid() && tooBig)) win->setVisibility(QWindow::Maximized); else win->setVisible(true);
      // a window closed in its normal state is remembered there; maximised, the normal place it had before is kept
      if (!testRun) QObject::connect(&app, &QCoreApplication::aboutToQuit, win, [win] { QSettings s; const bool mx = win->visibility() == QWindow::Maximized;
        s.setValue("window/maximized", mx); if (!mx && win->visibility() != QWindow::Minimized && win->visibility() != QWindow::FullScreen) s.setValue("window/geometry", win->geometry()); });
      // MOVED TO ANOTHER MONITOR (another scale), or the monitor it was on removed: kept on a visible screen and redrawn at once
      QObject::connect(qApp, &QGuiApplication::screenRemoved, win, [win](QScreen*) { QTimer::singleShot(0, win, [win] {
        if (win->visibility() != QWindow::Windowed || !win->screen()) return; const QRect a = win->screen()->availableGeometry();
        if (!a.intersects(win->geometry())) win->setGeometry(winplace::centred(win->size(), a, win->frameMargins())); else win->setGeometry(winplace::clamp(win->geometry(), a, win->frameMargins(), win->minimumSize())); win->update(); }); });
      if (placeTest) QTimer::singleShot(qEnvironmentVariableIntValue("AMIX_PLACE_TEST") > 1 ? qEnvironmentVariableIntValue("AMIX_PLACE_TEST") : 2500, &app, [win, &app] { const QRect g = win->geometry(), a = win->screen()->availableGeometry();
        std::printf("amixos: PLACE window %d,%d %dx%d on screen %s (work area %d,%d %dx%d, scale %.2f), %s\n", g.x(), g.y(), g.width(), g.height(), qPrintable(win->screen()->name()), a.x(), a.y(), a.width(), a.height(), win->screen()->devicePixelRatio(),
                    win->visibility() == QWindow::Maximized ? "maximised" : win->isVisible() ? "shown" : "hidden"); std::fflush(stdout); app.quit(); });
    } else win->setVisible(true);
    if (placeTest && placeOff) QTimer::singleShot(2500, &app, [win, &app] { const QRect g = win->geometry(), a = win->screen()->availableGeometry();
      std::printf("amixos: PLACE (unplaced) window %d,%d %dx%d on screen %s (work area %d,%d %dx%d)\n", g.x(), g.y(), g.width(), g.height(), qPrintable(win->screen()->name()), a.x(), a.y(), a.width(), a.height()); std::fflush(stdout); app.quit(); });
    // NEVER A BLANK WINDOW: a frame is asked for again when the window comes back from minimised, changes screen (another scale), or
    // the graphics device was lost (a driver update, a GPU reset, remote desktop): then the scene's resources are dropped and rebuilt
    // instead of the window staying empty
    QObject::connect(win, &QWindow::visibilityChanged, win, [win](QWindow::Visibility v) { if (v != QWindow::Minimized && v != QWindow::Hidden) QTimer::singleShot(0, win, [win] { win->update(); }); });
    QObject::connect(win, &QWindow::screenChanged, win, [win](QScreen*) { QTimer::singleShot(0, win, [win] { win->update(); }); });
    QObject::connect(win, &QQuickWindow::sceneGraphError, win, [win](QQuickWindow::SceneGraphError, const QString& m) {
      qWarning("amixos: the graphics device reported an error (%s); the interface is drawn again", qPrintable(m)); QTimer::singleShot(200, win, [win] { win->releaseResources(); win->update(); }); });
  }
  if (!smoke && settings.autoStart()) QTimer::singleShot(300, &app, [&] { settings.startLastDevice(); });   // the remembered device, once the interface is up
  // AUTO AUDIO INTERFACE: when an interface is plugged in (hot-plug) and the engine is idle, start it (the remembered device if it is the one, else AUTO DETECT)
  QObject::connect(&engine, &Engine::hotplug, &app, [&](bool added, const QString& what) { if (!added || engine.running() || !settings.autoStart()) return; QTimer::singleShot(1500, &app, [&] { if (!engine.running()) settings.startLastDevice(); }); qInfo("hot-plug: %s", qPrintable(what)); });
  if (!smoke) engine.setHotplugWatch(true);
  if (qml.rootObjects().isEmpty()) return 2;

  if (!film.isEmpty()) {
    // --film SCRIPT.json DIR: films the running console for the product videos, frame by frame (DIR/<clip>/f0000.png ...). The script
    // plays a beat through real channels (the built-in samples) and moves the console on a timeline: calls on the engine or on any
    // named interface object, held or ramped, and properties set or ramped. Nothing here is a mock-up: it is the console, running.
    auto* win = qobject_cast<QQuickWindow*>(qml.rootObjects().first());
    QFile sf(filmScript); if (!sf.open(QIODevice::ReadOnly)) { std::fprintf(stderr, "amixos --film: cannot read %s\n", qPrintable(filmScript)); return 2; }
    const QJsonObject js = QJsonDocument::fromJson(sf.readAll()).object(); const int fps = js["fps"].toInt(30);
    win->setWidth(js["w"].toInt(1380)); win->setHeight(js["h"].toInt(860));
    if (!engine.open("null", "", "", 48000, 256, 2, 2)) { std::fprintf(stderr, "amixos --film: %s\n", qPrintable(engine.status())); return 3; }
    am::setNullManualClock(true);   // the engine hears exactly 1/fps of audio per frame, however long a frame takes to capture
    auto advance = [&](double sec) { const std::uint64_t target = am::nullClockProcessed() + static_cast<std::uint64_t>(sec * 48000); am::advanceNullClock(static_cast<std::uint64_t>(sec * 48000));
      QElapsedTimer w; w.start(); while (am::nullClockProcessed() + 256 <= target && w.elapsed() < 2000) QThread::usleep(200); };
    auto target = [&](const QString& name) -> QObject* { if (name == "engine") return &engine; if (name == "window") return win; return win->findChild<QObject*>(name); };
    auto call = [&](QObject* o, const QString& method, const QVariantList& args) {
      if (!o) return false; const QMetaObject* mo = o->metaObject();
      for (int m = mo->methodOffset() > 0 ? 0 : 0; m < mo->methodCount(); ++m) {
        const QMetaMethod mm = mo->method(m); if (mm.name() != method.toLatin1() || mm.parameterCount() != args.size()) continue;
        std::vector<QVariant> v(10); QGenericArgument ga[10];
        for (int k = 0; k < args.size(); ++k) { v[k] = args[k]; if (mm.parameterMetaType(k).id() != QMetaType::QVariant) v[k].convert(mm.parameterMetaType(k)); ga[k] = QGenericArgument(mm.parameterMetaType(k).id() == QMetaType::QVariant ? "QVariant" : mm.parameterTypeName(k).constData(), mm.parameterMetaType(k).id() == QMetaType::QVariant ? static_cast<const void*>(&args[k]) : v[k].constData()); }
        return mm.invoke(o, Qt::DirectConnection, ga[0], ga[1], ga[2], ga[3], ga[4], ga[5], ga[6], ga[7], ga[8], ga[9]);
      }
      std::fprintf(stderr, "amixos --film: no method %s(%lld args)\n", qPrintable(method), static_cast<long long>(args.size())); return false;
    };
    // one action at clip time t (s): fires once when its time passes; with "dur", ramps its "$v" argument (or the property) from -> to
    auto apply = [&](const QJsonObject& a, double t, double prevT) {
      const double t0 = a["t"].toDouble(0), dur = a["dur"].toDouble(0);
      const bool once = dur <= 0, fire = once ? (t0 > prevT && t0 <= t) || (t0 == 0 && prevT < 0) : (t >= t0 && prevT < t0 + dur);
      if (!fire) return; double k = once ? 1 : std::clamp((t - t0) / dur, 0.0, 1.0); k = k < 0.5 ? 4 * k * k * k : 1 - std::pow(-2 * k + 2, 3) / 2;
      const double v = a["from"].toDouble() + (a["to"].toDouble() - a["from"].toDouble()) * k; QObject* o = target(a["target"].toString("engine"));
      if (a["op"] == "prop") { if (o) o->setProperty(a["prop"].toString().toLatin1(), a.contains("value") ? a["value"].toVariant() : QVariant(v)); return; }
      if (a["op"] == "param") {   // a plug-in knob by its index on the panel
        const QVariantList pp = engine.pluginParams(a["strip"].toInt(), a["slot"].toInt()); const int ix = a["index"].toInt();
        if (ix < pp.size()) engine.setPluginParam(a["strip"].toInt(), a["slot"].toInt(), pp[ix].toMap()["id"].toInt(), v);
        if (QObject* ed = target("plugEd")) QMetaObject::invokeMethod(ed, "reload"); return; }
      QVariantList args; for (const QJsonValue& x : a["args"].toArray()) args << (x.toString() == "$v" ? QVariant(v) : x.toVariant());
      call(o, a["method"].toString(), args);
    };
    // THE CURSOR: real mouse events into the window (press, drag, release), its place logged per frame for the film to draw it
    QPointF cur(js["w"].toInt(1380) * 0.6, js["h"].toInt(860) * 0.7); bool down = false;
    auto mouse = [&](QEvent::Type ty, QPointF p) {
      QMouseEvent ev(ty, p, win->mapToGlobal(p.toPoint()), ty == QEvent::MouseButtonRelease ? Qt::LeftButton : (ty == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton),
                     ty == QEvent::MouseButtonRelease ? Qt::NoButton : (down || ty == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton), Qt::NoModifier);
      QCoreApplication::sendEvent(win, &ev); };
    auto mouseOp = [&](const QJsonObject& a, double t, double prevT) {   // {op: mouse, t, dur, path: [[x, y], ...], press: bool}
      const double t0 = a["t"].toDouble(0), dur = std::max(1e-3, a["dur"].toDouble(0.5)); if (t < t0 || prevT >= t0 + dur) return;
      const QJsonArray path = a["path"].toArray(); if (path.isEmpty()) return; const bool press = a["press"].toBool(false);
      double k = std::clamp((t - t0) / dur, 0.0, 1.0); k = k < 0.5 ? 4 * k * k * k : 1 - std::pow(-2 * k + 2, 3) / 2;
      const double seg = k * (path.size() - 1); const int i = std::min<int>(path.size() - 2, static_cast<int>(seg)); const double u = path.size() == 1 ? 0 : seg - i;
      const QJsonArray A = path[std::max(0, i)].toArray(), B = path[std::min<int>(path.size() - 1, i + 1)].toArray();
      const QPointF p(A[0].toDouble() + (B[0].toDouble() - A[0].toDouble()) * u, A[1].toDouble() + (B[1].toDouble() - A[1].toDouble()) * u);
      if (press && !down && prevT < t0 + 1e-9) { cur = p; mouse(QEvent::MouseMove, p); down = true; mouse(QEvent::MouseButtonPress, p); }
      cur = p; mouse(QEvent::MouseMove, p);
      if (press && down && t >= t0 + dur - 1e-9) { mouse(QEvent::MouseButtonRelease, p); down = false; }
    };
    const double bpm = js["bpm"].toDouble(120), step = 60.0 / bpm / 4;   // sixteenth notes
    for (const QJsonValue& a : js["setup"].toArray()) apply(a.toObject(), 0, -1);
    advance(0.8); waitMs(800);
    for (const QJsonValue& cv : js["clips"].toArray()) {
      const QJsonObject c = cv.toObject(); const QString name = c["name"].toString(); QDir().mkpath(film + "/" + name);
      win->setProperty("current", c["page"].toString("mixer")); advance(0.3); waitMs(700);
      const QJsonArray acts = c["actions"].toArray(), pat = c.contains("pattern") ? c["pattern"].toArray() : js["pattern"].toArray();
      const int n = static_cast<int>(c["seconds"].toDouble(4) * fps); double prevT = -1; QElapsedTimer clock; clock.start(); QJsonArray curLog;
      for (int f = 0; f < n; ++f) {
        const double t = static_cast<double>(f) / fps;
        for (const QJsonValue& a : acts) { const QJsonObject ao = a.toObject(); if (ao["op"] == "mouse") mouseOp(ao, t, prevT); else apply(ao, t, prevT); }
        for (const QJsonValue& pv : pat) {   // {strip, samples: [id per 16th, 0 = rest], len: 16}
          const QJsonObject p = pv.toObject(); const QJsonArray sm = p["samples"].toArray(); const int len = sm.size(); if (!len) continue;
          for (long k = static_cast<long>(std::ceil((prevT < 0 ? -1e-9 : prevT) / step + 1e-9)); k * step <= t + 1e-9; ++k) { const int id = sm[static_cast<int>(k % len)].toInt(); if (id > 0 && k * step > prevT) engine.playSampleOnStrip(p["strip"].toInt(), id); }
        }
        prevT = t;
        advance(1.0 / fps); waitMs(js["settleMs"].toInt(40));   // one frame of audio, then let the interface pick up the new meters
        win->grabWindow().save(film + "/" + name + QStringLiteral("/f%1.png").arg(f, 4, 10, QChar('0')));
        curLog.append(QJsonArray{cur.x(), cur.y(), down ? 1 : 0});
      }
      { QFile cf(film + "/" + name + "/cursor.json"); if (cf.open(QIODevice::WriteOnly)) cf.write(QJsonDocument(QJsonObject{{"w", win->width()}, {"h", win->height()}, {"cursor", curLog}}).toJson(QJsonDocument::Compact)); }
      std::printf("amixos --film: %s, %d frames in %.1f s (%.2fx real time)\n", qPrintable(name), n, clock.elapsed() / 1000.0, clock.elapsed() / 1000.0 / (n / static_cast<double>(fps)));
      for (const QJsonValue& a : c["after"].toArray()) apply(a.toObject(), 0, -1);
    }
    am::setNullManualClock(false); engine.stop(); return 0;
  }
  if (bench) {   // --bench: CPU time of the interface thread on the MIXER page, idle (meters running) and while faders move
    auto* win = qobject_cast<QQuickWindow*>(qml.rootObjects().first());
    // BLANK HUNT: every frame shown with no page on it (still building) is counted, and those with no sign of building either (a blank)
    static int emptyFrames = 0, silentEmpty = 0, framesSeen = 0; static QMap<QString, int> silentOn;
    if (qEnvironmentVariableIsSet("AMIX_BENCH_BLANK")) { QQuickItem* busy = win->findChild<QQuickItem*>("pageBusy");
      QObject::connect(win, &QQuickWindow::frameSwapped, win, [win, busy] { ++framesSeen; if (win->property("current").toString().isEmpty() || win->property("pageReady").toBool()) return;
        ++emptyFrames; if (!busy || !busy->isVisible()) { ++silentEmpty; ++silentOn[win->property("current").toString()]; } }); }
    engine.setNullTestInput(true); engine.open("null", "", "", 48000, 256, 2, 2); win->setProperty("current", qEnvironmentVariable("AMIX_BENCH_PAGE", "mixer")); waitMs(1500);
    auto cpuMs = [] { return 1000.0 * double(std::clock()) / CLOCKS_PER_SEC; };
    if (qEnvironmentVariableIsSet("AMIX_BENCH_LAG")) {   // INPUT LAG: a fader dragged with a 1000 Hz mouse, a button clicked, cached pages switched; each to the frame that shows it
      std::function<QQuickItem*(QQuickItem*, const QString&)> walk = [&](QQuickItem* r, const QString& n) -> QQuickItem* { if (!r->isVisible()) return nullptr; if (r->objectName() == n && r->width() > 0) return r; for (QQuickItem* ch : r->childItems()) if (QQuickItem* f = walk(ch, n)) return f; return nullptr; };
      auto find = [&](const char* name) { return walk(win->contentItem(), QString::fromLatin1(name)); };
      auto post = [&](QEvent::Type ty, QPointF p, Qt::MouseButtons bs) { QCoreApplication::postEvent(win, new QMouseEvent(ty, p, p, win->mapToGlobal(p), Qt::LeftButton, bs, Qt::NoModifier)); };
      qint64 swaps = 0; QElapsedTimer clk; clk.start(); qint64 lastSwap = 0; auto c = QObject::connect(win, &QQuickWindow::frameSwapped, win, [&] { ++swaps; lastSwap = clk.nsecsElapsed(); }, Qt::DirectConnection);
      auto emitCost = [&] { waitMs(200); QElapsedTimer e; e.start(); for (int k = 0; k < 20; ++k) engine.setGain(3, -10.0 - k * 0.1); return e.nsecsElapsed() / 1e6 / 20; };
      if (qEnvironmentVariableIsSet("AMIX_LAG_EACH")) { win->setProperty("current", "mixer"); waitMs(1500); std::printf("amixos --bench: LAG strip change, mixer only: %.2f ms\n", emitCost());
        for (const char* pg : {"home", "asio", "dsp", "routing", "meter", "fx"}) { win->setProperty("current", QString::fromLatin1(pg)); waitMs(400); win->setProperty("current", "mixer"); std::printf("amixos --bench: LAG strip change, + %s cached: %.2f ms\n", pg, emitCost()); } }
      for (const char* pg : {"home", "asio", "dsp", "routing", "meter", "fx"}) { win->setProperty("current", QString::fromLatin1(pg)); waitMs(400); }
      win->setProperty("current", "mixer"); waitMs(1500);
      if (QQuickItem* f = find("stripFader")) {
        const QPointF top = f->mapToScene(QPointF(f->width() / 2, 20)), bot = f->mapToScene(QPointF(f->width() / 2, f->height() - 20));
        const int t0 = engine.stripTick(); const double c0 = 1000.0 * double(std::clock()) / CLOCKS_PER_SEC; const qint64 s0 = swaps;
        double lagSum = 0, lagMax = 0, stall = 0; int lagN = 0, moves = 0; post(QEvent::MouseButtonPress, bot, Qt::LeftButton);
        QElapsedTimer t; t.start(); qint64 last = 0, pending = -1;
        while (t.elapsed() < 3000) {   // the mouse reports every millisecond; the event loop takes what it can
          const double ph = std::fmod(t.elapsed() / 1500.0, 1.0); const double k = ph < 0.5 ? ph * 2 : 2 - ph * 2;
          post(QEvent::MouseMove, bot + (top - bot) * k, Qt::LeftButton); ++moves; if (pending < 0) pending = clk.nsecsElapsed();
          const qint64 before = lastSwap; QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
          if (lastSwap != before && pending >= 0) { const double l = (lastSwap - pending) / 1e6; lagSum += l; lagMax = std::max(lagMax, l); ++lagN; pending = -1; }
          const qint64 now = t.elapsed(); stall = std::max(stall, double(now - last)); last = now; QThread::usleep(1000 - 0); }
        post(QEvent::MouseButtonRelease, bot, Qt::NoButton); waitMs(300);
        std::printf("amixos --bench: LAG fader drag 3 s, %d mouse moves at 1 kHz: %d engine strip updates, %lld frames, GUI CPU %.0f ms/s, input-to-frame mean %.1f ms / worst %.1f ms, longest event-loop stall %.1f ms\n",
                    moves, engine.stripTick() - t0, swaps - s0, (1000.0 * double(std::clock()) / CLOCKS_PER_SEC - c0) / 3.0, lagN ? lagSum / lagN : 0.0, lagMax, stall);
      } else { int n = 0, v = 0; for (QQuickItem* it : win->contentItem()->findChildren<QQuickItem*>()) { if (it->objectName() == "stripFader") { ++n; if (it->isVisible()) ++v; } } QObject* mp = win->findChild<QObject*>("mixerPage"); std::printf("amixos --bench: LAG no strip fader found (%d, %d visible; mixer page %p visible %d, current %s)\n", n, v, (void*)mp, mp ? mp->property("visible").toBool() : -1, qPrintable(win->property("current").toString())); }
      if (QQuickItem* b = find("masterMuteBtn")) {
        const QPointF p = b->mapToScene(QPointF(b->width() / 2, b->height() / 2)); double sum = 0, worst = 0;
        for (int k = 0; k < 10; ++k) { const qint64 at = clk.nsecsElapsed(); post(QEvent::MouseButtonPress, p, Qt::LeftButton); post(QEvent::MouseButtonRelease, p, Qt::NoButton);
          bool seen = false; qint64 shown = 0; const qint64 s0 = swaps; QElapsedTimer t; t.start();
          while (t.elapsed() < 1000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 1); if (!seen) { seen = true; win->update(); } if (seen && swaps > s0 && lastSwap > at) { shown = lastSwap; break; } }
          const double ms = (shown - at) / 1e6; sum += ms; worst = std::max(worst, ms); waitMs(120); }
        std::printf("amixos --bench: LAG button click to frame: mean %.1f ms, worst %.1f ms (10 clicks)\n", sum / 10, worst);
      }
      { double sum = 0, worst = 0; int n = 0;
        for (int r = 0; r < 3; ++r) for (const char* pg : {"home", "asio", "dsp", "routing", "meter", "fx", "mixer"}) { const qint64 at = clk.nsecsElapsed(); win->setProperty("current", QString::fromLatin1(pg)); if (qEnvironmentVariableIsSet("AMIX_LAG_EACH")) std::printf("amixos --bench: LAG   switch %s: %.1f ms in the click itself\n", pg, (clk.nsecsElapsed() - at) / 1e6);
          QElapsedTimer t; t.start(); bool ready = false; while (t.elapsed() < 2000) { ready = win->property("pageReady").toBool(); if (ready && lastSwap > at) break; if (ready) win->update(); QCoreApplication::processEvents(QEventLoop::AllEvents, 1); }
          const double ms = (lastSwap - at) / 1e6; if (qEnvironmentVariableIsSet("AMIX_LAG_EACH")) std::printf("amixos --bench: LAG   switch %s: %.1f ms to the frame\n", pg, ms); if (r > 0) { sum += ms; worst = std::max(worst, ms); ++n; } waitMs(250); }
        std::printf("amixos --bench: LAG cached page switch to frame: mean %.1f ms, worst %.1f ms (%d switches)\n", sum / std::max(1, n), worst, n); }
      { QElapsedTimer g; g.start(); qml.collectGarbage(); std::printf("amixos --bench: LAG one JavaScript garbage collection %.1f ms\n", g.nsecsElapsed() / 1e6); }
      { double c0 = 1000.0 * double(std::clock()) / CLOCKS_PER_SEC; const qint64 s0 = swaps; waitMs(3000); std::printf("amixos --bench: LAG mixer idle: GUI CPU %.0f ms/s, %.0f frames/s\n", (1000.0 * double(std::clock()) / CLOCKS_PER_SEC - c0) / 3.0, (swaps - s0) / 3.0); }
      QObject::disconnect(c); engine.stop(); return 0; }
    if (qEnvironmentVariableIsSet("AMIX_BENCH_BLANK")) {   // BLANK PAGE HUNT (with AMIX_ASYNC_PAGES=1, pages built over frames as on a desktop): fast page switches, minimise / restore and resizes; after each, the page shown must be drawn
      // (a built page, fully opaque, with something on it) and a page held while hidden must show the engine's current state again
      const int cycles = std::max(1, qEnvironmentVariableIntValue("AMIX_BENCH_BLANK")); QStringList ids; for (const QVariant& v : engine.pages()) ids << v.toMap().value("id").toString();
      engine.open("null", "", "", 48000, 256, 2, 2); std::srand(7); int blank = 0, stale = 0, notReady = 0, checks = 0; QStringList bad;
      auto shownLoader = [&]() -> QObject* { QObject* hit = nullptr; std::function<void(QQuickItem*)> walk = [&](QQuickItem* p) { for (QQuickItem* it : p->childItems()) {
          if (!it->isVisible()) continue; if (it->inherits("QQuickLoader") && it->property("item").value<QObject*>() && it->width() > 300 && it->height() > 200 && it->z() < 100) { if (!hit) hit = it; continue; } walk(it); } };
        walk(win->contentItem()); return hit; };
      auto check = [&](const QString& why) {
        const QString id = win->property("current").toString(); QElapsedTimer t; t.start(); while (!win->property("pageReady").toBool() && t.elapsed() < 4000) waitMs(10); waitMs(160);   // the fade-in done
        ++checks; QObject* l = shownLoader(); auto* li = qobject_cast<QQuickItem*>(l); auto* item = l ? qobject_cast<QQuickItem*>(l->property("item").value<QObject*>()) : nullptr;
        if (!win->property("pageReady").toBool() || !item) { ++notReady; bad << QString("%1 %2: no page built (status %3)").arg(why, id).arg(l ? l->property("status").toInt() : -1); return; }
        const QImage img = win->grabWindow(); const QPointF tl = li->mapToScene(QPointF(0, 0)); const qreal d = img.devicePixelRatio();
        const QRect area = QRectF(tl.x() * d, tl.y() * d, li->width() * d, li->height() * d).toRect().intersected(img.rect()); QSet<QRgb> colours;
        for (int y = area.top(); y < area.bottom(); y += 3) for (int x = area.left(); x < area.right(); x += 3) colours.insert(img.pixel(x, y) & 0xF0F0F0);
        const double op = li->opacity() * item->opacity();
        if (colours.size() < 6 || op < 0.99 || !item->isVisible()) { ++blank; bad << QString("%1 %2: blank (%3 colours, opacity %4)").arg(why, id).arg(colours.size()).arg(op, 0, 'f', 2);
          if (blank <= 3) img.save(QString("%1/blank-%2.png").arg(qEnvironmentVariable("AMIX_BENCH_BLANK_DIR", "/tmp"), id)); }
        // held while hidden (the lag fix): shown again, the page's held tick is the engine's
        if (item->property("sTick").isValid() && item->property("sTick").toInt() != engine.property("stripTick").toInt()) { ++stale; const int was = item->property("sTick").toInt(); waitMs(600); bad << QString("%1 %2: strip state not current (page %3, engine %4, after 600 ms %5, class %6)").arg(why, id).arg(was).arg(engine.property("stripTick").toInt()).arg(item->property("sTick").toInt()).arg(item->metaObject()->className()); }
        if (item->property("mTick").isValid() && item->property("mTick").toInt() + 3 < engine.property("meterTick").toInt()) { ++stale; bad << why + " " + id + ": meters not current"; }
      };
      for (int c = 0; c < cycles; ++c) {
        for (int k = 0; k < 6; ++k) { win->setProperty("current", ids[std::rand() % ids.size()]); waitMs(std::rand() % 40); }   // a burst of clicks on the rail
        engine.setGain(std::rand() % 8, -30.0 + std::rand() % 36); engine.setName(std::rand() % 8, QString("CH %1").arg(c));   // the console changes behind the hidden pages
        check("burst"); const int r = std::rand() % 3;
        if (r == 0) { win->showMinimized(); waitMs(60); win->setProperty("current", ids[std::rand() % ids.size()]); waitMs(30); win->showNormal(); check("restore"); }
        else if (r == 1) { win->resize(720 + std::rand() % 900, 480 + std::rand() % 500); waitMs(30); check("resize"); }
        else { const QString back = win->property("current").toString(); win->setProperty("current", ids[std::rand() % ids.size()]); waitMs(20); win->setProperty("current", back); check("back"); }
        if (c % 10 == 9) std::printf("amixos --bench: BLANK cycle %d: %d checks, %d blank, %d not built, %d stale\n", c + 1, checks, blank, notReady, stale); }
      for (const QString& b : bad.mid(0, 30)) std::printf("amixos --bench: BLANK  %s\n", qPrintable(b));
      for (auto it = silentOn.cbegin(); it != silentOn.cend(); ++it) std::printf("amixos --bench: BLANK  %s: %d frames with no page and no sign of building\n", qPrintable(it.key()), it.value());
      std::printf("amixos --bench: BLANK %d cycles, %d checks: %d blank, %d not built, %d stale; %d of %d frames drawn with no page yet, %d of them with no sign of it building\n", cycles, checks, blank, notReady, stale, emptyFrames, framesSeen, silentEmpty);
      engine.stop(); return blank || notReady || stale || silentEmpty ? 9 : 0; }
    if (qEnvironmentVariableIsSet("AMIX_BENCH_FRAMES")) {   // FRAME TIME: every page with the meters running, AMIX_BENCH_FRAMES ms each (default 3000)
      const int ms = std::max(500, qEnvironmentVariableIntValue("AMIX_BENCH_FRAMES") > 1 ? qEnvironmentVariableIntValue("AMIX_BENCH_FRAMES") : 3000);
      double sumMean = 0, worstP95 = 0, worstMax = 0; long frames = 0; int n = 0; QString worstId;
      std::printf("amixos --bench: FRAMES back end %s, %d ms per page (frames drawn; gap = time between frames; work = sync to shown)\n", qPrintable(graphics.inUse()), ms);
      for (const QVariant& v : engine.pages()) { const QString id = v.toMap().value("id").toString(); win->setProperty("current", id); waitMs(400); graphics.resetFrames(); waitMs(ms);
        const QVariantMap f = graphics.frameStats(); const double p95 = f["workP95Ms"].toDouble();
        std::printf("amixos --bench: FRAMES %-12s %4d frames (%5.1f/s), gap mean %6.1f / 95%% %6.1f ms, work mean %5.2f / 95%% %5.2f / max %6.2f ms\n", qPrintable(id), f["frames"].toInt(), f["frames"].toInt() * 1000.0 / ms, f["gapMeanMs"].toDouble(), f["gapP95Ms"].toDouble(), f["workMeanMs"].toDouble(), p95, f["workMaxMs"].toDouble());
        sumMean += f["workMeanMs"].toDouble(); frames += f["frames"].toInt(); ++n; if (p95 > worstP95) { worstP95 = p95; worstId = id; } worstMax = std::max(worstMax, f["workMaxMs"].toDouble()); }
      std::printf("amixos --bench: FRAMES %d pages, %.1f frames/s on average, work mean %.2f ms, worst 95%% %.2f ms (%s), worst frame %.2f ms\n", n, frames * 1000.0 / ms / std::max(1, n), sumMean / std::max(1, n), worstP95, qPrintable(worstId), worstMax);
      engine.stop(); return 0;
    }
    if (qEnvironmentVariableIsSet("AMIX_BENCH_MEM")) {   // memory: every page opened in turn, five rounds; the resident size after each round (a leak grows)
      auto rssMb = [] { QFile f("/proc/self/statm"); if (!f.open(QIODevice::ReadOnly)) return 0.0; const QList<QByteArray> v = f.readAll().split(' '); return v.size() > 1 ? v[1].toDouble() * 4096.0 / 1048576.0 : 0.0; };
      std::printf("amixos --bench: memory at start %.0f MB\n", rssMb());
      for (int r = 0; r < 5; ++r) { for (const QVariant& v : engine.pages()) { win->setProperty("current", v.toMap().value("id").toString()); waitMs(60); } win->setProperty("current", "mixer"); waitMs(1600); if (qEnvironmentVariableIsSet("AMIX_BENCH_GC")) { qml.collectGarbage(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); waitMs(100); } std::printf("amixos --bench: memory after round %d of every page: %.0f MB\n", r + 1, rssMb()); }
      engine.stop(); return 0; }
    if (qEnvironmentVariableIsSet("AMIX_BENCH_PAGES")) {   // PAGE SWITCHING AUDIT: every page, three rounds; warnings, freezes, memory, sound, mixer state
      qInstallMessageHandler(auditLog);
      auto rssMb = [] { QFile f("/proc/self/statm"); if (!f.open(QIODevice::ReadOnly)) return 0.0; const QList<QByteArray> v = f.readAll().split(' '); return v.size() > 1 ? v[1].toDouble() * 4096.0 / 1048576.0 : 0.0; };
      // the longest the interface thread stays away from the event loop while a page opens (a heartbeat every 5 ms; its lateness is the freeze)
      double switchMs = 0; QMap<QString, double> swCold, swWarm;
      auto openPage = [&](const QString& id, double& blockMs, double& stallMs) {
        g_auditPage = id; QElapsedTimer o; o.start(); win->setProperty("current", id);
        { bool ready = false; int swaps = 0; auto c = QObject::connect(win, &QQuickWindow::frameSwapped, win, [&] { if (ready) ++swaps; });   // SWITCH: from the click to the first frame shown after the page is ready
          while (o.elapsed() < 3000) { ready = win->property("pageReady").toBool(); if (ready && swaps > 0) break; if (ready) win->update(); QCoreApplication::processEvents(QEventLoop::AllEvents, 2); }
          QObject::disconnect(c); switchMs = o.nsecsElapsed() / 1e6; }
        QCoreApplication::processEvents(); blockMs = o.nsecsElapsed() / 1e6;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);   // the old page goes, as the running event loop does it
        stallMs = 0; QElapsedTimer hb; hb.start(); qint64 last = 0;
        while (hb.elapsed() < 450) { QCoreApplication::processEvents(QEventLoop::AllEvents, 5); const qint64 now = hb.elapsed(); stallMs = std::max(stallMs, double(now - last)); last = now; QThread::msleep(2); }
        stallMs = std::max(stallMs, blockMs); };
      if (qEnvironmentVariableIsSet("AMIX_BENCH_FIT")) {   // BUTTON AUDIT: every page at several window sizes; a clickable area cut off by the window or its panel, or lying on another one, is listed
        const QList<QSize> sizes{{1380, 860}, {1920, 1010}, {1600, 812}, {1280, 700}, {1100, 640}}; int found = 0; long long checked = 0; QStringList seen;
        auto isButton = [](QQuickItem* it) { return it->inherits("QQuickMouseArea") && it->objectName() != QLatin1String("scrollHintArea") && it->isEnabled() && it->width() >= 8 && it->height() >= 8 && it->width() <= 460 && it->height() <= 90; };
        auto nameOf = [](QQuickItem* it) { QString n, l; for (QQuickItem* p = it; p && n.isEmpty(); p = p->parentItem()) { n = p->objectName(); if (l.isEmpty()) l = p->property("label").toString(); if (l.isEmpty()) l = p->property("text").toString(); }
          if (l.isEmpty()) for (QQuickItem* sib : it->parentItem() ? it->parentItem()->childItems() : QList<QQuickItem*>{}) { l = sib->property("text").toString(); if (!l.isEmpty()) break; }
          return (n.isEmpty() ? QStringLiteral("(no name)") : n) + (l.isEmpty() ? QString() : " \"" + l.left(28) + "\""); };
        for (const QSize& sz : sizes) { win->setWidth(sz.width()); win->setHeight(sz.height());
          QVariantList pages = engine.pages(); pages.prepend(QVariantMap{{"id", "mixer"}});
          for (const QVariant& v : pages) { const QString id = v.toMap().value("id").toString(); double b = 0, st = 0; openPage(id, b, st);
            const QRectF winR(0, 0, win->width(), win->height()); QList<QPair<QQuickItem*, QRectF>> all;
            auto report = [&](QQuickItem* it, const QString& why) { ++found; const QString key = id + "|" + nameOf(it) + "|" + why.section(' ', 0, 3);
              if (!seen.contains(key)) { seen << key; std::printf("amixos --bench: FIT %dx%d [%s] %s: %s\n", sz.width(), sz.height(), qPrintable(id), qPrintable(nameOf(it)), qPrintable(why)); } };
            std::function<void(QQuickItem*, QRectF, bool)> walk = [&](QQuickItem* it, QRectF clip, bool scrolls) {
              if (!it->isVisible() || it->opacity() <= 0.01) return;
              const QRectF r = it->mapRectToScene(QRectF(0, 0, it->width(), it->height()));
              if (isButton(it)) { ++checked;
                if (!scrolls) { QString why;
                  if (r.right() > clip.right() + 1.5) why = QStringLiteral("cut off on the right by %1 px").arg(qRound(r.right() - clip.right()));
                  else if (r.left() < clip.left() - 1.5) why = QStringLiteral("cut off on the left by %1 px").arg(qRound(clip.left() - r.left()));
                  else if (r.bottom() > clip.bottom() + 1.5) why = QStringLiteral("cut off at the bottom by %1 px").arg(qRound(r.bottom() - clip.bottom()));
                  else if (r.top() < clip.top() - 1.5) why = QStringLiteral("cut off at the top by %1 px").arg(qRound(clip.top() - r.top()));
                  if (!why.isEmpty()) report(it, why); }
                if (r.intersects(clip)) all << qMakePair(it, r.intersected(clip)); }
              const bool fl = it->inherits("QQuickFlickable"); QRectF c2 = clip; if (it->clip() || fl) c2 = clip.intersected(r);
              for (QQuickItem* ch : it->childItems()) walk(ch, c2, scrolls || fl); };
            walk(win->contentItem(), winR, false);
            auto related = [](QQuickItem* a, QQuickItem* b) { QQuickItem* pa = a->parentItem(); QQuickItem* pb = b->parentItem(); if (pa == pb) return true;
              for (QQuickItem* p = pb; p; p = p->parentItem()) if (p == pa) return true; for (QQuickItem* p = pa; p; p = p->parentItem()) if (p == pb) return true; return false; };
            for (int x = 0; x < all.size(); ++x) for (int y = x + 1; y < all.size(); ++y) { const QRectF o = all[x].second.intersected(all[y].second); const double oa = o.width() * o.height();
              if (o.width() <= 2 || o.height() <= 2 || oa < 0.2 * std::min(all[x].second.width() * all[x].second.height(), all[y].second.width() * all[y].second.height())) continue;
              if (related(all[x].first, all[y].first)) continue;
              report(all[x].first, QStringLiteral("lies on ") + nameOf(all[y].first)); } } }
        std::printf("amixos --bench: FIT %lld clickable areas looked at\n", checked);
        std::printf("amixos --bench: FIT %d button problems on %lld pages x %lld window sizes (%lld different)\n", found, static_cast<long long>(engine.pages().size() + 1), static_cast<long long>(sizes.size()), static_cast<long long>(seen.size()));
        engine.stop(); return found ? 107 : 0; }
      if (qEnvironmentVariableIsSet("AMIX_BENCH_LEAK")) {   // which page keeps memory: each page opened 6 times (and the mixer between), memory after vs before
        // deferred deletes (an old page) are only delivered by a running event loop; this audit runs before app.exec(), so it delivers them
        // itself, as the real event loop does after every page switch (without this the audit itself would keep every old page alive)
        auto cycle = [&](const QString& id) { double b, st; openPage(id, b, st); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); openPage("mixer", b, st); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); };
        auto settle = [&] { QElapsedTimer t; t.start(); while (t.elapsed() < 1500) { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(5); } };
        if (qEnvironmentVariableIsSet("AMIX_LEAK_PAGE")) {   // one page, batches of 6 opens: does it level off (memory reused) or keep growing (a leak)?
          const QString id = qEnvironmentVariable("AMIX_LEAK_PAGE"); cycle(id); settle(); std::printf("amixos --bench: LEAK %s start %.1f MB\n", qPrintable(id), rssMb());
          const bool force = qEnvironmentVariableIsSet("AMIX_LEAK_FORCE");
          if (qEnvironmentVariableIsSet("AMIX_LEAK_HIDE")) win->hide();   // pages built but never drawn: is the growth in the renderer?   // a full JS collection and the renderer's caches released after each batch
          const int batches = qEnvironmentVariableIsSet("AMIX_LEAK_N") ? qEnvironmentVariableIntValue("AMIX_LEAK_N") : 6;
          for (int b = 1; b <= batches; ++b) { for (int k = 0; k < 6; ++k) cycle(id); if (force) { qml.collectGarbage(); win->releaseResources(); } settle(); std::printf("amixos --bench: LEAK %s after %2d opens %.1f MB, %d items in the window, %d objects in the engine\n", qPrintable(id), b * 6, rssMb(), int(win->contentItem()->findChildren<QQuickItem*>().size()), int(qml.findChildren<QObject*>().size())); }
          engine.stop(); return 0; }
        QList<QPair<double, QString>> grow;
        for (const QVariant& v : engine.pages()) { const QString id = v.toMap().value("id").toString(); cycle(id); settle(); const double m0 = rssMb();
          for (int k = 0; k < 6; ++k) cycle(id); settle(); grow << qMakePair(rssMb() - m0, id); }
        std::sort(grow.begin(), grow.end(), [](auto& a, auto& b) { return a.first > b.first; });
        for (const auto& g : grow) std::printf("amixos --bench: LEAK %-12s %+6.1f MB after 6 more opens\n", qPrintable(g.second), g.first);
        engine.stop(); return 0; }
      engine.setSelectedStrip(5); const qulonglong blocks0 = engine.telemetry().value("blocks").toULongLong(); const qulonglong xr0 = engine.telemetry().value("xruns").toULongLong();
      const QVariantList pages = engine.pages(); QMap<QString, double> firstOpen, worstReopen; double mem[4] = {rssMb(), 0, 0, 0}; int warnRound[3] = {0, 0, 0};
      for (int r = 0; r < 3; ++r) { const int w0 = g_auditWarn;
        for (const QVariant& v : pages) { const QString id = v.toMap().value("id").toString(); double b = 0, st = 0; openPage(id, b, st);
          if (r == 0) { firstOpen[id] = st; swCold[id] = switchMs; } else { worstReopen[id] = std::max(worstReopen.value(id), st); swWarm[id] = std::max(swWarm.value(id), switchMs); }
          double b2 = 0, st2 = 0; openPage("mixer", b2, st2); if (r > 0) worstReopen["mixer"] = std::max(worstReopen.value("mixer"), st2); }
        { QElapsedTimer settle; settle.start(); while (settle.elapsed() < 2000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(5); } }   // the user settles on the mixer: the window collects its garbage
        warnRound[r] = g_auditWarn - w0; mem[r + 1] = rssMb(); }
      { const QStringList ws = {"home", "asio", "dsp", "routing", "meter", "fx"}; QMap<QString, double> wsWorst; double wsSum = 0; int wsN = 0;   // SWITCH among a few pages, as one works: each re-open
        for (int r = 0; r < 3; ++r) for (const QString& id : ws) { double b = 0, st = 0; openPage(id, b, st); if (r > 0) { wsWorst[id] = std::max(wsWorst.value(id), switchMs); wsSum += switchMs; ++wsN; } openPage("mixer", b, st); if (r > 0) { wsWorst["mixer"] = std::max(wsWorst.value("mixer"), switchMs); wsSum += switchMs; ++wsN; } }
        for (auto it = wsWorst.cbegin(); it != wsWorst.cend(); ++it) std::printf("amixos --bench: SWITCH working set %-8s worst re-open %7.1f ms\n", qPrintable(it.key()), it.value());
        std::printf("amixos --bench: SWITCH working set average re-open %.1f ms\n", wsSum / std::max(1, wsN)); }
      const qulonglong blocks1 = engine.telemetry().value("blocks").toULongLong(), xr1 = engine.telemetry().value("xruns").toULongLong();
      QList<QPair<double, QString>> slow; for (auto it = firstOpen.cbegin(); it != firstOpen.cend(); ++it) slow << qMakePair(it.value(), it.key());
      std::sort(slow.begin(), slow.end(), [](auto& a, auto& b) { return a.first > b.first; });
      double reWorst = 0; QString reWorstId; for (auto it = worstReopen.cbegin(); it != worstReopen.cend(); ++it) if (it.value() > reWorst) { reWorst = it.value(); reWorstId = it.key(); }
      std::printf("amixos --bench: PAGES %d pages x 3 rounds (each page, then back to the mixer)\n", int(pages.size()));
      std::printf("amixos --bench: PAGES slowest first opens:"); for (int k = 0; k < std::min<int>(8, slow.size()); ++k) std::printf(" %s %.0f ms;", qPrintable(slow[k].second), slow[k].first); std::printf("\n");
      { double cs = 0, ws = 0, cw = 0, ww = 0; for (auto it = swCold.cbegin(); it != swCold.cend(); ++it) { std::printf("amixos --bench: SWITCH %-12s cold %7.1f ms, warm %7.1f ms\n", qPrintable(it.key()), it.value(), swWarm.value(it.key())); cs += it.value(); ws += swWarm.value(it.key()); cw = std::max(cw, it.value()); ww = std::max(ww, swWarm.value(it.key())); }
        const double n = std::max<qsizetype>(1, swCold.size()); std::printf("amixos --bench: SWITCH average cold %.1f ms (worst %.1f), warm %.1f ms (worst %.1f)\n", cs / n, cw, ws / n, ww); }
      std::printf("amixos --bench: PAGES worst re-open (rounds 2-3): %s %.0f ms\n", qPrintable(reWorstId), reWorst);
      std::printf("amixos --bench: PAGES memory %.0f -> %.0f -> %.0f -> %.0f MB; warnings per round %d / %d / %d\n", mem[0], mem[1], mem[2], mem[3], warnRound[0], warnRound[1], warnRound[2]);
      if (g_auditFontNotes > 0) std::printf("amixos --bench: PAGES host fonts: %d notes that this computer has no font able to shape a script used by a language name (not counted)\n", g_auditFontNotes);
      std::printf("amixos --bench: PAGES sound kept running %d (%llu blocks, dropouts %llu); selected strip kept %d\n", blocks1 > blocks0 + 100, blocks1 - blocks0, xr1 - xr0, engine.selectedStrip() == 5);
      for (const QString& l : g_auditLines) std::printf("amixos --bench: WARNING %s\n", qPrintable(l));
      engine.stop(); return g_auditWarn > 0 || engine.selectedStrip() != 5 || blocks1 <= blocks0 + 100 ? 1 : 0; }
    if (qEnvironmentVariableIsSet("AMIX_BENCH_ALL")) {   // the opening time of every page, slowest first
      QList<QPair<double, QString>> t; for (const QVariant& v : engine.pages()) { const QString id = v.toMap().value("id").toString(); win->setProperty("current", "home"); waitMs(150);
        QElapsedTimer o; o.start(); win->setProperty("current", id); QCoreApplication::processEvents(); double first = o.nsecsElapsed() / 1e6; waitMs(150);
        if (qEnvironmentVariable("AMIX_BENCH_ALL") == "2") { win->setProperty("current", "home"); waitMs(150); o.restart(); win->setProperty("current", id); QCoreApplication::processEvents(); first = o.nsecsElapsed() / 1e6; waitMs(150); }   // the second open: the page's own cost, without compiling its QML
        t << qMakePair(first, id); }
      std::sort(t.begin(), t.end(), [](auto& a, auto& b) { return a.first > b.first; });
      for (const auto& x : t) std::printf("amixos --bench: open %-12s %6.0f ms\n", qPrintable(x.second), x.first);
      engine.stop(); return 0; }
    { double worst = 0, sum = 0; for (int k = 0; k < 5; ++k) { win->setProperty("current", "home"); waitMs(200); QElapsedTimer o; o.start(); win->setProperty("current", qEnvironmentVariable("AMIX_BENCH_PAGE", "mixer")); QCoreApplication::processEvents(); const double ms = o.nsecsElapsed() / 1e6; worst = std::max(worst, ms); sum += ms; waitMs(300); }
      std::printf("amixos --bench: opening the page takes %.0f ms on average, %.0f ms at worst\n", sum / 5, worst); }
    double c0 = cpuMs(); waitMs(5000); const double idle = (cpuMs() - c0) / 5.0;   // ms of CPU per second
    c0 = cpuMs(); QElapsedTimer t; t.start(); int moves = 0;
    while (t.elapsed() < 5000) { engine.setGain(moves % 32, -30.0 + (moves % 60) * 0.5); ++moves; QCoreApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(16); }   // a fader moved at 60 Hz, round the strips
    const double moving = (cpuMs() - c0) / 5.0;
    std::printf("amixos --bench: MIXER idle %.1f ms CPU per second, faders moving (%d moves) %.1f ms CPU per second\n", idle, moves, moving);
    engine.stop(); return 0;
  }
  // REMOTE HOLD (tests with the real phone app, AMIX_REMOTE_HOLD=<seconds>, AMIX_REMOTE_DUMP=<file>): the phone remote starts for this
  // network as the button does, the MIXER page is on screen, and every 100 ms the console's own state goes to the file (the pairing PIN,
  // strips 1-4, master, remotes connected). Every 4 s the console moves strip 4 itself (-12 / -24 dB) so the phone's side can be checked.
  if (qEnvironmentVariableIsSet("AMIX_REMOTE_HOLD")) {
    // AMIX_REMOTE_PAGE: the page on screen (default MIXER); AMIX_REMOTE_SHOT=<png>: one picture of it at AMIX_REMOTE_SHOT_AT seconds (tests)
    auto* win = qobject_cast<QQuickWindow*>(qml.rootObjects().first()); if (win) win->setProperty("current", qEnvironmentVariable("AMIX_REMOTE_PAGE", "mixer"));
    const QString shotFile = qEnvironmentVariable("AMIX_REMOTE_SHOT"); const qint64 shotAt = qEnvironmentVariableIsSet("AMIX_REMOTE_SHOT_AT") ? qEnvironmentVariableIntValue("AMIX_REMOTE_SHOT_AT") * 1000LL : -1; bool shotDone = false;
    if (!remote.start(true)) { std::fprintf(stderr, "amixos: remote hold: the remote did not start: %s\n", qPrintable(remote.status())); return 3; }
    const int secs = qEnvironmentVariableIntValue("AMIX_REMOTE_HOLD"); const QString dump = qEnvironmentVariable("AMIX_REMOTE_DUMP");
    QElapsedTimer t; t.start(); qint64 lastMove = 0; int k = 0;
    while (t.elapsed() < secs * 1000LL) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(10);
      if (t.elapsed() - lastMove > 4000) { lastMove = t.elapsed(); engine.setGain(3, (k++ % 2) ? -24.0 : -12.0); }
      if (!shotDone && win && !shotFile.isEmpty() && shotAt >= 0 && t.elapsed() >= shotAt) { shotDone = true; remote.refreshLinks(); QElapsedTimer w; w.start(); while (w.elapsed() < 600) { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(10); } win->grabWindow().save(shotFile); }
      static qint64 lastDump = -1000; if (t.elapsed() - lastDump < 100 || dump.isEmpty()) continue; lastDump = t.elapsed();
      QJsonArray st; for (int i = 0; i < 4; ++i) { const QVariantMap m = engine.stripAt(i); st.append(QJsonObject{{"db", m["db"].toDouble()}, {"mute", m["mute"].toBool()}, {"name", m["name"].toString()}}); }
      QFile f(dump + ".tmp"); if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(QJsonObject{{"t", t.elapsed()}, {"pin", remote.pairPin()}, {"port", remote.port()}, {"running", remote.running()},
          {"remotes", remote.remotes()}, {"master", engine.masterDb()}, {"strips", st}, {"status", remote.status()}}).toJson(QJsonDocument::Compact)); f.close(); QFile::remove(dump); QFile::rename(dump + ".tmp", dump); }
    }
    remote.stop(); waitMs(500); return 0;
  }
  if (smoke && bigShots.isEmpty()) {
    std::printf("amixos --smoke: start master %.1f dB, monitor %.1f dB; %s\n", engine.masterDb(), engine.monitor()["db"].toDouble(), qPrintable(engine.status()));
    if (engine.masterDb() <= -60.0) { std::fprintf(stderr, "amixos --smoke: the console started with the master at -inf\n"); return 166; }
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
    { // DUAL LISTEN captures the monitor's EQ mode as AI EQ AUTO, and off restores it (the show path has no part in it)
      const int was = engine.monitor().value("eqMode").toInt(); engine.setMonitorDual(true, 3, 0.25, 0); const QVariantMap on = engine.monitor();
      engine.setMonitorDual(false, 3, 0.25, 0); const QVariantMap off = engine.monitor();
      if (!on.value("dual").toBool() || !on.value("aiEq").toBool() || on.value("sourceB").toInt() != 3 || off.value("dual").toBool() || off.value("aiEq").toBool() || off.value("eqMode").toInt() != was) { std::fprintf(stderr, "amixos --smoke: DUAL LISTEN / AI EQ AUTO capture and restore\n"); return 52; } }
    // the licence: this test machine has no key, so BASIC with 10 channels; a malformed key is refused without storing anything
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
      { // ROOM CORRECTION (cuts only, on the predicted modes) and a microphone calibration written back and read again
        const bool rc = measure.roomCorrection(5.2, 3.9, 2.6, 0.45, 0.6, 0.5, 1.0, 2.6, 3.1, 1.2, 6, 12); bool cutsOnly = rc;
        for (const QVariant& f : measure.filters()) cutsOnly = cutsOnly && f.toMap().value("gain").toDouble() < 0;
        const QString calIn = QDir::tempPath() + "/amixos-smoke-cal.txt", calOut = QDir::tempPath() + "/amixos-smoke-cal-out.txt";
        { QFile f(calIn); f.open(QIODevice::WriteOnly); f.write("* test mic\n20 -2.5\n1000 0\n20000 1.25\n"); }
        const bool calOk = measure.loadCalibration(calIn) && measure.saveCalibration(calOut) == calOut && measure.loadCalibration(calOut) && measure.calibrated();
        measure.clearCalibration(); QFile::remove(calIn); QFile::remove(calOut);
        std::fprintf(stderr, "amixos --smoke: ROOM CORRECTION %d cuts (cuts only %d), %.1f -> %.1f dB RMS; calibration saved and read back %d\n", static_cast<int>(measure.filters().size()), cutsOnly,
                     measure.eqCurve().value("errorBefore").toDouble(), measure.eqCurve().value("errorAfter").toDouble(), calOk);
        if (!rc || !cutsOnly || !calOk || measure.eqCurve().value("errorAfter").toDouble() >= measure.eqCurve().value("errorBefore").toDouble()) return 64; }
      QFile::remove(mw);
      engine.setSpatial(5, true, 90, 0, 2, 0); if (!engine.spatial(5).value("on").toBool() || engine.spatial(5).value("az").toDouble() != 90) { std::fprintf(stderr, "amixos --smoke: 360 audio state did not take\n"); return 63; } engine.setSpatial(5, false, 0, 0, 1, 0);
      { // 360 AND IAMF ARE ONE PLACE: an IAMF ELEMENT takes over the strip's position; the 360 controls move its knobs and its knobs move the 360 view
        const int S = 6; int k = -1; const QVariantList pl = engine.plugins(S); for (int j = 0; j < pl.size(); ++j) if (pl[j].toMap()["kind"].toString().isEmpty()) { k = j; break; }
        engine.setSpatial(S, true, 45, 0, 2, 0.2); engine.setPlugin(S, k, "fx:iamfelement");
        const QVariantMap a = engine.spatial(S); const bool took = a["iamf"].toBool() && std::fabs(a["az"].toDouble() - 45) < 0.6 && std::fabs(a["dist"].toDouble() - 2) < 0.05;
        engine.setSpatial(S, true, 90, 10, 3, 0.3); const QVariantList pp = engine.pluginParams(S, k); const bool knob = pp.size() > 2 && std::fabs(pp[0].toMap()["value"].toDouble() - 0.75) < 0.002 && std::fabs(pp[1].toMap()["value"].toDouble() - 100.0 / 180) < 0.002;
        engine.setPluginParam(S, k, 0, 0.25); const bool back = std::fabs(engine.spatial(S)["az"].toDouble() + 90) < 0.6;
        engine.setPlugin(S, k, ""); const QVariantMap c = engine.spatial(S); const bool held = !c["iamf"].toBool() && c["on"].toBool() && std::fabs(c["az"].toDouble() + 90) < 0.6;
        engine.setSpatial(S, false, 0, 0, 1, 0);
        if (!took || !knob || !back || !held) { std::fprintf(stderr, "amixos --smoke: 360 / IAMF link (took %d, knobs %d, back %d, held %d)\n", took, knob, back, held); return 167; } }
      { // FRONT 180 · 10 SPEAKERS: on, a sound at the right feeds WIDE R (out 5) loudest and outputs 1-10 carry the ten feeds; off, everything comes back
        const QVariantMap s0 = engine.send(3, 4); engine.setSpeakerArray(true); engine.setSpatial(3, true, 90, 0, 1, 0);
        double best = -100; int at = -1; for (int k = 0; k < 10; ++k) { const double d = engine.send(3, k)["db"].toDouble(); if (d > best) { best = d; at = k; } }
        const bool routed = engine.outputRoutes()[0].toInt() == am::Runtime::kSrcBus0L && engine.outputRoutes()[9].toInt() == am::Runtime::kSrcBus0L + 18;
        engine.setSpatial(3, false, 0, 0, 1, 0); engine.setSpeakerArray(false);
        const bool back = std::fabs(engine.send(3, 4)["db"].toDouble() - s0["db"].toDouble()) < 1e-6 && engine.outputRoutes()[0].toInt() != am::Runtime::kSrcBus0L;
        if (at != 4 || !routed || !back) { std::fprintf(stderr, "amixos --smoke: front 10 speakers (loudest %d, routed %d, given back %d)\n", at, routed, back); return 170; } }
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
    { QElapsedTimer gt; gt.start(); const bool r = engine.guarded([] { std::this_thread::sleep_for(std::chrono::milliseconds(600)); return true; }, 120); if (r || gt.elapsed() > (qEnvironmentVariableIsSet("AMIXOS_SMOKE_RELAX") ? 4000 : 450)) {   /* RELAX: a slow display (an emulator) takes long to draw between the checks of the wait */ std::fprintf(stderr, "amixos --smoke: a stuck driver call was not left behind (%lld ms)\n", static_cast<long long>(gt.elapsed())); return 74; }
      if (!engine.guarded([] { return true; }, 2000)) { std::fprintf(stderr, "amixos --smoke: a quick guarded call failed\n"); return 75; } }
    { // EQUALIZER APO round trip on the program EQ; the signature sound preset
      engine.setEq(-1, 0, 2, 80, 0.7, 3); engine.setEq(-1, 1, 1, 2500, 2, -4); const QString apo = engine.exportApo(-1, QDir::tempPath() + "/amixos-smoke-apo.txt"); engine.setEq(-1, 0, 0, 31.5, 1.41, 0); engine.setEq(-1, 1, 0, 63, 1.41, 0);
      if (apo.isEmpty() || engine.importApo(-1, apo) != 2 || engine.eqBand(-1, 0).value("type").toInt() != 2 || std::fabs(engine.eqBand(-1, 1).value("gainDb").toDouble() + 4) > 0.05 || std::fabs(engine.eqBand(-1, 1).value("freq").toDouble() - 2500) > 1) { std::fprintf(stderr, "amixos --smoke: Equalizer APO export / import round trip failed\n"); return 76; }
      QFile::remove(apo); engine.setEq(-1, 0, 0, 31.5, 1.41, 0); engine.setEq(-1, 1, 0, 63, 1.41, 0);
      for (const char* g : {"METAL", "HARD ROCK"}) { engine.applyMasteringPreset(g); const QVariantMap mm = engine.mastering();   // guitar presets: applied, mono low end, mids shaped (scooped for METAL, forward for HARD ROCK)
        if (mm.value("preset").toString() != g || !mm.value("bassMonoOn").toBool() || (mm.value("mid").toDouble() < 0) != (QString(g) == "METAL")) { std::fprintf(stderr, "amixos --smoke: %s preset\n", g); return 77; } }
      { QFile pf(QStringLiteral(AMIX_SOURCE_DIR) + "/../../releases/presets/presets-pack.json"); const QJsonObject pk = pf.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(pf.readAll()).object() : QJsonObject(); const QJsonObject now = engine.presetPack();   // the published pack = the presets in the code
        for (const char* k : {"mastering", "effects", "tone"}) if (pk.value(k).toArray() != now.value(k).toArray()) { std::fprintf(stderr, "amixos --smoke: releases/presets/presets-pack.json %s differs from the code: run node scripts/make-presets.js\n", k); return 253; }
        if (now.value("mastering").toArray().size() != engine.masteringPresets().size() || now.value("effects").toArray().size() != 614) { std::fprintf(stderr, "amixos --smoke: preset pack counts\n"); return 253; } }
      engine.applyMasteringPreset("AMFB SIGNATURE"); if (engine.mastering().value("preset").toString() != "AMFB SIGNATURE" || !engine.mastering().value("bassMonoOn").toBool()) { std::fprintf(stderr, "amixos --smoke: signature preset\n"); return 77; } QVariantMap so; so["on"] = false; so["preset"] = ""; so["bassMonoOn"] = false; so["exciterOn"] = false; engine.setMastering(so); }
    { // WINDOWS SOUND SYNC on a stand-in for the system volume: -inf mutes it, raising the fader restores what it was, sync off leaves it alone, exit takes the mute back
      bool sysMute = false; int sets = 0; engine.setSystemMuteBackend([&](bool& m) { m = sysMute; return true; }, [&](bool m) { sysMute = m; ++sets; return true; });
      const double keep = engine.masterDb(); engine.setWindowsMuteSync(true); engine.setMaster(-60); if (!sysMute || !engine.windowsMuted()) { std::fprintf(stderr, "amixos --smoke: the fader at -inf did not mute the system sound\n"); return 78; }
      if (!QSettings().value("mixer/weMutedWindows", false).toBool()) { std::fprintf(stderr, "amixos --smoke: the Windows mute was not written down (a crash would leave Windows silent)\n"); return 83; }
      engine.setMaster(-10); if (sysMute || engine.windowsMuted()) { std::fprintf(stderr, "amixos --smoke: raising the fader did not un-mute the system sound\n"); return 79; }
      if (QSettings().value("mixer/weMutedWindows", false).toBool()) { std::fprintf(stderr, "amixos --smoke: the Windows mute note was not cleared when the sound came back\n"); return 84; }
      sysMute = true; engine.syncSystemSound(); engine.setMaster(-60); engine.setMaster(0); if (!sysMute) { std::fprintf(stderr, "amixos --smoke: a mute the person had set in Windows was not put back\n"); return 80; } sysMute = false; engine.syncSystemSound();
      engine.setWindowsMuteSync(false); engine.setMaster(-60); if (sysMute) { std::fprintf(stderr, "amixos --smoke: sync off still muted the system sound\n"); return 81; }
      engine.setWindowsMuteSync(true); if (!sysMute) { std::fprintf(stderr, "amixos --smoke: switching sync on at -inf did not mute\n"); return 82; } engine.restoreSystemSound(); if (sysMute) { std::fprintf(stderr, "amixos --smoke: exit did not take the mute back\n"); return 83; }
      engine.setMaster(keep); engine.setSystemMuteBackend([](bool&) { return false; }, [](bool) { return false; }); engine.syncSystemSound(); }
    { // WINDOWS VOLUME and GAIN MODE (a stand-in for the Windows slider): both ways, the trim curve, and never a double cut through Windows' own mixer
      float winLevel = 0.5f; int sets = 0; const bool keepMode = engine.windowsGainMode(); const double keepMaster = engine.masterDb();
      engine.setSystemVolumeBackend([&](float& f) { f = winLevel; return true; }, [&](float f) { winLevel = f; ++sets; return true; });
      engine.setWindowsGainMode(false); engine.syncSystemVolume(); const bool offOk = engine.windowsVolume() == 50 && engine.windowsTrimDb() == 0.0;
      engine.setWindowsGainMode(true); engine.setMaster(-6); const double t50 = engine.windowsTrimDb();
      winLevel = 1.0f; engine.syncSystemVolume(); const double t100 = engine.windowsTrimDb();          // the keyboard's volume-up in Windows
      engine.setWindowsVolume(25); const double t25 = engine.windowsTrimDb(); const bool wrote = sets == 1 && std::abs(winLevel - 0.25f) < 1e-6;   // the console's own fader moves Windows
      winLevel = 0.0f; engine.syncSystemVolume(); const double t0 = engine.windowsTrimDb();
      const bool fader = std::abs(engine.masterDb() + 6) < 1e-9;                                        // the master fader never moves for it
      const bool curve = std::abs(Engine::trimForLevel(0.5) + 9.03) < 0.01 && std::abs(Engine::trimForLevel(0.1) + 30) < 1e-9 && Engine::trimForLevel(0) <= -119;
      const bool shared = !engine.windowsSetsLevel();                                                   // the null device: not Windows' mixer, so the trim applies
      std::printf("amixos --smoke: WINDOWS VOLUME gain mode trims 50%% %.1f, 100%% %.1f, 25%% %.1f, 0%% %.0f dB; console->Windows %d; master stays %d\n", t50, t100, t25, t0, wrote, fader);
      engine.setSystemVolumeBackend([](float&) { return false; }, [](float) { return false; }); engine.syncSystemVolume(); const bool gone = engine.windowsVolume() == -1 && engine.windowsTrimDb() == 0.0;
      engine.setWindowsGainMode(keepMode); engine.setMaster(keepMaster);
      if (!(offOk && std::abs(t50 + 9.03) < 0.01 && t100 == 0.0 && std::abs(t25 + 18.06) < 0.01 && t0 <= -119 && wrote && fader && curve && shared && gone)) {
        std::fprintf(stderr, "amixos --smoke: WINDOWS VOLUME failed (off %d, 50 %.2f, 100 %.2f, 25 %.2f, 0 %.0f, wrote %d, fader %d, curve %d, shared %d, gone %d)\n", offOk, t50, t100, t25, t0, wrote, fader, curve, shared, gone); return 288; } }
    { // INPUT: MIC on a console started with outputs only (the default start keeps the microphone off): the press opens the input and the
      // sound goes on; a press that cannot open the input (here: a sound card that is not there) leaves the sound as it was, and says why
      const double r0 = engine.sampleRate() > 0 ? engine.sampleRate() : 48000; const int f0 = engine.blockFrames() > 0 ? engine.blockFrames() : 256;
      engine.stop(); engine.open("null", "-", "", r0, f0, 0, 2); const int before = engine.deviceInputs();
      const QVariantMap r = engine.setInputSource("mic"); const int after = engine.deviceInputs(); const bool on = engine.running();
      std::printf("amixos --smoke: INPUT MIC on an outputs-only start: inputs %d -> %d, sound running %d, source %s\n", before, after, on, qPrintable(engine.inputSource()));
      if (!(before == 0 && after >= 2 && on && engine.inputSource() == "mic" && !r.isEmpty())) { std::fprintf(stderr, "amixos --smoke: INPUT MIC failed: %s\n", qPrintable(engine.status())); return 1; }
      // APP SOUND: one program's sound is a Windows audio stream; on another driver the press says so and the sound keeps playing as it was;
      // the list of programs is Windows' own (none here); the toolbar has the picker
      const QVariantMap ra = engine.setInputSource("loopback:app:4321"); const bool keptOn = engine.running(); const QString why = engine.status();
      const QVariantList appList = engine.appSources(); QObject* appBtn = nullptr;
      if (!qml.rootObjects().isEmpty()) { QObject* w = qml.rootObjects().first(); const QVariant pg = w->property("current"); w->setProperty("current", "mixer");
        QElapsedTimer tw; tw.start(); while (!appBtn && tw.elapsed() < 3000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); appBtn = w->findChild<QObject*>("inputSource_app"); }
        w->setProperty("current", pg); }
      std::printf("amixos --smoke: APP SOUND on a non-Windows driver: refused %d, sound running %d, says WASAPI %d; programs listed %d; picker %d\n", int(ra.isEmpty()), int(keptOn), int(why.contains("APP SOUND") && why.contains("WASAPI")), int(appList.size()), int(appBtn != nullptr));
      if (!(ra.isEmpty() && keptOn && why.contains("APP SOUND") && why.contains("WASAPI") && engine.inputSource() != "app" && appBtn)) { std::fprintf(stderr, "amixos --smoke: APP SOUND failed: %s\n", qPrintable(why)); return 1; }
      { // APP CAPTURE: two programs beside the device's inputs, each on its own strips (test tones stand in for the programs here)
        auto pause = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20); };
        auto inDb = [&](int strip) { return engine.meters()[strip].toMap()["inDb"].toDouble(); };
        const QVariantList routes0 = engine.inputRoutes(); const double pan5 = engine.stripState()[5].toMap()["pan"].toDouble(), pan6 = engine.stripState()[6].toMap()["pan"].toDouble();
        apps.testMode = true; apps.testPrograms = QVariantList{QVariantMap{{"id", "loopback:app:1001"}, {"name", "Spotify"}}, QVariantMap{{"id", "loopback:app:1002"}, {"name", "Chrome (YouTube, web players)"}}};
        const QVariantList pl = apps.programs(); const bool listed = pl.size() == 2 && pl[1].toMap()["pid"].toInt() == 1002 && pl[0].toMap()["slot"].toInt() == -1;
        const QVariantMap a = apps.captureTest("Spotify", 440, 4, false), b = apps.captureTest("Chrome (YouTube, web players)", 880, 5, true); pause(600);
        const QVariantList r = engine.inputRoutes(); const QVariantList caps = apps.captures();
        const bool routed = a["ok"].toBool() && b["ok"].toBool() && r[4].toInt() == 272 + 16 + 0 && r[5].toInt() == 272 + 2 && r[6].toInt() == 272 + 3 && apps.count() == 2 && caps.size() == 2 && caps[0].toMap()["strips"].toList() == QVariantList{4} && caps[1].toMap()["strips"].toList() == (QVariantList{5, 6}) && caps[0].toMap()["state"].toString() == "live";
        const double l4 = inDb(4), l5 = inDb(5), l6 = inDb(6);
        const bool heard = std::fabs(caps[0].toMap()["peakDb"].toDouble() + 12.0) < 1.0 && std::fabs(caps[1].toMap()["peakDb"].toDouble() + 12.0) < 1.0 && l4 > -40 && l5 > -40 && l6 > -40 && inDb(7) < -100;   // the slots carry -12 dBFS; the strips receive it (their own trim and DSP, left by an earlier run, may change what their meters read)
        const bool named = engine.inputLabel(r[4].toInt()) == "APP · Spotify" && engine.inputLabel(r[5].toInt()) == "APP · Chrome L" && engine.inputLabel(r[6].toInt()) == "APP · Chrome R" && apps.slotOfInput(r[6].toInt()) == 1 && apps.slotOfInput(3) == -1;
        const bool panned = engine.stripState()[5].toMap()["pan"].toDouble() == -1 && engine.stripState()[6].toMap()["pan"].toDouble() == 1;
        QList<double> dbs; for (const QVariant& v : engine.stripState()) dbs << v.toMap()["db"].toDouble();
        const int pulled = engine.infUnused(); const bool kept = engine.stripState()[4].toMap()["db"].toDouble() > -59 && engine.stripState()[5].toMap()["db"].toDouble() > -59 && pulled >= 0;   // AUTO -INF leaves a program's strips alone
        for (int i = 0; i < dbs.size(); ++i) engine.setGain(i, dbs[i]);
        // a real program where Windows cannot capture one: refused with the reason, the strip keeps its input
        bool refused = true; if (!am::app::Capture::available()) { const QVariantMap x = apps.capture("loopback:app:1001", "Spotify", 8, false); refused = !x["ok"].toBool() && x["message"].toString().contains("Windows") && engine.inputRoutes()[8].toInt() == routes0[8].toInt() && apps.count() == 2; }
        const bool bad = !apps.capture("not-a-program", "x", 8, false)["ok"].toBool() && !apps.capture("loopback:app:1001", "Spotify", 999, false)["ok"].toBool();
        // WINDOWS RECORDING DEVICES: the list is Windows' recording devices only; capture, properties and the panels
        apps.testDevices = QVariantList{QVariantMap{{"backend", "wasapi"}, {"id", "{0.0.1.0}.{mic}"}, {"name", "Microphone (USB)"}, {"kind", "microphone"}, {"inChannels", 1}, {"outChannels", 0}, {"defaultRate", 48000.0}, {"defaultIn", true}},
          QVariantMap{{"backend", "wasapi"}, {"id", "{0.0.1.0}.{cam}"}, {"name", "Webcam microphone"}, {"kind", "microphone"}, {"inChannels", 2}, {"outChannels", 0}, {"defaultRate", 44100.0}},
          QVariantMap{{"backend", "wasapi"}, {"id", "{0.0.0.0}.{spk}"}, {"name", "Speakers"}, {"inChannels", 0}, {"outChannels", 2}}, QVariantMap{{"backend", "wasapi"}, {"id", "loopback:system"}, {"name", "Computer sound"}, {"inChannels", 2}},
          QVariantMap{{"backend", "asio"}, {"id", "X"}, {"name", "ASIO box"}, {"inChannels", 8}}};
        const QVariantList rd = apps.recordingDevices(); QVariantMap gotProps; QString gotId;
        const auto pc = QObject::connect(&apps, &Apps::propertiesReady, &app, [&](const QString& id, const QVariantMap& p) { gotId = id; gotProps = p; }); apps.readProperties("{0.0.1.0}.{cam}"); QObject::disconnect(pc);
        const bool devList = rd.size() == 2 && rd[0].toMap()["isDefault"].toBool() && rd[1].toMap()["name"].toString() == "Webcam microphone" && rd[1].toMap()["slot"].toInt() == -1 && apps.devicesAvailable();
        const bool devProps = gotId == "{0.0.1.0}.{cam}" && gotProps["ok"].toBool() && gotProps["bits"].toInt() == 24 && gotProps["enhancements"].toString() == "on" && gotProps["levelPct"].toInt() == 80;
        bool devRefused = !apps.captureDevice("loopback:system", "Computer sound", 8, false)["ok"].toBool() && !apps.captureDevice("", "nothing", 8, false)["ok"].toBool() && !apps.captureDevice("{0.0.1.0}.{cam}", "Webcam microphone", 999, false)["ok"].toBool();
        if (!am::app::Capture::deviceAvailable()) { const QVariantMap x = apps.captureDevice("{0.0.1.0}.{cam}", "Webcam microphone", 8, false); devRefused = devRefused && !x["ok"].toBool() && x["message"].toString().contains("Windows") && engine.inputRoutes()[8].toInt() == routes0[8].toInt() && apps.count() == 2; }
        const bool panels = apps.openWindowsSound("recording") && apps.lastPanel == "recording" && apps.openWindowsSound("mixer") && apps.openWindowsSound("settings") && apps.openWindowsSound("playback") && !apps.openWindowsSound("cmd.exe") && apps.lastPanel == "playback";
        apps.testDevices.clear();
        std::printf("amixos --smoke: WINDOWS SOUND recording devices listed %d, properties %d, bad picks refused %d, panels %d\n", devList, devProps, devRefused, panels);
        if (!(devList && devProps && devRefused && panels)) { std::fprintf(stderr, "amixos --smoke: WINDOWS SOUND failed\n"); return 99; }
        { // ONE INTERFACE FOR INPUT AND OUTPUT: endpoints of one box (Windows container id / ALSA card) pair up; built-in devices (null container), one-way boxes and loopbacks do not
          auto d = [](const char* be, const char* id, const char* name, int in, int out) { return QVariant(QVariantMap{{"backend", be}, {"id", id}, {"name", name}, {"inChannels", in}, {"outChannels", out}, {"defaultRate", 48000.0}}); };
          const QVariantList fake{d("wasapi", "{0.0.0.0}.{usbout}", "Speakers (USB Audio CODEC)", 0, 2), d("wasapi", "{0.0.1.0}.{usbin}", "Microphone (USB Audio CODEC)", 2, 0),
            d("wasapi", "{0.0.0.0}.{spk}", "Speakers (Realtek(R) Audio)", 0, 2), d("wasapi", "{0.0.1.0}.{mic}", "Microphone (Realtek(R) Audio)", 2, 0), d("wasapi", "{0.0.0.0}.{hdmi}", "Monitor (HDMI)", 0, 2),
            d("wasapi", "loopback:{0.0.0.0}.{usbout}", "App audio", 2, 0), d("alsa", "hw:CARD=Box,DEV=0", "Box USB, USB Audio", 4, 4), d("alsa", "front:CARD=Box,DEV=0", "Box USB, USB Audio Front", 0, 2),
            d("alsa", "plughw:CARD=Box,DEV=0", "Box USB, USB Audio", 4, 4), d("alsa", "hw:CARD=PCH,DEV=0", "HDA Intel PCH, ALC", 0, 2), d("alsa", "default", "Default", 2, 2), d("asio", "X", "ASIO box", 8, 8)};
          const QVariantMap cont{{"{0.0.0.0}.{usbout}", "{AAAA0000-1111-2222-3333-444455556666}"}, {"{0.0.1.0}.{usbin}", "{aaaa0000-1111-2222-3333-444455556666}"},
            {"{0.0.0.0}.{spk}", "{00000000-0000-0000-FFFF-FFFFFFFFFFFF}"}, {"{0.0.1.0}.{mic}", "{00000000-0000-0000-ffff-ffffffffffff}"}, {"{0.0.0.0}.{hdmi}", "{BBBB0000-1111-2222-3333-444455556666}"}};
          const QVariantList pairs = Apps::pairInterfaces(fake, cont);
          const QVariantMap p0 = pairs.value(0).toMap(), p1 = pairs.value(1).toMap();
          const bool paired = pairs.size() == 2 && p0["backend"] == "wasapi" && p0["in"] == "{0.0.1.0}.{usbin}" && p0["out"] == "{0.0.0.0}.{usbout}" && p0["name"] == "USB Audio CODEC" && p0["usb"].toBool()
            && p1["backend"] == "alsa" && p1["in"] == "plughw:CARD=Box,DEV=0" && p1["out"] == "plughw:CARD=Box,DEV=0" && p1["inChannels"].toInt() == 4 && p1["name"] == "Box USB";
          const bool cards = Apps::alsaCard("hw:CARD=Box,DEV=0") == "Box" && Apps::alsaCard("plughw:2,0") == "2" && Apps::alsaCard("default").isEmpty() && Apps::pairInterfaces(fake, {}).size() == 1;
          const QVariantMap wd = apps.windowsDefaults();   // on Linux an honest "Windows only"; on Windows the defaults (no test data set)
#ifdef _WIN32
          const bool winOk = wd.contains("playback") && wd.contains("endpoints");
#else
          const bool winOk = !wd["available"].toBool() && wd["note"].toString().startsWith("Windows only") && wd["endpoints"].toList().isEmpty();
#endif
          std::printf("amixos --smoke: ONE INTERFACE pairs %d (paired %d, ALSA cards %d), Windows defaults %d\n", int(pairs.size()), int(paired), int(cards), int(winOk));
          if (!(paired && cards && winOk)) { std::fprintf(stderr, "amixos --smoke: ONE INTERFACE / WINDOWS DEFAULTS failed\n"); return 99; } }
        { // PROGRAM PICTURES: the picker's icons and window pictures, a live picture that follows the program, and nothing left when it is forgotten
          int made = 0; appWindows.testSource = [&made](quint32 pid, bool icon) { ++made; QImage i(icon ? 32 : 320, icon ? 32 : 180, QImage::Format_RGB32); i.fill(QColor(int(pid % 200), 60, made % 255)); return i; };
          appWindows.testTitle = [](quint32 pid) { return QStringLiteral("Window of %1").arg(pid); };
          AppWindowImages prov(&appWindows); QSize sz; const QImage none = prov.requestImage("window/1001?0", &sz, QSize());
          const bool placeholder = !none.isNull() && sz == QSize(320, 180) && none.pixelColor(5, 5) == QColor(18, 20, 24);            // no picture yet: a dark tile, never an error
          const int t0 = appWindows.tick(); appWindows.snapshot(1001); pause(300);
          const QImage one = prov.requestImage("window/1001?1", &sz, QSize()); const bool snap = appWindows.tick() > t0 && one.pixelColor(5, 5).red() == 1001 % 200;
          const QImage ic = prov.requestImage("icon/1001", &sz, QSize(20, 20)); const bool icon = sz == QSize(20, 20) && !ic.isNull();
          const int t1 = appWindows.tick(); appWindows.watch(1001, true); pause(700); const int frames = appWindows.tick() - t1; const bool live = appWindows.isWatched(1001) && appWindows.watched().size() == 1 && frames >= 3 && frames <= 12;   // about 8 a second
          appWindows.watch(1001, false); pause(300); const int t2 = appWindows.tick(); pause(400); const bool stopped = appWindows.tick() == t2 && appWindows.watched().isEmpty();
          for (quint32 p = 2000; p < 2012; ++p) appWindows.watch(p, true); const bool capped = appWindows.watched().size() == 8; for (quint32 p = 2000; p < 2012; ++p) appWindows.forget(p);
          appWindows.forget(1001); const bool forgotten = prov.requestImage("window/1001?2", &sz, QSize()).pixelColor(5, 5) == QColor(18, 20, 24) && appWindows.titleOf(1001) == "Window of 1001";
          const bool junk = !prov.requestImage("window/abc", &sz, QSize()).isNull() && !prov.requestImage("", &sz, QSize()).isNull() && !prov.requestImage("icon/0", &sz, QSize()).isNull();
          appWindows.testSource = nullptr; appWindows.testTitle = nullptr;
          std::printf("amixos --smoke: PROGRAM PICTURES placeholder %d, snapshot %d, icon %d, live %d (%d pictures in 0.7 s), stops %d, 8 at most %d, forgotten %d, odd requests %d\n", placeholder, snap, icon, live, frames, stopped, capped, forgotten, junk);
          if (!(placeholder && snap && icon && live && stopped && capped && forgotten && junk)) { std::fprintf(stderr, "amixos --smoke: PROGRAM PICTURES failed\n"); return 100; } }
        // a slot stays while a strip listens to it, and is given back with its last strip
        engine.setInputRoute(5, -1); const bool stays = apps.count() == 2; engine.setInputRoute(6, routes0[6].toInt()); const bool freed = apps.count() == 1;
        apps.release(0); pause(300); const bool released = apps.count() == 0 && engine.inputRoutes()[4].toInt() == -1 && inDb(4) < -100 && apps.captures().isEmpty();
        // remembered by name: a console that starts with a strip on a program's slot waits for that program
        engine.setInputRoute(9, 272 + 16 + 2); QSettings().setValue("apps/captures", "[{\"slot\":2,\"name\":\"Deezer\"},{\"slot\":5,\"name\":\"Nobody listens\"}]");
        bool waits = false, still = false;
        { Apps again(&engine); const QVariantList w = again.captures(); waits = w.size() == 1 && w[0].toMap()["state"].toString() == "waiting" && w[0].toMap()["name"].toString() == "Deezer" && w[0].toMap()["strips"].toList() == QVariantList{9} && engine.inputLabel(272 + 16 + 2) == "APP · Deezer";
          again.sessionsNow = [] { return std::vector<std::pair<quint32, QString>>{{4242, QStringLiteral("Deezer")}}; }; again.poll(); pause(200); again.poll();
          still = again.count() == 1 && (am::app::Capture::available() || again.captures()[0].toMap()["state"].toString() == "waiting"); }
        engine.appInputName = [&apps](int input) { return apps.inputName(input); };
        engine.setInputRoute(9, routes0[9].toInt()); engine.setInputRoute(4, routes0[4].toInt()); engine.setInputRoute(5, routes0[5].toInt()); engine.setPan(5, pan5); engine.setPan(6, pan6); QSettings().remove("apps/captures"); apps.testMode = false; apps.testPrograms.clear();
        std::printf("amixos --smoke: APP CAPTURE programs listed %d, two on strips %d, heard %d (CH 5 %.1f, CH 6 %.1f, CH 7 %.1f dBFS), named %d, panned %d, AUTO -INF keeps them %d, refused without Windows %d, bad picks %d, slot stays %d / freed %d, released %d, waits for its program %d / %d\n",
                    listed, routed, heard, l4, l5, l6, named, panned, kept, refused, bad, stays, freed, released, waits, still);
        if (!(listed && routed && heard && named && panned && kept && refused && bad && stays && freed && released && waits && still)) { std::fprintf(stderr, "amixos --smoke: APP CAPTURE failed\n"); return 97; } }
      // AUTO INPUT: OFF closes the input (nothing captured), ON opens it again; "no input" never means COMPUTER SOUND behind the user's back
      const bool keepAuto = engine.micAtStart();
      engine.setAutoInput(false); const int offIn = engine.deviceInputs(); const bool offRun = engine.running(); const QString offSrc = engine.inputSource();
      engine.setAutoInput(true); const int onIn = engine.deviceInputs(); const QString onSrc = engine.inputSource();
      const bool noHidden = engine.autoInput("wasapi") == "-";
      std::printf("amixos --smoke: AUTO INPUT off: inputs %d (%s), sound running %d; on: inputs %d (%s); no hidden computer sound %d\n", offIn, qPrintable(offSrc), int(offRun), onIn, qPrintable(onSrc), int(noHidden));
      engine.setAutoInput(keepAuto);
      { // LOW LATENCY: the open device's report; the ROUND TRIP runs, gives the input and output back, and on a device with no loop says no click came back
        engine.stop(); engine.open("null", "", "", 48000, 256, 2, 2);
        const QVariantMap lr = engine.latencyReport(); const QVariantMap st = engine.startRoundTrip(); const QVariantMap again = engine.startRoundTrip();
        QElapsedTimer tw; tw.start(); while (engine.roundTripResult().value("state").toString() == "running" && tw.elapsed() < 5000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        const QVariantMap rr = engine.roundTripResult(); const bool stillRuns = engine.running(); const qint64 rtMs = tw.elapsed();
        const bool cleanTimeout = rtMs <= 2300 && rr.value("note").toString().startsWith("No click heard") && lr.value("driver").toString() == "null" && lr.value("frames").toInt() == 256;
        std::printf("amixos --smoke: LOW LATENCY driver %s, round trip with no loop stopped after %lld ms (clean %d)\n", qPrintable(lr.value("driver").toString()), static_cast<long long>(rtMs), int(cleanTimeout));
        std::printf("amixos --smoke: LOW LATENCY %s (%.1f ms, block %.2f ms, %d advice); round trip started %d, second refused %d, result %s, sound kept %d\n", qPrintable(lr.value("level").toString()), lr.value("totalMs").toDouble(), lr.value("blockMs").toDouble(),
                    int(lr.value("advice").toStringList().size()), int(st.value("ok").toBool()), int(!again.value("ok").toBool()), qPrintable(rr.value("state").toString()), int(stillRuns));
        if (!(lr.value("running").toBool() && !lr.value("level").toString().isEmpty() && lr.value("advice").toStringList().size() > 0 && st.value("ok").toBool() && !again.value("ok").toBool() && rr.value("state").toString() == "none" && stillRuns && cleanTimeout)) {
          std::fprintf(stderr, "amixos --smoke: LOW LATENCY failed\n"); return 1; } }
      if (!(offIn == 0 && offRun && offSrc == "none" && onIn >= 2 && onSrc == "mic" && noHidden)) { std::fprintf(stderr, "amixos --smoke: AUTO INPUT failed: %s\n", qPrintable(engine.status())); return 1; }
      engine.stop(); engine.open("null", "", "", r0, f0, 2, 2); }
    { // AUTO ZOOM on a 14" laptop, 1920 x 1080 (work area 1920 x 1032 device pixels): 100 % and 125 % fit as they are; 150 % is zoomed
      // to fit (drawn as at about 130 %); a user's fixed zoom wins; a large screen is never zoomed
      const auto z = [](double os, int pct) { return autozoom::factor(1920 / os, 1032 / os, pct); };
      { // 14 INCHES AND UNDER: AUTO enlarges to fill (1920 x 1080 at 100 % -> 1.25), a 27-inch monitor with the same pixels stays at 1.00;
        // RESPONSIVE fills both; nothing ever goes past the screen; 13.3" 1366 x 768 still shrinks to fit
        const double s14 = autozoom::factor(1920, 1032, 0, 14.0), s27 = autozoom::factor(1920, 1032, 0, 27.0), unk = autozoom::factor(1920, 1032, 0), fill27 = autozoom::factor(2560, 1392, autozoom::kFill, 27.0), small = autozoom::factor(1366, 728, 0, 13.3), fix125 = autozoom::factor(1920, 1032, 125, 14.0), s14at125 = autozoom::factor(1536, 824, 0, 14.0);
        std::printf("amixos --smoke: RESPONSIVE ZOOM 14\" 1080p %.2f, 27\" 1080p %.2f, unknown size %.2f, RESPONSIVE on 27\" 1440p %.2f, 13.3\" 1366x768 %.2f, fixed 125 %% %.2f, 14\" at 125 %% %.2f\n", s14, s27, unk, fill27, small, fix125, s14at125);
        if (!(s14 == 1.25 && 1480 * s14 <= 1920 && 800 * s14 <= 1032 && s27 == 1.0 && unk == 1.0 && fill27 == 1.70 && 1480 * fill27 <= 2560 && 800 * fill27 <= 1392 && small < 1.0 && 1480 * small <= 1366 + 15 && fix125 == 1.25 && s14at125 == 1.0)) { std::fprintf(stderr, "amixos --smoke: RESPONSIVE ZOOM failed\n"); return 102; } }
      const double z100 = z(1.0, 0), z125 = z(1.25, 0), z150 = z(1.5, 0), zUser = z(1.5, 90), z4k = autozoom::factor(3840 / 1.5, 2112 / 1.5, 0);
      std::printf("amixos --smoke: AUTO ZOOM 1920x1080: 100 %% -> %.2f, 125 %% -> %.2f, 150 %% -> %.2f (drawn as %.0f %%), fixed 90 %% -> %.2f, 4K at 150 %% -> %.2f\n", z100, z125, z150, z150 * 150, zUser, z4k);
      if (!(z100 == 1.0 && z125 == 1.0 && z150 < 0.9 && z150 * 1.5 >= 1.25 && z150 * 1.5 <= 1.35 && std::abs(zUser - 0.9) < 1e-9 && z4k == 1.0)) { std::fprintf(stderr, "amixos --smoke: AUTO ZOOM failed\n"); return 1; } }
    { // LOW-CPU INTERFACE: the meter rate follows the window (front / behind another app / minimised) and ECO; the process CPU is measured
      // in each state over 3 s of the live mixer (the interface itself, not the sound, is what is slowed)
      const bool eco0 = engine.ecoUi(); engine.setEcoUi(false);
      engine.setUiActivity(2); const int f2 = engine.uiFps(); engine.setEcoUi(true); const int fe = engine.uiFps(); engine.setEcoUi(false);
      engine.setUiActivity(1); const int f1 = engine.uiFps(); engine.setUiActivity(0); const int f0 = engine.uiFps();
      auto cpuPct = [&](int level) { engine.setUiActivity(level); const std::clock_t c0 = std::clock(); QElapsedTimer w; w.start();
        while (w.elapsed() < 3000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        return 100.0 * static_cast<double>(std::clock() - c0) / CLOCKS_PER_SEC / (w.elapsed() / 1000.0); };
      const double cFront = cpuPct(2), cBack = cpuPct(1), cMin = cpuPct(0); engine.setUiActivity(2); engine.setEcoUi(eco0);
      std::printf("amixos --smoke: LOW-CPU INTERFACE %d fps in front (ECO %d), %d behind another app, %d minimised; process CPU %.1f%% / %.1f%% / %.1f%% (one core, offscreen)\n", f2, fe, f1, f0, cFront, cBack, cMin);
      if (!(f2 == 30 && fe == 15 && f1 == 4 && f0 == 1)) { std::fprintf(stderr, "amixos --smoke: LOW-CPU INTERFACE failed\n"); return 1; } }
    { // the ten effects in the rack of any channel: listed, loaded into a slot, a knob moves, bypass, cleared; a late channel too
      { // 2 IN / 2 OUT at least on every driver: asking for none opens the first pair all the same (a device that reported 0 channels is not left silent)
        const double r0 = engine.sampleRate(); const int f0 = engine.blockFrames(); engine.stop();
        engine.open("null", "null-device", "", 48000, 256, 0, 0); const int in0 = engine.deviceInputs(); engine.stop();   // a NAMED device that reported 0 channels: the first pair opens (an unnamed input with 0 channels is "no input": see NO INPUT ASKED)
        engine.open("null", "", "", r0 > 0 ? r0 : 48000, f0 > 0 ? f0 : 256, 2, 2);
        const QVariantList st = engine.stripState(); const bool names = st.size() > 1 && st[0].toMap()["name"].toString() == "DIGITAL AUDIO" && st[1].toMap()["name"].toString() == "MIC" && !st[0].toMap()["link"].toBool();
        if (in0 < 2 || !names) { std::fprintf(stderr, "amixos --smoke: 2 in / 2 out (got %d in), CH 1-2 DIGITAL AUDIO / MIC %d\n", in0, names); return 160; } }
      { // YOUR FILE in the SAMPLE PLAYER: a file the user picks plays instead of the built-in sample; BUILT-IN goes back; a missing file is reported, not a crash
        const QString wav = QDir::tempPath() + "/amfx1-smoke-own.wav"; { std::vector<float> l(4410, 0.25f); std::string e; am::arr::writeWav24(wav.toStdString(), l, l, 44100, e); }
        const int k = engine.addToRack(22, "fx:sampler"); const QString e1 = engine.loadSampleFile(22, k, QUrl::fromLocalFile(wav).toString()); const QString shown = engine.sampleFileOf(22, k);
        const QString e2 = engine.loadSampleFile(22, k, QDir::tempPath() + "/amfx1-no-such-file.flac"); engine.loadSampleFile(22, k, ""); const QString back = engine.sampleFileOf(22, k);
        if (k >= 0) engine.setPlugin(22, k, ""); QFile::remove(wav);
        if (k < 0 || !e1.isEmpty() || shown != "amfx1-smoke-own.wav" || e2.isEmpty() || !back.isEmpty()) { std::fprintf(stderr, "amixos --smoke: own sample file (slot %d, \"%s\", shown \"%s\", missing \"%s\")\n", k, qPrintable(e1), qPrintable(shown), qPrintable(e2)); return 161; } }
      { // the strip menu's PLAY A SAMPLE: the channel's SAMPLE PLAYER is set to that sample and playing
        const int k = engine.playSampleOnStrip(21, 7); const QVariantList p = k >= 0 ? engine.pluginParams(21, k) : QVariantList();
        const bool ok = k >= 0 && p.size() >= 2 && p[0].toMap()["text"].toString().startsWith("7 ") && p[1].toMap()["text"].toString() == "ON" && engine.playSampleOnStrip(21, 0) == -1;
        // ... and it is HEARD: the channel's meter and the master move (a quiet channel that the engine lets rest still plays its rack),
        // on a low channel and on the last one the plan allows
        auto heard = [&](int strip) { engine.setGain(strip, 0); engine.setMute(strip, false); const int s = engine.playSampleOnStrip(strip, 1); double ch = -120, ms = -120; QElapsedTimer t; t.start();
          while (t.elapsed() < 400) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); engine.tick(); ch = std::max(ch, engine.meterAt(strip)["peakDb"].toDouble()); ms = std::max(ms, engine.telemetry()["masterPeakDb"].toDouble()); }
          if (s >= 0) engine.setPlugin(strip, s, ""); return std::make_pair(ch, ms); };
        const int last = std::max(0, std::min(engine.licensedChannels(), 32) - 1); const auto h0 = heard(1), h1 = heard(last);
        std::printf("amixos --smoke: SAMPLE PADS on a channel heard: CH 2 %.1f dBFS (master %.1f), CH %d %.1f dBFS (master %.1f)\n", h0.first, h0.second, last + 1, h1.first, h1.second);
        if (k >= 0) engine.setPlugin(21, k, "");
        if (!ok || h0.first < -40 || h0.second < -40 || h1.first < -40 || h1.second < -40) { std::fprintf(stderr, "amixos --smoke: play a sample on a strip (slot %d) / not heard\n", k); return 159; } }
      { // BUFFER 8192: the largest block opens on the null device and runs; no size (0) opens with the 256 default; 20000 frames is refused
        const double r0 = engine.sampleRate(); const int f0 = engine.blockFrames(); engine.stop();
        const bool ok = engine.open("null", "", "", 48000, 8192, 2, 2); QElapsedTimer t; t.start(); while (t.elapsed() < 700) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        const bool at = engine.blockFrames() == 8192 && engine.running(); const double lat = 8192 * 1000.0 / 48000.0; engine.stop();
        const bool zero = engine.open("null", "", "", 48000, 0, 2, 2) && engine.blockFrames() == 256; engine.stop(); const bool huge = !engine.open("null", "", "", 48000, 20000, 2, 2);   // no size given: the console opens with 256; a size past the limit is refused
        engine.stop(); engine.open("null", "", "", r0 > 0 ? r0 : 48000, f0 > 0 ? f0 : 256, 2, 2);
        std::printf("amixos --smoke: BUFFER 8192 opens and runs %d %d (%.0f ms a block); 0 opens with 256 %d; 20000 frames refused %d\n", ok, at, lat, zero, huge);
        if (!ok || !at || !zero || !huge) { std::fprintf(stderr, "amixos --smoke: BUFFER 8192 failed\n"); return 105; } }
      { // WINDOWS MICROPHONE PRIVACY: switched off in Windows, the console opens no microphone by itself and takes it back when allowed again
        const auto was = engine.privacyNow; int answer = 1; engine.privacyNow = [&answer] { return answer; };
        engine.stop(); engine.checkMicPrivacy(); const bool seen = engine.micPrivacyOff();
        const bool openOff = engine.open("null", "", "", 48000, 256, 2, 2) && engine.deviceInputs() == 0 && engine.deviceOutputs() == 2 && engine.micOffNote().contains("Windows privacy"); engine.stop();
        answer = -1; engine.checkMicPrivacy(); const bool unknownKeeps = engine.micPrivacyOff();          // Windows gives no answer: nothing changes
        int changes = 0; const auto c = QObject::connect(&engine, &Engine::micPrivacyChanged, [&changes] { ++changes; });
        answer = 0; engine.checkMicPrivacy(); engine.checkMicPrivacy(); const bool back = !engine.micPrivacyOff() && changes == 1;
        const bool openOn = engine.open("null", "", "", 48000, 256, 2, 2) && engine.deviceInputs() == 2; QObject::disconnect(c); engine.privacyNow = was; engine.checkMicPrivacy();
        std::printf("amixos --smoke: MIC PRIVACY off is seen %d, opens outputs only %d, no answer keeps it %d, allowed again once %d, the microphone is back %d\n", seen, openOff, unknownKeeps, back, openOn);
        if (!seen || !openOff || !unknownKeeps || !back || !openOn) { std::fprintf(stderr, "amixos --smoke: MIC PRIVACY failed\n"); return 106; } }
      { // A MICROPHONE IS A MICROPHONE: computer sound and one program's sound are inputs, but never microphones. The microphone rules
        // (Windows privacy off, MIC ACCESS OFF, ONE VOICE) leave them alone.
        const bool kinds = Engine::isMicrophoneInput("") && Engine::isMicrophoneInput("{0.0.1.00000000}.{mic}") && !Engine::isMicrophoneInput("loopback:system")
          && !Engine::isMicrophoneInput("loopback:app:4242") && !Engine::isMicrophoneInput("loopback:{0.0.0.00000000}.{speakers}") && !Engine::isMicrophoneInput("-");
        const auto was = engine.privacyNow; engine.privacyNow = [] { return 1; }; engine.stop(); engine.checkMicPrivacy(); const bool keepAccess = engine.micAccessOff(); engine.setMicAccessOff(true);
        // both microphone switches are off, and a computer-sound input still opens with its two channels
        const bool music = engine.open("null", "loopback:system", "", 48000, 256, 2, 2) && engine.deviceInputs() == 2 && engine.micOffNote().isEmpty(); engine.stop();
        const bool app = engine.open("null", "loopback:app:4242", "", 48000, 256, 2, 2) && engine.deviceInputs() == 2; engine.stop();
        const bool mic = engine.open("null", "", "", 48000, 256, 2, 2) && engine.deviceInputs() == 0; engine.stop();   // the microphone itself stays closed
        engine.setMicAccessOff(keepAccess); engine.privacyNow = [] { return 0; }; engine.checkMicPrivacy(); engine.privacyNow = was; engine.open("null", "", "", 48000, 256, 2, 2);   // allowed again before the real reader is put back ("no answer" would keep it off)
        std::printf("amixos --smoke: MIC IS MIC kinds %d, computer sound opens with the microphone off %d, a program's sound too %d, the microphone stays closed %d\n", kinds, music, app, mic);
        if (!kinds || !music || !app || !mic) { std::fprintf(stderr, "amixos --smoke: MIC IS MIC failed\n"); return 108; } }
      { // a custom rate as high as 1100 kHz opens on the null device and runs (the engine has no rate ceiling of its own)
        const double r0 = engine.sampleRate(); const int f0 = engine.blockFrames(); engine.stop();
        const bool ok = engine.open("null", "", "", 1100000, 1024, 2, 2); QElapsedTimer t; t.start(); while (t.elapsed() < 300) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        const bool at = std::fabs(engine.sampleRate() - 1100000) < 1; engine.stop(); engine.open("null", "", "", r0 > 0 ? r0 : 48000, f0 > 0 ? f0 : 256, 2, 2);
        if (!ok || !at) { std::fprintf(stderr, "amixos --smoke: 1100 kHz custom rate (open %d, rate %.0f)\n", ok, engine.sampleRate()); return 158; } }
      { // LOCAL STORE: an effect switched off leaves the rack chooser and comes back when switched on; unknown ids are ignored
        auto has = [&](const char* k) { for (const QVariant& v : engine.pluginCatalog()) if (v.toMap()["kind"].toString() == k) return true; return false; };
        const int n0 = engine.storeItems().size(); engine.setStoreEnabled("fx:chorus", false); const bool gone = !has("fx:chorus"); engine.setStoreEnabled("fx:chorus", true); const bool back = has("fx:chorus");
        engine.setStoreEnabled("rm -rf", false); const bool same = engine.storeItems().size() == n0;
        if (n0 < 25 || !gone || !back || !same) { std::fprintf(stderr, "amixos --smoke: local store (%d items, gone %d, back %d)\n", n0, gone, back); return 157; } }
      { // the device's own sample format is reported while it runs; the record format choice keeps the old 24 / float switch in step
        const QString df = engine.deviceFormat(); const int rf0 = engine.recordFormat();
        engine.setRecordFormat(2); const bool f64 = engine.recordFormat() == 2 && engine.recordFloat(); engine.setRecordFormat(3); const bool p32 = engine.recordFormat() == 3 && !engine.recordFloat();
        engine.setRecordFormat(9); const bool clamp = engine.recordFormat() == 4; engine.setRecordFloat(false); const bool back = engine.recordFormat() == 0; engine.setRecordFormat(rf0);
        if ((engine.running() && df.isEmpty()) || !f64 || !p32 || !clamp || !back) { std::fprintf(stderr, "amixos --smoke: formats (device \"%s\", 64f %d, 32i %d, clamp %d, back %d)\n", qPrintable(df), f64, p32, clamp, back); return 156; } }
      { // COMPRESSION TARGET: listens first, then suggests; nothing is applied without the call; the apply sets the channel compressor
        const QVariantMap before = engine.compSuggestion(0); const QVariantMap ch0 = engine.channelParams(0);
        engine.setCompTargetDb(6); bool okT = std::fabs(engine.compTargetDb() - 6) < 1e-9; engine.setCompTargetDb(100); okT = okT && std::fabs(engine.compTargetDb() - 30) < 1e-9; engine.setCompTargetDb(6);
        const bool untouched = engine.channelParams(0)["compOn"] == ch0["compOn"] && engine.channelParams(0)["compThresholdDb"] == ch0["compThresholdDb"];
        if (!okT || !before.contains("ready") || !untouched) { std::fprintf(stderr, "amixos --smoke: compression target (clamp %d, keys %d, untouched %d)\n", okT, before.contains("ready"), untouched); return 155; }
        if (before["ready"].toBool() && engine.applyCompSuggestion(0)) { const QVariantMap after = engine.channelParams(0); if (std::fabs(after["compThresholdDb"].toDouble() - before["thresholdDb"].toDouble()) > 1e-6) { std::fprintf(stderr, "amixos --smoke: compression target apply\n"); return 155; } engine.setCompressor(0, ch0["compOn"].toBool(), ch0["compThresholdDb"].toDouble(), ch0["compRatio"].toDouble(), ch0["compKneeDb"].toDouble(), ch0["compAttackMs"].toDouble(), ch0["compReleaseMs"].toDouble(), ch0["compMakeupDb"].toDouble()); } }
      { // the rack helpers of the strip menu and the DUAL LISTEN switch: addToRack finds the slot again, never a second copy
        const int a = engine.addToRack(20, "fx:sampler"), b = engine.addToRack(20, "fx:sampler"), d = engine.addToRack(20, "fx:dualeq"); int dual = -1, n = 0;
        for (const QVariant& v : engine.plugins(20)) { const QVariantMap m = v.toMap(); if (m["kind"].toString() == "fx:sampler") ++n; if (m["kind"].toString() == "fx:dualeq") dual = m["slot"].toInt(); }
        const QVariantList sp = engine.pluginParams(20, a); const bool txt = sp.size() >= 4 && sp[0].toMap()["text"].toString().startsWith("1 ");
        if (a < 0 || a != b || n != 1 || d < 0 || dual != d || !txt) { std::fprintf(stderr, "amixos --smoke: rack helpers (sampler %d/%d x%d, dual %d/%d, text %d: %d params, first \"%s\")\n", a, b, n, d, dual, txt, static_cast<int>(sp.size()), sp.isEmpty() ? "" : qPrintable(sp[0].toMap()["text"].toString())); return 154; }
        engine.setPlugin(20, a, ""); engine.setPlugin(20, d, ""); }
      int fxn = 0; for (const QVariant& v : engine.pluginCatalog()) if (v.toMap().value("fx").toBool()) ++fxn; if (fxn != 52) { std::fprintf(stderr, "amixos --smoke: %d effects in the rack catalogue (want 52: 48 + the I/O FRAME objects AEC, TELEPHONE HYBRID, LOGIC / GPIO + AmF DYNAMIC EQ)\n", fxn); return 84; }
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
      engine.setOutputBits(16); engine.setDcBlock(true); if (engine.outputBits() != 16 || !engine.dcBlock()) { std::fprintf(stderr, "amixos --smoke: output stage settings\n"); return 115; } engine.setOutputBits(-1); if (engine.outputBits() != -1 || engine.effectiveOutputBits() != 0) { std::fprintf(stderr, "amixos --smoke: AUTO word length\n"); return 115; } media.clear(); }
    // SAMPLES: the bank is rendered, a pad plays into the master, stops; EXPORT writes 100 WAVs. FX PRESETS: 300 listed, one applied loads the kind.
    { if (!samples.ready() || samples.list().size() != 100) { std::fprintf(stderr, "amixos --smoke: samples bank\n"); return 120; }
      if (!samples.play(1) || !samples.play(7)) { std::fprintf(stderr, "amixos --smoke: sample trigger\n"); return 121; } waitMs(200);
      if (engine.telemetry()["masterPeakDb"].toDouble() < -40) { std::fprintf(stderr, "amixos --smoke: sample not heard on the master (%.1f dBFS)\n", engine.telemetry()["masterPeakDb"].toDouble()); return 122; } samples.stopAll();
      // 1000 CUSTOM PADS + 1000 SOUND EFFECTS: listed, a pad and an effect heard, a pad changed and put back (the shared file)
      { if (samples.padCount() != 1000 || samples.sfxFamilies().size() != 20 || samples.sfx(QString()).size() != 1000 || samples.pads(1, 1000).size() != 1000 || samples.pad(101)["source"].toString() != "sfx:1") { std::fprintf(stderr, "amixos --smoke: 1000 pads / effects listed\n"); return 124; }
        waitMs(300); if (!samples.playPad(150) || !samples.playSfx(999)) { std::fprintf(stderr, "amixos --smoke: pad / effect play: %s\n", qPrintable(samples.message())); return 125; } waitMs(200);
        const double pk = engine.telemetry()["masterPeakDb"].toDouble(); samples.stopAll();
        const int v0 = samples.padsVersion(); const QVariantMap before = samples.pad(777);
        const bool set = samples.setPad(777, "sfx:5", "Smoke Pad", -3, 2) && samples.pad(777)["name"].toString() == "Smoke Pad" && samples.padsVersion() > v0;
        const bool refused = !samples.setPad(777, "sfx:5000", "", 0, 0) && !samples.setPadFile(777, QDir::tempPath() + "/no-such-file.wav");
        const bool back = before["custom"].toBool() ? samples.setPad(777, before["source"].toString(), before["name"].toString(), before["gainDb"].toDouble(), before["color"].toInt()) : samples.resetPad(777);
        std::fprintf(stderr, "amixos --smoke: 1000 PADS + 1000 EFFECTS: pad and effect heard %.1f dBFS, pad changed %d, bad sources refused %d, put back %d\n", pk, set, refused, back);
        if (pk < -40 || !set || !refused || !back) return 126; }
      const QString sdir = QDir::tempPath() + "/amixos-smoke-samples"; QDir(sdir).removeRecursively(); std::string serr; if (!am::samples::writeAll(sdir.toStdString(), 48000, serr) || QDir(sdir).entryList({"*.wav"}, QDir::Files).size() != 100) { std::fprintf(stderr, "amixos --smoke: sample export: %s\n", serr.c_str()); return 123; } QDir(sdir).removeRecursively();
      if (engine.fxPresets().size() != 614 || !engine.applyFxPreset(8, 301) || engine.busFx(8)["kind"].toString() != "pingpong" || engine.busFx(8)["params"].toList().size() != 6 || !engine.applyFxPreset(8, 1) || engine.busFx(8)["kind"].toString() != "reverb" || engine.busFxPreset(8) != 1) { std::fprintf(stderr, "amixos --smoke: fx presets\n"); return 124; }
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
        const QString keepPref = engine.autoPrefer();
        engine.simulateDevices(pc); { const QVariantMap w = engine.autoDetect();   // the default: AUTO is the system's WASAPI speakers with the default microphone
          if (w["backend"] != "wasapi" || w["out"] != "{r-out}" || w["in"] != "{r-in}") { std::fprintf(stderr, "amixos --smoke: AUTO did not pick the WASAPI default speakers + microphone: %s %s / %s\n", qPrintable(w["backend"].toString()), qPrintable(w["out"].toString()), qPrintable(w["in"].toString())); return 169; } }
        { QVariantList own = pc; own.prepend(dev("asio", "Audio Mixer", "Audio Mixer", 2, 2)); own.prepend(dev("asio", "BEHRINGER USB AUDIO", "BEHRINGER USB AUDIO", 0, 0)); engine.simulateDevices(own);   // AMFB's own virtual ASIO driver is never automatic
          for (const char* pf : {"best", "asio", "wasapi"}) { engine.setAutoPrefer(pf); const QVariantMap a = engine.autoDetect();
            if (a["name"].toString() == "Audio Mixer" || engine.bestAsioDriver() == "Audio Mixer") { std::fprintf(stderr, "amixos --smoke: AUTO (%s) picked AMFB's own virtual ASIO driver\n", pf); return 168; } }
          engine.simulateDevices(pc); }
        { // UNIVERSAL ASIO DETECTION: any brand identified, a busy driver keeps its last channel counts, virtual drivers and wrappers rank last
          QVariantList u = pc; u.prepend(dev("asio", "Focusrite USB ASIO", "Focusrite USB ASIO", 18, 20)); u << dev("asio", "Voicemeeter Virtual ASIO", "Voicemeeter Virtual ASIO", 8, 8) << dev("asio", "ASIO4ALL v2", "ASIO4ALL v2", 2, 2);
          engine.simulateDevices(u);   // read once: the counts are remembered
          QVariantMap busy = u[0].toMap(); busy["inChannels"] = 0; busy["outChannels"] = 0; busy["probeError"] = "did not start: the device is in use"; u[0] = busy; engine.simulateDevices(u);
          QVariantMap fr, vm, a4; for (const QVariant& v : engine.asioDrivers()) { const QVariantMap m = v.toMap(); if (m["id"] == "Focusrite USB ASIO") fr = m; else if (m["id"] == "Voicemeeter Virtual ASIO") vm = m; else if (m["id"] == "ASIO4ALL v2") a4 = m; }
          if (fr["maker"] != "Focusrite" || fr["status"] != "busy" || !fr["lastSeen"].toBool() || fr["inChannels"].toInt() != 18 || fr["outChannels"].toInt() != 20 || vm["asioType"] != "virtual" || vm["status"] != "virtual" || a4["asioType"] != "wrapper") {
            std::fprintf(stderr, "amixos --smoke: universal ASIO detection: focusrite %s/%s/%d+%d, voicemeeter %s, asio4all %s\n", qPrintable(fr["maker"].toString()), qPrintable(fr["status"].toString()), fr["inChannels"].toInt(), fr["outChannels"].toInt(), qPrintable(vm["asioType"].toString()), qPrintable(a4["asioType"].toString())); return 167; }
          if (engine.bestAsioDriver() != "Focusrite USB ASIO") { std::fprintf(stderr, "amixos --smoke: ASIO ranking picked %s over the busy Focusrite's own driver\n", qPrintable(engine.bestAsioDriver())); return 167; }
          engine.setAutoPrefer("asio"); const QVariantMap a = engine.autoDetect(); engine.setAutoPrefer("best");
          if (a["id"] != "Focusrite USB ASIO" || a["in"] == "-") { std::fprintf(stderr, "amixos --smoke: AUTO (asio) picked %s (in %s)\n", qPrintable(a["id"].toString()), qPrintable(a["in"].toString())); return 167; }
          QSettings().remove("asio/seen/Focusrite USB ASIO"); engine.simulateDevices(pc); }
        engine.setAutoPrefer("best");   // the UM2 checks below are for BEST (an interface's own ASIO driver first)
        const QVariantMap d0 = engine.autoDetect();
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
        engine.setAutoPrefer(keepPref);
        std::printf("amixos --smoke: UM2 machine OK: detect -> %s (%s); plan %s -> %s (%s / %s) -> %s -> %s (%s); DirectSound-only -> %d steps\n", qPrintable(d1["name"].toString()), qPrintable(d1["why"].toString()), qPrintable(plan[0].toMap()["mode"].toString()), qPrintable(plan[1].toMap()["mode"].toString()), qPrintable(plan[1].toMap()["in"].toString()), qPrintable(plan[1].toMap()["out"].toString()), qPrintable(plan[2].toMap()["mode"].toString()), qPrintable(plan[3].toMap()["mode"].toString()), qPrintable(plan[3].toMap()["out"].toString()), int(plan2.size()));
        { // a laptop with no ASIO: the pick is an output endpoint, and it must come with an input (it used to start with 0 inputs); a headset pairs with its own microphone; kinds are named
          const QVariantList lap = {dev("wasapi", "{spk}", "Speakers (Realtek High Definition Audio)", 0, 2, false, true), dev("wasapi", "{mic}", "Microphone Array (Realtek High Definition Audio)", 2, 0, true, false), dev("wasapi", "{hp}", "Headphones (Realtek High Definition Audio)", 0, 2), dev("wasapi", "{bt}", "Headphones (WH-1000XM4 Stereo)", 0, 2), dev("dsound", "{ds-spk}", "Speakers (Realtek High Definition Audio)", 0, 2, false, true), dev("dsound", "{ds-mic}", "Microphone Array (Realtek High Definition Audio)", 2, 0, true, false)};
          engine.simulateDevices(lap); const QVariantMap l = engine.autoDetect(); QStringList kinds; for (const QVariant& v : engine.devices()) kinds << v.toMap()["kind"].toString();
          if (l["out"] != "{spk}" || l["in"] != "{mic}" || l["inChannels"].toInt() != 2 || l["inKind"] != "microphone" || l["outKind"] != "speakers") { std::fprintf(stderr, "amixos --smoke: laptop detection: out %s in %s (%d in, %s / %s)\n", qPrintable(l["out"].toString()), qPrintable(l["in"].toString()), l["inChannels"].toInt(), qPrintable(l["inKind"].toString()), qPrintable(l["outKind"].toString())); return 175; }
          if (kinds != QStringList({"speakers", "microphone", "headphones", "bluetooth", "speakers", "microphone"})) { std::fprintf(stderr, "amixos --smoke: device kinds: %s\n", qPrintable(kinds.join(", "))); return 176; }
          std::printf("amixos --smoke: laptop without ASIO: %s + %s (%d in / %d out); kinds %s\n", qPrintable(l["outName"].toString()), qPrintable(l["inName"].toString()), l["inChannels"].toInt(), l["outChannels"].toInt(), qPrintable(kinds.join(", "))); }
        { // THE DEFAULT MICROPHONE and DEVICES OFF: a USB microphone set as the system default is paired (not the Realtek one by name);
          // a device switched OFF is never auto-picked and is not opened by hand; switched ON again it is
          const QVariantList lap2 = {dev("wasapi", "{spk}", "Speakers (Realtek High Definition Audio)", 0, 2, false, true), dev("wasapi", "{mic}", "Microphone Array (Realtek High Definition Audio)", 2, 0, false, false),
                                     dev("wasapi", "{usbmic}", "Microphone (USB Audio CODEC)", 1, 0, true, false), dev("wasapi", "{hp}", "Headphones (Realtek High Definition Audio)", 0, 2)};
          engine.simulateDevices(lap2); const QVariantMap a1 = engine.autoDetect();
          engine.setDeviceEnabled("wasapi", "{spk}", false); const QVariantMap a2 = engine.autoDetect();
          engine.setDeviceEnabled("wasapi", "{usbmic}", false); const QVariantMap a3 = engine.autoDetect();
          const bool refused = !engine.open("wasapi", "-", "{spk}", 48000, 256, 0, 2);
          engine.setDeviceEnabled("wasapi", "{spk}", true); engine.setDeviceEnabled("wasapi", "{usbmic}", true); const QVariantMap a4 = engine.autoDetect();
          const bool ok = a1["in"] == "{usbmic}" && a2["out"] != "{spk}" && a3["in"] == "{mic}" && refused && a4["out"] == "{spk}" && a4["in"] == "{usbmic}";
          std::printf("amixos --smoke: default microphone and devices ON / OFF %s (default mic %s; speakers OFF -> %s; USB mic OFF -> %s; open OFF refused %d)\n", ok ? "ok" : "FAILED",
                      qPrintable(a1["in"].toString()), qPrintable(a2["out"].toString()), qPrintable(a3["in"].toString()), refused);
          if (!ok) return 178; }
        { // MIC ACCESS OFF: on Windows audio no microphone of any kind is opened (the note says so); an ASIO interface keeps its inputs
          engine.setMicAccessOff(true); engine.open("wasapi", "{mic}", "{spk}", 48000, 256, 2, 2); const bool blocked = engine.micOffNote().contains("MIC ACCESS OFF");
          engine.open("asio", "{mic}", "{spk}", 48000, 256, 2, 2); const bool asioKept = !engine.micOffNote().contains("MIC ACCESS OFF"); engine.setMicAccessOff(false);
          engine.open("wasapi", "{mic}", "{spk}", 48000, 256, 2, 2); const bool back = !engine.micOffNote().contains("MIC ACCESS OFF");
          if (!blocked || !asioKept || !back) { std::fprintf(stderr, "amixos --smoke: mic access off (blocked %d, asio kept %d, back on %d)\n", blocked, asioKept, back); return 169; } }
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
      { // A NEW INSTALLATION starts in the factory setup, and the setup is locked: nothing under it can be clicked
        const QString ini = QDir::tempPath() + "/amix-setup-test.ini"; QFile::remove(ini);
        bool fresh, again, closed, used, kept, test;
        { QSettings s(ini, QSettings::IniFormat); fresh = !Settings::decideSetup(s, false); again = !Settings::decideSetup(s, false); }      // empty settings: the setup, and still the setup at the next ask
        { QSettings s(ini, QSettings::IniFormat); s.setValue("audio/rate", 48000); closed = !Settings::decideSetup(s, false); }              // closed mid-setup, something else was saved: still the setup
        QFile::remove(ini); { QSettings s(ini, QSettings::IniFormat); s.setValue("ui/lang", "en"); used = Settings::decideSetup(s, false); }  // a console in use, updated: straight to the console
        { QSettings s(ini, QSettings::IniFormat); s.setValue("general/setupDone", false); kept = !Settings::decideSetup(s, false); }          // a factory reset asked for it: kept
        QFile::remove(ini); { QSettings s(ini, QSettings::IniFormat); test = Settings::decideSetup(s, true); } QFile::remove(ini);
        auto* win = qobject_cast<QQuickWindow*>(qml.rootObjects().first()); win->setProperty("forceSetup", true); QCoreApplication::processEvents();
        QQuickItem* wiz = win->findChild<QQuickItem*>("setupPage"); QQuickItem* blocker = win->findChild<QQuickItem*>("setupBlocker");
        const bool covers = wiz && blocker && wiz->isVisible() && blocker->isEnabled() && qAbs(blocker->width() - win->width()) < 1.5 && qAbs(blocker->height() - win->height()) < 1.5 && win->property("setupLocked").toBool();
        const QString before = win->property("current").toString();
        // a click where the navigation rail is: with the setup up it must not open a page
        for (const QPointF p : {QPointF(40, 200), QPointF(40, 330), QPointF(300, 20)}) { QMouseEvent d(QEvent::MouseButtonPress, p, win->mapToGlobal(p.toPoint()), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier), u(QEvent::MouseButtonRelease, p, win->mapToGlobal(p.toPoint()), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
          QCoreApplication::sendEvent(win, &d); QCoreApplication::sendEvent(win, &u); QCoreApplication::processEvents(); }
        const bool locked = win->property("current").toString() == before;
        win->setProperty("forceSetup", false); QCoreApplication::processEvents(); const bool open = !win->property("setupLocked").toBool();
        std::printf("amixos --smoke: FACTORY SETUP new install asks %d (again %d, closed mid-way %d), a console in use does not %d, a reset is kept %d, tests skip %d; locked: covers %d, clicks do nothing %d, open after %d\n", fresh, again, closed, used, kept, test, covers, locked, open);
        if (!fresh || !again || !closed || !used || !kept || !test || !covers || !locked || !open) { std::fprintf(stderr, "amixos --smoke: FACTORY SETUP failed\n"); return 109; } }
      { // ZOOM: in, out and actual size, clamped to 50 .. 200 %, and the console is drawn at that scale; MULTITRACK: at most 100 armed channels
        auto* win = qobject_cast<QQuickWindow*>(qml.rootObjects().first()); QQuickItem* zr = win->findChild<QQuickItem*>("zoomRoot");
        const double z0 = settings.liveZoom(); settings.zoomReset(); settings.zoomIn(); settings.zoomIn(); QCoreApplication::processEvents();
        const bool in = std::abs(settings.liveZoom() - 1.2) < 1e-9 && zr && std::abs(zr->scale() - 1.2) < 1e-6 && std::abs(zr->width() * 1.2 - win->width()) < 1.5;
        for (int k = 0; k < 30; ++k) settings.zoomOut(); const bool floor = std::abs(settings.liveZoom() - 0.5) < 1e-9;
        for (int k = 0; k < 30; ++k) settings.zoomIn(); const bool ceil = std::abs(settings.liveZoom() - 2.0) < 1e-9;
        settings.zoomReset(); QCoreApplication::processEvents(); const bool back = zr && std::abs(zr->scale() - 1.0) < 1e-9; settings.setLiveZoom(z0);
        std::vector<int> was; for (int i = 0; i < 110; ++i) if (engine.armed(i)) was.push_back(i);
        for (int i = 0; i < 110; ++i) engine.setArmed(i, true); const int armed = engine.armedCount(); const bool said = engine.recordMessage().contains("100");
        for (int i = 0; i < 110; ++i) engine.setArmed(i, false); for (int i : was) engine.setArmed(i, true);
        std::printf("amixos --smoke: ZOOM in 120 %% %d, floor 50 %% %d, ceiling 200 %% %d, actual size %d; MULTITRACK armed %d of 110 asked (said %d)\n", in, floor, ceil, back, armed, said);
        if (!in || !floor || !ceil || !back || armed != 100 || !said) { std::fprintf(stderr, "amixos --smoke: ZOOM / MULTITRACK failed\n"); return 110; } }
      { // MASTER MUTE comes back to where the fader was; LIMIT CHANNELS switches every channel limiter; AUTO -INF pulls down the strips without an input
        { engine.setMonitorSync(true); engine.setMaster(0); const double m0 = engine.monitor()["db"].toDouble(); engine.setMaster(-6);
          const bool follow = std::fabs(engine.monitor()["db"].toDouble() - (m0 - 6)) < 1e-9; engine.setMonitor(engine.monitor()["source"].toInt(), m0 - 6 + 3, false, false, false);
          const bool back = std::fabs(engine.masterDb() + 3) < 1e-9; engine.setMonitorSync(false); engine.setMaster(0); const bool free = std::fabs(engine.monitor()["db"].toDouble() - (m0 - 3)) < 1e-9;
          engine.setMonitorSync(true); engine.setMonitor(engine.monitor()["source"].toInt(), m0, false, false, false); engine.setMaster(0);
          if (!follow || !back || !free) { std::fprintf(stderr, "amixos --smoke: master / monitor SYNC\n"); return 153; } }
        engine.setMaster(-4.5); engine.setMasterMuted(true); const bool down = engine.masterDb() <= -59.9; engine.setMasterMuted(false);
        if (!down || std::fabs(engine.masterDb() + 4.5) > 1e-6 || engine.masterMuted()) { std::fprintf(stderr, "amixos --smoke: master mute\n"); return 212; } engine.setMaster(0);
        { // LIVE MODE: START puts the voice chain on the channel, holds the master and switches the watchdog on; STOP takes out exactly what it put in
          auto kinds = [&](int st) { QStringList l; for (const QVariant& v : engine.plugins(st)) { const QString k = v.toMap()["kind"].toString(); if (!k.isEmpty()) l << k; } return l; };
          const QStringList before = kinds(5); const int hWas = engine.headroomDb(); const bool wWas = engine.watchdog(); const bool hadRider = before.contains("fx:rider");
          const int riderSlot = engine.addToRack(5, "fx:rider");   // the user's own VOICE RIDER is already there (the channel may carry other effects from the tests before)
          const QStringList mine = kinds(5); engine.startLive(5); const QStringList on = kinds(5);
          const bool chain = on.contains("fx:clean") && on.contains("fx:automiceq") && on.contains("fx:peakremover") && on.count("fx:rider") == 1 && engine.liveMode() && engine.liveStrip() == 5 && engine.headroomDb() >= 3 && engine.watchdog();
          const bool twice = engine.startLive(2).contains("already") && engine.liveStrip() == 5;
          engine.stopLive(); const QStringList off = kinds(5); const bool back = off == mine && !engine.liveMode() && engine.headroomDb() == hWas && engine.watchdog() == wWas;
          if (!hadRider && riderSlot >= 0) engine.setPlugin(5, riderSlot, QString()); const bool clean = kinds(5) == before;
          { // AUTO LIVE: a streaming program in the list of running programs starts LIVE MODE, once per run of that program; other programs do not
            const bool names = Apps::streamAppOf("TikTok LIVE Studio.exe") == "TikTok LIVE Studio" && Apps::streamAppOf("obs64.exe") == "OBS Studio" && Apps::streamAppOf("OBS") == "OBS Studio" && Apps::streamAppOf("Streamlabs Desktop.exe") == "Streamlabs"
              && Apps::streamAppOf("jobs.exe").isEmpty() && Apps::streamAppOf("chrome.exe").isEmpty() && Apps::streamAppOf("observer.exe").isEmpty() && Apps::streamAppOf("").isEmpty();
            const bool was = apps.autoLive(); apps.setAutoLive(false); apps.checkStreamApps({"explorer.exe", "TikTok LIVE Studio.exe"}); const bool offNo = !engine.liveMode() && apps.streamApp() == "TikTok LIVE Studio";
            apps.setAutoLive(true); apps.checkStreamApps({"explorer.exe", "chrome.exe"}); const bool otherNo = !engine.liveMode() && apps.streamApp().isEmpty();
            apps.checkStreamApps({"explorer.exe", "TikTok LIVE Studio.exe"}); const bool started = engine.liveMode();
            engine.stopLive(); apps.checkStreamApps({"TikTok LIVE Studio.exe"}); const bool notAgain = !engine.liveMode();          // stopped by hand while it still runs: left alone
            apps.checkStreamApps({"explorer.exe"}); apps.checkStreamApps({"obs64.exe"}); const bool again = engine.liveMode() && apps.streamApp() == "OBS Studio"; engine.stopLive(); apps.checkStreamApps({}); apps.setAutoLive(was);
            std::printf("amixos --smoke: AUTO LIVE names %d, off does nothing %d, other programs %d, a streaming program starts it %d, not twice %d, the next one again %d\n", names, offNo, otherNo, started, notAgain, again);
            if (!names || !offNo || !otherNo || !started || !notAgain || !again) { std::fprintf(stderr, "amixos --smoke: AUTO LIVE failed\n"); return 104; } }
          std::printf("amixos --smoke: LIVE MODE chain on %d (%d effects on the channel), second start refused %d, STOP leaves the user's own effect and gives the rest back %d %d\n", chain, int(on.size()), twice, back, clean);
          if (!chain || !twice || !back || !clean) { std::fprintf(stderr, "amixos --smoke: LIVE MODE failed (%s -> %s -> %s)\n", qPrintable(mine.join(",")), qPrintable(on.join(",")), qPrintable(off.join(","))); return 103; } }
        { // HEADROOM MODE: the master stays under the chosen level, whatever the faders say; OFF gives the limiter back as it was
          auto pause2 = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20); };
          const bool nt = engine.nullTestInput(); const double m0 = engine.masterDb(), g0 = engine.stripState()[0].toMap()["db"].toDouble();
          engine.setNullTestInput(true); engine.open("null", "", "", 48000, 256, 2, 2); engine.setInputRoute(0, 0); engine.setMute(0, false); engine.setMaster(10); engine.setGain(0, 10); engine.setHeadroomDb(6); pause2(900);   // the null device with its -12 dBFS tone, pushed 20 dB up
          const double pk6 = engine.telemetry().value("masterPeakDb").toDouble(); const bool held = pk6 <= -5.9 && pk6 > -9 && engine.headroomDb() == 6 && engine.ceilingNow() == -6.0 && engine.limiterOnNow();
          engine.setHeadroomDb(7); const bool stepped = engine.headroomDb() == 9; engine.setHeadroomDb(99); const bool top = engine.headroomDb() == 12 && engine.ceilingNow() == -12.0;
          engine.setHeadroomDb(0); pause2(700); const double pk0 = engine.telemetry().value("masterPeakDb").toDouble(); const bool off = engine.headroomDb() == 0 && engine.ceilingNow() > -1.0 && pk0 > pk6 + 3;
          engine.setMaster(m0); engine.setGain(0, g0); engine.setNullTestInput(nt); engine.open("null", "", "", 48000, 256, 2, 2);
          std::printf("amixos --smoke: HEADROOM MODE -6 dB holds the master at %.1f dBFS %d, steps %d, -12 at most %d, OFF gives it back (%.1f dBFS) %d\n", pk6, held, stepped, top, pk0, off);
          if (!(held && stepped && top && off)) { std::fprintf(stderr, "amixos --smoke: HEADROOM MODE failed\n"); return 101; } }
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
        { int asioN = 0; for (const QVariant& v : rig) if (v.toMap()["backend"].toString() == "asio") ++asioN;   // MANUAL: discovery lists the ASIO drivers, nothing is picked or started
          engine.setAutoPrefer("manual");
          if (!engine.autoDetect().isEmpty() || engine.autoStart(256) || engine.asioDrivers().size() != asioN) { std::fprintf(stderr, "amixos --smoke: MANUAL selection picked a driver by itself, or the ASIO list is wrong (%d of %d)\n", int(engine.asioDrivers().size()), asioN); return 242; }
          if (engine.openAsioDriver("no-such-driver", 256)) return 243;
          engine.setAutoPrefer(keep); }
        for (const char* pf : {"best", "asio", "wasapi", "dsound", "bluetooth"}) { engine.setAutoPrefer(pf); got << engine.autoDetect()["id"].toString(); }
        rig.removeLast(); engine.simulateDevices(rig); engine.setAutoPrefer("bluetooth"); const QVariantMap fb = engine.autoDetect(); engine.setAutoPrefer(keep); engine.simulateDevices(real);
        if (got != QStringList{"Focusrite USB ASIO", "Focusrite USB ASIO", "{w-out}", "{d-out}", "{bt}"} || fb["id"].toString() != "Focusrite USB ASIO" || !fb["why"].toString().contains("no BLUETOOTH device")) { std::fprintf(stderr, "amixos --smoke: auto select picked %s / fallback %s\n", qPrintable(got.join(", ")), qPrintable(fb["id"].toString())); return 225; } }
      { // INPUT / OUTPUT CHECK: with the test tone on inputs 1-2 both are live on strips 1-2, the outputs carry the monitor mix, and real time holds
        engine.resetRouting(); engine.setNullTestInput(true); engine.stop(); engine.open("null", "", "", 48000, 256, 2, 2); QElapsedTimer t; t.start(); while (t.elapsed() < 500) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    { engine.muteAll(false); engine.setMute(2, true); engine.toggleMuteAll();
      if (!engine.allMuted()) { std::fprintf(stderr, "MUTE ALL with one channel already muted did not mute the rest\n"); return 236; }
      engine.toggleMuteAll(); const QVariantList st = engine.stripState();
      if (engine.allMuted() || !st[2].toMap()["mute"].toBool() || st[0].toMap()["mute"].toBool()) { std::fprintf(stderr, "UNMUTE ALL did not give the earlier mutes back\n"); return 237; }
      engine.setMute(2, false); }
        const QVariantMap io = engine.ioCheck(); const QVariantList ii = io["inputs"].toList(), oo = io["outputs"].toList();
        if (!io["running"].toBool() || !io["realtime"].toBool() || ii.size() != 2 || oo.size() != 2 || io["liveInputs"].toInt() != 2 || io["sameChannel"].toInt() != 2 || ii[1].toMap()["strip"].toInt() != 2 || !ii[0].toMap()["live"].toBool() || !oo[0].toMap()["source"].toString().startsWith("MONITOR") || !oo[0].toMap()["live"].toBool() || io["blockMs"].toDouble() < 5 || !io["verdict"].toString().contains("2 of 2")) { std::fprintf(stderr, "amixos --smoke: input / output check: %s\n", qPrintable(io["verdict"].toString())); return 226; }
        engine.setInputRoute(1, 0); const QVariantMap io2 = engine.ioCheck(); engine.resetRouting(); if (io2["sameChannel"].toInt() != 1) { std::fprintf(stderr, "amixos --smoke: the check did not notice a channel on another strip\n"); return 227; }
        engine.setNullTestInput(false); engine.stop(); const bool off = !engine.ioCheck()["running"].toBool(); engine.open("null", "", "", 48000, 256, 2, 2); if (!off) return 228; }
      { // A PLUG-IN'S OWN WINDOW: the test VST2 has an editor; it opens once in a window of its own, is idled, and is closed when the window is closed or the plug-in leaves its slot
        QString k2, k3; for (const QVariant& v : engine.vstCatalog()) { const QString k = v.toMap()["kind"].toString(); if (k2.isEmpty() && k.startsWith("vst2:")) k2 = k; if (k3.isEmpty() && k.startsWith("vst3:")) k3 = k; }
        engine.setPlugin(26, 0, k2); engine.setPlugin(26, 1, k3); engine.setPlugin(26, 2, "fx:hall");
        if (engine.openPluginWindow(26, 0)) {
#ifndef _WIN32
          std::fprintf(stderr, "amixos --smoke: plug-in windows must stay off on this system unless asked\n"); return 229;
#endif
        }
        engine.closePluginWindows();
        if (!engine.openPluginWindow(26, 0, true) || engine.pluginWindows() != 1 || !engine.openPluginWindow(26, 0, true) || engine.pluginWindows() != 1) { std::fprintf(stderr, "amixos --smoke: the plug-in's own window did not open once\n"); return 230; }
        if (engine.openPluginWindow(26, 1, true) || engine.openPluginWindow(26, 2, true)) { std::fprintf(stderr, "amixos --smoke: a plug-in without an editor must fall back to the knob view\n"); return 231; }
        const double v0 = engine.pluginParams(26, 0)[0].toMap()["value"].toDouble(); engine.setPluginParam(26, 0, engine.pluginParams(26, 0)[0].toMap()["id"].toInt(), 1.0);
        QElapsedTimer t; t.start(); while (t.elapsed() < 250) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (std::fabs(engine.pluginParams(26, 0)[0].toMap()["value"].toDouble() - 1.0) < 0.05) { std::fprintf(stderr, "amixos --smoke: the open editor was not idled (%.2f)\n", v0); return 232; }
        engine.setPlugin(26, 0, ""); t.restart(); while (t.elapsed() < 200) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (engine.pluginWindows() != 0) { std::fprintf(stderr, "amixos --smoke: the window stayed open after its plug-in was removed\n"); return 233; }
        engine.setPlugin(26, 0, k2); if (!engine.openPluginWindow(26, 0, true)) return 234; engine.closePluginWindows(); if (engine.pluginWindows() != 0) return 235;
        { // ... and INSIDE THE CONSOLE: the editor docked into the main window (the PLUG-IN dock): it replaces the floating one, is idled, follows
          // the dock, and closes when the plug-in leaves its slot; one without an editor says so instead
          QQuickItem* body = qml.rootObjects().isEmpty() ? nullptr : qml.rootObjects().first()->findChild<QQuickItem*>("pluginDockBody");
          QObject* dockObj = qml.rootObjects().isEmpty() ? nullptr : qml.rootObjects().first()->findChild<QObject*>("pluginDock");
          if (!body || !dockObj) { std::fprintf(stderr, "amixos --smoke: the PLUG-IN dock is not in the window\n"); return 238; }
          engine.openPluginWindow(26, 0, true); dockObj->setProperty("visible", true);
          const QVariantMap r = engine.dockPluginEditor(26, 0, body, true);
          QElapsedTimer t2; t2.start(); while (t2.elapsed() < 250) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
          const bool docked = r.value("ok").toBool() && engine.pluginDocked() && engine.pluginWindows() == 0 && r.value("w").toInt() > 0 && r.value("h").toInt() > 0;
          const QVariantMap none = engine.dockPluginEditor(26, 2, body, true);   // the built-in hall: no window of its own
          const bool noted = !none.value("ok").toBool() && !none.value("note").toString().isEmpty() && engine.pluginDocked();   // and the docked one stays
          engine.setPlugin(26, 0, ""); t2.restart(); while (t2.elapsed() < 250) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
          const bool gone = !engine.pluginDocked(); dockObj->setProperty("visible", false);
          std::printf("amixos --smoke: PLUG-IN INSIDE THE CONSOLE docked %d (%dx%d, floating closed), no-editor note %d, closed with its plug-in %d\n", docked, r.value("w").toInt(), r.value("h").toInt(), noted, gone);
          if (!(docked && noted && gone)) { std::fprintf(stderr, "amixos --smoke: PLUG-IN dock failed: %s\n", qPrintable(r.value("note").toString())); return 239; } }
        for (int k = 0; k < 3; ++k) engine.setPlugin(26, k, ""); }
      { // VST FOLDER: the console's own plug-in folder is created and saved among the scanned folders, once
        const QStringList before = settings.vstDirs(); const QString vf = settings.vstUserFolder(); settings.vstUserFolder();
        if (vf.isEmpty() || !QDir(vf).exists() || settings.vstDirs().count(vf) != 1 || QSettings().value("vst3/dirs").toStringList().count(vf) != 1) { std::fprintf(stderr, "amixos --smoke: the VST folder was not created and saved\n"); return 198; }
        if (!before.contains(vf)) settings.removeVstDir(vf); }
      { // the update link is open to any https host (and to nothing else); the licence server takes https or this computer; a product key needs 25 characters and a server
        const QString keepUrl = update.url(), keepLic = update.licenseServer();
        if (update.setServerLink("http://updates.example.org/update.json") || update.setServerLink("https://user@updates.example.org/x") || !update.setServerLink("https://updates.example.org/amfx1/update.json") || update.url() != "https://updates.example.org/amfx1/update.json") { std::fprintf(stderr, "amixos --smoke: the update link rule is wrong\n"); return 207; }
        update.setLicenseServer("http://licence.example.org"); if (update.licenseServer() == "http://licence.example.org") return 208;
        update.setLicenseServer(""); update.redeemProductKey("AAAAA-BBBBB-CCCCC-DDDDD-EEEEE"); if (update.redeemState().isEmpty() || update.redeemState() == "asking") return 209;
        update.setLicenseServer("https://licence.example.org/"); if (update.licenseServer() != "https://licence.example.org") return 210;
        update.redeemProductKey("TOO-SHORT"); if (update.redeemState() == "asking") return 211;
        QSettings().remove("update/hosts"); QSettings().setValue("update/url", keepUrl); update.setLicenseServer(keepLic); }
      { const QString pin = remote.pairPin(); const int left = remote.pairSecondsLeft();   // the pairing PIN: six digits once the master token exists, 1..60 s to the next
        if ((!pin.isEmpty() && (pin.size() != 6 || pin.toInt() < 0)) || left < 1 || left > 60) { std::fprintf(stderr, "amixos --smoke: the pairing PIN is wrong (%s, %d s)\n", qPrintable(pin), left); return 206; } }
      { // HIGH-RESOLUTION AUDIO: r8brain, libFLAC, dr_wav / dr_flac built in; libsoxr loaded from beside the program
        const QString hs = engine.hiresSummary(); if (!hs.contains("r8brain-free-src 6.5") || !hs.contains("libFLAC") || !hs.contains("libsoxr") || hs.contains("not installed")) { std::fprintf(stderr, "amixos --smoke: hi-res libraries: %s\n", qPrintable(hs)); return 281; }
        engine.setRecordFormat(4); if (engine.recordFormat() != 4 || engine.recordFloat()) { std::fprintf(stderr, "amixos --smoke: FLAC recording format did not take\n"); return 282; } engine.setRecordFormat(0); }
      { // MIC AT ENGINE ON (default off): a Windows microphone is not opened by itself at engine on; ASIO, COMPUTER SOUND and "no input" are
        const bool keep = engine.micAtStart(); engine.setMicAtStart(false);
        const bool wasapiMic = engine.keepInputAtStart("wasapi", ""), dsMic = engine.keepInputAtStart("dsound", "{mic}"), asio = engine.keepInputAtStart("asio", "UMC ASIO"), music = engine.keepInputAtStart("wasapi", "loopback:system"), none = engine.keepInputAtStart("wasapi", "-");
        engine.setMicAtStart(true); const bool onKeeps = engine.keepInputAtStart("wasapi", ""); engine.setMicAtStart(keep);
        if (wasapiMic || dsMic || !asio || !music || !none || !onKeeps) { std::fprintf(stderr, "amixos --smoke: MIC AT ENGINE ON (wasapi %d, dsound %d, asio %d, music %d, none %d, on %d)\n", wasapiMic, dsMic, asio, music, none, onKeeps); return 283; } }
      { // NO INPUT ASKED, NO MICROPHONE: no device named with 0 input channels opens no input (it used to become 2 channels of the default
        // microphone: a remembered outputs-only start, CONNECTIONS with no input picked or a muted input opened the mic by itself)
        const bool wasOn = engine.running(); const QString wb = engine.backend(); const double wr = engine.sampleRate(); const int wf = engine.blockFrames(), wi = engine.deviceInputs(), wo = engine.deviceOutputs();
        const bool a = engine.open("null", "", "", 48000, 256, 0, 2); const int inA = engine.deviceInputs();
        const bool b = engine.openSmart("null", "", "", 48000, 256, 0, 2); const int inB = engine.deviceInputs();
        const bool c = engine.open("null", "", "", 48000, 256, 2, 2); const int inC = engine.deviceInputs();   // asked for inputs: still opened
        engine.stop(); if (wasOn) engine.open(wb, "", "", wr, wf, wi, wo);
        if (!a || !b || !c || inA != 0 || inB != 0 || inC < 2) { std::fprintf(stderr, "amixos --smoke: NO INPUT ASKED opened an input (open %d:%d, smart %d:%d, asked %d:%d)\n", a, inA, b, inB, c, inC); return 286; } }
      if (remote.available() && qEnvironmentVariableIsSet("AMIXOS_SMOKE_SYNC")) { // LIVE SYNC: with the remote service on, a move on a "phone" lands on the console and one on the console reaches the service
        const auto wait = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) { app.processEvents(QEventLoop::AllEvents, 20); QThread::msleep(5); } };
        const double g0 = engine.stripAt(0)["db"].toDouble(), g1 = engine.stripAt(1)["db"].toDouble();
        const bool wasRunning = engine.running(); const QString wb = engine.backend(); const double wr = engine.sampleRate(); const int wf = engine.blockFrames(), wi = engine.deviceInputs(), wo = engine.deviceOutputs();
        const QString rackFx = QStringLiteral("fx:") + QString::fromStdString(am::fx::catalog().front().first); engine.setPlugin(5, 0, rackFx);   // a rack effect: the service must play it
        engine.setGain(1, 0); engine.setMute(2, false); engine.setGain(3, 0);   // a known start: a run stopped half-way never leaves its moves behind for the next one
        engine.setGain(0, -9); if (!remote.start(false)) { std::fprintf(stderr, "amixos --smoke: LIVE SYNC: the remote did not start: %s\n", qPrintable(remote.status())); return 284; }
        wait(1500); QNetworkAccessManager nam; QString lastHttpErr; const auto http = [&](const QString& path, const QByteArray& body) { QNetworkRequest q(QUrl("http://127.0.0.1:8765" + path)); q.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"); q.setRawHeader("X-Amix-Token", remote.token().toUtf8());
          QNetworkReply* r = body.isEmpty() ? nam.get(q) : nam.post(q, body); QElapsedTimer t; t.start(); while (!r->isFinished() && t.elapsed() < 3000) app.processEvents(QEventLoop::AllEvents, 20);
          if (!r->isFinished() || r->error() != QNetworkReply::NoError) lastHttpErr = path + ": " + (r->isFinished() ? r->errorString() : QStringLiteral("no answer in 3 s"));
          const QVariantMap m = QJsonDocument::fromJson(r->readAll()).toVariant().toMap(); r->deleteLater(); return m; };
        const double seeded = http("/api/state", {})["strips"].toList().value(0).toMap()["db"].toDouble();
        // a "phone" watching the live stream (it counts as a connected remote), and the MIXER page on screen for AMIX_SYNC_SHOT
        const bool shot = qEnvironmentVariableIsSet("AMIX_SYNC_SHOT");
        QTcpSocket phone; if (shot) phone.connectToHost("127.0.0.1", 8765); if (shot && phone.waitForConnected(2000)) { phone.write(("GET /ws/meters?token=" + QUrl::toPercentEncoding(remote.token()) + " HTTP/1.1\r\nHost: 127.0.0.1:8765\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n")); phone.flush(); }
        auto* sw = qobject_cast<QQuickWindow*>(qml.rootObjects().first()); if (sw && shot) { sw->setProperty("current", "mixer"); wait(800); }
        http("/api/control", "{\"op\":\"gain\",\"strip\":1,\"db\":-14}"); http("/api/control", "{\"op\":\"mute\",\"strip\":2,\"on\":true}"); QElapsedTimer t1; t1.start(); while (t1.elapsed() < 2000 && std::abs(engine.stripAt(1)["db"].toDouble() + 14) > 0.05) wait(10); const qint64 inMs = t1.elapsed();
        const bool lit = remote.touched(1);   // the fader the phone moved lights up on the console
        { QElapsedTimer tw; tw.start(); while (tw.elapsed() < 1500 && remote.remotes() < 1) wait(30); }
        const int remotesSeen = shot ? remote.remotes() : 1;
        if (sw && qEnvironmentVariableIsSet("AMIX_SYNC_SHOT")) { http("/api/control", "{\"op\":\"gain\",\"strip\":1,\"db\":-16}"); wait(140); sw->grabWindow().save(qEnvironmentVariable("AMIX_SYNC_SHOT")); }
        engine.setGain(3, -21); QElapsedTimer t2; t2.start(); double out = 0; while (t2.elapsed() < 2000) { wait(40); out = http("/api/state", {})["strips"].toList().value(3).toMap()["db"].toDouble(); if (std::abs(out + 21) < 0.05) break; } const qint64 outMs = t2.elapsed();
        const bool ok = std::abs(seeded + 9) < 0.05 && std::abs(engine.stripAt(1)["db"].toDouble() + (qEnvironmentVariableIsSet("AMIX_SYNC_SHOT") ? 16 : 14)) < 0.05 && engine.stripAt(2)["mute"].toBool() && std::abs(out + 21) < 0.05 && lit && remotesSeen >= 1;
        // THE CONSOLE'S PLUG-INS while phones are linked: the service plays the rack effect; a phone turns one of its knobs; the console's own
        // instance follows (so it sounds the same when the console plays again)
        bool rackOk = false; QString rackWhy;
        { const auto get = [&](const QString& path) { QNetworkRequest q(QUrl("http://127.0.0.1:8765" + path)); q.setRawHeader("X-Amix-Token", remote.token().toUtf8()); QNetworkReply* r = nam.get(q);
            QElapsedTimer t; t.start(); while (!r->isFinished() && t.elapsed() < 3000) app.processEvents(QEventLoop::AllEvents, 20); const QVariantMap m = QJsonDocument::fromJson(r->readAll()).toVariant().toMap(); r->deleteLater(); return m; };
          if (!http("/api/status", {}).value("running").toBool()) { http("/api/control", "{\"op\":\"open\",\"backend\":\"null\",\"inChannels\":2,\"outChannels\":2}"); wait(300); }   // this test computer has no sound card: the timed one
          const QVariantMap slot = get("/api/racks?strip=5").value("racks").toList().value(0).toMap().value("slots").toList().value(0).toMap();
          const QVariantList ps = slot.value("params").toList();
          if (!slot.value("loaded").toBool() || ps.isEmpty()) rackWhy = "not loaded by the service: " + slot.value("error").toString();
          else { const int id = ps[0].toMap().value("id").toInt(); const double want = ps[0].toMap().value("value").toDouble() < 0.5 ? 0.9 : 0.1;
            http("/api/control", QJsonDocument(QJsonObject{{"op", "plugin"}, {"strip", 5}, {"slot", 0}, {"param", id}, {"value", want}}).toJson(QJsonDocument::Compact));
            QElapsedTimer t; t.start(); double now = -1;
            while (t.elapsed() < 3000) { wait(40); for (const QVariant& v : engine.pluginParams(5, 0)) if (v.toMap().value("id").toInt() == id) now = v.toMap().value("value").toDouble(); if (std::abs(now - want) < 0.02) break; }
            rackOk = std::abs(now - want) < 0.02; if (!rackOk) rackWhy = QStringLiteral("the console's instance did not follow (%1, wanted %2)").arg(now).arg(want);
            std::printf("amixos --smoke: CONSOLE PLUG-INS %s played by the service, a phone set %s to %.2f, the console followed in %lld ms\n", qPrintable(slot.value("name").toString()), qPrintable(ps[0].toMap().value("title").toString()), want, static_cast<long long>(t.elapsed())); } }
        // ... and the other way: a knob turned on the console (or in the plug-in's own window) reaches the service's instance; the console's
        // 8-band channel EQ goes out to the remotes, and a remote's EQ band lands on the console, as they move
        if (rackOk) { const auto get = [&](const QString& path) { QNetworkRequest q(QUrl("http://127.0.0.1:8765" + path)); q.setRawHeader("X-Amix-Token", remote.token().toUtf8()); QNetworkReply* r = nam.get(q);
            QElapsedTimer t; t.start(); while (!r->isFinished() && t.elapsed() < 3000) app.processEvents(QEventLoop::AllEvents, 20); const QVariantMap m = QJsonDocument::fromJson(r->readAll()).toVariant().toMap(); r->deleteLater(); return m; };
          const QVariantList cps = engine.pluginParams(5, 0); const int id = cps.value(0).toMap().value("id").toInt(); const double want = cps.value(0).toMap().value("value").toDouble() < 0.5 ? 0.8 : 0.2;
          engine.setPluginParam(5, 0, id, want); QElapsedTimer t; t.start(); double there = -1;
          while (t.elapsed() < 4000) { wait(60); for (const QVariant& v : get("/api/racks?strip=5").value("racks").toList().value(0).toMap().value("slots").toList().value(0).toMap().value("params").toList()) if (v.toMap().value("id").toInt() == id) there = v.toMap().value("value").toDouble(); if (std::abs(there - want) < 0.02) break; }
          const qint64 plugMs = t.elapsed();
          engine.setEq(4, 2, 1, 2000, 1.0, 5.0); QElapsedTimer te; te.start(); double eqOut = 0;
          while (te.elapsed() < 3000) { wait(40); eqOut = http("/api/state", {})["strips"].toList().value(4).toMap()["eq"].toList().value(2).toList().value(3).toDouble(); if (std::abs(eqOut - 5) < 0.05) break; }
          const qint64 eqOutMs = te.elapsed();
          http("/api/control", "{\"op\":\"eq\",\"strip\":6,\"band\":1,\"type\":\"lowshelf\",\"freq\":150,\"q\":0.7,\"gainDb\":-3.5}"); QElapsedTimer ti; ti.start(); QVariantMap eb;
          while (ti.elapsed() < 3000) { wait(40); eb = engine.eqBand(6, 1); if (std::abs(eb.value("gainDb").toDouble() + 3.5) < 0.05 && eb.value("type").toInt() == 2) break; }
          const bool eqIn = std::abs(eb.value("gainDb").toDouble() + 3.5) < 0.05 && eb.value("type").toInt() == 2 && std::abs(eb.value("freq").toDouble() - 150) < 0.5;
          std::printf("amixos --smoke: REAL-TIME SYNC console plug-in knob -> service %s in %lld ms; console EQ -> remotes %s in %lld ms; remote EQ -> console %s in %lld ms\n",
                      std::abs(there - want) < 0.02 ? "yes" : "NO", static_cast<long long>(plugMs), std::abs(eqOut - 5) < 0.05 ? "yes" : "NO", static_cast<long long>(eqOutMs), eqIn ? "yes" : "NO", static_cast<long long>(ti.elapsed()));
          engine.setEq(4, 2, 0, 2000, 1.0, 0.0); engine.setEq(6, 1, 0, 150, 0.7, 0.0);
          // MASTERING both ways: a phone's move lands on the console; the console's own move reaches the service (and the phones)
          { const QVariantMap keep = engine.mastering();
            http("/api/control", "{\"op\":\"mastering\",\"on\":true,\"thr\":-22,\"low\":2.5}"); QElapsedTimer tm; tm.start(); QVariantMap cm;
            while (tm.elapsed() < 3000) { wait(40); cm = engine.mastering(); if (cm.value("on").toBool() && std::abs(cm.value("thr").toDouble() + 22) < 0.05) break; }
            const bool inOk = cm.value("on").toBool() && std::abs(cm.value("thr").toDouble() + 22) < 0.05 && std::abs(cm.value("low").toDouble() - 2.5) < 0.05; const qint64 inMs = tm.elapsed();
            engine.setMastering(QVariantMap{{"makeup", 4.5}}); tm.restart(); double mk = 0;
            while (tm.elapsed() < 3000) { wait(40); mk = http("/api/state", {}).value("mastering").toMap().value("makeup").toDouble(); if (std::abs(mk - 4.5) < 0.05) break; }
            const bool outOk = std::abs(mk - 4.5) < 0.05;
            std::printf("amixos --smoke: MASTERING phone -> console %s in %lld ms; console -> phones %s in %lld ms\n", inOk ? "yes" : "NO", static_cast<long long>(inMs), outOk ? "yes" : "NO", static_cast<long long>(tm.elapsed()));
            engine.setMastering(keep);
            if (!(inOk && outOk)) { rackOk = false; rackWhy = QStringLiteral("MASTERING sync (in %1, out %2)").arg(inOk).arg(outOk); } }
          // ROUTING both ways: a phone routes input 6 to CH 4 and MASTER L to OUT 3; the console's own change of CH 5 reaches the phones
          { const QVariantList keepIn = engine.inputRoutes(), keepOut = engine.outputRoutes();
            http("/api/control", "{\"op\":\"inRoute\",\"strip\":3,\"input\":5}"); http("/api/control", "{\"op\":\"outRoute\",\"out\":2,\"source\":1}");
            QElapsedTimer tr; tr.start(); bool in = false; while (tr.elapsed() < 3000 && !in) { wait(40); in = engine.inputRoutes().value(3).toInt() == 5 && engine.outputRoutes().value(2).toInt() == 1; }
            const qint64 inMs = tr.elapsed(); engine.setInputRoute(4, 6); tr.restart(); bool out = false;
            while (tr.elapsed() < 3000 && !out) { wait(40); out = http("/api/state", {}).value("routing").toMap().value("in").toList().value(4).toInt() == 6; }
            std::printf("amixos --smoke: ROUTING phone -> console %s in %lld ms; console -> phones %s in %lld ms\n", in ? "yes" : "NO", static_cast<long long>(inMs), out ? "yes" : "NO", static_cast<long long>(tr.elapsed()));
            for (int i = 0; i < keepIn.size(); ++i) engine.setInputRoute(i, keepIn[i].toInt()); for (int o = 0; o < keepOut.size(); ++o) engine.setOutputRoute(o, keepOut[o].toInt());
            if (!(in && out)) { rackOk = false; rackWhy = QStringLiteral("ROUTING sync (in %1, out %2)").arg(in).arg(out); } }
          // SAMPLE PADS while the phones are linked: the console's engine is stopped, a pad goes to the service and is heard on its master
          { double before = 0, during = 0; for (int k = 0; k < 5; ++k) { wait(40); before = std::max(before, get("/api/meters").value("master").toMap().value("l").toDouble()); }
            const bool sent = !engine.running() && samples.play(1); QElapsedTimer tp; tp.start();
            while (tp.elapsed() < 1500) { wait(40); during = std::max(during, get("/api/meters").value("master").toMap().value("l").toDouble()); if (during > 0.05) break; }
            samples.stopAll();
            std::printf("amixos --smoke: SAMPLE PADS while phones are linked: sent to the service %d, its master %.3f -> %.3f (in %lld ms)\n", sent, before, during, static_cast<long long>(tp.elapsed()));
            if (!(sent && during > 0.05 && during > before * 4)) { rackOk = false; rackWhy = QStringLiteral("SAMPLE PADS not heard through the service (%1 -> %2)").arg(before).arg(during); } }
          // 1000 CUSTOM PADS in sync: a custom pad pressed on the console plays through the service; a pad a phone changes shows on the console
          { wait(400); double during = 0; const bool sent = !engine.running() && samples.playPad(251); QElapsedTimer tp; tp.start();
            while (tp.elapsed() < 2000) { wait(40); during = std::max(during, get("/api/meters").value("master").toMap().value("l").toDouble()); if (during > 0.05) break; }
            samples.stopAll(); const QVariantMap keep = samples.pad(888);
            http("/api/control", "{\"op\":\"padSet\",\"n\":888,\"source\":\"sfx:321\",\"name\":\"From Phone\"}"); QElapsedTimer ts; ts.start(); bool seen = false;
            while (ts.elapsed() < 3000 && !seen) { wait(50); seen = samples.pad(888)["name"].toString() == "From Phone"; }
            const qint64 seenMs = ts.elapsed();
            if (keep["custom"].toBool()) samples.setPad(888, keep["source"].toString(), keep["name"].toString(), keep["gainDb"].toDouble(), keep["color"].toInt()); else samples.resetPad(888);
            std::printf("amixos --smoke: 1000 PADS while phones are linked: pad 251 (Impact 01) sent %d, heard %.3f; a phone's pad change seen on the console %s in %lld ms\n", sent, during, seen ? "yes" : "NO", static_cast<long long>(seenMs));
            if (!(sent && during > 0.05 && seen)) { rackOk = false; rackWhy = QStringLiteral("1000 PADS sync (sent %1, heard %2, seen %3)").arg(sent).arg(during).arg(seen); } }
          // INPUT: MIC while phones are linked: the service reopens its sound card with the microphone, and back to outputs only
          { http("/api/control", "{\"op\":\"input\",\"in\":\"-\"}"); wait(200);
            const int inNone = http("/api/status", {}).value("inChannels").toInt(); const QVariantMap r = engine.setInputSource("mic"); QElapsedTimer ti; ti.start(); int inMic = 0;
            while (ti.elapsed() < 3000) { wait(60); inMic = http("/api/status", {}).value("inChannels").toInt(); if (inMic >= 2) break; }
            std::printf("amixos --smoke: INPUT MIC while phones are linked: sent to the service %d, its inputs %d -> %d\n", r.value("remote").toBool(), inNone, inMic);
            if (!(r.value("remote").toBool() && inMic >= 2)) { rackOk = false; rackWhy = QStringLiteral("INPUT MIC through the service (%1)").arg(inMic); } }
          if (!(std::abs(there - want) < 0.02 && std::abs(eqOut - 5) < 0.05 && eqIn)) { rackOk = false; rackWhy = QStringLiteral("REAL-TIME SYNC: plug-in %1 / EQ out %2 / EQ in %3").arg(there).arg(eqOut).arg(eqIn); } }
        engine.setPlugin(5, 0, QString());
        if (!rackOk) { std::fprintf(stderr, "amixos --smoke: CONSOLE PLUG-INS failed: %s\n", qPrintable(rackWhy)); remote.stop(); return 290; }
        // RTA EQ and RTA: a remote sets the console's RTA EQ (the service plays it, the console takes it back); the analyzer answers
        { const QVariantList keep = engine.programEqGains(); bool eqOk = false; qint64 eqMs = -1;
          http("/api/control", "{\"op\":\"programEq\",\"gains\":[0,0,3,0,0,-2.5,0,0,0,0],\"on\":true}"); QElapsedTimer t; t.start();
          while (t.elapsed() < 3000) { wait(40); const QVariantList g = engine.programEqGains(); if (g.size() == 10 && std::abs(g[2].toDouble() - 3) < 0.01 && std::abs(g[5].toDouble() + 2.5) < 0.01) { eqOk = true; eqMs = t.elapsed(); break; } }
          http("/api/control", "{\"op\":\"rta\",\"src\":-1}"); wait(400);
          QNetworkRequest q(QUrl("http://127.0.0.1:8765/api/rta")); q.setRawHeader("X-Amix-Token", remote.token().toUtf8()); QNetworkReply* r = nam.get(q); QElapsedTimer tr; tr.start(); while (!r->isFinished() && tr.elapsed() < 3000) app.processEvents(QEventLoop::AllEvents, 20);
          const QVariantMap rta = QJsonDocument::fromJson(r->readAll()).toVariant().toMap(); r->deleteLater(); http("/api/control", "{\"op\":\"rta\",\"src\":\"off\"}");
          engine.setGraphicEq(-1, keep, true); wait(200);
          std::printf("amixos --smoke: RTA EQ remote->console %lld ms; RTA %d bands\n", static_cast<long long>(eqMs), static_cast<int>(rta.value("f").toList().size()));
          if (!eqOk || rta.value("f").toList().size() < 50) { std::fprintf(stderr, "amixos --smoke: RTA EQ / RTA failed (eq %d, bands %d)\n", eqOk, static_cast<int>(rta.value("f").toList().size())); remote.stop(); return 291; } }
        // SETTINGS SYNC: a remote changes the stereo width and GAIN MODE: the console follows; the console changes the width: the service follows
        const double keepW = engine.width(); const bool keepG = engine.windowsGainMode(); bool setOk = false; qint64 setIn = -1, setOut = -1;
        { http("/api/control", "{\"op\":\"setting\",\"key\":\"width\",\"value\":0.5}"); http("/api/control", "{\"op\":\"setting\",\"key\":\"winGain\",\"value\":" + QByteArray(keepG ? "false" : "true") + "}");
          QElapsedTimer ts; ts.start(); while (ts.elapsed() < 3000 && !(std::abs(engine.width() - 0.5) < 1e-6 && engine.windowsGainMode() != keepG)) wait(20);
          if (std::abs(engine.width() - 0.5) < 1e-6 && engine.windowsGainMode() != keepG) setIn = ts.elapsed();
          engine.setWidth(1.25); ts.restart(); while (ts.elapsed() < 3000 && std::abs(http("/api/state", {})["settings"].toMap()["width"].toDouble() - 1.25) > 1e-6) wait(40);
          if (std::abs(http("/api/state", {})["settings"].toMap()["width"].toDouble() - 1.25) < 1e-6) setOut = ts.elapsed();
          setOk = setIn >= 0 && setOut >= 0; engine.setWidth(keepW); engine.setWindowsGainMode(keepG); wait(300); }
        std::printf("amixos --smoke: SETTINGS SYNC remote->console %lld ms, console->remote %lld ms\n", static_cast<long long>(setIn), static_cast<long long>(setOut));
        if (!setOk) { std::fprintf(stderr, "amixos --smoke: SETTINGS SYNC failed (width %.2f, gain mode %d)\n", engine.width(), engine.windowsGainMode()); remote.stop(); return 289; }
        // CONNECTED PHONES: a phone pairs with the PIN and links up: the NETWORK page lists it under its name; DISCONNECT drops it; FORGET revokes it
        int linkStep = 0; qint64 listedMs = -1, droppedMs = -1;
        { QNetworkRequest q(QUrl("http://127.0.0.1:8765/api/pair")); q.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
          QNetworkReply* r = nam.post(q, QJsonDocument(QJsonObject{{"pin", remote.pairPin()}, {"name", "Smoke Phone"}}).toJson(QJsonDocument::Compact)); QElapsedTimer tp; tp.start(); while (!r->isFinished() && tp.elapsed() < 3000) app.processEvents(QEventLoop::AllEvents, 20);
          const QVariantMap pr = QJsonDocument::fromJson(r->readAll()).toVariant().toMap(); r->deleteLater(); const QString ptok = pr.value("token").toString(), pid = pr.value("id").toString();
          if (!ptok.isEmpty()) { linkStep = 1; QTcpSocket ph; ph.connectToHost("127.0.0.1", 8765);
            if (ph.waitForConnected(2000)) { ph.write(("GET /ws/meters?token=" + QUrl::toPercentEncoding(ptok) + " HTTP/1.1\r\nHost: 127.0.0.1:8765\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n").constData()); ph.flush();
              QElapsedTimer tl; tl.start(); int linkId = -1; remote.refreshLinks();
              while (tl.elapsed() < 6000 && linkId < 0) { wait(50); ph.readAll(); for (const QVariant& v : remote.links()) { const QVariantMap m = v.toMap(); if (m["name"].toString() == "Smoke Phone" && m["tokenId"].toString() == pid && m["phone"].toBool()) linkId = m["id"].toInt(); } }
              if (linkId >= 0) { linkStep = 2; listedMs = tl.elapsed(); remote.disconnectLink(linkId); QElapsedTimer td; td.start(); bool gone = false;
                while (td.elapsed() < 6000 && !gone) { wait(50); ph.readAll(); gone = true; for (const QVariant& v : remote.links()) if (v.toMap()["id"].toInt() == linkId) gone = false; }
                if (gone) { linkStep = 3; droppedMs = td.elapsed(); } } }
            ph.abort(); remote.forgetLink(pid); wait(1200);
            for (const QVariant& v : remote.tokens()) { const QVariantMap m = v.toMap(); if (m["id"].toString() == pid && m["state"].toString() == "revoked") linkStep = linkStep == 3 ? 4 : linkStep; } } }
        std::fprintf(stdout, "amixos --smoke: CONNECTED PHONES paired %d, listed in %lld ms, disconnected in %lld ms, forgotten %d\n", linkStep >= 1, static_cast<long long>(listedMs), static_cast<long long>(droppedMs), linkStep == 4);
        if (linkStep != 4) { std::fprintf(stderr, "amixos --smoke: CONNECTED PHONES failed at step %d (%s)\n", linkStep, qPrintable(remote.linkNote())); remote.stop(); return 287; }
        phone.abort();
        std::fprintf(stdout, "amixos --smoke: LIVE SYNC seed %.1f, phone->console %lld ms, console->phone %lld ms, changes %d, fader lit %d, remotes %d\n", seeded, static_cast<long long>(inMs), static_cast<long long>(outMs), remote.syncedChanges(), lit, remotesSeen);
        const bool remoteWasRunning = remote.running(); const QString remoteStatus = remote.status() + " | " + remote.syncDebug();
        remote.stop(); wait(600); if (wasRunning && !engine.running()) engine.open(wb, "", "", wr, wf, wi, wo); wait(300); engine.setGain(0, g0); engine.setGain(1, std::abs(g1 + 14) < 0.05 || std::abs(g1 + 16) < 0.05 ? 0.0 : g1); engine.setGain(3, 0); engine.setMute(2, false);   // never put back a value this check itself left
        if (!ok) { std::fprintf(stderr, "amixos --smoke: LIVE SYNC failed (seed %.1f, strip 2 %.1f, mute %d, out %.1f; remote running %d, status %s; http %s)\n", seeded, engine.stripAt(1)["db"].toDouble(), engine.stripAt(2)["mute"].toBool(), out, remoteWasRunning, qPrintable(remoteStatus), qPrintable(lastHttpErr)); return 285; } }
      { // SECURE INPUT: an input not on the allowed list is never opened; an allowed WASAPI input opens exclusively; switched off, nothing changes
        const bool keepOn = engine.secureInput(); const QStringList keepList = QSettings().value("audio/allowedInputs").toStringList();
        engine.setSecureInput(true); engine.setInputAllowed("wasapi", "{mic-a}", false);
        QVariantMap r = engine.secureInputPreview("wasapi", "{mic-a}", "", 2);
        if (r["inChannels"].toInt() != 0 || r["in"].toString() != "-" || !r["note"].toString().startsWith("SECURE INPUT")) { std::fprintf(stderr, "amixos --smoke: SECURE INPUT opened a device that is not allowed\n"); return 275; }
        engine.setInputAllowed("wasapi", "{mic-a}", true); r = engine.secureInputPreview("wasapi", "{mic-a}", "", 2);
        if (r["inChannels"].toInt() != 2 || r["in"].toString() != "{mic-a}" || !r["exclusive"].toBool() || !r["note"].toString().isEmpty()) { std::fprintf(stderr, "amixos --smoke: SECURE INPUT did not open the allowed device exclusively\n"); return 276; }
        r = engine.secureInputPreview("asio", "", "Other ASIO", 8); if (r["inChannels"].toInt() != 0) return 277;   // ASIO's input is the driver: not allowed, no input
        r = engine.secureInputPreview("wasapi", "-", "", 0); if (!r["note"].toString().isEmpty() || r["exclusive"].toBool()) return 278;   // no input asked: nothing to secure
        engine.setInputAllowed("wasapi", "loopback:system", true); r = engine.secureInputPreview("wasapi", "loopback:system", "", 2);   // COMPUTER SOUND can be allowed; a loopback is never exclusive
        if (r["inChannels"].toInt() != 2 || r["in"].toString() != "loopback:system" || r["exclusive"].toBool()) { std::fprintf(stderr, "amixos --smoke: SECURE INPUT and COMPUTER SOUND (in %s, exclusive %d)\n", qPrintable(r["in"].toString()), r["exclusive"].toBool()); return 280; }
        engine.setSecureInput(false); r = engine.secureInputPreview("wasapi", "{mic-b}", "", 2); if (r["inChannels"].toInt() != 2 || r["exclusive"].toBool()) return 279;
        QSettings().setValue("audio/allowedInputs", keepList); engine.setSecureInput(keepOn); }
      { // THE PAIRING QR CODE: a real QR (finder squares in three corners, a size of 21 + 4k), the PIN in the #fragment only
        const QVariantList q = remote.qr("http://192.168.1.20:8765/remote#pin=123456"); const int n = q.isEmpty() ? 0 : q[0].toInt();
        auto mod = [&](int x, int y) { return q.value(1 + y * n + x).toInt() == 1; };
        if (n < 21 || (n - 21) % 4 || q.size() != n * n + 1 || !mod(0, 0) || !mod(6, 6) || mod(7, 7) || !mod(n - 1, 0) || !mod(0, n - 1) || !mod(3, 3) || mod(1, 1)) { std::fprintf(stderr, "amixos --smoke: the pairing QR code is wrong (size %d)\n", n); return 271; }
        if (!remote.qr(QString(300, 'x')).isEmpty() || !remote.qr("").isEmpty()) return 272;
        const QString u = remote.pairUrl("http://192.168.1.20:8765"); if (!remote.pairPin().isEmpty() && (!u.startsWith("http://192.168.1.20:8765/remote#pin=") || u.size() != 42)) { std::fprintf(stderr, "amixos --smoke: the pairing link is wrong (%s)\n", qPrintable(u)); return 273; }
        for (const QString& a : remote.addresses()) if (!a.startsWith("http://10.") && !a.startsWith("http://172.") && !a.startsWith("http://192.168.")) return 274; }
      if (remote.available()) {   // AMF X1 NETWORK tokens from the console: made, listed without the secret, revoked; a nameless one is refused
        remote.refreshTokens(); const int n0 = remote.tokens().size();
        if (!remote.newToken("  ", "control", 0).isEmpty()) return 249;
        const QString t = remote.newToken("Smoke phone", "view", 1);
        if (t.size() != 68 || !t.startsWith("amx_") || remote.tokens().size() != n0 + 1) { std::fprintf(stderr, "amixos --smoke: the console could not make a network token (%s)\n", qPrintable(remote.tokenNote())); return 250; }
        const QVariantMap m = remote.tokens().last().toMap(); bool leak = false; for (const QVariant& v : m) leak = leak || v.toString().contains(t.mid(4, 20));
        if (leak || m["name"].toString() != "Smoke phone" || m["role"].toString() != "view" || m["state"].toString() != "active") { std::fprintf(stderr, "amixos --smoke: the token list is wrong\n"); return 251; }
        if (!remote.revokeToken(m["id"].toString()) || remote.tokens().last().toMap()["state"].toString() != "revoked") { std::fprintf(stderr, "amixos --smoke: the token was not revoked\n"); return 252; } }
      { // PIN AT START: refused with the factory PIN; with an own PIN it sticks and a new Access starts locked
        const bool keep = access.pinAtStart(); access.setPinAtStart(true);
        if (access.pinIsDefault() && access.pinAtStart()) { std::fprintf(stderr, "amixos --smoke: PIN AT START switched on with the factory PIN\n"); return 201; }
        if (!access.pinIsDefault()) { Access again(&engine); if (!again.surfaceLocked()) { std::fprintf(stderr, "amixos --smoke: PIN AT START did not lock the console at start\n"); return 202; } }
        access.setPinAtStart(keep); }
      { // ADD FOLDER + RESCAN: a plug-in two folders down (maker / product) is found; the path may come in quotes with a slash at the end; removing the folder takes it away again
        const QString src = QCoreApplication::applicationDirPath() + "/engine/vst2/amixtest2.so";
        if (QFile::exists(src)) {
          QTemporaryDir td; const QString deep = td.path() + "/Maker/Product"; QDir().mkpath(deep); QFile::copy(src, deep + "/amixtest2.so");
          const auto has = [&] { for (const QVariant& v : engine.pluginCatalog()) if (v.toMap()["module"].toString().startsWith(td.path())) return true; return false; };
          settings.addVstDir("\"" + td.path() + "/\"");
          if (!settings.vstDirs().contains(td.path()) || !has()) { std::fprintf(stderr, "amixos --smoke: ADD FOLDER did not find a plug-in in a sub-folder\n"); return 239; }
          settings.rescanVst(); if (!has()) { std::fprintf(stderr, "amixos --smoke: RESCAN lost the plug-in\n"); return 240; }
          settings.removeVstDir(td.path()); if (has() || settings.vstDirs().contains(td.path())) { std::fprintf(stderr, "amixos --smoke: removing the folder left its plug-in in the list\n"); return 241; } } }
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
      bool loop = false; for (const QVariant& v : osc.interfaces()) if (v.toMap()["kind"] == "loopback") loop = true; if (!loop && !qEnvironmentVariableIsSet("AMIXOS_SMOKE_RELAX")) {   /* RELAX: an emulator may list no loopback adapter */ std::fprintf(stderr, "amixos --smoke: network interfaces\n"); return 127; }
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
      // LOCAL UPDATE (offline, like firmware from a USB stick): the same signature and checksum checks as an update from the internet
      { const QString fx = QStringLiteral(AMIX_SOURCE_DIR) + "/test/local-update"; QFile kf(fx + "/test-key.json");
        if (kf.open(QIODevice::ReadOnly)) { const QJsonObject k = QJsonDocument::fromJson(kf.readAll()).object(); const std::vector<am::lic::Jwk> test{{k.value("x").toString().toStdString(), k.value("y").toString().toStdString()}};
          QTemporaryDir td; const QString nd = td.path() + "/new", od = td.path() + "/old"; QDir().mkpath(nd); QDir().mkpath(od);
          for (const auto& [from, to] : {std::pair<QString, QString>{fx + "/new", nd}, {fx + "/old", od}}) for (const QString& n : QDir(from).entryList(QDir::Files)) QFile::copy(from + "/" + n, to + "/" + n);
          const bool good = update.loadLocalWith(nd, false, test) && update.state() == "downloaded" && update.latest() == "99.0.0.0" && QFileInfo::exists(update.downloaded()) && !update.downloaded().startsWith(nd);   // the copy in its own folder is what runs, never the file on the stick
          { // THE UPDATE NOTICE: a banner, a badge on the menu and one Windows notification per version; LATER, SKIP THIS VERSION, seen
            QSettings qs; for (const char* k : {"update/skipped", "update/seen", "update/toasted", "update/laterUntil"}) qs.remove(k);
            qint64 t = 1800000000000LL; update.clock = [&t] { return t; }; int toasts = 0; QString toastTitle;
            const auto conn = QObject::connect(&update, &Update::notifySystem, &app, [&](const QString& title, const QString&) { ++toasts; toastTitle = title; });
            QObject* w = qml.rootObjects().isEmpty() ? nullptr : qml.rootObjects().first();
            auto shown = [&](const char* name) { QCoreApplication::processEvents(); QObject* o = w ? w->findChild<QObject*>(name) : nullptr; return o && o->property("visible").toBool(); };
            update.skipThisVersion(); const bool before = !shown("updateBanner"); update.unskip(); qs.remove("update/toasted");   // (the step above already found 99.0.0.0: start from a version nobody is told about)
            update.loadLocalWith(nd, false, test);   // 99.0.0.0 is found
            const bool told = update.found() && update.noticeBanner() && update.noticeBadge() && toasts == 1 && toastTitle.contains("99.0.0.0") && shown("updateBanner");
            update.loadLocalWith(nd, false, test); const bool once = toasts == 1;                                   // the same version again: no second notification
            update.markUpdateSeen(); const bool seen = !update.noticeBadge() && update.noticeBanner();              // the page was opened: the badge goes, the banner stays
            update.remindLater(24); const bool later = !update.noticeBanner() && !shown("updateBanner"); t += 23LL * 3600000; const bool stillLater = !update.noticeBanner(); t += 2LL * 3600000; const bool back = update.noticeBanner();
            update.skipThisVersion(); const bool skipped = !update.noticeBanner() && !update.noticeBadge() && update.skippedVersion() == "99.0.0.0" && !shown("updateBanner");
            update.unskip(); const bool unskipped = update.noticeBanner() && update.skippedVersion().isEmpty();
            update.setSystemNotice(false); qs.remove("update/toasted"); update.loadLocalWith(nd, false, test); const bool quiet = toasts == 1 && update.noticeBanner(); update.setSystemNotice(true);   // switched off: the banner still tells
            QObject::disconnect(conn); update.clock = nullptr; for (const char* k : {"update/skipped", "update/seen", "update/toasted", "update/laterUntil"}) qs.remove(k);
            std::printf("amixos --smoke: UPDATE NOTICE none before %d, banner + badge + one notification %d, once per version %d, seen %d, LATER %d / %d / back %d, SKIP %d, shown again %d, notification off %d\n", before, told, once, seen, later, stillLater, back, skipped, unskipped, quiet);
            if (!(before && told && once && seen && later && stillLater && back && skipped && unskipped && quiet)) { std::fprintf(stderr, "amixos --smoke: UPDATE NOTICE failed\n"); return 98; } }
          const bool vendorRefuses = !update.loadLocal(nd) && update.message().contains("signature");   // the vendor key did not sign the test bundle: refused
          const bool olderRefused = !update.loadLocalWith(od, false, test) && update.message().contains("older"), olderAllowed = update.loadLocalWith(od, true, test);
          QString bin; for (const QString& n : QDir(nd).entryList({QStringLiteral("*.bin")}, QDir::Files)) { QFile b(nd + "/" + n); if (b.open(QIODevice::Append)) { b.write("x"); b.close(); } }
          const bool tamperRefused = !update.loadLocalWith(nd, false, test) && update.message().contains("checksum");
          const bool viaManifest = update.loadLocalWith(od + "/update.json", true, test);
          if (!good || !vendorRefuses || !olderRefused || !olderAllowed || !tamperRefused || !viaManifest) { std::fprintf(stderr, "amixos --smoke: local update (good %d, vendor key refuses %d, older refused %d / allowed %d, changed file refused %d, by manifest path %d): %s\n", good, vendorRefuses, olderRefused, olderAllowed, tamperRefused, viaManifest, qPrintable(update.message())); return 124; }
          update.clearLocal(); if (update.canInstall() || !update.downloaded().isEmpty()) { std::fprintf(stderr, "amixos --smoke: CLEAR kept the local update\n"); return 125; }
          std::printf("amixos --smoke: local update OK (signed bundle accepted, vendor-key / changed file / older version refused, older allowed on request)\n"); } }
      { // FACTORY RESET is complete: language, PIN-at-start, device choices and network tokens all go; only the licence stays; the factory setup is due again
        QSettings qs; qs.setValue("ui/lang", "de"); qs.setValue("audio/autoPrefer", "manual"); qs.setValue("vst3/dirs", QStringList{"C:/x"}); qs.sync();
        { QFile f(settings.dataFolder() + "/daemon.tokens.json"); if (f.open(QIODevice::WriteOnly)) f.write("{}"); } { QFile f(settings.dataFolder() + "/license.json"); if (f.open(QIODevice::WriteOnly)) f.write("{}"); }
        settings.factoryReset(); QSettings after;
        if (after.contains("ui/lang") || after.contains("audio/autoPrefer") || after.contains("vst3/dirs") || settings.setupDone() || QFile::exists(settings.dataFolder() + "/daemon.tokens.json") || !QFile::exists(settings.dataFolder() + "/license.json")) { std::fprintf(stderr, "amixos --smoke: the factory reset is not complete\n"); return 204; }
        QFile::remove(settings.dataFolder() + "/license.json"); settings.finishSetup(); engine.open("null", "", "", 48000, 256, 2, 2); }
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
      // DCA GROUP CHANNELS: a group's fader is an offset on its members (their faders stay), its mute mutes them; colour and count
      { engine.setGain(2, -10); engine.setDca(2, 3); engine.setDca(3, 3); engine.setDcaLevel(3, -6); engine.setDcaColor(3, 4);
        const QVariantMap g = engine.dcaState()[2].toMap();
        if (g.value("count").toInt() != 2 || g.value("color").toInt() != 4 || std::fabs(engine.effectiveDb(2) + 16) > 1e-9 || std::fabs(engine.stripState()[2].toMap()["db"].toDouble() + 10) > 1e-9) { std::fprintf(stderr, "amixos --smoke: DCA strip state\n"); return 800; }
        engine.setGain(3, 0); engine.setDcaLevel(3, 20); if (std::fabs(engine.effectiveDb(3) - 10) > 1e-9) { std::fprintf(stderr, "amixos --smoke: DCA offset clamp\n"); return 801; }
        engine.setDcaMute(3, true); if (!engine.effectiveMute(2) || !engine.effectiveMute(3) || engine.effectiveMute(4)) { std::fprintf(stderr, "amixos --smoke: DCA mute\n"); return 802; }
        engine.setDcaMute(3, false); engine.setDcaLevel(3, 0); engine.setDca(2, 0); engine.setDca(3, 0); engine.setGain(2, 0); engine.setDcaColor(3, 0); }
      // AUTO DCA: the proposal from features (speech-like, broadband music, silence), APPLY makes the groups, CANCEL leaves the console
      { am::FeatureSummary voice; voice.seconds = 4; voice.rmsDb = -28; voice.activity = 0.7; voice.voiceRatio = 0.9; voice.lowRatio = 0.02; voice.highRatio = 0.08; voice.centroidHz = 1800; voice.zcr = 0.05; voice.pauseFrac = 0.4; voice.crestDb = 15;
        am::FeatureSummary music = voice; music.voiceRatio = 0.33; music.lowRatio = 0.43; music.highRatio = 0.24; music.pauseFrac = 0; music.zcr = 0.27; music.activity = 1; music.crestDb = 10;
        const am::FeatureSummary silent;
        const QVariantList prop = engine.classifyStrips({voice, music, silent, voice});
        auto cls = [&](int i) { return prop.value(i).toMap().value("cls").toString(); };
        if (prop.size() < 4 || cls(0) != "VOICE" || cls(1) != "MUSIC" || cls(2) != "SILENT" || cls(3) != "VOICE" || prop[0].toMap().value("why").toString().isEmpty()) { std::fprintf(stderr, "amixos --smoke: AUTO DCA classes %s %s %s\n", qPrintable(cls(0)), qPrintable(cls(1)), qPrintable(cls(2))); return 803; }
        engine.startAutoDca(0.5); for (int k = 0; k < 100 && engine.autoDcaBusy(); ++k) waitMs(50);
        if (engine.autoDcaBusy() || engine.autoDcaProposal().isEmpty()) { std::fprintf(stderr, "amixos --smoke: AUTO DCA listen did not finish\n"); return 804; }
        engine.cancelAutoDca(); for (int i = 0; i < 4; ++i) if (engine.stripState()[i].toMap()["dca"].toInt() != 0) { std::fprintf(stderr, "amixos --smoke: CANCEL changed a group\n"); return 805; }
        engine.startAutoDca(0.5); for (int k = 0; k < 100 && engine.autoDcaBusy(); ++k) waitMs(50);
        // the measured proposal replaced by the synthetic one through the same rows (setAutoDcaClass), then APPLY
        for (int i = 0; i < engine.autoDcaProposal().size(); ++i) engine.setAutoDcaClass(i, i < 4 ? cls(i) : QStringLiteral("SILENT"));
        engine.applyAutoDca(true);
        const int gv = engine.stripState()[0].toMap()["dca"].toInt(), gm = engine.stripState()[1].toMap()["dca"].toInt();
        if (gv <= 0 || gm <= 0 || gv == gm || engine.stripState()[3].toMap()["dca"].toInt() != gv || engine.dcaState()[gv - 1].toMap()["name"].toString() != "VOICES" || engine.dcaState()[gm - 1].toMap()["name"].toString() != "MUSIC"
            || !engine.stripState()[2].toMap()["mute"].toBool() || engine.stripState()[2].toMap()["dca"].toInt() != 0 || !engine.autoDcaProposal().isEmpty()) { std::fprintf(stderr, "amixos --smoke: AUTO DCA apply\n"); return 806; }
        // DUCKER: APPLY set the key (voices) and the target (music) but did not switch it on; the roles reach the engine; saved and loaded
        QVariantMap d = engine.duckState(); if (d.value("on").toBool() || d.value("keyDca").toInt() != gv || d.value("targetDca").toInt() != gm) { std::fprintf(stderr, "amixos --smoke: ducker set up by AUTO DCA\n"); return 807; }
        engine.setDuck({{"on", true}, {"depth", -15.0}, {"attack", 25.0}, {"lift", 9.0}, {"targetStrips", QVariantList{5}}});
        d = engine.duckState(); if (!d.value("on").toBool() || std::fabs(d.value("depth").toDouble() + 15) > 1e-9 || std::fabs(d.value("lift").toDouble() - 6) > 1e-9 || engine.duckRole(0) != 1 || engine.duckRole(1) != 2 || engine.duckRole(5) != 2 || engine.duckRole(2) != 0) { std::fprintf(stderr, "amixos --smoke: ducker state\n"); return 808; }
        waitMs(200); if (engine.duckGrDb() > 0) { std::fprintf(stderr, "amixos --smoke: ducker meter\n"); return 809; }
        if (engine.dcaState()[gm - 1].toMap()["duck"].toInt() != 1 || engine.dcaState()[gv - 1].toMap()["duck"].toInt() != 2) { std::fprintf(stderr, "amixos --smoke: ducker on the DCA strips\n"); return 810; }
        { const QJsonObject saved = engine.consoleExport(); const QJsonObject dk = saved.value("ducker").toObject(); const QJsonArray dc = saved.value("dcas").toArray();
          if (!dk.value("on").toBool() || dk.value("keyDca").toInt() != gv || std::fabs(dk.value("depth").toDouble() + 15) > 1e-9 || dc.size() < gm || dc[gm - 1].toObject().value("color").toInt() != 5) { std::fprintf(stderr, "amixos --smoke: ducker / DCA colour in the saved console\n"); return 811; } }
        engine.setDuck({{"on", false}, {"keyDca", 0}, {"targetDca", 0}, {"targetStrips", QVariantList{}}, {"depth", -12.0}, {"attack", 20.0}, {"lift", 0.0}});
        for (int i = 0; i < 6; ++i) { engine.setDca(i, 0); engine.setMute(i, false); }
        for (int g = 1; g <= 8; ++g) engine.setDcaName(g, QStringLiteral("DCA %1").arg(g)); }
      {  // LOUDNESS MANAGER: master preset, per-channel AUTO FADER / AUTO ON/OFF, class targets, saved with the console, meters, ALL ON / OFF
        engine.setLoud({{"preset", "BROADCAST"}, {"masterOn", true}, {"speed", "slow"}});
        QVariantMap L = engine.loudState(); if (!L.value("masterOn").toBool() || std::fabs(L.value("masterTarget").toDouble() + 23) > 1e-9 || std::fabs(L.value("up").toDouble() - 1.5) > 1e-9) { std::fprintf(stderr, "amixos --smoke: loudness manager master preset\n"); return 820; }
        engine.setLoudStrip(2, {{"ride", true}, {"gate", true}});
        L = engine.loudState(); { const QVariantMap s2 = L.value("strips").toList().value(2).toMap(); if (!s2.value("ride").toBool() || !s2.value("gate").toBool() || !engine.stripState()[2].toMap().value("loudRide").toBool() || L.value("rides").toInt() != (engine.linked(2) ? 2 : 1)) { std::fprintf(stderr, "amixos --smoke: loudness manager strip switches\n"); return 821; } }
        if (std::fabs(engine.loudTargetOf(2) + 18) > 1e-9) { std::fprintf(stderr, "amixos --smoke: loudness manager voice target\n"); return 822; }
        engine.setLoudStrip(2, {{"cls", 1}}); if (std::fabs(engine.loudTargetOf(2) + 20) > 1e-9) { std::fprintf(stderr, "amixos --smoke: loudness manager music target\n"); return 822; }
        { const QJsonObject lo = engine.consoleExport().value("loudness").toObject(); const QJsonArray ls = lo.value("strips").toArray();
          if (!lo.value("masterOn").toBool() || lo.value("preset").toString() != QLatin1String("BROADCAST") || ls.size() < 3 || !ls[2].toObject().value("ride").toBool() || ls[2].toObject().value("cls").toInt() != 1) { std::fprintf(stderr, "amixos --smoke: loudness manager in the saved console\n"); return 823; } }
        engine.setGain(2, -3); waitMs(200); { const QVariantMap mt = engine.meterAt(2); if (!mt.contains("rideDb") || !mt.contains("rideLufs") || !mt.contains("rideGateDb")) { std::fprintf(stderr, "amixos --smoke: loudness manager meters\n"); return 824; } }
        engine.setLoudAll(QStringLiteral("ride"), true); if (engine.loudState().value("rides").toInt() != engine.shownStrips()) { std::fprintf(stderr, "amixos --smoke: loudness manager ALL AUTO ON\n"); return 825; }
        engine.setLoudAll(QStringLiteral("ride"), false); engine.setLoudAll(QStringLiteral("gate"), false); engine.setLoud({{"masterOn", false}, {"preset", "STREAMING"}, {"speed", "medium"}}); engine.setLoudStrip(2, {{"cls", -1}}); engine.setGain(2, 0);
        L = engine.loudState(); if (L.value("rides").toInt() || L.value("gates").toInt() || L.value("masterOn").toBool()) { std::fprintf(stderr, "amixos --smoke: loudness manager off\n"); return 826; } }
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
        { // TODAY'S PAGES: the 1000 sound effects, PROCESSOR NOW in SETTINGS, the ROOM CORRECTION curve, AUTO MIX badges on the strips
          auto settle = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20); };
          win->setProperty("current", "samples"); settle(700);
          if (QObject* sp = win->findChild<QObject*>("samplesPage")) { win->grabWindow().save(shots + "/samples-pads.png"); sp->setProperty("view", "EFFECTS"); sp->setProperty("family", "Laser"); sp->setProperty("pickedSfx", 257); settle(500); win->grabWindow().save(shots + "/samples-effects.png"); sp->setProperty("view", "PADS"); }
          win->setProperty("current", "sys"); settle(2500);
          if (auto* panel = win->findChild<QQuickItem*>("sysPanel")) { QQuickItem* f = panel->parentItem(); while (f && !f->inherits("QQuickFlickable")) f = f->parentItem();
            if (f) { QQuickItem* ci = f->property("contentItem").value<QQuickItem*>(); const QPointF at = panel->mapToItem(ci, QPointF(0, 0)); f->setProperty("contentY", std::max(0.0, at.y() - 8)); settle(600); win->grabWindow().save(shots + "/settings-processor.png"); f->setProperty("contentY", 0); } }
          measure.roomCorrection(5, 4, 2.7, 0.5, 1, 1, 1.2, 2.5, 3, 1.2, 6, 12); win->setProperty("current", "acoustics"); settle(700);
          if (QObject* ap = win->findChild<QObject*>("acousticsPage")) { ap->setProperty("tab", 3); settle(800); win->grabWindow().save(shots + "/acoustics-room-correction.png"); ap->setProperty("tab", 0); }
          if (QQuickItem* ms = win->findChild<QQuickItem*>("monitorStrip")) {   // the MONITOR strip with DUAL LISTEN and AI EQ AUTO on
            win->setProperty("current", "mixer"); engine.setMonitor(0, -12, false, false, false); engine.setMonitorDual(true, 2, 0.4, 0); settle(900);
            if (ms->isVisible()) { const QPointF at = ms->mapToScene(QPointF(0, 0)); const qreal dpr = win->devicePixelRatio(); win->grabWindow().copy(QRectF(at.x() * dpr - 4, at.y() * dpr - 4, (ms->width() + 8) * dpr, (ms->height() + 8) * dpr).toRect()).save(shots + "/monitor-dual-listen.png"); }
            engine.setMonitorDual(false, 2, 0.4, 0); engine.setMonitor(0, 0, false, false, false); }
          engine.setAutomix(0, true, 1); engine.setAutomix(1, true, 1); win->setProperty("current", "mixer"); settle(900); win->grabWindow().save(shots + "/mixer-automix.png"); engine.setAutomix(0, false, 1); engine.setAutomix(1, false, 1);
          win->setProperty("current", "update"); settle(900); win->grabWindow().save(shots + "/software-update.png");
          engine.startLive(0); settle(900); win->grabWindow().save(shots + "/live-mode.png"); engine.stopLive(); settle(300);
          { // STORE > DOWNLOADS with two example entries (one already there)
            const QByteArray sha(64, 'a'); const auto entry = [&](const char* id, const char* name, const char* ver, const char* note, const char* file, double size) { QJsonObject f; f["name"] = file; f["url"] = QStringLiteral("https://raw.githubusercontent.com/amirf1389/audio-mixer/main/releases/plugins/") + file; f["sha256"] = QString::fromLatin1(sha); f["size"] = size; QJsonObject files; for (const char* k : {"win-x64-exe", "linux-deb", "linux-tar", "macos-dmg", "macos"}) files[k] = f; QJsonObject o; o["id"] = id; o["name"] = name; o["version"] = ver; o["note"] = note; o["files"] = files; return o; };
            QJsonArray list; list << entry("amfb-warm", "AMFB Warm Channel", "1.0", "tube warmth and a soft ceiling for a voice", "AMFB Warm Channel.vst3", 4.2e6) << entry("amfb-air", "AMFB Air Shelf", "1.1", "a bright, open top end", "AMFB Air Shelf.vst3", 3.1e6);
            update.setPluginList(list); { QDir().mkpath(update.pluginDir); QFile f(update.pluginDir + "/AMFB Air Shelf.vst3"); if (f.open(QIODevice::WriteOnly)) f.write("x"); }
            win->setProperty("current", "store"); settle(700); if (QObject* dp = win->findChild<QObject*>("storeDownloadPanel")) dp->setProperty("visible", true); settle(500); win->grabWindow().save(shots + "/store-downloads.png");
            if (QObject* dp = win->findChild<QObject*>("storeDownloadPanel")) dp->setProperty("visible", false); QFile::remove(update.pluginDir + "/AMFB Air Shelf.vst3"); update.setPluginList(QJsonArray()); }
          for (const char* u : {"https://www.tiktok.com/@amfb.audio/live", "https://www.deezer.com/en/album/302127", "https://tidal.com/browse/track/12345678", "https://open.spotify.com/playlist/37i9dQZF1DXcBWIGoYBM5M", "https://youtu.be/dQw4w9WgXcQ"}) links.add(QString::fromLatin1(u), QString());
          win->setProperty("current", "streaming"); settle(800); win->grabWindow().save(shots + "/streaming.png");
          { // APP CAPTURE: two programs on their own strips (test tones stand in for them), on the MIXER and on MEDIA
            apps.testMode = true; apps.testPrograms = QVariantList{QVariantMap{{"id", "loopback:app:1001"}, {"name", "Spotify"}}, QVariantMap{{"id", "loopback:app:1002"}, {"name", "Chrome (YouTube, web players)"}}, QVariantMap{{"id", "loopback:app:1003"}, {"name", "Zoom"}}};
            const QVariantList r0 = engine.inputRoutes(); const QVariantList st0 = engine.stripState();
            apps.captureTest("Spotify", 440, 2, false); apps.captureTest("Chrome (YouTube, web players)", 660, 3, true); engine.setName(2, "SPOTIFY"); engine.setName(3, "WEB L"); engine.setName(4, "WEB R");
            win->setProperty("current", "mixer"); settle(900); win->grabWindow().save(shots + "/mixer-app-capture.png");
            apps.testDevices = QVariantList{QVariantMap{{"backend", "wasapi"}, {"id", "{0.0.1.0}.{mic}"}, {"name", "Microphone (USB Audio Device)"}, {"kind", "microphone"}, {"inChannels", 1}, {"defaultRate", 48000.0}, {"defaultIn", true}}, QVariantMap{{"backend", "wasapi"}, {"id", "{0.0.1.0}.{cam}"}, {"name", "Microphone (HD Webcam)"}, {"kind", "microphone"}, {"inChannels", 2}, {"defaultRate", 44100.0}}, QVariantMap{{"backend", "wasapi"}, {"id", "{0.0.1.0}.{mix}"}, {"name", "Stereo Mix (Realtek Audio)"}, {"kind", "line"}, {"inChannels", 2}, {"defaultRate", 48000.0}}};
            apps.readProperties("{0.0.1.0}.{cam}");
            win->setProperty("current", "media"); settle(900); apps.readProperties("{0.0.1.0}.{cam}"); settle(300); win->grabWindow().save(shots + "/media-app-capture.png"); apps.testDevices.clear();
            { // the picker with icons and window pictures, and the live PROGRAM VIEW (drawn pictures stand in for the windows here)
              appWindows.testTitle = [](quint32 pid) { return pid == 1001 ? QStringLiteral("Spotify Premium") : pid == 1002 ? QStringLiteral("AMFB live set - YouTube - Google Chrome") : QStringLiteral("Zoom Meeting"); };
              appWindows.testSource = [](quint32 pid, bool icon) { const QColor c = pid == 1001 ? QColor(30, 185, 84) : pid == 1002 ? QColor(219, 68, 55) : QColor(45, 140, 255);
                if (icon) { QImage i(32, 32, QImage::Format_ARGB32_Premultiplied); i.fill(Qt::transparent); QPainter p(&i); p.setRenderHint(QPainter::Antialiasing); p.setBrush(c); p.setPen(Qt::NoPen); p.drawEllipse(2, 2, 28, 28); return i; }
                QImage i(640, 360, QImage::Format_RGB32); i.fill(QColor(24, 26, 32)); QPainter p(&i); p.fillRect(0, 0, 640, 34, c.darker(160)); p.fillRect(20, 60, 180, 280, QColor(36, 40, 48)); p.fillRect(220, 60, 400, 200, c.darker(220)); p.fillRect(220, 280, 400, 12, QColor(60, 64, 72)); p.fillRect(220, 280, 150, 12, c); return i; };
              if (QObject* pk = win->findChild<QObject*>("appPickerOpen")) QMetaObject::invokeMethod(pk, "hit");
              settle(900); win->grabWindow().save(shots + "/app-picker.png");
              for (QObject* o : win->findChildren<QObject*>("appPicker")) QMetaObject::invokeMethod(o, "close");
              appWindows.watch(1002, true); settle(900); win->grabWindow().save(shots + "/media-program-view.png"); appWindows.forget(1002); appWindows.forget(1001); appWindows.forget(1003);
              appWindows.testSource = nullptr; appWindows.testTitle = nullptr; }
            for (int k = 0; k < 8; ++k) apps.release(k);
            for (int i2 : {2, 3, 4}) { engine.setInputRoute(i2, r0[i2].toInt()); engine.setName(i2, st0[i2].toMap()["name"].toString()); engine.setPan(i2, st0[i2].toMap()["pan"].toDouble()); }
            apps.testMode = false; apps.testPrograms.clear(); }
          { // A NEW VERSION: the banner, the dot on SOFTWARE UPDATE in the menu, and the page itself (the signed test bundle of the local update check)
            const QString fx = QStringLiteral(AMIX_SOURCE_DIR) + "/test/local-update"; QFile kf(fx + "/test-key.json");
            if (kf.open(QIODevice::ReadOnly)) { const QJsonObject k = QJsonDocument::fromJson(kf.readAll()).object(); const std::vector<am::lic::Jwk> test{{k.value("x").toString().toStdString(), k.value("y").toString().toStdString()}};
              QTemporaryDir td; for (const QString& n : QDir(fx + "/new").entryList(QDir::Files)) QFile::copy(fx + "/new/" + n, td.path() + "/" + n);
              QSettings qs; for (const char* key : {"update/skipped", "update/seen", "update/toasted", "update/laterUntil"}) qs.remove(key);
              if (update.loadLocalWith(td.path(), false, test)) { win->setProperty("current", "home"); settle(800); win->grabWindow().save(shots + "/update-banner.png");
                win->setProperty("current", "update"); settle(900); win->grabWindow().save(shots + "/software-update-found.png"); }
              update.clearLocal(); for (const char* key : {"update/skipped", "update/seen", "update/toasted", "update/laterUntil"}) qs.remove(key); win->setProperty("current", "home"); settle(300); } }
          { win->setProperty("current", "mixer"); settle(500); QObject* mp = nullptr; for (QObject* o : win->findChildren<QObject*>("mixerPage")) if (o->property("visible").toBool()) mp = o;
            if (mp) { mp->setProperty("editing", true); settle(700); win->grabWindow().save(shots + "/mixer-edit-layout.png"); mp->setProperty("editing", false); settle(200); } }
          engine.setPlugin(0, 0, "fx:preamp"); engine.setPlugin(0, 1, "fx:regulator"); engine.setPlugin(0, 2, "fx:reverb"); engine.setPlugin(0, 3, "fx:protect"); engine.setPluginBypass(0, 2, true);   // THE RACK with effects in
          settle(900); win->grabWindow().save(shots + "/mixer-rack.png"); for (int sl = 0; sl < 4; ++sl) engine.setPlugin(0, sl, "");
          std::printf("amixos --shots: samples-pads, samples-effects, settings-processor, acoustics-room-correction, mixer-automix, mixer-daw\n"); }
        if (QObject* wiz = win->findChild<QObject*>("setupPage")) {   // the factory setup, every step
          win->setProperty("forceSetup", true);
          wiz->setProperty("step", 1); wiz->setProperty("termsAlert", true); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50); win->grabWindow().save(shots + QStringLiteral("/setup-2-alert.png")); wiz->setProperty("termsAlert", false);
          for (int step = 0; step < 7; ++step) { wiz->setProperty("step", step); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50); win->grabWindow().save(shots + QStringLiteral("/setup-%1.png").arg(step + 1)); std::printf("amixos --shots: setup-%d\n", step + 1); }
          win->setProperty("forceSetup", false); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
          { win->setProperty("current", "mixer"); for (int k = 0; k < 10; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);   // the plug-in editor on a tube amp and on the hall reverb
            if (QObject* edw = win->findChild<QObject*>("plugEd")) { for (const auto& [kind, name, file] : {std::tuple<const char*, const char*, const char*>{"fx:tubeamp", "TUBE AMP", "plugin-editor-tubeamp.png"}, {"fx:hall", "HALL REVERB", "plugin-editor-hall.png"}, {"fx:pingpong", "PING-PONG DELAY", "plugin-editor-pingpong.png"}, {"fx:peakremover", "PEAK REMOVER", "plugin-editor-peakremover.png"}, {"fx:automiceq", "AUTO MIC EQ", "plugin-editor-automiceq.png"}, {"fx:regulator", "POWER REGULATOR", "plugin-editor-regulator.png"}, {"fx:protect", "SHORT PROTECT", "plugin-editor-protect.png"}, {"fx:rider", "VOICE RIDER", "plugin-editor-rider.png"}, {"fx:clean", "VOICE CLEAN", "plugin-editor-clean.png"}, {"fx:reverb", "PLATE REVERB", "plugin-editor-plate.png"}, {"fx:syncdelay", "SYNC DELAY", "plugin-editor-delay.png"}, {"fx:analog", "ANALOG CHANNEL", "plugin-editor-analog.png"}, {"fx:chorus", "STEREO CHORUS", "plugin-editor-chorus.png"}, {"fx:pitch", "PITCH SHIFTER", "plugin-editor-pitch.png"}, {"fx:barrier", "SOUND BARRIER", "plugin-editor-barrier.png"}, {"fx:sampler", "SAMPLE PLAYER", "plugin-editor-sampler.png"}, {"fx:desquash", "DE-SQUASH", "plugin-editor-desquash.png"}, {"fx:mseq", "MS COLOUR EQ", "plugin-editor-mseq.png"}, {"fx:isoeq", "ISO EQ 10", "plugin-editor-isoeq.png"}, {"fx:mod", "MOD MACHINE", "plugin-editor-mod.png"}, {"fx:opto", "OPTO LEVELER", "plugin-editor-opto.png"}, {"fx:glue", "GLUE COMP", "plugin-editor-glue.png"}, {"fx:tape", "TAPE MACHINE", "plugin-editor-tape.png"}, {"fx:transient", "TRANSIENT SHAPER", "plugin-editor-transient.png"}, {"fx:deesser", "DE-ESSER", "plugin-editor-deesser.png"}, {"fx:maximizer", "MAXIMIZER", "plugin-editor-maximizer.png"}, {"fx:multiband", "MULTIBAND COMP", "plugin-editor-multiband.png"}, {"fx:imager", "STEREO IMAGER", "plugin-editor-imager.png"}, {"fx:autopan", "AUTO PAN", "plugin-editor-autopan.png"}, {"fx:doubler", "VOCAL DOUBLER", "plugin-editor-doubler.png"}, {"fx:iamfelement", "IAMF ELEMENT", "plugin-editor-iamfelement.png"}, {"fx:iamfrender", "IAMF RENDERER", "plugin-editor-iamfrender.png"}, {"fx:wshape", "W-SHAPE TUNER", "plugin-editor-wshape.png"}, {"fx:convclip", "CONVERTER CLIP", "plugin-editor-convclip.png"}, {"fx:delay", "TAPE DELAY", "plugin-editor-tapedelay.png"}, {"fx:antifeedback", "FEEDBACK KILLER", "plugin-editor-antifeedback.png"}, {"fx:subsynth", "SUB SYNTH", "plugin-editor-subsynth.png"}, {"fx:dualeq", "DUAL LISTEN EQ", "plugin-editor-dualeq.png"}, {"fx:drive", "TUBE DRIVE", "plugin-editor-drive.png"}, {"fx:flanger", "FLANGER", "plugin-editor-flanger.png"}, {"fx:phaser", "PHASER", "plugin-editor-phaser.png"}, {"fx:tremolo", "TREMOLO", "plugin-editor-tremolo.png"}, {"fx:wah", "AUTO WAH", "plugin-editor-wah.png"}, {"fx:crusher", "BIT CRUSHER", "plugin-editor-crusher.png"}, {"fx:exciter", "EXCITER", "plugin-editor-exciter.png"}, {"fx:preamp", "PRE AMP", "plugin-editor-preamp.png"}, {"fx:postamp", "POST AMP", "plugin-editor-postamp.png"}, {"fx:room", "ROOM REVERB", "plugin-editor-room.png"}}) {
                engine.setPlugin(0, 8, kind); QMetaObject::invokeMethod(edw, "openFor", Q_ARG(QVariant, 0), Q_ARG(QVariant, 8), Q_ARG(QVariant, QString::fromLatin1(kind)), Q_ARG(QVariant, QString::fromLatin1(name)));
                { QElapsedTimer bt; bt.start(); while (bt.elapsed() < 500) QCoreApplication::processEvents(QEventLoop::AllEvents, 20); } win->grabWindow().save(shots + "/" + file);
                { QObject* lv = win->findChild<QObject*>("liveView"); QObject* tint = win->findChild<QObject*>("plateTint"); const QString k = QString::fromLatin1(kind);   // each family: its own plate colour, and its display where it has one
                  const bool wantView = k == "fx:tubeamp" || k == "fx:hall" || k == "fx:reverb" || k == "fx:analog" || k == "fx:chorus" || k == "fx:mod" || k == "fx:autopan" || k == "fx:flanger" || k == "fx:phaser" || k == "fx:tremolo" || k == "fx:wah" || k == "fx:drive" || k == "fx:preamp" || k == "fx:room";   // the families with a live display (sweep, transfer curve, tail)
                  if (!lv || !tint || lv->property("visible").toBool() != wantView || tint->property("opacity").toDouble() < 0.2) { std::fprintf(stderr, "amixos --shots: the panel of %s has the wrong look\n", kind); std::exit(203); } }
                if (QString::fromLatin1(kind) == "fx:syncdelay") {   // 120 BPM and a quarter note must read 500 ms on the delay panel
                  { const QVariantList pp = engine.pluginParams(0, 8); engine.setPluginParam(0, 8, pp[0].toMap()["id"].toInt(), (120.0 - 60.0) / 140.0); engine.setPluginParam(0, 8, pp[1].toMap()["id"].toInt(), 1.5 / 6.0); } QMetaObject::invokeMethod(edw, "reload"); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                  QObject* ms = win->findChild<QObject*>("delayMs"); QObject* panel = win->findChild<QObject*>("delayTime");
                  if (!ms || !panel || !panel->property("visible").toBool() || ms->property("text").toString() != "500") { std::fprintf(stderr, "the delay panel does not read 500 ms at 120 BPM, 1/4 (reads %s)\n", ms ? qPrintable(ms->property("text").toString()) : "nothing"); std::exit(238); } }
                QMetaObject::invokeMethod(edw, "close"); }
              engine.setPlugin(0, 8, ""); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50); }
            else { std::fprintf(stderr, "amixos --shots: the plug-in editor was not found\n"); } }
          win->setProperty("bootOn", true); { QElapsedTimer bt; bt.start(); while (bt.elapsed() < 1700) QCoreApplication::processEvents(QEventLoop::AllEvents, 20); } win->grabWindow().save(shots + QStringLiteral("/boot-splash.png")); win->setProperty("bootOn", false); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        frame.setCardType(0, "aec-in"); frame.setCardType(1, "digital-in"); frame.setCardType(2, "analog-out"); frame.setCardType(3, "hybrid");
        frame.setPhantom(0, 0, true); frame.setPhantom(0, 1, true); frame.setChannelStrip(0, 0, 0); frame.setChannelStrip(0, 1, 1); frame.setLogicIn(2, true); frame.setLogicIn(7, true);   // a populated frame for the I/O FRAME picture
        for (const char* id : {"home", "frame", "engctl", "delay", "cloud", "cascade", "mixer", "dsp", "dyn", "gate", "bus", "dca", "scenes", "presets", "routing", "renew", "arrange", "io", "meter", "score", "amp", "assistant", "store", "lufs", "sys", "fx", "osc", "phase", "acoustics", "surround", "master", "rtaeq", "miceq", "live", "tools", "calib", "boot", "help", "acct", "access", "admin", "ai", "plugins", "network", "media", "asio", "samples", "rtaeq", "bluetooth"}) {
          if (const QStringList only = qEnvironmentVariable("AMIX_SHOTS_PAGES").split(',', Qt::SkipEmptyParts); !only.isEmpty() && !only.contains(QString::fromLatin1(id))) continue;   // AMIX_SHOTS_PAGES=mixer,asio: only those pages (a quick look at one page)
          if (QString::fromLatin1(id) == "rtaeq") { QVariantList golden; for (double g : {1.0, 5.0, 3.0, 3.0, 4.0, 2.0, 2.0, 3.0, 3.0, 1.0}) golden << g; engine.setGraphicEq(0, golden, true); }   // the beta's Golden EQ on the shot
          win->setProperty("current", QString::fromLatin1(id)); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
          const QImage img = win->grabWindow(); img.save(shots + "/" + id + ".png"); std::printf("amixos --shots: %s %dx%d\n", id, img.width(), img.height());
          if (QString::fromLatin1(id) == "amp") {   // AMP / SYSTEM: a sub on output 3, tops on 1-2, a delayed fill; the stage of output 1 is checked against the engine
            const QString setup = engine.setupSystem("2way", "mono", 100, true);   // LOW L, HIGH L, LOW R, HIGH R, SUB (mono)
            { const QVariantList r0 = engine.outputRoutes(); const QString no = engine.setupSystem("3way", "stereo", 100);   // 8 outputs on the 2-output test card: refused, nothing changes
              if (engine.running() && engine.deviceOutputs() < 8 && (!no.startsWith("Not set up") || engine.outputRoutes() != r0 || engine.ampState(0)["name"] != "LOW L")) { std::fprintf(stderr, "amixos --shots: a system too big for the sound card was not refused (%s)\n", qPrintable(no)); std::exit(166); }
              if (engine.ampOutputsUsed() != 5) { std::fprintf(stderr, "amixos --shots: outputs used %d (want 5)\n", engine.ampOutputsUsed()); std::exit(166); } }
            const QVariantList routes = engine.outputRoutes(); const int subSrc = routes.size() > 4 ? routes[4].toInt() : -1;
            if (engine.ampState(4)["name"] != "SUB" || engine.ampState(0)["name"] != "LOW L" || subSrc != am::Runtime::kSrcMasterMono) { std::fprintf(stderr, "amixos --shots: SYSTEM SETUP is wrong (%s; sub source %d)\n", qPrintable(setup), subSrc); std::exit(163); }
            { QVariantList eq = engine.ampState(1)["eq"].toList(); QVariantMap b = eq[3].toMap(); b["type"] = 1; b["freq"] = 2500.0; b["gain"] = -4.0; b["q"] = 2.0; eq[3] = b; engine.setAmp(1, {{"eq", eq}}); }   // a horn's presence peak, tamed
            { double at = 0; for (const QVariant& v : engine.ampResponse(1)) { const QVariantMap m = v.toMap(); if (std::fabs(m["hz"].toDouble() - 2500) < 150) at = m["db"].toDouble(); } if (at > -2.5) { std::fprintf(stderr, "amixos --shots: the speaker EQ is not in the response (%.1f dB at 2.5 kHz)\n", at); std::exit(162); } }
            engine.setAmp(4, {{"delayMs", 2.9}});   // the sub 1 m behind the tops
            const QVariantMap a0 = engine.ampState(0); const QVariantList r = engine.ampResponse(4); double at40 = 0, at2k = 0; for (const QVariant& v : r) { const QVariantMap m = v.toMap(); if (std::fabs(m["hz"].toDouble() - 40) < 4) at40 = m["db"].toDouble(); if (std::fabs(m["hz"].toDouble() - 2000) < 150) at2k = m["db"].toDouble(); }
            if (!a0["on"].toBool() || a0["name"] != "LOW L" || at40 < -6 || at2k > -40 || !QSettings().value("console/state").toString().contains("LOW L")) { std::fprintf(stderr, "amixos --shots: the AMP stage is wrong (on %d, sub 40 Hz %.1f dB, 2 kHz %.1f dB)\n", a0["on"].toBool(), at40, at2k); std::exit(165); }
            QObject* ap = win->findChild<QObject*>("ampPage"); if (ap) ap->setProperty("sel", 1); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            win->grabWindow().save(shots + "/amp.png"); std::printf("amixos --shots: amp (%s; sub 40 Hz %.1f dB, 2 kHz %.1f dB)\n", qPrintable(setup), at40, at2k);
            engine.clearSystem(); if (engine.ampOutputsUsed() != 0 || engine.outputRoutes()[1].toInt() != am::Runtime::defaultOutputSource(1)) { std::fprintf(stderr, "amixos --shots: RESET left a stage or a route behind\n"); std::exit(166); } }   // the pages after this one are checked with plain outputs
          if (QString::fromLatin1(id) == "network") {   // DANTE + CLOCK: a simulated Dante Virtual Soundcard and two PTP grandmasters (injected packets, nothing sent)
            const QVariantList real = engine.devices(); QVariantList rig = real; QVariantMap dvs; dvs["backend"] = "asio"; dvs["id"] = "Dante Virtual Soundcard"; dvs["name"] = "Dante Virtual Soundcard"; dvs["inChannels"] = 64; dvs["outChannels"] = 64; dvs["defaultRate"] = 48000.0; rig.prepend(dvs);
            engine.simulateDevices(rig);
            auto ann = [](unsigned char last, int prio, int cls) { QByteArray p(64, 0); p[0] = 11; p[1] = 2; p[3] = 64; p[47] = static_cast<char>(prio); p[48] = static_cast<char>(cls); p[49] = 0x21; p[52] = static_cast<char>(128); for (int i = 0; i < 8; ++i) { p[20 + i] = static_cast<char>(0x10 + i); p[53 + i] = static_cast<char>(0x10 + i); } p[27] = static_cast<char>(last); p[60] = static_cast<char>(last); p[63] = 0x20; return p; };
            net.injectPtp(ann(0xA1, 100, 6), "192.168.10.21"); net.injectPtp(ann(0xB2, 128, 248), "192.168.10.40");
            const QVariantList ms = net.ptpMasters(); if (ms.size() != 2 || ms[0].toMap()["priority1"].toInt() != 100) { std::fprintf(stderr, "amixos --shots: the PTP monitor is wrong (%d clocks)\n", int(ms.size())); std::exit(161); }
            win->setProperty("current", "network"); QList<QObject*> nps; for (int k = 0; k < 40 && nps.isEmpty(); ++k) { QCoreApplication::processEvents(QEventLoop::AllEvents, 50); nps = win->findChildren<QObject*>("networkPage"); } for (QObject* o : nps) o->setProperty("view", 2); for (int k = 0; k < 10; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50); std::printf("amixos --shots: %d network page(s)\n", int(nps.size()));
            win->grabWindow().save(shots + "/network-dante.png"); std::printf("amixos --shots: network-dante (%d PTP clocks)\n", int(ms.size()));
            for (QObject* o : nps) o->setProperty("view", 0); engine.simulateDevices(real); }
          if (QString::fromLatin1(id) == "store") {   // STORE: every effect and extension listed; ADD VST adds a folder to the scan
            QObject* sp = win->findChild<QObject*>("storePage"); const int n = sp ? sp->property("items").toList().size() : 0;
            const QString vdir = QDir::tempPath() + "/amixos-store-vst"; QDir().mkpath(vdir); settings.addVstDir(vdir); const bool added = settings.vstDirs().contains(vdir);
            if (n < 46 || !added) { std::fprintf(stderr, "amixos --shots: the STORE is wrong (%d items, folder added %d)\n", n, added); std::exit(160); }
            std::printf("amixos --shots: store (%d items, ADD VST folder ok)\n", n); }
          if (QString::fromLatin1(id) == "assistant") {   // the assistant answers three questions from the live console
            QObject* as = win->findChild<QObject*>("assistantPage");
            if (as) for (const char* q : {"Why is there no sound?", "How does my mix sound?", "Set up a sub and tops"}) QMetaObject::invokeMethod(as, "ask", Q_ARG(QVariant, QString::fromLatin1(q)));
            for (int k = 0; k < 8; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            const int n = as ? as->property("chat").toList().size() : 0; if (n < 7) { std::fprintf(stderr, "amixos --shots: the assistant did not answer (%d messages)\n", n); std::exit(164); }
            win->grabWindow().save(shots + "/assistant.png"); std::printf("amixos --shots: assistant (%d messages)\n", n); }
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
          if (QString::fromLatin1(id) == "media") {   // the formats FFmpeg adds (OGG Vorbis, Opus, WavPack, APE ...): made with the ffmpeg program when it is here
            media.clear(); int added = 0;
            for (const char* f : {"ogg:libvorbis", "opus:libopus", "wv:wavpack", "flac:flac", "m4a:aac", "mp3:libmp3lame"}) { const QString x = QString::fromLatin1(f), ext = x.section(':', 0, 0), codec = x.section(':', 1);
              const QString path = "/tmp/amfx1-shot-format." + ext; QProcess::execute("ffmpeg", {"-nostdin", "-v", "error", "-y", "-f", "lavfi", "-i", "sine=frequency=440:duration=3", "-ac", "2", "-c:a", codec, path});
              if (QFile::exists(path)) added += media.add(path); }
            waitMs(400); win->grabWindow().save(shots + "/media-formats.png"); std::printf("amixos --shots: media-formats (%d files)\n", added); media.clear(); }
          if (QString::fromLatin1(id) == "surround") { engine.setCinemaHall(true); engine.setHeadTrack(true); engine.setHeadYaw(35); waitMs(700); win->grabWindow().save(shots + "/360-cinema-hall.png"); std::printf("amixos --shots: 360-cinema-hall\n");
            if (!win->findChild<QObject*>("sceneMap")) std::fprintf(stderr, "amixos --shots: FAILED the 360 scene map is missing\n"); engine.setHeadYaw(0); engine.setHeadTrack(false); engine.setCinemaHall(false); engine.setBusFx(15, "");
            { int k = -1; const QVariantList pl = engine.plugins(0); for (int j = 0; j < pl.size(); ++j) if (pl[j].toMap()["kind"].toString().isEmpty()) { k = j; break; }   // CH 1 placed by an IAMF ELEMENT, CH 2 by the 360 stage
              engine.setPlugin(0, k, "fx:iamfelement"); engine.setSpatial(0, true, -40, 15, 2.5, 0.2); engine.setSpatial(1, true, 70, 0, 4, 0.1);
              if (QObject* sp = win->findChild<QObject*>("spatialPage")) sp->setProperty("sel", 0); waitMs(500); win->grabWindow().save(shots + "/360-iamf.png"); std::printf("amixos --shots: 360-iamf\n");
              engine.setSpeakerArray(true); engine.setSpatial(2, true, 30, 30, 1.5, 0.1); waitMs(500); win->grabWindow().save(shots + "/360-front10.png"); std::printf("amixos --shots: 360-front10\n"); engine.setSpatial(2, false, 0, 0, 1, 0); engine.setSpeakerArray(false);
              engine.setPlugin(0, k, ""); engine.setSpatial(0, false, 0, 0, 1, 0); engine.setSpatial(1, false, 0, 0, 1, 0); } }
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
            { // PORT CLASH: network MIDI must not share the AES67 receiver's UDP port (both reuse addresses: packets would go to the wrong one)
              const int keepMidi = net.midiNetPort(); net.setMidiNetPort(46421);   // MIDI on 46421 + 46422; the receiver listens on 47422: no clash
              if (Net::portsClash(47422, 46421) || !Net::portsClash(47422, 47422) || !Net::portsClash(47422, 47421)) { std::fprintf(stderr, "amixos --shots: the UDP port clash rule is wrong\n"); std::exit(216); }
              net.setMidiNetPort(47421); if (net.setMidiNet(true) || net.midiNetOn()) { std::fprintf(stderr, "amixos --shots: network MIDI opened on the AES67 receiver's port\n"); std::exit(217); }
              net.setMidiNet(false); net.setMidiNetPort(keepMidi); std::printf("amixos --shots: UDP port clash refused\n"); }
            if (!win->findChild<QObject*>("aoipPanel")) { std::fprintf(stderr, "amixos --shots: FAILED the AUDIO + MIDI panel is missing\n"); }
            net.stopSend(); net.stopReceive(); net.setRxFirstStrip(first); net.setRxAddress(rxA); net.setTxAddress(txA); net.setRxPort(rxP); net.setTxPort(txP); engine.setGain(1, 0); np->setProperty("view", 0); } }
          if (QString::fromLatin1(id) == "network" && remote.available()) {   // PHONE REMOTE running: the pairing PIN and its QR code (scan to connect)
            const bool wasRunning = engine.running(); const QString wb = engine.backend(); const double wr = engine.sampleRate(); const int wf = engine.blockFrames(), wi = engine.deviceInputs(), wo = engine.deviceOutputs();
            remote.start(true); waitMs(1800); win->grabWindow().save(shots + "/network-remote-qr.png");
            std::printf("amixos --shots: network-remote-qr (%d address(es))\n", int(remote.addresses().size()));
            remote.stop(); waitMs(600); if (wasRunning && !engine.running()) engine.open(wb, "", "", wr, wf, wi, wo); waitMs(300); }   // the sound card back exactly as it was
          if (QString::fromLatin1(id) == "fx") { if (QObject* fp = win->findChild<QObject*>("effectsPage")) { fp->setProperty("browse", true); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50); win->grabWindow().save(shots + "/fx-presets.png"); fp->setProperty("browse", false); std::printf("amixos --shots: fx-presets\n"); } }
          if (QString::fromLatin1(id) == "dsp") { if (QObject* tp = win->findChild<QObject*>("tonePage")) { tp->setProperty("rta", true); waitMs(500); win->grabWindow().save(shots + "/tone-rta.png"); tp->setProperty("rta", false);
            engine.applyEqPreset(1, 0); tp->setProperty("contentY", 400); waitMs(300); win->grabWindow().save(shots + "/tone-presets.png"); tp->setProperty("contentY", 0); for (int b = 0; b < 10; ++b) engine.setEq(0, b, 0, 1000, 1, 0); engine.setCrossover(0, 0, 0, 80, 0, 16000); std::printf("amixos --shots: tone-rta, tone-presets\n"); } }
          if (QString::fromLatin1(id) == "rtaeq") { if (QObject* rp = win->findChild<QObject*>("rtaEqPage")) { for (const char* v : {"faders", "both"}) { rp->setProperty("view", QString::fromLatin1(v)); waitMs(250); win->grabWindow().save(shots + "/rtaeq-" + v + ".png"); } rp->setProperty("view", "diamond"); std::printf("amixos --shots: rtaeq-faders, rtaeq-both\n"); } engine.setGraphicEq(0, QVariantList(), false); }
          if (QString::fromLatin1(id) == "routing") {
            if (QObject* rp = win->findChild<QObject*>("routingPage")) { rp->setProperty("tab", 1); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50); win->grabWindow().save(shots + "/routing-outputs.png"); std::printf("amixos --shots: routing-outputs\n"); }
          }
          if (QString::fromLatin1(id) == "mixer") {   // SOUND SCORE: the badge and its breakdown
            QObject* pop = nullptr; for (QObject* o : win->findChildren<QObject*>("soundScorePop")) pop = o;
            if (!win->findChild<QObject*>("soundScore") || !pop) { std::fprintf(stderr, "amixos --shots: FAILED the sound score is missing\n"); std::exit(168); }
            waitMs(1500); QMetaObject::invokeMethod(pop, "open"); waitMs(400); win->grabWindow().save(shots + "/mixer-score.png"); std::printf("amixos --shots: mixer-score\n"); QMetaObject::invokeMethod(pop, "close"); waitMs(150);
            const QVariantMap sc = engine.soundScore(); if (!sc.contains("parts") || !sc.contains("valid")) { std::fprintf(stderr, "amixos --shots: FAILED sound score shape\n"); std::exit(168); } }
          if (QString::fromLatin1(id) == "mixer") {   // the same page with the analogue VU faces
            QObject* mixerShown = nullptr; for (QObject* o : win->findChildren<QObject*>("mixerPage")) if (o->property("visible").toBool()) mixerShown = o;   // the page on screen (an earlier one may still be on its way out)
            if (QObject* mixer = mixerShown) {
              mixer->setProperty("rackCompact", true); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50); win->grabWindow().save(shots + "/mixer-compact.png"); mixer->setProperty("rackCompact", false); std::printf("amixos --shots: mixer-compact\n");
              if (QObject* pop = mixer->findChild<QObject*>("mixerQuick")) { QMetaObject::invokeMethod(pop, "open"); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50); win->grabWindow().save(shots + "/mixer-settings.png"); QMetaObject::invokeMethod(pop, "close"); std::printf("amixos --shots: mixer-settings\n"); }
              mixer->setProperty("stripMenuDemo", true); waitMs(600); win->grabWindow().save(shots + "/mixer-strip-menu.png"); mixer->setProperty("stripMenuDemo", false); waitMs(200);   // the strip menu with ADD AN EFFECT open
              mixer->setProperty("sendView", 1); waitMs(400); win->grabWindow().save(shots + "/mixer-aux.png"); mixer->setProperty("sendView", 2); waitMs(400); win->grabWindow().save(shots + "/mixer-fx.png"); mixer->setProperty("sendView", 0); std::printf("amixos --shots: mixer-aux, mixer-fx\n");
              { QMetaObject::invokeMethod(win, "openAudioSettings"); waitMs(500); win->grabWindow().save(shots + "/audio-midi-settings.png");   // AUDIO / MIDI SETTINGS: opens, shows the running device type, APPLY keeps the engine running
                QObject* as = win->findChild<QObject*>("audioSettings");
                if (!as || !as->property("opened").toBool() || as->property("type").toString().isEmpty()) { std::fprintf(stderr, "amixos --shots: AUDIO / MIDI SETTINGS did not open\n"); std::exit(244); }
                QMetaObject::invokeMethod(as, "apply"); waitMs(300); if (!engine.running()) { std::fprintf(stderr, "amixos --shots: APPLY in AUDIO / MIDI SETTINGS left the engine stopped\n"); std::exit(245); }
                engine.setMonitor(0, 0, false, false, true); waitMs(300);   // the monitor muted: the only thing on the outputs is the test tone, at -20 dBFS
                if (!engine.testTone() || !engine.testToneActive()) { std::fprintf(stderr, "amixos --shots: the test tone did not start\n"); std::exit(246); }
                waitMs(500); { const double l = engine.telemetry()["monitorLDb"].toDouble(), r = engine.telemetry()["monitorRDb"].toDouble();
                  if (std::fabs(l + 20.0) > 1.5 || std::fabs(r + 20.0) > 1.5) { std::fprintf(stderr, "amixos --shots: the test tone is not at -20 dBFS on outputs 1-2 (%.1f / %.1f)\n", l, r); std::exit(248); } }
                waitMs(1600); if (engine.testToneActive()) { std::fprintf(stderr, "amixos --shots: the test tone did not end after its second\n"); std::exit(247); }
                engine.setMonitor(0, 0, false, false, false);
                QMetaObject::invokeMethod(as, "close"); waitMs(200); }
              { // DSP MODE: ANALOG leaves a quiet tone alone and rounds a loud one off; DIGITAL is exact again. DAC DETECTION: a USB DAC is listed, its microphone side is not
                engine.setDspMode("analog"); if (engine.dspMode() != "analog" || !engine.mastering()["analog"].toBool()) { std::fprintf(stderr, "amixos --shots: DSP MODE ANALOG did not switch on\n"); std::exit(255); }
                engine.setDspMode("digital"); if (engine.dspMode() != "digital") std::exit(255);
                { const int bits0 = engine.outputBits(); engine.setDspMode("engineer");   // HI-RES ENGINEER: 24-bit dither, DC block, limiter at -1.0 dB; DIGITAL gives the word length back
                  if (engine.dspMode() != "engineer" || engine.outputBits() != 24 || !engine.dcBlock() || engine.mastering()["analog"].toBool()) { std::fprintf(stderr, "amixos --shots: DSP MODE HI-RES ENGINEER did not set 24-bit / DC block / digital\n"); std::exit(255); }
                  engine.setDspMode("analog"); if (engine.dspMode() != "analog" || engine.outputBits() != bits0) { std::fprintf(stderr, "amixos --shots: leaving HI-RES ENGINEER did not restore the word length\n"); std::exit(255); }
                  engine.setDspMode("digital"); if (engine.dspMode() != "digital") std::exit(255); }
                { const QVariantMap lim0 = engine.masterLimiter(); engine.setMasterLimiter(false, -0.3); const int bits0 = engine.outputBits(); const bool dc0 = engine.dcBlock();   // FAST · TRUE BUFFER: limiter at -1.0 dBTP; leaving it restores everything
                  engine.setDspMode("fast"); const QVariantMap l = engine.masterLimiter(), rep = engine.latencyReport();
                  if (engine.dspMode() != "fast" || !l["on"].toBool() || std::fabs(l["ceiling"].toDouble() + 1.0) > 1e-9 || engine.outputBits() != bits0 || engine.mastering()["analog"].toBool() || (rep["running"].toBool() && rep["fastBlock"].toInt() < 16)) { std::fprintf(stderr, "amixos --shots: DSP MODE FAST did not set the -1.0 dBTP limiter / the block suggestion\n"); std::exit(255); }
                  engine.setDspMode("digital"); const QVariantMap b = engine.masterLimiter();
                  if (engine.dspMode() != "digital" || b["on"].toBool() || std::fabs(b["ceiling"].toDouble() + 0.3) > 1e-9 || engine.outputBits() != bits0 || engine.dcBlock() != dc0) { std::fprintf(stderr, "amixos --shots: leaving FAST did not restore the limiter\n"); std::exit(255); }
                  engine.setDspMode("fast"); engine.setDspMode("engineer"); if (engine.dspMode() != "engineer" || std::fabs(engine.masterLimiter()["ceiling"].toDouble() + 1.0) > 1e-9) { std::fprintf(stderr, "amixos --shots: FAST -> HI-RES ENGINEER\n"); std::exit(255); }
                  engine.setDspMode("digital"); if (engine.dspMode() != "digital" || engine.outputBits() != bits0) std::exit(255);
                  engine.setMasterLimiter(lim0["on"].toBool(), lim0["ceiling"].toDouble()); engine.setDcBlock(dc0); }
                const QVariantList real = engine.devices(); QVariantList rig;
                const auto dev = [](const char* b, const char* id, const char* name, int in, int out) { QVariantMap m; m["backend"] = b; m["id"] = id; m["name"] = name; m["inChannels"] = in; m["outChannels"] = out; m["defaultRate"] = 48000.0; m["sampleRates"] = QVariantList{44100.0, 48000.0, 96000.0, 192000.0}; return m; };
                rig << dev("wasapi", "{d1}", "FiiO K7 USB DAC", 0, 2) << dev("wasapi", "{s1}", "Speakers (Realtek(R) Audio)", 0, 2) << dev("wasapi", "{m1}", "Microphone (USB DAC mic)", 1, 0);
                engine.simulateDevices(rig); const QVariantList dl = engine.dacsFound();
                if (dl.size() != 1 || dl[0].toMap()["name"].toString() != "FiiO K7 USB DAC" || dl[0].toMap()["maxRate"].toDouble() != 192000.0 || dl[0].toMap()["current"].toBool()) { std::fprintf(stderr, "amixos --shots: DAC detection is wrong (%d found)\n", int(dl.size())); std::exit(256 - 1); }
                if (engine.openDac("wasapi", "{nope}", 256)) std::exit(255);
                engine.openDac("wasapi", "{d1}", 256, 1100000, 24); std::printf("amixos --shots: DAC at 1100 kHz / 24 bit -> %s\n", qPrintable(engine.status()));
                engine.simulateDevices(real); if (!engine.running()) engine.open("null", "", "", 48000, 256, 2, 2); }
              { // WASAPI: a headset microphone is never opened; with headphones out the default microphone stays off; a desk microphone picked by name is left alone
                const QVariantList real = engine.devices(); QVariantList rig;
                const auto dev = [](const char* id, const char* name, int in, int out, bool def) { QVariantMap m; m["backend"] = "wasapi"; m["id"] = id; m["name"] = name; m["inChannels"] = in; m["outChannels"] = out; m["defaultRate"] = 48000.0; m["defaultIn"] = def && in > 0; m["defaultOut"] = def && out > 0; return m; };
                rig << dev("{hp}", "Headphones (Realtek(R) Audio)", 0, 2, true) << dev("{hm}", "Headset Microphone (Realtek(R) Audio)", 1, 0, true) << dev("{dm}", "Microphone (USB Condenser)", 1, 0, false) << dev("{sp}", "Speakers (Realtek(R) Audio)", 0, 2, false);
                engine.simulateDevices(rig);
                engine.setHeadsetMicOff(false); engine.open("wasapi", "", "{hp}", 48000, 256, 2, 2); const bool a0 = engine.micOffNote().isEmpty();   // the switch off (the default): 2 in stay open
                engine.setHeadsetMicOff(true);
                engine.open("wasapi", "{hm}", "{sp}", 48000, 256, 1, 2); const bool a1 = !engine.micOffNote().isEmpty();   // a headset microphone, named
                engine.open("wasapi", "", "{hp}", 48000, 256, 2, 2); const bool a2 = !engine.micOffNote().isEmpty();        // headphones out, the default microphone
                engine.open("wasapi", "{dm}", "{hp}", 48000, 256, 1, 2); const bool a3 = engine.micOffNote().isEmpty();     // a desk microphone picked by name
                engine.setHeadsetMicOff(false);
                engine.open("wasapi", "", "{sp}", 48000, 256, 0, 2); const bool a4 = engine.micOffNote().isEmpty();          // no input asked for, switch off: 2 in all the same, nothing turned off
                if (!a0 || !a1 || !a2 || !a3 || !a4) { std::fprintf(stderr, "amixos --shots: the WASAPI headset microphone rule is wrong (%d %d %d %d %d)\n", a0, a1, a2, a3, a4); std::exit(205); }
                engine.simulateDevices(real); engine.open("null", "", "", 48000, 256, 2, 2); }
              { // THE ASIO SECTION with three simulated drivers: the list shows them all, MANUAL picks none, OPEN on one that cannot start leaves none marked as running
                const QVariantList real = engine.devices(); const QString keepPrefer = engine.autoPrefer(); QVariantList rig;
                const auto dev = [](const char* b, const char* id, const char* name, int in, int out) { QVariantMap m; m["backend"] = b; m["id"] = id; m["name"] = name; m["inChannels"] = in; m["outChannels"] = out; m["defaultRate"] = 48000.0; return m; };
                rig << dev("asio", "{a1}", "Focusrite USB ASIO", 18, 20) << dev("asio", "{a2}", "ASIO4ALL v2", 2, 2) << dev("asio", "{a3}", "Realtek ASIO", 2, 2) << dev("wasapi", "{w1}", "Speakers (Realtek(R) Audio)", 0, 2);
                engine.simulateDevices(rig); engine.setAutoPrefer("manual"); win->setProperty("current", "asio"); waitMs(500); win->grabWindow().save(shots + "/asio-drivers.png");
                { // UNIVERSAL ASIO DETECTION, every status on one page: ready, busy with remembered channels, virtual, other bitness, wrapper
                  QVariantList all; all << dev("asio", "{u1}", "Focusrite USB ASIO", 18, 20) << dev("asio", "{u2}", "BEHRINGER UMC ASIO Driver", 4, 4) << dev("asio", "{u3}", "Pioneer DDJ-FLX10 ASIO", 4, 6) << dev("asio", "{u4}", "Line 6 Helix ASIO", 0, 0)
                      << dev("asio", "{u5}", "Voicemeeter Virtual ASIO", 8, 8) << dev("asio", "{u6}", "Realtek ASIO", 2, 2) << dev("asio", "{u7}", "ASIO4ALL v2", 2, 2) << dev("asio", "{u8}", "Dante Virtual Soundcard", 64, 64);
                  engine.simulateDevices(all);   // read once: the Focusrite's counts are remembered
                  QVariantMap f = all[0].toMap(); f["inChannels"] = 0; f["outChannels"] = 0; f["probeError"] = "did not start: the device is in use by another program"; all[0] = f;
                  QVariantMap h = all[3].toMap(); h["probeError"] = "a 32-bit driver: only the 32-bit AmF X1 can load it"; all[3] = h;
                  engine.simulateDevices(all); waitMs(500); win->grabWindow().save(shots + "/asio-universal.png"); std::printf("amixos --shots: asio-universal (best %s)\n", qPrintable(engine.bestAsioDriver()));
                  QSettings().remove("asio/seen/Focusrite USB ASIO"); engine.simulateDevices(rig); waitMs(200); }
                const QVariantList l = engine.asioDrivers(); bool any = false; for (const QVariant& v : l) any = any || v.toMap()["current"].toBool();
                if (l.size() != 3 || l[0].toMap()["name"].toString() != "Focusrite USB ASIO" || l[0].toMap()["inChannels"].toInt() != 18 || any || !engine.autoDetect().isEmpty()) { std::fprintf(stderr, "amixos --shots: the ASIO DRIVERS list is wrong\n"); std::exit(253); }
                if (engine.bestAsioDriver() != "{a1}" || engine.asioFrames(48000) != 480 || engine.asioFrames(44100) != 441 || engine.asioFrames(96000) != 960) { std::fprintf(stderr, "amixos --shots: ASIO auto detect or the 10 ms buffer is wrong (%s, %d)\n", qPrintable(engine.bestAsioDriver()), engine.asioFrames(48000)); std::exit(212); }
                { QVariantList only; only << dev("asio", "{w1}", "ASIO4ALL v2", 2, 2) << dev("asio", "{w2}", "FlexASIO", 0, 0); engine.simulateDevices(only); if (engine.bestAsioDriver() != "{w1}") std::exit(213); engine.simulateDevices(QVariantList{dev("wasapi", "{x}", "Speakers", 0, 2)}); if (!engine.bestAsioDriver().isEmpty() || engine.autoOpenAsio()) std::exit(214); engine.simulateDevices(rig); }
                engine.openAsioDriver("{a2}", 256); for (const QVariant& v : engine.asioDrivers()) if (v.toMap()["current"].toBool() && engine.backend() != "asio") { std::fprintf(stderr, "amixos --shots: a driver that did not start is shown as running\n"); std::exit(254); }
                { // SECURE INPUT on SOUND CARD: two microphones, one allowed. Opening the other by hand (the real open path) gives outputs only and says why
                  const QStringList keepList = QSettings().value("audio/allowedInputs").toStringList(); const bool keepOn = engine.secureInput();
                  QVariantList mics; QVariantMap m1 = dev("wasapi", "{m1}", "Microphone (USB Audio Interface)", 2, 0); m1["defaultIn"] = true; mics << m1 << dev("wasapi", "{m2}", "Microphone Array (Realtek(R) Audio)", 2, 0) << dev("wasapi", "{w1}", "Speakers (Realtek(R) Audio)", 0, 2);
                  engine.simulateDevices(mics); engine.setSecureInput(true); engine.setInputAllowed("wasapi", "{m1}", true); engine.setInputAllowed("wasapi", "{m2}", false);
                  engine.open("wasapi", "{m2}", "{w1}", 48000, 256, 2, 2); const bool blocked = engine.micOffNote().startsWith("SECURE INPUT");
                  if (!blocked) { std::fprintf(stderr, "amixos --shots: SECURE INPUT let a device that is not allowed open (%s)\n", qPrintable(engine.micOffNote())); std::exit(214); }
                  win->setProperty("current", "asio"); waitMs(500); win->grabWindow().save(shots + "/asio-secure-input.png"); std::printf("amixos --shots: asio-secure-input\n");
                  QSettings().setValue("audio/allowedInputs", keepList); engine.setSecureInput(keepOn); }
                engine.setAutoPrefer(keepPrefer); engine.simulateDevices(real); if (!engine.running()) engine.open("null", "", "", 48000, 256, 2, 2); win->setProperty("current", "mixer"); waitMs(300);
                { // the channels are read by themselves: with a device of 2 inputs running, 2 are put on strips 1 and 2 and the note says so; with no inputs it says outputs only
                  const int got = engine.readAsioChannels("{a1}"); if (got != engine.deviceInputs() || got != 2 || !engine.asioNote().contains("2 input channel")) { std::fprintf(stderr, "amixos --shots: the device's channels were not read (%d of %d: %s)\n", got, engine.deviceInputs(), qPrintable(engine.asioNote())); std::exit(215); }
                  engine.open("null", "-", "", 48000, 256, 0, 2); if (engine.readAsioChannels("{a1}") != 0 || !engine.asioNote().contains("outputs only")) std::exit(216);   // no input asked for by name ("-"): outputs only
                  engine.open("null", "", "", 48000, 256, 2, 2); engine.resetRouting(); } }
              mixer->setProperty("vuMode", true); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50); win->grabWindow().save(shots + "/mixer-vu.png"); mixer->setProperty("vuMode", false); std::printf("amixos --shots: mixer-vu\n"); }
          }
        }
      }
      if (!shots.isEmpty()) {   // the light theme on three pages
        { // CONNECTIONS with devices ON / OFF: three simulated ALSA devices (the picture machine has none), the USB microphone switched OFF
          auto* cw = qobject_cast<QQuickWindow*>(qml.rootObjects().first()); const QVariantList keep = engine.devices();
          const auto d = [](const char* id, const char* name, int in, int out, bool defIn, bool defOut) { QVariantMap m; m["backend"] = "alsa"; m["id"] = id; m["name"] = name; m["inChannels"] = in; m["outChannels"] = out; m["defaultRate"] = 48000.0; m["defaultIn"] = defIn; m["defaultOut"] = defOut; m["kind"] = in ? "microphone" : "speakers"; return m; };
          engine.simulateDevices({d("hw:0,0", "Speakers (Realtek High Definition Audio)", 0, 2, false, true), d("hw:0,1", "Microphone Array (Realtek High Definition Audio)", 2, 0, true, false), d("hw:1,0", "Microphone (USB Audio CODEC)", 1, 0, false, false), d("hw:1,1", "Speakers (USB Audio CODEC)", 0, 2, false, false)});
          engine.setDeviceEnabled("alsa", "hw:1,0", false); cw->setProperty("current", "home"); waitMs(150); cw->setProperty("current", "io"); waitMs(500); cw->grabWindow().save(shots + "/connections-onoff.png");
          engine.setDeviceEnabled("alsa", "hw:1,0", true); engine.simulateDevices(keep); }
        auto* lw = qobject_cast<QQuickWindow*>(qml.rootObjects().first()); settings.setDarkTheme(false); for (const char* id : {"home", "mixer", "dsp"}) { lw->setProperty("current", QString::fromLatin1(id)); waitMs(350); lw->grabWindow().save(shots + "/" + id + "-light.png"); } settings.setDarkTheme(true); waitMs(200); std::printf("amixos --shots: home-light, mixer-light, dsp-light\n"); }
      // the language tables: 80 languages, every one covers the page names
      int full = 0; for (const QVariant& l : i18n.languages()) if (l.toMap().value("coverage").toInt() >= 44) ++full;
      std::printf("amixos --smoke: %d languages, %d cover all page names; MIXER in fa = %s\n", i18n.count(), full, qPrintable(i18n.t("MIXER", "fa")));
      const int offered = i18n.languages().size(); std::printf("amixos --smoke: %d languages offered in the menu, Swahili offered %d, Swahili MIXER = %s\n", offered, int(i18n.offered("sw")), qPrintable(i18n.t("MIXER", "sw")));
      if (i18n.count() < 80 || offered < 80 || full < offered || !i18n.offered("sw") || i18n.t("MIXER", "sw") == "MIXER" || i18n.t("MIXER", "fa") == "MIXER") { std::fprintf(stderr, "amixos --smoke: language tables incomplete\n"); QCoreApplication::exit(7); return; }   // (a bare `return 7` here would make the lambda non-void and fall off the end: heap corruption)
      { // SYSTEM OPTIMIZATION: the computer is described, memory can be freed, the switches are kept
        const QVariantMap si = engine.systemInfo(); const QVariantMap tr = engine.trimMemoryNow();
        engine.setSysOpt("fastCores", false); const bool off = !engine.systemInfo().value("fastCores").toBool(); engine.setSysOpt("fastCores", true);
        std::printf("amixos --smoke: SYSTEM: %s, %d threads in %d core types, RAM %llu MB, console %llu MB, NPU %s; free memory now: %s; sound thread: %s; switch kept %d\n", qPrintable(si.value("cpuName").toString()),
                    si.value("logical").toInt(), si.value("classes").toInt(), si.value("ramTotalMb").toULongLong(), si.value("processMb").toULongLong(), si.value("npuFound").toBool() ? qPrintable(si.value("npuName").toString()) : "none",
                    qPrintable(tr.value("note").toString()), qPrintable(si.value("audio").toString()), int(off));
        { (void)engine.systemInfo(); QThread::msleep(250); const QVariantMap z = engine.systemInfo();   // PROCESSOR NOW: the second reading has the load; every value in range or plainly not available
          std::fprintf(stderr, "amixos --smoke: PROCESSOR NOW load %s %.0f %%, clock %s, temperatures %d, power %s\n", z.value("loadOk").toBool() ? "yes" : "NO", z.value("cpuLoadPct").toDouble(),
                       z.value("clockOk").toBool() ? "yes" : "not exposed", static_cast<int>(z.value("temps").toList().size()), z.value("powerOk").toBool() ? "yes" : "not exposed");
          if (!z.value("loadOk").toBool() || z.value("cpuLoadPct").toDouble() < 0 || z.value("cpuLoadPct").toDouble() > 100 || !z.contains("ac")) { std::fprintf(stderr, "amixos --smoke: PROCESSOR NOW failed\n"); QCoreApplication::exit(7); return; } }
        if (si.value("logical").toInt() <= 0 || si.value("ramTotalMb").toULongLong() == 0 || tr.value("note").toString().isEmpty() || !off || !engine.systemInfo().value("fastCores").toBool()) { std::fprintf(stderr, "amixos --smoke: SYSTEM OPTIMIZATION failed\n"); QCoreApplication::exit(7); return; } }
      const QVariantMap m = engine.telemetry();
      const qulonglong blocks = m.value("blocks").toULongLong();
      { const QString b = QCoreApplication::applicationDirPath() + "/AmF X1 Plugin Builder"; const bool there = QFileInfo::exists(b) || QFileInfo::exists(b + ".exe");   // the separate PLUG-IN BUILDER sits next to the console (TOOLS opens it)
        std::printf("amixos --smoke: PLUG-IN BUILDER app next to the console %s\n", there ? "ok" : "MISSING"); if (!there) { QCoreApplication::exit(90); return; } }
      { // I/O FRAME: cards, APPLY (routes a strip and puts the echo canceller in its rack), GPIO in from OSC and MIDI, LOCATE both ways
        frame.setCardType(0, "aec-in"); frame.setCardType(1, "analog-out"); frame.setChannelStrip(0, 0, 5); frame.setChannelDevice(0, 0, 1); frame.apply();
        bool aec = false; for (const QVariant& p : engine.plugins(5)) if (p.toMap().value("kind").toString() == "fx:aec") aec = true;
        const bool routed = engine.inputRoutes().size() > 5 && engine.inputRoutes()[5].toInt() == 1;
        frame.handleOsc("/gpio/in/3", 1); const bool oscIn = frame.logicIn()[2].toBool(); frame.handleMidi(0x90, 36 + 6, 100); const bool midiIn = frame.logicIn()[6].toBool();
        frame.handleMidi(0x90, 48, 100); const bool loc = frame.locating(); frame.locate(false);
        std::printf("amixos --smoke: I/O FRAME %s (cards %d ch on the bus, AEC in rack %d, routed %d, GPIO OSC %d MIDI %d, LOCATE %d)\n", aec && routed && oscIn && midiIn && loc ? "ok" : "FAILED", frame.busUsed(), aec, routed, oscIn, midiIn, loc);
        frame.setCardType(0, ""); frame.setCardType(1, ""); frame.setLogicIn(2, false); frame.setLogicIn(6, false); }
      { remote.writeConsoleInfo(); QFile f(QString::fromStdString(am::paths::file("remote-console.json"))); bool ok = f.open(QIODevice::ReadOnly); const QJsonObject o = ok ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
        std::printf("amixos --smoke: phone PLUG-INS snapshot %s (%d VST, %d store items, %d racks)\n", o.value("available").toBool() ? "written" : "MISSING", int(o.value("vst").toArray().size()), int(o.value("store").toArray().size()), int(o.value("racks").toArray().size())); }
      if (auto* bw = qobject_cast<QQuickWindow*>(qml.rootObjects().first())) {   // NO SOUND, AND WHY: the master at -inf is shown, and its button gives the sound back
        QObject* ban = bw->findChild<QObject*>("masterSilentBanner"); const double keepM = engine.masterDb(); engine.setMaster(-60); QCoreApplication::processEvents();
        const bool shown = ban && ban->property("visible").toBool(); QObject* raise = bw->findChild<QObject*>("raiseMasterBtn");
        if (auto* it = qobject_cast<QQuickItem*>(raise)) { const QPointF c = it->mapToScene(QPointF(it->width() / 2, it->height() / 2));   // a real click on the button
          QMouseEvent p(QEvent::MouseButtonPress, c, bw->mapToGlobal(c.toPoint()), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier); QCoreApplication::sendEvent(bw, &p);
          QMouseEvent r(QEvent::MouseButtonRelease, c, bw->mapToGlobal(c.toPoint()), Qt::LeftButton, Qt::NoButton, Qt::NoModifier); QCoreApplication::sendEvent(bw, &r); QCoreApplication::processEvents(); }
        const bool raised = engine.masterDb() > -60; engine.setMaster(keepM); QCoreApplication::processEvents(); const bool hidden = ban && !ban->property("visible").toBool();
        if (!shown || !hidden) { std::fprintf(stderr, "amixos --smoke: the MASTER -inf banner is wrong (shown %d, hidden after %d)\n", shown, hidden); std::exit(85); }
        if (!raised) { std::fprintf(stderr, "amixos --smoke: RAISE TO 0 dB did not raise the master\n"); std::exit(86); }
        std::printf("amixos --smoke: MASTER -inf banner shown, RAISE TO 0 dB works, cleared after\n"); }
      { // SYSTEM INFORMATION: read only, every field safe to show, never an empty map
        const QVariantMap fw = engine.firmwareInfo(); const QVariantList dr = engine.soundDrivers(); bool ok = fw.contains("biosVersion") && fw.contains("os") && fw.contains("maker");
        for (const QString& k : fw.keys()) ok = ok && fw[k].toString().size() <= 96; for (const QVariant& v : dr) ok = ok && !v.toMap()["name"].toString().isEmpty();
        std::printf("amixos --smoke: SYSTEM INFORMATION computer \"%s %s\", firmware \"%s\" \"%s\", OS \"%s\", %d sound driver(s): %s\n", qPrintable(fw["maker"].toString()), qPrintable(fw["model"].toString()), qPrintable(fw["biosVendor"].toString()), qPrintable(fw["biosVersion"].toString()), qPrintable(fw["os"].toString()), int(dr.size()), ok ? "ok" : "FAILED");
        if (!ok) std::exit(96); }
      { // EXPORT: a .zip with only the console's own files, in the folder asked for, never over an earlier one
        const QString dir = QDir::tempPath() + "/amixos-smoke-export"; QDir(dir).removeRecursively(); engine.saveNow();
        { QFile f(settings.dataFolder() + "/daemon.tokens.json"); const bool had = f.exists(); if (!had && f.open(QIODevice::WriteOnly)) { f.write("{\"secret\":1}"); f.close(); }
          const QVariantMap a1 = settings.exportZip(dir), a2 = settings.exportZip(dir); std::vector<am::zip::Listed> got; std::string err; const bool read = am::zip::listZip(a1["path"].toString().toStdString(), got, err);
          bool crc = !got.empty(), leak = false, state = false; for (const auto& g : got) { crc = crc && g.crcOk; if (g.name.find("token") != std::string::npos || g.name.find("clock") != std::string::npos || g.name.find("audit") != std::string::npos) leak = true; if (g.name == "console/autosave.json") state = true; }
          if (!had) QFile::remove(settings.dataFolder() + "/daemon.tokens.json");
          std::printf("amixos --smoke: EXPORT %d files in a .zip that reads back %d with the saved state %d, nothing secret in it %d, a second export beside the first %d, user \"%s\"\n", int(got.size()), read && crc, state, !leak, a2["ok"].toBool() && a2["path"] != a1["path"], qPrintable(settings.systemUser()));
          if (!a1["ok"].toBool() || !read || !crc || !state || leak || !a2["ok"].toBool() || a2["path"] == a1["path"] || settings.systemUser().isEmpty()) std::exit(94); }
        QDir(dir).removeRecursively(); }
      { // PLUG-IN DOWNLOADS: only well-formed entries of the list are offered; a file there counts as installed and REMOVE deletes only that file
        const QByteArray sha(64, 'a'); const auto entry = [&](const char* id, const char* file, const char* url, double size) { QJsonObject f; f["name"] = file; f["url"] = url; f["sha256"] = QString::fromLatin1(sha); f["size"] = size; QJsonObject files; for (const char* k : {"win-x64-exe", "win-x64-native", "linux-deb", "linux-tar", "macos", "android-apk", "win-x86-native"}) files[k] = f; QJsonObject o; o["id"] = id; o["name"] = "Test plug-in"; o["version"] = "1.0"; o["files"] = files; return o; };
        const char* good = "https://raw.githubusercontent.com/amirf1389/audio-mixer/main/releases/plugins/Test.vst3";
        QJsonArray list; list << entry("warm", "AMFB Warm.vst3", good, 1000) << entry("evil-host", "A.vst3", "https://evil.example/A.vst3", 1000) << entry("http", "B.vst3", "http://raw.githubusercontent.com/x/B.vst3", 1000)
             << entry("path", "../../C.vst3", good, 1000) << entry("exe", "D.exe", good, 1000) << entry("huge", "E.vst3", good, 3e9) << entry("Bad Id", "F.vst3", good, 1000) << entry("warm", "G.vst3", good, 1000);
        const int kept = update.setPluginList(list); const QString dir = update.pluginDir, path = dir + "/AMFB Warm.vst3";
        const bool notYet = !update.plugins()[0].toMap()["installed"].toBool(); { QDir().mkpath(dir); QFile f(path); if (f.open(QIODevice::WriteOnly)) f.write("x"); }
        const bool there = update.plugins()[0].toMap()["installed"].toBool(), noUrl = !update.plugins()[0].toMap().contains("url"), removed = update.removePlugin("warm") && !QFile::exists(path), unknown = !update.removePlugin("nope");
        update.setPluginList(QJsonArray());
        std::printf("amixos --smoke: PLUG-IN DOWNLOADS %d of 8 entries offered (other hosts, http, paths, .exe, over 200 MB, odd ids and repeats left out), installed seen %d, REMOVE %d\n", kept, there && notYet, removed && unknown);
        if (kept != 1 || !notYet || !there || !noUrl || !removed || !unknown) std::exit(93); }
      { // WATCHDOG FAILOVER: on by default; a restart by hand brings the same device back and says so
        const bool wasRunning = engine.running(); const QString dev = engine.backend(); const bool ok = engine.failover("the self-test asked for it");
        const bool same = engine.running() && engine.backend() == dev, said = engine.watchdogNote().contains("started again") && engine.failovers() >= 1;
        std::printf("amixos --smoke: WATCHDOG on by default %d, FAILOVER restarted %d on the same device %d and said so %d\n", watchdogWas, ok, same, said);
        if (!watchdogWas || !wasRunning || !ok || !same || !said) std::exit(92); }
      { // STREAMING LINKS: a pasted link is recognised, kept once, opened by its app address or its https address; anything else is refused; 360 AUDIO is off by default
        while (!links.list().isEmpty()) links.remove(0);
        const bool added = links.add(" https://open.spotify.com/intl-de/track/4uLU6hMCjMI75M1A2tKUQC?si=x ", "Opener"), twice = links.add("spotify:track:4uLU6hMCjMI75M1A2tKUQC", "");
        const bool bad = links.add("https://open.spotify.com.evil.example/track/4uLU6hMCjMI75M1A2tKUQC", "") || links.add("javascript:alert(1)", "") || links.add("http://youtu.be/dQw4w9WgXcQ", "");
        const bool tik = links.add("https://www.tiktok.com/@amfb.audio/live", "");
        const bool inApp = links.open(1, true) && links.lastOpened() == "spotify:track:4uLU6hMCjMI75M1A2tKUQC", inWeb = links.open(0, true) && links.lastOpened() == "https://www.tiktok.com/@amfb.audio/live";   // TikTok has no app address: https
        const int n = static_cast<int>(links.list().size()); while (!links.list().isEmpty()) links.remove(0);
        const bool off360 = !QSettings().contains("spatial/enabled") ? !settings.spatialEnabled() : true;
        std::printf("amixos --smoke: STREAMING LINKS added %d, the same link twice refused %d, look-alike / script / http refused %d, TikTok %d, opens the app address %d, https where there is none %d (%d kept); 360 AUDIO off by default %d\n", added, !twice, !bad, tik, inApp, inWeb, n, off360);
        if (!added || twice || bad || !tik || !inApp || !inWeb || n != 2 || !off360) std::exit(90);
        const bool preset = engine.masteringPresets().contains("SHORT VIDEO / LIVE"); engine.applyMasteringPreset("SHORT VIDEO / LIVE"); const bool applied = engine.mastering().value("preset").toString() == "SHORT VIDEO / LIVE"; engine.applyMasteringPreset("TRANSPARENT");
        if (!preset || !applied) { std::fprintf(stderr, "amixos --smoke: the SHORT VIDEO / LIVE mastering preset (%d, %d)\n", preset, applied); std::exit(91); } }
      if (auto* dw = qobject_cast<QQuickWindow*>(qml.rootObjects().first())) {   // THE MIXER STRIPS: the easy keys beside every fader; ten plug-ins per channel at most
        dw->setProperty("current", "mixer"); { QElapsedTimer t; t.start(); while (t.elapsed() < 800) QCoreApplication::processEvents(QEventLoop::AllEvents, 20); }
        // the strips are made by a Repeater: they are children in the item tree (not QObject children of the window), so the tree is searched
        std::function<QQuickItem*(QQuickItem*, const QString&)> find = [&](QQuickItem* it, const QString& n) -> QQuickItem* { if (!it) return nullptr; if (it->objectName() == n) return it; for (QQuickItem* c : it->childItems()) if (QQuickItem* f = find(c, n)) return f; return nullptr; };
        QQuickItem* root = dw->contentItem();
        // real clicks on LOUDER, then BACK (the level before that press)
        auto click = [&](QQuickItem* it) { if (!it) return; const QPointF c = it->mapToScene(QPointF(it->width() / 2, it->height() / 2));
          QMouseEvent pr(QEvent::MouseButtonPress, c, dw->mapToGlobal(c.toPoint()), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier); QCoreApplication::sendEvent(dw, &pr);
          QMouseEvent rl(QEvent::MouseButtonRelease, c, dw->mapToGlobal(c.toPoint()), Qt::LeftButton, Qt::NoButton, Qt::NoModifier); QCoreApplication::sendEvent(dw, &rl);
          QElapsedTimer t; t.start(); while (t.elapsed() < 900) QCoreApplication::processEvents(QEventLoop::AllEvents, 20); };
        const double g0 = engine.stripState()[0].toMap()["db"].toDouble();
        click(find(root, "easy_louder0")); const double g1 = engine.stripState()[0].toMap()["db"].toDouble();
        click(find(root, "easy_back0")); const double g2 = engine.stripState()[0].toMap()["db"].toDouble();
        const bool easy = std::fabs(g1 - (g0 + 1)) < 0.01 && std::fabs(g2 - g0) < 0.01 && find(root, "easy_quieter0") && find(root, "easy_zero0");
        std::printf("amixos --smoke: EASY KEYS louder %.1f -> %.1f dB, BACK -> %.1f dB: %s\n", g0, g1, g2, easy ? "ok" : "FAILED"); if (!easy) std::exit(88);
        const bool keys = find(root, "stripFader") && find(root, "stripMute0") && find(root, "stripSolo0");
        // EDIT LAYOUT: hide REC and the easy keys and widen the strips; a damaged setting falls back to the shipped layout; RESET brings it back
        auto vis = [&](const char* n) { QQuickItem* it = find(root, QString::fromLatin1(n)); return it && it->isVisible(); }; auto pump = [&] { QElapsedTimer t; t.start(); while (t.elapsed() < 350) QCoreApplication::processEvents(QEventLoop::AllEvents, 20); };
        const bool before = vis("side_rec0") && vis("easy_louder0"); const double w0 = find(root, "stripFader") ? find(root, "stripFader")->parentItem()->parentItem()->parentItem()->width() : 0;
        settings.setMixerLayout("{\"rec\":false,\"easy\":false,\"stripW\":160}"); pump(); const bool hidden = !vis("side_rec0") && !vis("easy_louder0") && vis("side_fx0") && vis("stripMute0");
        settings.setMixerLayout("{\"stripW\":99999,\"rec\":\"no\",\"fx\":[1]} trailing junk"); pump(); const bool safe = vis("side_rec0") && vis("side_fx0") && vis("easy_louder0") && vis("stripMute0");
        settings.setMixerLayout(""); pump(); const bool back = vis("side_rec0") && vis("easy_louder0");
        std::printf("amixos --smoke: EDIT LAYOUT hides REC and the easy keys %d (shown before %d), a damaged setting keeps the shipped layout %d, RESET brings them back %d (strip %.0f px)\n", hidden, before, safe, back, w0);
        if (!before || !hidden || !safe || !back) std::exit(95);
        engine.setPlugin(0, 9, "fx:reverb"); const bool tenth = engine.plugins(0)[9].toMap().value("kind").toString() == "fx:reverb";
        engine.setPlugin(0, 10, "fx:delay"); const bool eleventhRefused = engine.plugins(0).size() <= 10 || engine.plugins(0)[10].toMap().value("kind").toString().isEmpty();
        engine.setPlugin(0, 9, "");
        std::printf("amixos --smoke: MIXER STRIPS fader, MUTE and SOLO keys %d, 10th plug-in in %d, 11th refused %d (limit %d)\n", keys, tenth, eleventhRefused, engine.pluginSlotLimit());
        if (!keys || !tenth || !eleventhRefused || engine.pluginSlotLimit() != 10) std::exit(87);
        // W-SHAPE TUNER EDITOR: the new controls, A / B kept with the effect, dragging a band's point, AUTO W, the ear training (off in LIVE MODE),
        // the nine presets, and an old saved state (only the first eight controls) loading exactly as before
        if (QObject* wed = dw->findChild<QObject*>("plugEd")) {
          auto call = [&](const char* fn, QVariant a = QVariant(), QVariant b = QVariant(), QVariant c = QVariant()) { QVariant r;
            if (!c.isNull()) QMetaObject::invokeMethod(wed, fn, Q_RETURN_ARG(QVariant, r), Q_ARG(QVariant, a), Q_ARG(QVariant, b), Q_ARG(QVariant, c));
            else if (!b.isNull()) QMetaObject::invokeMethod(wed, fn, Q_RETURN_ARG(QVariant, r), Q_ARG(QVariant, a), Q_ARG(QVariant, b));
            else if (!a.isNull()) QMetaObject::invokeMethod(wed, fn, Q_RETURN_ARG(QVariant, r), Q_ARG(QVariant, a)); else QMetaObject::invokeMethod(wed, fn, Q_RETURN_ARG(QVariant, r)); return r; };
          auto pv = [&](int k) { const QVariantList l = engine.pluginParams(0, 7); return k < l.size() ? l[k].toMap().value("value").toDouble() : -1.0; };
          engine.setPlugin(0, 7, "fx:wshape"); QVariant r; QMetaObject::invokeMethod(wed, "openFor", Q_RETURN_ARG(QVariant, r), Q_ARG(QVariant, 0), Q_ARG(QVariant, 7), Q_ARG(QVariant, QStringLiteral("fx:wshape")), Q_ARG(QVariant, QStringLiteral("W-SHAPE TUNER"))); pump();
          const QVariantList ps = engine.pluginParams(0, 7); const bool params = ps.size() == 19 && ps[8].toMap().value("text").toString() == "90 Hz" && ps[12].toMap().value("text").toString() == "10.0 kHz" && ps[14].toMap().value("text").toString() == "ON" && ps[15].toMap().value("text").toString() == "STEREO";
          QQuickItem* ci = wed->property("contentItem").value<QQuickItem*>(); const bool tools = ci && find(ci, "wshapeTools") && find(ci, "wshapeTools")->isVisible() && find(ci, "wshapeDrag") && find(ci, "wsLearn") && find(ci, "wsAnswer4");
          // A / B: B gets its own BASS, A comes back; both kept with the effect (ui=...), and a reopened editor has them again
          call("setParam", 1, 0.9); call("pick", QStringLiteral("B")); call("setParam", 1, 0.1); const double onB = pv(1); call("pick", QStringLiteral("A")); const double onA = pv(1);
          const QString ui = engine.pluginUiState(0, 7); QMetaObject::invokeMethod(wed, "close"); pump();
          QMetaObject::invokeMethod(wed, "openFor", Q_RETURN_ARG(QVariant, r), Q_ARG(QVariant, 0), Q_ARG(QVariant, 7), Q_ARG(QVariant, QStringLiteral("fx:wshape")), Q_ARG(QVariant, QStringLiteral("W-SHAPE TUNER"))); pump();
          const QVariantList sb = wed->property("stateB").toList(); const bool ab = std::fabs(onB - 0.1) < 1e-3 && std::fabs(onA - 0.9) < 1e-3 && ui.startsWith("A,") && ui.contains("|B,") && ui.endsWith("|A") && sb.size() == 19 && std::fabs(sb[1].toDouble() - 0.1) < 1e-3;
          // drag PRESENCE's point to 3 kHz, +2 dB
          call("wDrag", 2, 3000.0, 2.0); const double A = pv(0); const bool drag = std::fabs(pv(10) - am::fx::WShape::bandKnob(2, 3000)) < 1e-3 && std::fabs(pv(3) - 2.0 / (A * 6)) < 1e-3;
          // EAR TRAINING: 4 RAW EQ starts on BASS; 4 RANDOM EQ boosts the hidden band, a right and a wrong answer are scored
          call("wTrainRaw"); const bool raw = std::fabs(pv(16) - 0.2) < 1e-3 && wed->property("rawStep").toInt() == 1; call("wTrainStop"); const bool stopped = pv(16) == 0;
          const double presHz = am::fx::WShape::bandHz(2, pv(10)), d0 = call("wCurveDbAt", presHz).toDouble();
          call("wQuizNext", 3); const bool hidden = std::fabs(pv(16) - 0.6) < 1e-3; const double dOpen = call("wCurveDbAt", presHz).toDouble();
          call("wQuizAnswer", 3); const double dShown = call("wCurveDbAt", presHz).toDouble(); call("wQuizAnswer", 1); call("wQuizNext", 0); const int qb = wed->property("quizBand").toInt(); call("wQuizAnswer", qb % 5 + 1);
          const bool noGiveaway = std::fabs(dOpen - d0) < 0.01 && dShown - d0 > 3.0;
          std::printf("amixos --smoke: W-SHAPE 4 RANDOM EQ curve at %.0f Hz: before %.2f dB, open question %.2f dB, answered %.2f dB, no giveaway %d\n", presHz, d0, dOpen, dShown, noGiveaway);
          const bool quiz = hidden && noGiveaway && wed->property("quizScore").toInt() == 1 && wed->property("quizTotal").toInt() == 2 && pv(16) > 0; call("wTrainStop");
          engine.startLive(1); pump(); call("wTrainRaw"); const bool locked = wed->property("wTrainLocked").toBool() && pv(16) == 0 && wed->property("rawStep").toInt() == 0; engine.stopLive(); pump();
          // AUTO W starts listening; switched off again here (the unit test plays it through)
          call("wLearn"); pump(); const bool learning = engine.wshapeInfo(0, 7).value("learning").toBool(); engine.setPluginParam(0, 7, 17, 0);
          // the presets: nine in the editor's PROGRAM window; SPEECH sets its BASS
          const bool presets = engine.rackPresets("fx:wshape").size() == 9 && engine.applyRackPreset(0, 7, 601) && std::fabs(pv(1) - 0.2) < 1e-3;
          QMetaObject::invokeMethod(wed, "close"); pump(); const bool trainOff = pv(16) == 0;
          // an old saved state: the new controls come up at their defaults and it sounds exactly like a fresh default unit
          auto oldP = am::fx::FxPlugin::make("wshape", 48000), newP = am::fx::FxPlugin::make("wshape", 48000);
          oldP->setState("amount=0.6000;bass=0.5500;lowmid=0.4000;presence=0.4000;upper=0.4000;air=0.5000;smooth=0.6000;output=0.5000;mix=1.0000;");
          std::vector<float> a0(4800), a1(4800), b0(4800), b1(4800); for (int i = 0; i < 4800; ++i) a0[i] = a1[i] = static_cast<float>(0.3 * std::sin(i * 0.05) + 0.2 * std::sin(i * 0.9));
          const float* in[2] = {a0.data(), a1.data()}; float* o1[2] = {b0.data(), b1.data()}; std::vector<float> c0(4800), c1(4800); float* o2[2] = {c0.data(), c1.data()};
          oldP->process(in, o1, 4800); newP->process(in, o2, 4800); bool same = oldP->state() == newP->state(); for (int i = 0; i < 4800; ++i) if (b0[i] != c0[i] || b1[i] != c1[i]) same = false;
          engine.setPlugin(0, 7, "");
          std::printf("amixos --smoke: W-SHAPE editor: 18 controls + MIX %d, tools %d, A / B kept (%s) %d, drag %d, 4 RAW EQ %d stop %d, 4 RANDOM EQ %d, off in LIVE MODE %d, AUTO W %d, 9 presets %d, training off on close %d, old state identical %d\n",
                      params, tools, qPrintable(ui.left(24)), ab, drag, raw, stopped, quiz, locked, learning, presets, trainOff, same);
          if (!params || !tools || !ab || !drag || !raw || !stopped || !quiz || !locked || !learning || !presets || !trainOff || !same) std::exit(827);
        } else { std::fprintf(stderr, "amixos --smoke: no plug-in editor\n"); std::exit(827); } }
      { // NEVER BLANK, ALWAYS CENTRED: every page switched to has a built, opaque page with something drawn on it (also after a minimise
        // and restore, and a resize); the window placement rules (work area without the taskbar, a saved place on a monitor that is gone,
        // two screens, a window larger than the screen); the boxes centred and whole in a small window
        auto* win = qobject_cast<QQuickWindow*>(qml.rootObjects().first()); int empty = 0; QStringList emptyIds; const QSize was = win->size();
        auto shown = [&]() -> QQuickItem* { QQuickItem* hit = nullptr; std::function<void(QQuickItem*)> walk = [&](QQuickItem* p) { for (QQuickItem* it : p->childItems()) {
            if (!it->isVisible()) continue; if (it->inherits("QQuickLoader") && it->z() < 100 && it->width() > 300 && it->height() > 200) { if (!hit) hit = it; continue; } walk(it); } };
          walk(win->contentItem()); return hit; };
        auto drawn = [&](const QString& id) { QCoreApplication::processEvents(); QQuickItem* l = shown(); auto* item = l ? qobject_cast<QQuickItem*>(l->property("item").value<QObject*>()) : nullptr;
          bool ok = l && item && item->isVisible() && l->opacity() > 0.99 && win->property("pageReady").toBool();
          if (ok) { const QImage img = win->grabWindow(); const qreal d = img.devicePixelRatio(); const QPointF tl = l->mapToScene(QPointF(0, 0)); QSet<QRgb> c;
            const QRect a = QRectF(tl.x() * d, tl.y() * d, l->width() * d, l->height() * d).toRect().intersected(img.rect());
            for (int y = a.top(); y < a.bottom(); y += 4) for (int x = a.left(); x < a.right(); x += 4) c.insert(img.pixel(x, y) & 0xF0F0F0); ok = c.size() >= 6; }
          if (!ok) { ++empty; emptyIds << id; } };
        QStringList ids; for (const QVariant& v : engine.pages()) ids << v.toMap().value("id").toString();
        for (const QString& id : ids) { win->setProperty("current", id); drawn(id); }
        for (int k = 0; k < 3; ++k) { const QString id = ids[(k * 7) % ids.size()]; win->showMinimized(); QCoreApplication::processEvents(); win->setProperty("current", id); win->showNormal(); drawn(id + "/restored");
          win->resize(720 + k * 300, 480 + k * 180); drawn(id + "/resized"); }
        win->resize(was); win->setProperty("current", "mixer"); drawn("mixer/back");
        using namespace winplace; const QMargins f(8, 31, 8, 8); const QSize want(1380, 860), mn(720, 480);
        const QRect laptop(0, 0, 1536, 824);   // 1920 x 1080 at 125 %, the taskbar (40 px) left out
        const QRect c1 = choose(want, mn, QRect(), {laptop}, laptop, f);
        const bool centred = std::abs((c1.left() - f.left()) - (laptop.right() - (c1.right() + f.right()))) <= 1 && std::abs((c1.top() - f.top()) - (laptop.bottom() - (c1.bottom() + f.bottom()))) <= 1;
        const QRect second(1536, -100, 1280, 800);   // a second monitor to the right, 150 %
        const QRect gone = choose(want, mn, QRect(3000, 200, 1380, 860), {laptop}, laptop, f);   // closed on a monitor unplugged since
        const QRect kept = choose(want, mn, QRect(1600, 0, 1000, 600), {laptop, second}, laptop, f);   // closed on the second monitor: stays there, whole
        const QRect over = choose(want, mn, QRect(1200, 600, 1000, 700), {laptop}, laptop, f);   // hanging over the corner: pulled in
        const QRect tiny = choose(want, mn, QRect(), {QRect(0, 0, 1024, 728)}, QRect(0, 0, 1024, 728), f);   // a small screen: fits it
        const bool place = centred && laptop.contains(gone.adjusted(-8, -31, 8, 8)) && second.contains(kept.adjusted(-8, -31, 8, 8)) && kept.topLeft() == QPoint(1600, 0)
                           && laptop.contains(over.adjusted(-8, -31, 8, 8)) && QRect(0, 0, 1024, 728).contains(tiny.adjusted(-8, -31, 8, 8)) && tiny.width() >= 720;
        // THE BOXES in a small window: centred on it, and whole (drawn smaller when the window is smaller than they are)
        win->resize(760, 500); QCoreApplication::processEvents(); int boxes = 0, offBoxes = 0; QStringList offNames;
        for (const char* n : {"audioSettings", "volumeEnhancer", "masterAnalyzer", "aboutPop"}) { QObject* p = win->findChild<QObject*>(n); if (!p) continue; ++boxes;
          QMetaObject::invokeMethod(p, "open"); waitMs(400); auto* pi = qobject_cast<QQuickItem*>(p->property("contentItem").value<QObject*>()); QQuickItem* bg = pi ? pi->parentItem() : nullptr;
          if (!bg) { ++offBoxes; offNames << n; continue; } const QRectF r = bg->mapRectToScene(QRectF(0, 0, bg->width(), bg->height()));
          const bool mid = std::abs(r.center().x() - win->width() / 2.0) <= 2 && std::abs(r.center().y() - win->height() / 2.0) <= 2, whole = r.left() >= -0.5 && r.top() >= -0.5 && r.right() <= win->width() + 0.5 && r.bottom() <= win->height() + 0.5;
          if (!mid || !whole) { ++offBoxes; offNames << QString("%1 %2,%3 %4x%5").arg(n).arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()); } QMetaObject::invokeMethod(p, "close"); waitMs(250); }
        win->resize(was); QCoreApplication::processEvents();
        std::printf("amixos --smoke: NEVER BLANK: %d pages + 3 minimise / restore + 3 resizes, %d empty%s%s; placement (centred on the work area, monitor gone, second monitor, over an edge, small screen) %d; %d boxes, %d off-centre or clipped%s%s\n",
                    int(ids.size()), empty, empty ? ": " : "", qPrintable(emptyIds.join(' ')), place, boxes, offBoxes, offBoxes ? ": " : "", qPrintable(offNames.join("; ")));
        if (empty || !place || boxes < 3 || offBoxes) std::exit(841); }
      std::printf("amixos --smoke: interface loaded, %s backend, %llu blocks in 1.2 s, xruns %llu, master %.1f dBFS, %d pages\n", qPrintable(engine.backend()), blocks, m.value("xruns").toULongLong(), m.value("masterPeakDb").toDouble(), engine.pageCount());
      arrange.newProject(); QFile::remove(smokeWav); QFile::remove(smokeOut);   // the smoke's demo arrangement is not kept
      for (int k = 0; k < 6; ++k) engine.setPlugin(0, k, "");
      engine.stop();
      QCoreApplication::exit(blocks > 100 && engine.pageCount() >= 30 ? 0 : 4);
    });
  }
#ifdef AMIXOS_TEST_LICENSE
  // THE BIG CONSOLE (test builds only: --big DIR). The console is run as PRO + one EXTEND pack (50 channels) and as ULTIMATE (300), on
  // the null device with its test tone, and every claim is checked: the strips shown, the pages of 16, strips built only when their
  // page opens, the last strip mixing, the saved console keeping every strip, and the first 32 strips untouched. Pictures go to DIR.
  if (!bigShots.isEmpty()) QTimer::singleShot(1500, &app, [&] {
    auto fail = [&](int code, const char* what) { std::fprintf(stderr, "amixos --big: %s\n", what); engine.stop(); std::exit(code); };
    auto settle = [&](int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20); };
    QDir().mkpath(bigShots); auto* win = qobject_cast<QQuickWindow*>(qml.rootObjects().first());
    auto shot = [&](const char* name) { win->grabWindow().save(bigShots + "/" + name + ".png"); };
    std::function<void(QQuickItem*, const QString&, bool, bool, QList<QQuickItem*>&)> walk = [&](QQuickItem* it, const QString& n, bool shownOnly, bool up, QList<QQuickItem*>& out) {
      if (!it || (shownOnly && !it->isVisible())) return; Q_UNUSED(up); if (it->objectName() == n) out << it; for (QQuickItem* c : it->childItems()) walk(c, n, shownOnly, up, out); };
    auto items = [&](const char* n, bool shownOnly = false) { QList<QQuickItem*> out; walk(win->contentItem(), QString::fromLatin1(n), shownOnly, false, out); return out; };
    auto item = [&](const char* n) -> QQuickItem* { const QList<QQuickItem*> l = items(n); return l.isEmpty() ? nullptr : l.first(); };
    auto mixer = [&]() -> QObject* { QObject* mp = nullptr; for (QQuickItem* o : items("mixerPage", true)) mp = o; return mp; };
    settings.finishSetup(); engine.setNullTestInput(true); engine.open("null", "", "", 48000, 256, 2, 2); settle(400);
    // 1. as shipped: 32 strips shown, 10 licensed (BASIC), every list 32 long
    if (engine.strips() != 32 || engine.licensedChannels() != 10 || engine.stripState().size() != 32 || engine.meters().size() != 32 || engine.inputRoutes().size() != 32 || engine.inputCap() != 32) fail(2, "BASIC is not the 32-strip console it was");
    win->setProperty("current", "mixer"); settle(900); QObject* mp = mixer(); if (!mp) fail(3, "no mixer page");
    if (mp->property("big").toBool() || items("stripFader").size() != 32) fail(4, "BASIC: the mixer does not build its 32 strips at once");
    // 2. PRO + one EXTEND pack: 50 strips, four pages, the last page holds two
    engine.setTestChannels(50); settle(900); mp = mixer();
    if (engine.strips() != 50 || engine.stripState().size() != 50 || engine.meters().size() != 50 || engine.license().value("extendPacks").toInt() != 1) fail(5, "50 channels: the lists do not follow the licence");
    if (!mp || !mp->property("big").toBool() || mp->property("bank").toInt() != 0) fail(6, "50 channels: the mixer is not paged");
    { QObject* bar = item("mixerBankBar"); if (!bar || bar->property("banks").toInt() != 4 || !bar->property("paged").toBool()) fail(7, "50 channels: not four pages"); }
    mp->setProperty("bank", 3); settle(700); shot("extend-50-last-page");
    { const int vis = static_cast<int>(items("stripFader", true).size());
      if (vis != 2) { std::fprintf(stderr, "amixos --big: page 4 of 50 channels shows %d strips\n", vis); fail(8, "the last page of 50 channels does not show its 2 strips"); } }
    // 3. ULTIMATE: 300 strips, nineteen pages; strips are built when their page opens, not before
    engine.setTestChannels(300); settle(900); mp = mixer();
    if (engine.strips() != 300 || engine.licensedChannels() != 300 || engine.stripState().size() != 300 || engine.meters().size() != 300 || engine.inputRoutes().size() != 300 || engine.inputCap() != 256 || engine.edition() != "ULTIMATE") fail(9, "ULTIMATE: not 300 strips");
    const int builtFirst = static_cast<int>(items("stripFader").size());
    if (builtFirst > 64) { std::fprintf(stderr, "amixos --big: %d strips built before their pages were opened\n", builtFirst); fail(10, "ULTIMATE: every strip is built at once"); }
    { QObject* bar = item("mixerBankBar"); if (!bar || bar->property("banks").toInt() != 19) fail(11, "ULTIMATE: not nineteen pages"); }
    shot("ultimate-mixer-page-1");
    mp->setProperty("bank", 18); settle(900); if (!item("stripMute299") || item("stripMute200") || items("stripFader", true).size() != 12) fail(12, "ULTIMATE: the last page did not build CH 289-300 (or built others)");
    // CH 300 mixes: route the test tone to it, hear it on the master; its neighbour stays silent
    engine.setMaster(0); for (int i = 0; i < 300; ++i) engine.setInputRoute(i, -1); settle(700);
    const double quiet = engine.telemetry().value("masterPeakDb").toDouble();
    engine.setInputRoute(299, 0); engine.setGain(299, 0); engine.setMute(299, false); settle(700);
    const double m299 = engine.meters()[299].toMap()["inDb"].toDouble(), m298 = engine.meters()[298].toMap()["inDb"].toDouble(), heard = engine.telemetry().value("masterPeakDb").toDouble();
    if (std::fabs(m299 + 12.0) > 1.0 || m298 > -100 || heard < -20 || quiet > -100) { std::fprintf(stderr, "amixos --big: CH 300 in %.1f dBFS, CH 299 in %.1f, master %.1f (silent before: %.1f)\n", m299, m298, heard, quiet); fail(13, "ULTIMATE: CH 300 does not mix"); }
    engine.setName(299, "LAST"); engine.setSolo(299, true); settle(500); shot("ultimate-mixer-page-19"); engine.setSolo(299, false);
    // the processor load with 300 strips, one live
    const double load = engine.telemetry().value("loadPct").toDouble(); if (load > 50) { std::fprintf(stderr, "amixos --big: load %.1f %%\n", load); fail(14, "ULTIMATE: idle strips cost processor time"); }
    // the whole console is saved and comes back: CH 300's name and route, and a scene keeps it
    { const QJsonObject c = engine.consoleExport(); if (c.value("strips").toArray().size() != 300 || c.value("inputRoutes").toArray().size() != 300) fail(15, "ULTIMATE: the saved console does not hold 300 strips");
      engine.storeScene(14, "big"); engine.setName(299, "X"); engine.setGain(299, -30); engine.recallScene(14);
      if (engine.stripState()[299].toMap()["name"].toString() != "LAST" || std::fabs(engine.stripState()[299].toMap()["db"].toDouble()) > 1e-9) fail(16, "ULTIMATE: a scene does not bring CH 300 back");
      engine.setGain(299, -6); engine.recordHistory(); engine.setGain(299, -9); engine.recordHistory(); engine.undo(); if (std::fabs(engine.stripState()[299].toMap()["db"].toDouble() + 6) > 1e-9) fail(17, "ULTIMATE: undo does not reach CH 300"); engine.setGain(299, 0);
      engine.setArmed(299, true); engine.setRecallSafe(250, true); if (!engine.armed(299) || engine.armed(298) || engine.armedCount() != 1 || !engine.recallSafe(250) || engine.recallSafe(249)) fail(18, "ULTIMATE: REC arm / recall safe above strip 32"); engine.setArmed(299, false); engine.setRecallSafe(250, false); }
    // the pages that work in pages of 16, on their last page, and the lists of every channel
    for (const char* pg : {"bus", "routing", "ai", "miceq", "surround", "tools", "renew", "engctl", "dca", "scenes", "dsp", "home"}) { win->setProperty("current", pg); settle(900);
      for (const char* nm : {"sendToPage", "routingPage", "aiMixPage", "micEqPage"}) for (QQuickItem* o : items(nm, true)) o->setProperty("bank", 18);
      settle(500); shot((QByteArray("ultimate-") + pg).constData()); }
    // 4. the licence goes away (a key expires): back to 32 strips, and nothing of the 300 is lost when the console is saved
    engine.setTestChannels(-1); settle(900);
    if (engine.strips() != 32 || engine.licensedChannels() != 10 || engine.stripState().size() != 32) fail(19, "back to BASIC: not 32 strips again");
    if (engine.consoleExport().value("strips").toArray().size() != 300 || engine.consoleExport().value("strips").toArray()[299].toObject().value("name").toString() != "LAST") fail(20, "back to BASIC: the strips above 32 are dropped from the saved console");
    win->setProperty("current", "mixer"); settle(900); mp = mixer(); if (!mp || mp->property("big").toBool() || mp->property("bank").toInt() != 0) fail(21, "back to BASIC: the mixer is still paged"); shot("basic-again");
    std::printf("amixos --big: BASIC 32 strips; EXTEND 50 in 4 pages; ULTIMATE 300 in 19 pages (%d strips built at first), CH 300 heard at %.1f dBFS, load %.1f %%, console / scene / undo / REC arm reach CH 300; back to BASIC keeps all 300 saved\n", builtFirst, heard, load);
    engine.stop(); QCoreApplication::exit(0);
  });
#endif
  // STALL LOG: when the interface thread was away from its event loop for more than 0.4 s, one line goes into console.log with the page
  // that was open and how long it lasted. A "Not Responding" on a customer's computer can then be traced to a page (DIAGNOSTICS shows the log).
  if (!smoke && !bench && shots.isEmpty()) {
    auto* beat = new QTimer(&app); auto* since = new QElapsedTimer; since->start(); beat->setInterval(100);
    QObject::connect(beat, &QTimer::timeout, &app, [since, &qml] { const qint64 gap = since->restart();
      if (gap > 500) { const QObject* w = qml.rootObjects().isEmpty() ? nullptr : qml.rootObjects().first(); qWarning("STALL: the interface did not answer for %lld ms (page %s)", static_cast<long long>(gap - 100), w ? qPrintable(w->property("current").toString()) : "?"); } });
    beat->start();
  }
  return app.exec();
}
