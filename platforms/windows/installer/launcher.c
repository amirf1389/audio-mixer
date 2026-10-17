/* AudioMixerServer.exe: native launcher for the Audio Mixer local server (installed next to runtime\node.exe and bridge\server.js).
 *
 * It replaces what used to be a hidden script: a signed program that starts the bundled Node.js server without a console window.
 * Nothing is written anywhere; it only starts <install folder>\runtime\node.exe <install folder>\bridge\server.js.
 *   AudioMixerServer.exe             start the local server in the background (once; a second start does nothing)
 *   AudioMixerServer.exe /plugins    open the plugin folder %USERPROFILE%\AudioMixerPlugins (creating it if needed)
 *   AudioMixerServer.exe /open       start-up screen (power LED, logo, boot lines, progress bar) while the server starts, opens the mixer in the browser,
 *                                    then stays in the notification area: a tray icon to reopen the mixer, stop/start the local server, or exit (which
 *                                    stops the server first and shows a system notification once it has).
 * The tray icon finds the running server by asking it for its own process id (GET /api/power, already served for other purposes) rather than tracking
 * a process handle: /open hands the actual start to client.js --ensure, which may itself exit once the server is confirmed running, so the launcher's
 * own CreateProcess handle is not the server's.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <shellapi.h>
#include <wchar.h>
#include <string.h>
#include <stdlib.h>

#ifndef AMIX_VERSION
#define AMIX_VERSION ""        /* set by scripts/build-exe.js: the version this launcher belongs to */
#endif

/* ---- start-up screen (/open) ---- */
#define PORT 8765
#define MIN_MS 2800          /* the animation is always seen, even when the server is already running */
#define MAX_MS 25000         /* the server did not answer: say so */
static DWORD g_t0, g_poll; static int g_up = 0, g_failed = 0; static HFONT g_big, g_small; static const wchar_t *g_lines[6] = { L"TITAN OS", L"DSP CORE", L"AUDIO ENGINE", L"I/O", L"FADERS", L"CONSOLE" };

static int server_up(void) {             /* does something listen on 127.0.0.1:PORT? (non-blocking connect, 40 ms) */
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return 0;
  u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
  struct sockaddr_in a; ZeroMemory(&a, sizeof a); a.sin_family = AF_INET; a.sin_port = htons(PORT); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  connect(s, (struct sockaddr *)&a, sizeof a);
  fd_set w; FD_ZERO(&w); FD_SET(s, &w); struct timeval tv = { 0, 40000 };
  int ok = select(0, NULL, &w, NULL, &tv) > 0;
  closesocket(s);
  return ok;
}

/* Is the server on PORT *this* version? An older server (started at login before an upgrade) answers too and would show the old page. */
static int server_current(void) {
  if (!AMIX_VERSION[0]) return server_up();
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return 0;
  struct sockaddr_in a; ZeroMemory(&a, sizeof a); a.sin_family = AF_INET; a.sin_port = htons(PORT); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  DWORD tmo = 300; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof tmo); setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tmo, sizeof tmo);
  int ok = 0;
  if (connect(s, (struct sockaddr *)&a, sizeof a) == 0) {
    const char *req = "GET /api/status HTTP/1.0\r\nHost: localhost:8765\r\nConnection: close\r\n\r\n";
    char buf[2048]; int n, got = 0;
    if (send(s, req, (int)strlen(req), 0) > 0) {
      while (got < (int)sizeof buf - 1 && (n = recv(s, buf + got, (int)sizeof buf - 1 - got, 0)) > 0) got += n;
      buf[got] = 0;
      ok = strstr(buf, "\"version\":\"" AMIX_VERSION "\"") != NULL;
    }
  }
  closesocket(s);
  return ok;
}

