/* AudioMixerServer.exe: native launcher for the Audio Mixer local server (installed next to runtime\node.exe and bridge\server.js).
 *
 * It replaces what used to be a hidden script: a signed program that starts the bundled Node.js server without a console window.
 * Nothing is written anywhere; it only starts <install folder>\runtime\node.exe <install folder>\bridge\server.js.
 *   AudioMixerServer.exe             start the local server in the background (once; a second start does nothing)
 *   AudioMixerServer.exe /plugins    open the plugin folder %USERPROFILE%\AudioMixerPlugins (creating it if needed)
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
  (void)inst; (void)prev; (void)show;
  wchar_t dir[MAX_PATH * 2];
  if (!GetModuleFileNameW(NULL, dir, MAX_PATH * 2)) return 1;
  wchar_t *slash = wcsrchr(dir, L'\\');
  if (!slash) return 1;
  slash[1] = 0;                                                  /* dir = install folder with trailing backslash */

  if (cmd && wcsstr(cmd, L"/plugins")) {
    wchar_t profile[MAX_PATH], folder[MAX_PATH + 32];
    if (!GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH)) return 1;
    _snwprintf(folder, MAX_PATH + 32, L"%ls\\AudioMixerPlugins", profile);
    folder[MAX_PATH + 31] = 0;
    CreateDirectoryW(folder, NULL);                              /* fine if it already exists */
    ShellExecuteW(NULL, L"open", folder, NULL, NULL, SW_SHOWNORMAL);
    return 0;
  }

  HANDLE once = CreateMutexW(NULL, FALSE, L"Local\\AudioMixerServerLauncher");
  if (once && GetLastError() == ERROR_ALREADY_EXISTS) return 0;  /* already started in this session */

  wchar_t line[MAX_PATH * 4 + 16];
  _snwprintf(line, MAX_PATH * 4 + 16, L"\"%lsruntime\\node.exe\" \"%lsbridge\\server.js\"", dir, dir);
  line[MAX_PATH * 4 + 15] = 0;
  STARTUPINFOW si; PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof si); si.cb = sizeof si;
  if (!CreateProcessW(NULL, line, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, dir, &si, &pi)) {
    MessageBoxW(NULL, L"Audio Mixer could not start its local server (runtime\\node.exe is missing). Reinstall Audio Mixer.", L"Audio Mixer", MB_ICONERROR | MB_OK);
    return 1;
  }
  CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
  return 0;
}
