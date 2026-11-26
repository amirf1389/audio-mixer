// BACKGROUND RUN: closing the console window keeps the sound running (capture, mixing, APP SOUND, the remote service) behind the tray icon.
//   CloseToTray watches the main window's close: with KEEP RUNNING IN THE TRAY on and the tray icon shown, the close is turned into a
//   hide (the engine and everything else carry on); the first time, a one-time notice says where the console went. With the setting off,
//   or no tray icon to come back from, the close quits as before. Minimising never stops anything either (the window only goes down).
//   The tray's SHOW brings the window back, its QUIT really quits.
//   START WITH WINDOWS: one value in HKCU\Software\Microsoft\Windows\CurrentVersion\Run (the user's own key, no administrator), off by
//   default; elsewhere a no-op.
// Qt Gui only (unit tested on the offscreen platform: test/journal_test.cpp).
#pragma once
#include <QObject>
#include <QString>
#include <functional>

class QWindow;

namespace bg {
enum class CloseAction { Quit, HideToTray, HideToTrayWithNotice };
/** what a close of the main window does (pure) */
CloseAction decideClose(bool keepInTray, bool trayShown, bool noticeShown);
/** the Run-key command line for `exe` (quoted; started in the background, the window hidden until SHOW) */
QString runCommand(const QString& exe);
/** START WITH WINDOWS: read / write the Run key value; false (and nothing written) off Windows */
bool startWithWindows();
bool setStartWithWindows(bool on, const QString& exe);
/** true when running from an MSIX package (Microsoft Store): GetCurrentPackageFullName succeeds. Then the Run key and the
 *  self-updater are off: the package's StartupTask and the Store do those jobs. Always false off Windows. */
bool packaged();
}  // namespace bg

class CloseToTray : public QObject {
 public:
  explicit CloseToTray(QObject* parent = nullptr) : QObject(parent) {}
  std::function<bool()> keepInTray;      // the setting
  std::function<bool()> trayShown;       // an icon is there to come back from
  std::function<void()> showNotice;      // the one-time notice (then noticeShown is set)
  bool noticeShown = false;
  std::function<void()> onNoticeShown;   // remember it (QSettings)
  std::function<void()> quit;            // a close that is a quit
  int hidden = 0;                        // closes turned into hides (tests)
  bool eventFilter(QObject* o, QEvent* e) override;
};
