#include "AppWindows.h"

#include <QCoreApplication>
#include <QPainter>
#include <QPointer>
#include <QVariant>
#include <chrono>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
namespace {
// the program's main window: visible, with a title, not a tool window, the largest one of that process
struct Find { DWORD pid; HWND best = nullptr; long area = -1; };
BOOL CALLBACK each(HWND w, LPARAM lp) {
  auto* f = reinterpret_cast<Find*>(lp); DWORD pid = 0; GetWindowThreadProcessId(w, &pid);
  if (pid != f->pid || !IsWindowVisible(w) || GetWindowTextLengthW(w) == 0 || (GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) return TRUE;
  RECT r{}; GetWindowRect(w, &r); const long a = (r.right - r.left) * (r.bottom - r.top); if (a > f->area) { f->area = a; f->best = w; }
  return TRUE;
}
HWND mainWindow(quint32 pid) { Find f{static_cast<DWORD>(pid)}; EnumWindows(each, reinterpret_cast<LPARAM>(&f)); return f.best; }
QImage fromBitmap(HDC dc, HBITMAP bmp, int w, int h) {
  BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
  QImage img(w, h, QImage::Format_RGB32); if (img.isNull()) return {};
  if (!GetDIBits(dc, bmp, 0, static_cast<UINT>(h), img.bits(), &bi, DIB_RGB_COLORS)) return {};
  return img;
}
// the window as Windows draws it, also when it is behind other windows (PW_RENDERFULLCONTENT: Windows 8.1 and later); scaled down to at most 640 wide
QImage windowPicture(quint32 pid) {
  HWND w = mainWindow(pid); if (!w || IsIconic(w)) return {};
  RECT r{}; if (!GetWindowRect(w, &r)) return {}; const int cw = r.right - r.left, ch = r.bottom - r.top; if (cw < 16 || ch < 16 || cw > 16384 || ch > 16384) return {};
  HDC screen = GetDC(nullptr); HDC dc = CreateCompatibleDC(screen); HBITMAP bmp = CreateCompatibleBitmap(screen, cw, ch); QImage out;
  if (dc && bmp) { HGDIOBJ old = SelectObject(dc, bmp); if (PrintWindow(w, dc, 2 /* PW_RENDERFULLCONTENT */)) out = fromBitmap(dc, bmp, cw, ch); SelectObject(dc, old); }
  if (bmp) DeleteObject(bmp); if (dc) DeleteDC(dc); ReleaseDC(nullptr, screen);
  return out.isNull() || out.width() <= 640 ? out : out.scaledToWidth(640, Qt::SmoothTransformation);
}
QImage programIcon(quint32 pid) {
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid); if (!h) return {};
  wchar_t path[MAX_PATH]; DWORD n = MAX_PATH; const bool ok = QueryFullProcessImageNameW(h, 0, path, &n) != 0; CloseHandle(h); if (!ok) return {};
  SHFILEINFOW fi{}; if (!SHGetFileInfoW(path, 0, &fi, sizeof fi, SHGFI_ICON | SHGFI_LARGEICON) || !fi.hIcon) return {};
  const QImage img = QImage::fromHICON(fi.hIcon); DestroyIcon(fi.hIcon); return img;
}
QString windowTitle(quint32 pid) { HWND w = mainWindow(pid); if (!w) return {}; wchar_t t[256]; const int n = GetWindowTextW(w, t, 256); return n > 0 ? QString::fromWCharArray(t, n) : QString(); }
}  // namespace
#else
namespace {
QImage windowPicture(quint32) { return {}; } QImage programIcon(quint32) { return {}; } QString windowTitle(quint32) { return {}; }
}  // namespace
#endif

