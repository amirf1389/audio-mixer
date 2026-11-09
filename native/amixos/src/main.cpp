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

#include "Engine.h"
#include "I18n.h"

int main(int argc, char* argv[]) {
  QGuiApplication app(argc, argv);
  app.setOrganizationName("AMFB Audio");
  app.setOrganizationDomain("amfb.audio");
  app.setApplicationName("A mix OS");
  app.setApplicationVersion(QStringLiteral(AMIX_VERSION));
  QQuickStyle::setStyle("Basic");   // the console draws its own controls (Theme.qml); the Basic style never fights the colours

  bool smoke = false; QString shots, lang;
  for (int i = 1; i < argc; ++i) { const QString a = QString::fromLocal8Bit(argv[i]); if (a == "--smoke") smoke = true; else if (a == "--shots" && i + 1 < argc) { smoke = true; shots = QString::fromLocal8Bit(argv[++i]); } else if (a == "--lang" && i + 1 < argc) lang = QString::fromLocal8Bit(argv[++i]); }

  Engine engine; I18n i18n;
  if (!lang.isEmpty()) i18n.setLang(lang);   // --lang fa: start in that language (also for --shots)
  QQmlApplicationEngine qml;
  qml.rootContext()->setContextProperty("engine", &engine);
  qml.rootContext()->setContextProperty("i18n", &i18n);
  qml.rootContext()->setContextProperty("appVersion", QStringLiteral(AMIX_VERSION));
  QObject::connect(&qml, &QQmlApplicationEngine::objectCreationFailed, &app, [] { std::fprintf(stderr, "amixos: the interface could not be created\n"); QCoreApplication::exit(2); }, Qt::QueuedConnection);
  qml.load(QUrl(QStringLiteral("qrc:/qt/qml/AmixOS/qml/Main.qml")));   // Qt 6.4 (loadFromModule is 6.5+)
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
    QTimer::singleShot(1200, &app, [&] {
      if (!shots.isEmpty()) {   // one picture per ported page
        QDir().mkpath(shots);
        auto* win = qobject_cast<QQuickWindow*>(qml.rootObjects().first());
        for (const char* id : {"home", "mixer", "dsp", "dyn", "gate", "bus", "dca", "scenes", "presets", "io", "meter", "lufs", "sys", "fx"}) {
          win->setProperty("current", QString::fromLatin1(id)); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
          const QImage img = win->grabWindow(); img.save(shots + "/" + id + ".png"); std::printf("amixos --shots: %s %dx%d\n", id, img.width(), img.height());
          if (QString::fromLatin1(id) == "mixer") {   // the same page with the analogue VU faces
            if (QObject* mixer = win->findChild<QObject*>("mixerPage")) { mixer->setProperty("vuMode", true); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50); win->grabWindow().save(shots + "/mixer-vu.png"); mixer->setProperty("vuMode", false); std::printf("amixos --shots: mixer-vu\n"); }
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
      engine.stop();
      QCoreApplication::exit(blocks > 100 && engine.pageCount() >= 30 ? 0 : 4);
    });
  }
  return app.exec();
}
