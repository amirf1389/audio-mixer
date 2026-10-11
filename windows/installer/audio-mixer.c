/* audio-mixer.exe: the Audio Mixer command line (installed next to runtime\node.exe and client\cli.js, and put on PATH by the installer).
 *
 * A console program that runs <install folder>\runtime\node.exe <install folder>\client\cli.js with the arguments it was given, in the same console,
 * and returns its exit code. It is the Windows form of "npm run": no Node.js or npm has to be installed on the PC, the installed runtime is used.
 *   audio-mixer                     start the local server and open the mixer
 *   audio-mixer doctor | drivers | verify | license | plugins | update | service install|uninstall|status
 *   audio-mixer npm install audify  the bundled npm, inside the app's bridge folder
 *   audio-mixer uninstall           stop the server and remove Audio Mixer
 * Build: x86_64-w64-mingw32-gcc / i686-w64-mingw32-gcc -mconsole -municode, see scripts/build-exe.js.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

/* the command line after the program name, exactly as typed (quotes and spaces stay as they are) */
static const wchar_t *rest_of_command_line(const wchar_t *cl) {
  if (*cl == L'"') { cl++; while (*cl && *cl != L'"') cl++; if (*cl) cl++; }
  else while (*cl && *cl != L' ' && *cl != L'\t') cl++;
  while (*cl == L' ' || *cl == L'\t') cl++;
  return cl;
}

int wmain(void) {
  wchar_t dir[MAX_PATH * 2];
  if (!GetModuleFileNameW(NULL, dir, MAX_PATH * 2)) return 1;
  wchar_t *slash = wcsrchr(dir, L'\\');
  if (!slash) return 1;
  slash[1] = 0;                                                   /* dir = install folder with trailing backslash */

  const wchar_t *args = rest_of_command_line(GetCommandLineW());
  size_t need = wcslen(dir) * 2 + wcslen(args) + 64;
  wchar_t *line = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, need * sizeof(wchar_t));
  if (!line) return 1;
  _snwprintf(line, need, L"\"%lsruntime\\node.exe\" \"%lsclient\\cli.js\" %ls", dir, dir, args);
  line[need - 1] = 0;

  STARTUPINFOW si; PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof si); si.cb = sizeof si;
  SetConsoleCtrlHandler(NULL, TRUE);                               /* Ctrl+C goes to node.exe, which stops the server cleanly; this program just waits */
  if (!CreateProcessW(NULL, line, NULL, NULL, TRUE, 0, NULL, dir, &si, &pi)) {
    fwprintf(stderr, L"audio-mixer: cannot start %lsruntime\\node.exe (reinstall Audio Mixer)\n", dir);
    return 1;
  }
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
  return (int)code;
}
