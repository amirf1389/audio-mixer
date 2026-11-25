// HIDE FROM SCREENSHOTS & SCREEN SHARING (Windows): every top-level window of the console (the main window, dialogs, popups that are
// windows of their own, the tray menu) gets SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE), so screenshots, screen recording and
// screen sharing see it black / not at all; Windows before 10 2004 gets WDA_MONITOR (shown black). Windows created later are caught
// as their native window appears. On by default (streaming apps such as OBS / TikTok LIVE Studio see it black too); SETTINGS switches it at once. AMIX_ALLOW_CAPTURE=1 turns it off for the repository's
// own tooling (--shots / --bench / CI). It cannot stop a camera pointed at the screen. macOS: NSWindow.sharingType = none (CaptureGuardMac.mm). Other systems: a no-op.
#pragma once
#include <QObject>

class QWindow;
class CaptureGuard : public QObject {
  Q_OBJECT
 public:
  explicit CaptureGuard(QObject* parent = nullptr);
  void setEnabled(bool on);                // applies to every window now
  bool enabled() const { return on_; }
  static bool envAllowsCapture();          // AMIX_ALLOW_CAPTURE=1
  static bool supported();                 // Windows and macOS builds
  static bool apply(QWindow* w, bool on);  // one window; false when the system refused (or not Windows)
  int applied() const { return applied_; } // how many windows got the setting (the smoke check reads it)
 protected:
  bool eventFilter(QObject* o, QEvent* e) override;
 private:
  bool on_ = false; int applied_ = 0;
};
