// PRO TOOLS: licence tiers, the session folder, correlation / goniometer, loudness history, markers (sorted, persisted, CSV-safe)
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>
#include "ProTools.h"
#include "Settings.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv); QTemporaryDir tmp;
  CHECK(!ProTools::tierAllows("BASIC")); CHECK(ProTools::tierAllows("PRO")); CHECK(ProTools::tierAllows("studio")); CHECK(ProTools::tierAllows("MASTER"));
  CHECK(ProTools::sessionName(QDateTime(QDate(2026, 11, 25), QTime(14, 3, 7))) == "Session 2026-11-25 14-03-07");
  ProTools p(tmp.filePath("pt.json")); const QString s = p.makeSessionFolder(tmp.path()); CHECK(!s.isEmpty() && QDir(s).exists()); CHECK(p.makeSessionFolder("").isEmpty());
  const QString s2 = p.makeSessionFolder(tmp.path()); CHECK(s2 != s);
  QVector<double> same, anti, quad; for (int i = 0; i < 480; ++i) { const double x = std::sin(i * 0.1); same << x << x; anti << x << -x; quad << x << std::cos(i * 0.1); }
  CHECK(std::abs(ProTools::correlation(same) - 1) < 1e-6); CHECK(std::abs(ProTools::correlation(anti) + 1) < 1e-6); CHECK(std::abs(ProTools::correlation(quad)) < 0.05); CHECK(ProTools::correlation({}) == 0);
  const QVariantList g = p.goniometer({0.5, 0.5}); CHECK(g.size() == 2 && std::abs(g[0].toDouble() - 0.5) < 1e-9 && g[1].toDouble() < 0.5);   // mono: straight up the middle
  for (int i = 0; i < 700; ++i) p.pushLoudness(-23, -200); CHECK(p.loudHistory().size() == 1200); CHECK(p.loudHistory()[1].toDouble() == -70);
  const int b = p.addMarker(20, "Chorus"); p.addMarker(5, "=cmd()"); const int c = p.addMarker(10, ""); CHECK(p.markers().size() == 3); CHECK(p.markers()[0].toMap()["t"].toDouble() == 5);
  CHECK(p.markers()[1].toMap()["label"].toString() == "Marker 3"); CHECK(p.renameMarker(c, "Verse")); CHECK(p.removeMarker(b)); CHECK(!p.removeMarker(999));
  p.setNotes("Mic 3 \"hot\""); CHECK(p.exportText().contains("\"'=cmd()\"")); CHECK(p.exportText().contains("\"\"hot\"\"")); CHECK(p.saveTo(s)); CHECK(QFile::exists(s + "/markers.csv")); CHECK(!p.saveTo(tmp.filePath("nope")));
  { ProTools q(tmp.filePath("pt.json")); CHECK(q.markers().size() == 2); CHECK(q.notes() == p.notes()); CHECK(q.addMarker(1, "x") > c); }
  // SETTINGS: the remote port (1024-65535, default 8765)
  CHECK(Settings::validPort(8765) && Settings::validPort(8196) && Settings::validPort(1024) && Settings::validPort(65535)); CHECK(!Settings::validPort(1023) && !Settings::validPort(65536) && !Settings::validPort(0));
  std::printf("protools_test: %s\n", fails ? "FAILED" : "all passed"); return fails ? 1 : 0;
}
