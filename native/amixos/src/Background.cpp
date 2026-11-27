#include "Background.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QSettings>
#include <QWindow>

namespace bg {
CloseAction decideClose(bool keepInTray, bool trayShown, bool noticeShown) {
  if (!keepInTray || !trayShown) return CloseAction::Quit;   // no icon to come back from: never a hidden console nobody can reach
  return noticeShown ? CloseAction::HideToTray : CloseAction::HideToTrayWithNotice;
}
QString runCommand(const QString& exe) { return QStringLiteral("\"%1\" --background").arg(QDir::toNativeSeparators(exe)); }

#ifdef _WIN32
}  // namespace bg
#include <windows.h>
namespace bg {
bool packaged() {
  using Fn = LONG(WINAPI*)(UINT32*, PWSTR);   // kernel32 (Windows 8+), looked up so older SDK headers still build
  static const int v = [] { auto f = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetCurrentPackageFullName")));
    if (!f) return 0; UINT32 n = 0; return f(&n, nullptr) == ERROR_INSUFFICIENT_BUFFER ? 1 : 0; }();
  return v == 1;
}
static const char* kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const char* kRunName = "AmF X1";
bool startWithWindows() { QSettings r(QString::fromLatin1(kRunKey), QSettings::NativeFormat); return r.contains(QString::fromLatin1(kRunName)); }
bool setStartWithWindows(bool on, const QString& exe) {
  if (packaged()) return false;   // the MSIX StartupTask (Settings > Apps > Startup) replaces the Run key
  QSettings r(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
  if (on) r.setValue(QString::fromLatin1(kRunName), runCommand(exe)); else r.remove(QString::fromLatin1(kRunName));
  r.sync(); return r.status() == QSettings::NoError;
}
#else
bool packaged() { return false; }
bool startWithWindows() { return false; }
bool setStartWithWindows(bool, const QString&) { return false; }
#endif
}  // namespace bg

bool CloseToTray::eventFilter(QObject* o, QEvent* e) {
  if (e->type() != QEvent::Close) return QObject::eventFilter(o, e);
  auto* w = qobject_cast<QWindow*>(o); if (!w) return QObject::eventFilter(o, e);
  const bg::CloseAction a = bg::decideClose(keepInTray && keepInTray(), trayShown && trayShown(), noticeShown);
  if (a == bg::CloseAction::Quit) { if (quit) quit(); return QObject::eventFilter(o, e); }
  e->ignore(); w->hide(); ++hidden;   // the window goes; the sound, the capture and the remote service stay
  if (a == bg::CloseAction::HideToTrayWithNotice) { noticeShown = true; if (showNotice) showNotice(); if (onNoticeShown) onNoticeShown(); }
  return true;
}