/* the server's own process id, from the JSON it already serves at GET /api/power ({"ok":true,"pid":NNNN,...}); 0 when nothing answers */
static int server_pid(void) {
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return 0;
  struct sockaddr_in a; ZeroMemory(&a, sizeof a); a.sin_family = AF_INET; a.sin_port = htons(PORT); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  DWORD tmo = 300; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof tmo); setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tmo, sizeof tmo);
  int pid = 0;
  if (connect(s, (struct sockaddr *)&a, sizeof a) == 0) {
    const char *req = "GET /api/power HTTP/1.0\r\nHost: localhost:8765\r\nConnection: close\r\n\r\n";
    char buf[1024]; int n, got = 0;
    if (send(s, req, (int)strlen(req), 0) > 0) {
      while (got < (int)sizeof buf - 1 && (n = recv(s, buf + got, (int)sizeof buf - 1 - got, 0)) > 0) got += n;
      buf[got] = 0;
      const char *p = strstr(buf, "\"pid\":");
      if (p) pid = atoi(p + 6);
    }
  }
  closesocket(s);
  return pid;
}

/* ---- tray icon (/open stays resident after opening the page): reopen, stop/start the server, exit ---- */
static void open_page(void);                                          /* defined below; the tray menu's "Open Mixer" and left-click reuse it */
#define WM_AMIX_TRAY (WM_APP + 1)
#define ID_TRAY_OPEN 1001
#define ID_TRAY_TOGGLE 1002
#define ID_TRAY_EXIT 1003
static wchar_t g_dir[MAX_PATH * 2];
static NOTIFYICONDATAW g_nid;

