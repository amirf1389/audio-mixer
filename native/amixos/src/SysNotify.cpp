#include "SysNotify.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <cwchar>

namespace sysnotify {
namespace {
constexpr UINT kMsg = WM_APP + 71, kTimer = 1;
HWND g_wnd = nullptr; bool g_shown = false; std::function<void()> g_click;
NOTIFYICONDATAW base() { NOTIFYICONDATAW n{}; n.cbSize = sizeof n; n.hWnd = g_wnd; n.uID = 1; return n; }
void take() { if (g_shown && g_wnd) { NOTIFYICONDATAW n = base(); Shell_NotifyIconW(NIM_DELETE, &n); KillTimer(g_wnd, kTimer); } g_shown = false; }
LRESULT CALLBACK proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
  if (m == kMsg) {
    // the notification was clicked, or the icon beside the clock: open the console on SOFTWARE UPDATE; closed or timed out: the icon goes
    if (lp == NIN_BALLOONUSERCLICK || lp == WM_LBUTTONUP) { const auto f = g_click; take(); if (f) f(); }
    else if (lp == NIN_BALLOONTIMEOUT || lp == NIN_BALLOONHIDE) SetTimer(w, kTimer, 60000, nullptr);   // it stays in the notification centre; the icon a minute longer
    return 0; }
  if (m == WM_TIMER && wp == kTimer) { take(); return 0; }
  return DefWindowProcW(w, m, wp, lp);
}
bool window() {
  if (g_wnd) return true;
  WNDCLASSW c{}; c.lpfnWndProc = proc; c.hInstance = GetModuleHandleW(nullptr); c.lpszClassName = L"AmFX1Notify"; RegisterClassW(&c);   // registered twice: the second call fails, the class is there
  g_wnd = CreateWindowExW(0, L"AmFX1Notify", L"AmF X1", 0, 0, 0, 0, 0, nullptr, nullptr, c.hInstance, nullptr);   // never shown: it only receives the icon's messages
  return g_wnd != nullptr;
}
void put(wchar_t* dst, std::size_t cap, const QString& s) { const std::wstring w = s.left(static_cast<int>(cap) - 1).toStdWString(); std::wcsncpy(dst, w.c_str(), cap - 1); dst[cap - 1] = L'\0'; }
}  // namespace

bool show(const QString& title, const QString& text, std::function<void()> onClick) {
  if (!window()) return false;
  take(); g_click = std::move(onClick);
  NOTIFYICONDATAW n = base(); n.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_INFO; n.uCallbackMessage = kMsg;
  n.hIcon = LoadIconW(GetModuleHandleW(nullptr), L"IDI_ICON1"); if (!n.hIcon) n.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));   // the console's own icon, else Windows' program icon
  put(n.szTip, sizeof n.szTip / sizeof n.szTip[0], QStringLiteral("AmF X1"));
  put(n.szInfoTitle, sizeof n.szInfoTitle / sizeof n.szInfoTitle[0], title); put(n.szInfo, sizeof n.szInfo / sizeof n.szInfo[0], text);
  n.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
  g_shown = Shell_NotifyIconW(NIM_ADD, &n) != FALSE;
  if (g_shown) SetTimer(g_wnd, kTimer, 10 * 60000, nullptr);   // never left behind for good
  return g_shown;
}
void remove() { take(); g_click = nullptr; if (g_wnd) { DestroyWindow(g_wnd); g_wnd = nullptr; } }
}  // namespace sysnotify
#else
namespace sysnotify {
bool show(const QString&, const QString&, std::function<void()>) { return false; }
void remove() {}
}  // namespace sysnotify
#endif
