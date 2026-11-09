// A mix OS: the native console. Qt Quick interface (qml/) on the C++20 engine (Engine.h -> am::Runtime). No browser, no web runtime.
//   amixos              the console
//   amixos --smoke      offscreen: load the interface, open the null device, run one second, print the meters, exit 0 (the automated check)
//   amixos --shots DIR  like --smoke, and saves a PNG of every ported page into DIR (visual check without a display: QT_QPA_PLATFORM=offscreen)
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QDir>
#include <QTimer>
#include <cstdio>

#include "Engine.h"

int main(int argc, char* argv[]) {
  QGuiApplication app(argc, argv);
  app.setOrganizationName("AMFB Audio");
  app.setOrganizationDomain("amfb.audio");
  app.setApplicationName("A mix OS");
  app.setApplicationVersion(QStringLiteral(AMIX_VERSION));
  QQuickStyle::setStyle("Basic");   // the console draws its own controls (Theme.qml); the Basic style never fights the colours

  bool smoke = false; QString shots;
  for (int i = 1; i < argc; ++i) { const QString a = QString::fromLocal8Bit(argv[i]); if (a == "--smoke") smoke = true; else if (a == "--shots" && i + 1 < argc) { smoke = true; shots = QString::fromLocal8Bit(argv[++i]); } }

  Engine engine;
  QQmlApplicationEngine qml;
  qml.rootContext()->setContextProperty("engine", &engine);
  qml.rootContext()->setContextProperty("appVersion", QStringLiteral(AMIX_VERSION));
  QObject::connect(&qml, &QQmlApplicationEngine::objectCreationFailed, &app, [] { std::fprintf(stderr, "amixos: the interface could not be created\n"); QCoreApplication::exit(2); }, Qt::QueuedConnection);
  qml.load(QUrl(QStringLiteral("qrc:/qt/qml/AmixOS/qml/Main.qml")));   // Qt 6.4 (loadFromModule is 6.5+)
  if (qml.rootObjects().isEmpty()) return 2;

  if (smoke) {
    // the null device: a timed "sound card" that needs no hardware; the strips read silence, the meters must publish blocks
    const bool opened = engine.open("null", "", "", 48000, 256, 2, 2);
    if (!opened) { std::fprintf(stderr, "amixos --smoke: %s\n", qPrintable(engine.status())); return 3; }
    engine.setGain(0, -6); engine.setEq(0, 0, 1, 1000, 1.0, 3);   // a change through the control ring, a channel-DSP change
    QTimer::singleShot(1200, &app, [&] {
      if (!shots.isEmpty()) {   // one picture per ported page
        QDir().mkpath(shots);
        auto* win = qobject_cast<QQuickWindow*>(qml.rootObjects().first());
        for (const char* id : {"home", "mixer", "dsp", "dyn", "gate", "io", "meter", "lufs", "sys", "fx"}) {
          win->setProperty("current", QString::fromLatin1(id)); for (int k = 0; k < 6; ++k) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
          const QImage img = win->grabWindow(); img.save(shots + "/" + id + ".png"); std::printf("amixos --shots: %s %dx%d\n", id, img.width(), img.height());
        }
      }
      const QVariantMap m = engine.telemetry();
      const qulonglong blocks = m.value("blocks").toULongLong();
      std::printf("amixos --smoke: interface loaded, %s backend, %llu blocks in 1.2 s, xruns %llu, master %.1f dBFS, %d pages\n", qPrintable(engine.backend()), blocks, m.value("xruns").toULongLong(), m.value("masterPeakDb").toDouble(), engine.pageCount());
      engine.stop();
      QCoreApplication::exit(blocks > 100 && engine.pageCount() >= 30 ? 0 : 4);
    });
  }
  return app.exec();
}