static void start_server_now(void) {
  wchar_t line[MAX_PATH * 4 + 16];
  _snwprintf(line, MAX_PATH * 4 + 16, L"\"%lsruntime\\node.exe\" \"%lsbridge\\server.js\"", g_dir, g_dir); line[MAX_PATH * 4 + 15] = 0;
  STARTUPINFOW si; PROCESS_INFORMATION pi; ZeroMemory(&si, sizeof si); si.cb = sizeof si;
  if (CreateProcessW(NULL, line, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, g_dir, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
}
static void stop_server_now(void) {
  int pid = server_pid(); if (!pid) return;
  HANDLE p = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
  if (p) { TerminateProcess(p, 0); CloseHandle(p); }
}
/* a Windows system notification (the tray icon's own balloon tip, NIIF_INFO) - shown right before the icon is removed on exit */
static void tray_notify(const wchar_t *title, const wchar_t *text) {
  g_nid.uFlags = NIF_INFO; g_nid.dwInfoFlags = NIIF_INFO;
  wcsncpy(g_nid.szInfoTitle, title, ARRAYSIZE(g_nid.szInfoTitle) - 1); g_nid.szInfoTitle[ARRAYSIZE(g_nid.szInfoTitle) - 1] = 0;
  wcsncpy(g_nid.szInfo, text, ARRAYSIZE(g_nid.szInfo) - 1); g_nid.szInfo[ARRAYSIZE(g_nid.szInfo) - 1] = 0;
  Shell_NotifyIconW(NIM_MODIFY, &g_nid);
  g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;                    /* back to the plain icon for the next NIM_MODIFY (the tooltip refresh) */
}
static void show_tray_menu(HWND h) {
  POINT pt; GetCursorPos(&pt);
  int running = server_pid() != 0;
  HMENU m = CreatePopupMenu();
  AppendMenuW(m, MF_STRING, ID_TRAY_OPEN, L"Open Mixer");
  AppendMenuW(m, MF_SEPARATOR, 0, NULL);
  AppendMenuW(m, MF_STRING, ID_TRAY_TOGGLE, running ? L"Stop Server" : L"Start Server");
  AppendMenuW(m, MF_SEPARATOR, 0, NULL);
  AppendMenuW(m, MF_STRING, ID_TRAY_EXIT, L"Exit");
  SetForegroundWindow(h);                                             /* so the menu closes on an outside click, as a normal tray menu does */
  TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, NULL);
  DestroyMenu(m);
}
static LRESULT CALLBACK tray_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_AMIX_TRAY) {
    if (l == WM_LBUTTONUP || l == WM_LBUTTONDBLCLK) open_page();
    else if (l == WM_RBUTTONUP) show_tray_menu(h);
    return 0;
  }
  if (m == WM_COMMAND) {
    switch (LOWORD(w)) {
      case ID_TRAY_OPEN: open_page(); break;
      case ID_TRAY_TOGGLE:
        if (server_pid()) { stop_server_now(); tray_notify(L"Audio Mixer", L"The local server has stopped."); }
        else { start_server_now(); tray_notify(L"Audio Mixer", L"The local server is starting."); }
        break;
      case ID_TRAY_EXIT:
        if (server_pid()) stop_server_now();
        tray_notify(L"Audio Mixer", L"Audio Mixer has shut down.");
        SetTimer(h, 2, 1800, NULL);                                   /* give the notification a moment on screen before the icon disappears */
        break;
    }
    return 0;
  }
  if (m == WM_TIMER && w == 2) { DestroyWindow(h); return 0; }
  if (m == WM_DESTROY) { Shell_NotifyIconW(NIM_DELETE, &g_nid); PostQuitMessage(0); return 0; }
  return DefWindowProcW(h, m, w, l);
}
/* runs until Exit: the tray icon is the launcher's only remaining purpose once the mixer page is open */
static void run_tray(HINSTANCE inst) {
  WNDCLASSW wc; ZeroMemory(&wc, sizeof wc);
  wc.lpfnWndProc = tray_proc; wc.hInstance = inst; wc.lpszClassName = L"AudioMixerTray";
  RegisterClassW(&wc);
  HWND h = CreateWindowW(L"AudioMixerTray", L"Audio Mixer", 0, 0, 0, 0, 0, NULL, NULL, inst, NULL);
  if (!h) return;
  ZeroMemory(&g_nid, sizeof g_nid);
  g_nid.cbSize = sizeof g_nid; g_nid.hWnd = h; g_nid.uID = 1;
  g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  g_nid.uCallbackMessage = WM_AMIX_TRAY;
  g_nid.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
  wcsncpy(g_nid.szTip, L"Audio Mixer", ARRAYSIZE(g_nid.szTip) - 1); g_nid.szTip[ARRAYSIZE(g_nid.szTip) - 1] = 0;
  Shell_NotifyIconW(NIM_ADD, &g_nid);
  MSG msg; while (GetMessageW(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
}

static void paint(HWND h) {
  PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
  int W = rc.right, H = rc.bottom; HDC mem = CreateCompatibleDC(dc); HBITMAP bm = CreateCompatibleBitmap(dc, W, H); HGDIOBJ old = SelectObject(mem, bm);
  HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0)); FillRect(mem, &rc, bg); DeleteObject(bg);
  double e = (double)(GetTickCount() - g_t0) / 1000.0;
  /* power LED: red, amber, green */
  COLORREF led = e < 0.6 ? RGB(239, 68, 68) : e < 1.2 ? RGB(245, 158, 11) : RGB(34, 197, 94);
  if (g_failed) led = RGB(239, 68, 68);
  HBRUSH lb = CreateSolidBrush(led), glow = CreateSolidBrush(RGB(GetRValue(led) / 5, GetGValue(led) / 5, GetBValue(led) / 5));
  SelectObject(mem, GetStockObject(NULL_PEN));
  SelectObject(mem, glow); Ellipse(mem, W / 2 - 15, 26, W / 2 + 15, 56);
  SelectObject(mem, lb); Ellipse(mem, W / 2 - 7, 34, W / 2 + 7, 48);
  DeleteObject(lb); DeleteObject(glow);
  SetBkMode(mem, TRANSPARENT);
  /* logo: letters appear one by one, widely spaced */
  SelectObject(mem, g_big);
  const wchar_t *logo = L"AUDIO MIXER"; int n = (int)wcslen(logo), x = W / 2 - (n * 22) / 2;
  int shown = e < 0.7 ? 0 : (int)((e - 0.7) / 0.07); if (shown > n) shown = n;
  SetTextColor(mem, RGB(255, 255, 255));
  for (int i = 0; i < shown; i++) { wchar_t c = logo[i]; TextOutW(mem, x + i * 22, 70, &c, 1); }
  SelectObject(mem, g_small);
  SetTextColor(mem, RGB(100, 116, 139)); TextOutW(mem, W / 2 - 40, 108, L"TITAN STAGE", 11);
  /* OS boot lines tick to OK */
  for (int i = 0; i < 6; i++) {
    if (e < 1.0 + i * 0.28) break;
    SetTextColor(mem, RGB(148, 163, 184)); TextOutW(mem, W / 2 - 110, 138 + i * 17, g_lines[i], (int)wcslen(g_lines[i]));
    SetTextColor(mem, RGB(34, 197, 94)); TextOutW(mem, W / 2 + 90, 138 + i * 17, L"OK", 2);
  }
  /* progress bar */
  double pr = (e * 1000 / MIN_MS) * 0.75 + (e * 1000 > MIN_MS ? (e - MIN_MS / 1000.0) * 0.02 : 0);
  if (pr > 0.95) pr = 0.95;
  if (g_up && e * 1000 >= MIN_MS) pr = 1.0;
  RECT track = { W / 2 - 110, H - 34, W / 2 + 110, H - 31 }, fill = track; fill.right = track.left + (LONG)((track.right - track.left) * pr);
  HBRUSH tb = CreateSolidBrush(RGB(15, 23, 42)), fb = CreateSolidBrush(g_failed ? RGB(239, 68, 68) : RGB(34, 197, 94));
  FillRect(mem, &track, tb); FillRect(mem, &fill, fb); DeleteObject(tb); DeleteObject(fb);
  SetTextColor(mem, g_failed ? RGB(239, 68, 68) : RGB(100, 116, 139));
  const wchar_t *msg = g_failed ? L"The local server did not start" : g_up ? L"Opening the mixer ..." : L"Starting the local server ...";
  TextOutW(mem, W / 2 - (int)wcslen(msg) * 3, H - 24, msg, (int)wcslen(msg));
  BitBlt(dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
  SelectObject(mem, old); DeleteObject(bm); DeleteDC(mem); EndPaint(h, &ps);
}

static int show_splash(HINSTANCE inst);
/* opens the mixer page: the default browser, then Explorer, then "start"; when none works the address is shown (the server itself is running) */
static void open_page(void) {
  const wchar_t *url = L"http://localhost:8765/";
  if ((INT_PTR)ShellExecuteW(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL) > 32) return;
  if ((INT_PTR)ShellExecuteW(NULL, L"open", L"explorer.exe", url, NULL, SW_SHOWNORMAL) > 32) return;
  wchar_t line[128]; STARTUPINFOW si; PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof si); si.cb = sizeof si;
  _snwprintf(line, 128, L"cmd.exe /c start \"\" %ls", url); line[127] = 0;
  if (CreateProcessW(NULL, line, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return; }
  MessageBoxW(NULL, L"Audio Mixer is running, but no web browser could be opened.\n\nOpen http://localhost:8765/ in your browser.", L"Audio Mixer", MB_ICONINFORMATION | MB_OK);
}
static void finish_open(HINSTANCE inst) {
  if (show_splash(inst)) open_page();
  else MessageBoxW(NULL, L"The local server did not answer in time.\n\nAnother program may be using port 8765, or the Visual C++ runtime is missing. Run \"Audio Mixer diagnostics\" from the Start Menu and try again.", L"Audio Mixer", MB_ICONWARNING | MB_OK);
}

static LRESULT CALLBACK splash_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_PAINT) { paint(h); return 0; }
  if (m == WM_ERASEBKGND) return 1;
  if (m == WM_TIMER) {
    DWORD ms = GetTickCount() - g_t0;
    if (!g_up && !g_failed && ms - g_poll >= 150) { g_poll = ms; if (server_current()) g_up = 1; }   /* poll every 150 ms: the check waits up to ~300 ms */
    if (!g_up && ms > MAX_MS) g_failed = 1;
    InvalidateRect(h, NULL, FALSE);
    if (g_failed && ms > MAX_MS + 3500) DestroyWindow(h);
    else if (g_up && ms >= MIN_MS + 400) DestroyWindow(h);
    return 0;
  }
  if (m == WM_KEYDOWN && w == VK_ESCAPE) { DestroyWindow(h); return 0; }
  if (m == WM_LBUTTONDOWN && g_up) { DestroyWindow(h); return 0; }
  if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
  return DefWindowProcW(h, m, w, l);
}

