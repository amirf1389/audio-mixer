#include "CaptureGuard.h"

#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QWindow>
#ifdef _WIN32
#include <windows.h>
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#endif

CaptureGuard::CaptureGuard(QObject* parent) : QObject(parent) { qApp->installEventFilter(this); }
bool CaptureGuard::envAllowsCapture() { return qEnvironmentVariable("AMIX_ALLOW_CAPTURE") == QLatin1String("1"); }
bool CaptureGuard::supported() {
#ifdef _WIN32
  return true;
#else
  return false;
#endif
}
bool CaptureGuard::apply(QWindow* w, bool on) {
#ifdef _WIN32
  if (!w || !w->handle()) return false;
  HWND h = reinterpret_cast<HWND>(w->winId());
  if (!on) return SetWindowDisplayAffinity(h, WDA_NONE) != 0;
  if (SetWindowDisplayAffinity(h, WDA_EXCLUDEFROMCAPTURE)) return true;   // Windows 10 2004 and newer
  return SetWindowDisplayAffinity(h, WDA_MONITOR) != 0;                  // older: the window is shown black in captures
#else
  Q_UNUSED(w); Q_UNUSED(on); return false;
#endif
}
void CaptureGuard::setEnabled(bool on) {
  on_ = on && !envAllowsCapture();
  for (QWindow* w : QGuiApplication::allWindows()) if (w->handle() && apply(w, on_)) ++applied_;
}
// a window that gets its native surface now (a dialog, a popup window, the tray menu) gets the setting at once, before it is drawn
bool CaptureGuard::eventFilter(QObject* o, QEvent* e) {
  if (on_ && (e->type() == QEvent::PlatformSurface || e->type() == QEvent::Show) && o->isWindowType()) {
    if (e->type() != QEvent::PlatformSurface || static_cast<QPlatformSurfaceEvent*>(e)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated) {
      QWindow* w = static_cast<QWindow*>(o); if (w->handle() && apply(w, true)) ++applied_; }
  }
  return QObject::eventFilter(o, e);
}
