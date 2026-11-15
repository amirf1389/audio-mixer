// AmF X1 DIAGNOSTICS: the system check as its own program (installed next to the console; works when the console does not start).
//   "AmF X1 Diagnostics" [--selftest] [--shot FILE]
//   --selftest  reads the devices, runs every check, plays a tone and meters an input on the null device, writes the report; exit 0 = all ran
//   --shot      saves a picture of the window once the check has run, then quits (a check without a display)
#include <QDir>
#include <QElapsedTimer>
#include <functional>
#include <QGuiApplication>
#include <QIcon>
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <cstdio>

#include "Diag.h"

static void wait(int ms, const std::function<bool()>& done = {}) { QElapsedTimer t; t.start(); while (t.elapsed() < ms && !(done && done())) QCoreApplication::processEvents(QEventLoop::AllEvents, 20); }

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  app.setOrganizationName("AMFB Audio"); app.setApplicationName("A mix OS"); app.setApplicationDisplayName("AmF X1 Diagnostics");
  app.setWindowIcon(QIcon(QStringLiteral(":/qt/qml/AmixOS/icons/amfx1.png")));
  QQuickStyle::setStyle("Fusion");
  QPalette p; p.setColor(QPalette::Window, QColor("#040507")); p.setColor(QPalette::WindowText, QColor("#f1f5f9")); p.setColor(QPalette::Base, QColor("#0f141c"));
  p.setColor(QPalette::Text, QColor("#f1f5f9")); p.setColor(QPalette::Button, QColor("#141b26")); p.setColor(QPalette::ButtonText, QColor("#f1f5f9")); app.setPalette(p);
  QString shot; bool self = false;
  for (int i = 1; i < argc; ++i) { const QString a = QString::fromLocal8Bit(argv[i]); if (a == "--selftest") self = true; else if (a == "--shot" && i + 1 < argc) shot = QString::fromLocal8Bit(argv[++i]); }
  Diag diag;
  if (self) {
    diag.refresh(); wait(20000, [&] { return !diag.busy(); });
    const bool checked = !diag.checks().isEmpty() && !diag.busy();
    diag.setBackendOverride("null");
    const bool tone = diag.testOutput(QString()); wait(300); diag.stop();
    const bool meter = diag.listen(QStringLiteral("x")); wait(300); diag.stop();
    const QString rep = diag.report(); const QString file = diag.saveReport(QDir::tempPath() + "/amfx1-diag-selftest.txt");
    std::printf("diagnostics --selftest: %d checks, %d outputs, %d inputs, %d ASIO; tone %d, meter %d, report %d bytes -> %s\n", int(diag.checks().size()), int(diag.outputs().size()), int(diag.inputs().size()), int(diag.asio().size()), tone, meter, int(rep.size()), qPrintable(file));
    for (const QVariant& v : diag.checks()) { const QVariantMap m = v.toMap(); std::printf("  [%s] %s: %s\n", qPrintable(m["level"].toString()), qPrintable(m["name"].toString()), qPrintable(m["detail"].toString())); }
    return checked && tone && meter && !file.isEmpty() && rep.contains("CHECKS") ? 0 : 1;
  }
  QQmlApplicationEngine qml; qml.rootContext()->setContextProperty("diag", &diag);
  qml.load(QUrl(QStringLiteral("qrc:/qt/qml/AmixOS/qml/DiagApp.qml"))); if (qml.rootObjects().isEmpty()) return 1;
  if (!shot.isEmpty()) { QQuickWindow* w = qobject_cast<QQuickWindow*>(qml.rootObjects().first()); wait(20000, [&] { return !diag.busy() && !diag.checks().isEmpty(); }); if (qEnvironmentVariableIsSet("AMIX_DIAG_DEMO")) diag.loadDemo(); wait(600);
    const bool ok = w && w->grabWindow().save(shot); std::printf("diagnostics --shot: %s\n", ok ? qPrintable(shot) : "FAILED"); return ok ? 0 : 1; }
  return app.exec();
}
