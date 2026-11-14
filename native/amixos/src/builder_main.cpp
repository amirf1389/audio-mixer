// AmF X1 PLUG-IN BUILDER: the builder as its own program (next to the console, sharing its AMFB Plugin Shell.vst3 and the design folder).
//   "AmF X1 Plugin Builder" [--lang CODE] [--shot FILE]   (--shot: save a picture of the window and quit: a check without a display)
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlPropertyMap>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <cstdio>

#include "Builder.h"
#include "I18n.h"

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  app.setOrganizationName("AMFB Audio"); app.setApplicationName("A mix OS"); app.setApplicationDisplayName("AmF X1 Plug-in Builder");   // the console's settings (language) and data folder
  QQuickStyle::setStyle("Fusion");
  QPalette p; p.setColor(QPalette::Window, QColor("#0b0d10")); p.setColor(QPalette::WindowText, QColor("#e6edf3")); p.setColor(QPalette::Base, QColor("#11151b"));
  p.setColor(QPalette::Text, QColor("#e6edf3")); p.setColor(QPalette::Button, QColor("#1a2028")); p.setColor(QPalette::ButtonText, QColor("#e6edf3")); p.setColor(QPalette::Highlight, QColor("#EA580C")); app.setPalette(p);
  for (int i = 1; i < argc; ++i) if (QString::fromLocal8Bit(argv[i]) == "--selftest") {   // a design, exported to AMIX_VST3_OUT, read back from the bundle
    Builder b(nullptr); b.newDesign(); b.setName("Selftest Grit"); b.addBlock("crusher"); b.addBlock("tubeamp");
    const QVariantList bl = b.blocks(); b.exposeKnob(0, bl[0].toMap()["knobs"].toList()[0].toMap()["id"].toString(), true); b.setKnobLabel(0, bl[0].toMap()["knobs"].toList()[0].toMap()["id"].toString(), "Grit");
    const QString out = b.exportPlugin(); QFile f(out + "/Contents/Resources/design.json"); const bool read = f.open(QIODevice::ReadOnly) && f.readAll().contains("Selftest Grit");
    const bool tryRefused = !b.tryOnStrip(0);   // no console here
    b.enablePreview("null"); b.setPreviewSource(1); const bool played = b.previewPlay(true); QElapsedTimer w; w.start(); double outDb = -120;
    while (w.elapsed() < 600) { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); outDb = std::max(outDb, b.previewOutDb()); }
    b.setPreviewBypass(true); b.setPreviewSample(5); b.previewPlay(false);
    std::printf("plug-in builder --selftest: preview %s (played %d, out %.1f dBFS)\n", played && outDb > -60 ? "ok" : "FAILED", played, outDb);
    if (!(played && outDb > -60)) return 1;
    std::printf("plug-in builder --selftest: %s (exported %s, design read back %d, TRY refused without a console %d, %d designs saved)\n", !out.isEmpty() && read && tryRefused ? "ok" : "FAILED", qPrintable(out), read, tryRefused, int(b.designs().size()));
    return !out.isEmpty() && read && tryRefused ? 0 : 1; }
  QString lang, shot; for (int i = 1; i + 1 < argc; ++i) { const QString a = QString::fromLocal8Bit(argv[i]); if (a == "--lang") lang = QString::fromLocal8Bit(argv[++i]); else if (a == "--shot") shot = QString::fromLocal8Bit(argv[++i]); }
  I18n i18n; if (!lang.isEmpty()) i18n.setLang(lang);
  Builder builder(nullptr);   // no console here: EXPORT for the DAWs; TRY IN THE MIXER lives in the console
  builder.enablePreview(qEnvironmentVariable("AMIX_PREVIEW_BACKEND", "auto"));   // PREVIEW: the program's own engine on the sound card
  QQmlPropertyMap settings; settings.insert("darkTheme", true);
  QQmlApplicationEngine qml;
  qml.rootContext()->setContextProperty("i18n", &i18n); qml.rootContext()->setContextProperty("builder", &builder);
  qml.rootContext()->setContextProperty("settings", &settings); qml.rootContext()->setContextProperty("engine", QVariant::fromValue<QObject*>(nullptr));
  qml.load(QUrl(QStringLiteral("qrc:/qt/qml/AmixOS/qml/BuilderApp.qml")));
  if (qml.rootObjects().isEmpty()) return 2;
  if (!shot.isEmpty()) {
    builder.setName("Vocal Glue"); builder.setCategory("Fx|Dynamics"); for (const char* k : {"barrier", "exciter", "room"}) builder.addBlock(k);
    const QVariantList bl = builder.blocks(); int n = 0; for (int b = 0; b < bl.size(); ++b) { const QVariantList ks = bl[b].toMap()["knobs"].toList(); for (int k = 0; k < ks.size() && k < 2 && n < 5; ++k, ++n) builder.exposeKnob(b, ks[k].toMap()["id"].toString(), true); }
    builder.setPreviewSource(1); builder.previewPlay(true);   // the preview running in the picture (pink noise)
    QTimer::singleShot(900, &app, [&] { auto* w = qobject_cast<QQuickWindow*>(qml.rootObjects().first()); const bool ok = w->grabWindow().save(shot);
      std::printf("plug-in builder: picture %s (%s), shell %s, export to %s\n", ok ? "saved" : "FAILED", qPrintable(shot), builder.shellFound() ? "found" : "MISSING", qPrintable(builder.exportDir())); app.exit(ok ? 0 : 3); });
  }
  return app.exec();
}