/* shows the screen until the server answers (and the animation had its time), returns 1 when the server is up */
static int show_splash(HINSTANCE inst) {
  WNDCLASSW wc; ZeroMemory(&wc, sizeof wc);
  wc.lpfnWndProc = splash_proc; wc.hInstance = inst; wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW); wc.lpszClassName = L"AudioMixerBoot";
  RegisterClassW(&wc);
  g_big = CreateFontW(34, 0, 0, 0, FW_BLACK, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
  g_small = CreateFontW(13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
  int W = 460, H = 290, sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
  HWND h = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"AudioMixerBoot", L"Audio Mixer", WS_POPUP | WS_VISIBLE, (sx - W) / 2, (sy - H) / 2, W, H, NULL, NULL, inst, NULL);
  if (!h) return server_up();
  g_t0 = GetTickCount(); SetTimer(h, 1, 33, NULL); SetForegroundWindow(h);
  MSG msg; while (GetMessageW(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  DeleteObject(g_big); DeleteObject(g_small);
  return g_up;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
  (void)prev; (void)show;
  wchar_t dir[MAX_PATH * 2];
  if (!GetModuleFileNameW(NULL, dir, MAX_PATH * 2)) return 1;
  wchar_t *slash = wcsrchr(dir, L'\\');
  if (!slash) return 1;
  slash[1] = 0;                                                  /* dir = install folder with trailing backslash */
  wcsncpy(g_dir, dir, ARRAYSIZE(g_dir) - 1); g_dir[ARRAYSIZE(g_dir) - 1] = 0;   /* kept for the tray icon's own start/stop, after this function returns to show_splash */

  if (cmd && wcsstr(cmd, L"/plugins")) {
    wchar_t profile[MAX_PATH], folder[MAX_PATH + 32];
    if (!GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH)) return 1;
    _snwprintf(folder, MAX_PATH + 32, L"%ls\\AudioMixerPlugins", profile);
    folder[MAX_PATH + 31] = 0;
    CreateDirectoryW(folder, NULL);                              /* fine if it already exists */
    ShellExecuteW(NULL, L"open", folder, NULL, NULL, SW_SHOWNORMAL);
    return 0;
  }

  int open = cmd && wcsstr(cmd, L"/open") != NULL;              /* start-up screen, then the browser */
  WSADATA wsa; if (open) WSAStartup(MAKEWORD(2, 2), &wsa);
  HANDLE once = NULL;
  if (!open) {                                                   /* login autostart: once per session, no window */
    once = CreateMutexW(NULL, FALSE, L"Local\\AudioMixerServerLauncher");
    if (once && GetLastError() == ERROR_ALREADY_EXISTS) return 0;
  }
  wchar_t line[MAX_PATH * 4 + 16];
  /* /open runs the client: it ends an OLDER server still running on the port (which would show the old page), starts the current one, and exits
   * when a current one already runs (--ensure). The login autostart starts the server directly. */
  if (open) _snwprintf(line, MAX_PATH * 4 + 16, L"\"%lsruntime\\node.exe\" \"%lsclient\\cli.js\" --no-open --ensure", dir, dir);
  else _snwprintf(line, MAX_PATH * 4 + 16, L"\"%lsruntime\\node.exe\" \"%lsbridge\\server.js\"", dir, dir);
  line[MAX_PATH * 4 + 15] = 0;
  STARTUPINFOW si; PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof si); si.cb = sizeof si;
  if (!CreateProcessW(NULL, line, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, dir, &si, &pi)) {
    MessageBoxW(NULL, L"Audio Mixer could not start its local server (runtime\\node.exe is missing). Reinstall Audio Mixer.", L"Audio Mixer", MB_ICONERROR | MB_OK);
    return 1;
  }
  CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
  if (open) { finish_open(inst); run_tray(inst); }
  return 0;
}
