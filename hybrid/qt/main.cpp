#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include "MixerBridge.h"

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  MixerBridge bridge;
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("bridge"), &bridge);
  engine.load(QUrl(QStringLiteral("qrc:/hybrid/Main.qml")));
  if (engine.rootObjects().isEmpty()) return 1;
  const QStringList args = app.arguments();              // --shot FILE: save a picture of the window and leave
  const int at = args.indexOf(QStringLiteral("--shot"));
  if (at > 0 && at + 1 < args.size()) {
    QTimer::singleShot(1500, &app, [&] {
      auto* w = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
      const bool ok = w && bridge.connected() && w->grabWindow().save(args[at + 1]);
      QCoreApplication::exit(ok ? 0 : 3);
    });
  }
  return app.exec();
}
