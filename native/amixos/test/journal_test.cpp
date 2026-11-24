// AUTO-SAVE JOURNAL and BACKGROUND RUN, without the console: the debounce (one save after a burst of changes, and a save while changes
// never stop), the atomic write with its one backup, the restore after a crash (the marker left behind; a torn journal falls back to the
// backup), and the close of the main window turned into a hide while the tray is there (the notice once; a quit without the tray).
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QThread>
#include <QWindow>
#include <cstdio>

#include "Background.h"
#include "Journal.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static void spin(int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) { QCoreApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(2); } }
static QByteArray readAll(const QString& p) { QFile f(p); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QTemporaryDir dir; CHECK(dir.isValid());
  const QString path = dir.filePath("journal.json");

  { // DEBOUNCE: five changes 10 ms apart = one save, 100 ms after the last
    int n = 0; Journal j(path, [&] { ++n; return QByteArray("{\"kind\":\"console-export\",\"n\":") + QByteArray::number(n) + "}"; }, 100, 1000);
    for (int i = 0; i < 5; ++i) { j.touch(); spin(10); }
    CHECK(j.saves() == 0 && j.pending());
    spin(250); CHECK(j.saves() == 1); CHECK(!j.pending()); CHECK(Journal::parses(readAll(path)));
    // MAX WAIT: changes every 30 ms for 600 ms never let the 100 ms debounce run out, but the 250 ms ceiling still saves
    Journal k(dir.filePath("busy.json"), [] { return QByteArray("{\"a\":1}"); }, 100, 250);
    for (int i = 0; i < 20; ++i) { k.touch(); spin(30); }
    CHECK(k.saves() >= 2);
    // FLUSH on quit: a pending change written at once
    k.touch(); CHECK(k.pending()); CHECK(k.flush()); CHECK(!k.pending());
  }
  { // ATOMIC WRITE + ONE BACKUP: the previous good state becomes .bak; no temporary file is left behind
    CHECK(Journal::writeAtomic(path, "{\"v\":1}"));
    CHECK(Journal::writeAtomic(path, "{\"v\":2}"));
    CHECK(readAll(path) == "{\"v\":2}"); CHECK(readAll(path + ".bak") == "{\"v\":1}");
    QStringList left = QDir(dir.path()).entryList(QDir::Files); bool tmp = false; for (const QString& f : left) if (f.startsWith("journal.json.") && f != "journal.json.bak") tmp = true;
    CHECK(!tmp);
    // a torn journal (the power went mid-write on a system without atomic rename) never replaces the good backup
    { QFile f(path); f.open(QIODevice::WriteOnly | QIODevice::Truncate); f.write("{\"v\":3, \"half"); }
    CHECK(Journal::writeAtomic(path, "{\"v\":4}")); CHECK(readAll(path + ".bak") == "{\"v\":1}");   // still the last good one
  }
  { // RESTORE AFTER A CRASH: the first run begins and dies (no end); the next begin() sees it and loadGood() gives the last good state
    const QString p = dir.filePath("crash.json");
    { Journal a(p, [] { return QByteArray("{\"kind\":\"console-export\",\"fader\":-6}"); }); CHECK(!a.begin()); CHECK(a.saveNow()); }   // no end(): the crash
    { Journal b(p, {}); CHECK(b.begin()); QString from; CHECK(b.loadGood(&from).contains("-6")); CHECK(from == p); b.end(); }
    { Journal c(p, {}); CHECK(!c.begin()); c.end(); }   // a clean quit: no crash at the next start
    // the journal itself torn: the backup is used
    Journal::writeAtomic(p, "{\"kind\":\"console-export\",\"fader\":-3}");   // -6 goes to the backup
    { QFile f(p); f.open(QIODevice::WriteOnly | QIODevice::Truncate); f.write("{\"kind\":\"cons"); }
    { Journal d(p, {}); QString from; const QByteArray got = d.loadGood(&from); CHECK(got.contains("-6")); CHECK(from.endsWith(".bak")); }
    // nothing saved at all: empty, not invented
    { Journal e(dir.filePath("never.json"), {}); CHECK(e.loadGood().isEmpty()); }
  }
  { // CLOSE TO TRAY: the pure decision
    CHECK(bg::decideClose(true, true, false) == bg::CloseAction::HideToTrayWithNotice);
    CHECK(bg::decideClose(true, true, true) == bg::CloseAction::HideToTray);
    CHECK(bg::decideClose(false, true, false) == bg::CloseAction::Quit);
    CHECK(bg::decideClose(true, false, false) == bg::CloseAction::Quit);   // no icon to come back from: never hidden
    CHECK(bg::runCommand("/opt/amf/amixos").startsWith("\"") && bg::runCommand("/opt/amf/amixos").endsWith("--background"));
#ifndef _WIN32
    CHECK(!bg::startWithWindows()); CHECK(!bg::setStartWithWindows(true, "x")); CHECK(!bg::startWithWindows());   // a no-op off Windows
#endif
  }
  { // CLOSE TO TRAY on a real window: hidden, not quit; the notice once; the setting off = a quit
    QWindow w; w.resize(200, 100); w.show(); spin(50);
    bool keep = true, tray = true; int notices = 0, quits = 0, remembered = 0;
    CloseToTray k; k.keepInTray = [&] { return keep; }; k.trayShown = [&] { return tray; }; k.showNotice = [&] { ++notices; }; k.onNoticeShown = [&] { ++remembered; }; k.quit = [&] { ++quits; };
    w.installEventFilter(&k);
    w.close(); spin(20); CHECK(!w.isVisible()); CHECK(k.hidden == 1); CHECK(notices == 1 && remembered == 1); CHECK(quits == 0);
    w.show(); spin(20); w.close(); spin(20); CHECK(!w.isVisible()); CHECK(notices == 1); CHECK(k.hidden == 2);   // the notice only the first time
    w.show(); spin(20); tray = false; w.close(); spin(20); CHECK(quits == 1); CHECK(k.hidden == 2);   // no tray icon: the close quits
    w.show(); spin(20); tray = true; keep = false; w.close(); spin(20); CHECK(quits == 2);   // KEEP RUNNING IN THE TRAY off: quits as before
  }
  std::printf(fails ? "journal_test: %d failure(s)\n" : "journal_test: all passed\n", fails);
  return fails ? 1 : 0;
}