AppWindows::AppWindows(QObject* parent) : QObject(parent) {
  timer_.setInterval(125); connect(&timer_, &QTimer::timeout, this, [this] { if (busy_.load()) return; { std::lock_guard<std::mutex> g(mu_); for (quint32 p : watch_) if (!jobs_.contains(p)) jobs_ << p; } });   // 8 a second, never piling up behind a slow window
  thread_ = std::thread([this] { run(); });
}
AppWindows::~AppWindows() { stop_ = true; if (thread_.joinable()) thread_.join(); }
bool AppWindows::available() const {
#ifdef Q_OS_WIN
  return true;
#else
  return static_cast<bool>(testSource);
#endif
}
QVariantList AppWindows::watched() const { QVariantList l; for (quint32 p : watch_) l << p; return l; }
QString AppWindows::titleOf(quint32 pid) { return testTitle ? testTitle(pid) : windowTitle(pid); }
void AppWindows::snapshot(quint32 pid) { if (pid == 0) return; std::lock_guard<std::mutex> g(mu_); if (!jobs_.contains(pid)) jobs_ << pid; }
void AppWindows::watch(quint32 pid, bool on) {
  if (pid == 0 || on == watch_.contains(pid)) return;
  if (on) { if (watch_.size() >= 8) return; watch_.insert(pid); snapshot(pid); } else watch_.remove(pid);
  if (watch_.isEmpty()) timer_.stop(); else if (!timer_.isActive()) timer_.start();
  emit watchedChanged();
}
void AppWindows::forget(quint32 pid) { watch(pid, false); std::lock_guard<std::mutex> g(mu_); pics_.remove(pid); icons_.remove(pid); jobs_.removeAll(pid); }
QImage AppWindows::picture(quint32 pid) { std::lock_guard<std::mutex> g(mu_); return pics_.value(pid); }
QImage AppWindows::icon(quint32 pid) {
  { std::lock_guard<std::mutex> g(mu_); if (icons_.contains(pid)) return icons_.value(pid); }
  const QImage i = testSource ? testSource(pid, true) : programIcon(pid);   // small and quick: asked where it is needed, kept
  std::lock_guard<std::mutex> g(mu_); if (icons_.size() > 64) icons_.clear(); icons_.insert(pid, i); return i;
}
void AppWindows::grab(quint32 pid) {
  const QImage img = testSource ? testSource(pid, false) : windowPicture(pid);
  { std::lock_guard<std::mutex> g(mu_); if (pics_.size() > 32) pics_.clear(); if (!img.isNull() || !pics_.contains(pid)) pics_.insert(pid, img); }   // a window that gives nothing just now keeps its last picture
}
// the pictures are made here, never on the interface thread: a program that hangs holds up only its own picture
void AppWindows::run() {
  while (!stop_.load()) {
    quint32 pid = 0; { std::lock_guard<std::mutex> g(mu_); if (!jobs_.isEmpty()) pid = jobs_.takeFirst(); }
    if (pid == 0) { std::this_thread::sleep_for(std::chrono::milliseconds(15)); continue; }
    busy_ = true; grab(pid); bool more; { std::lock_guard<std::mutex> g(mu_); more = !jobs_.isEmpty(); } busy_ = more;
    QPointer<AppWindows> self(this); QMetaObject::invokeMethod(qApp, [self] { if (self) { ++self->tick_; emit self->tickChanged(); } }, Qt::QueuedConnection);
  }
}

// "icon/<pid>" or "window/<pid>?<anything>"; a missing picture is a plain dark tile (never an error in the page)
QImage AppWindowImages::requestImage(const QString& id, QSize* size, const QSize& requested) {
  const QString path = id.section('?', 0, 0); const bool icon = path.startsWith(QLatin1String("icon/")); const quint32 pid = path.section('/', 1, 1).toUInt();
  QImage img = pid ? (icon ? w_->icon(pid) : w_->picture(pid)) : QImage();
  if (img.isNull()) { img = QImage(icon ? 32 : 320, icon ? 32 : 180, QImage::Format_ARGB32_Premultiplied); img.fill(icon ? Qt::transparent : QColor(18, 20, 24)); }
  if (requested.width() > 0 && requested.height() > 0) img = img.scaled(requested, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  if (size) *size = img.size();
  return img;
}
