/* Audio Mixer setup bootstrapper (the "Audio Mixer-<version>.exe").
 *
 * A small signed program that carries the Windows Installer package (.msi) as a payload, checks its SHA-256 and runs msiexec on it.
 * Layout of the finished file:  [this program (PE)] [MSI payload] [96-byte trailer] [Authenticode signature added by signing]
 * Trailer: "AMIXSETUPv1" padded to 16 bytes | payload size (u64 LE) | SHA-256 of the payload (32 bytes) | product code "{GUID}" (40 bytes, NUL padded)
 *
 *   Audio Mixer-1.4.0.exe              install (shows the Windows Installer wizard)
 *   Audio Mixer-1.4.0.exe /quiet       silent install (also /S, /qn), /passive shows progress only
 *   Audio Mixer-1.4.0.exe /uninstall   remove Audio Mixer (same as Settings > Apps > Audio Mixer > Uninstall)
 *   Audio Mixer-1.4.0.exe /scan        after installing, run the verification scan (file hashes, signature, Microsoft Defender)
 *   Audio Mixer-1.4.0.exe /novcredist  do not install the Microsoft Visual C++ runtime that the native audio module needs (see below)
 *   Audio Mixer-1.4.0.exe /noplugins  do not install the VST plugin host;  /nohelpers  skip the native Windows device helpers
 *   Audio Mixer-1.4.0.exe /notools     no Start Menu tool shortcuts (license, plugins, drivers, update, diagnostics)
 *   Audio Mixer-1.4.0.exe /noshortcuts no Start Menu shortcuts at all;  /desktop  add the desktop shortcut;  /noautostart  do not start at login
 *   Anything else (for example INSTALLDIR="D:\Audio Mixer") is passed on to msiexec.
 * Build: x86_64-w64-mingw32-gcc / i686-w64-mingw32-gcc, see scripts/build-exe.js.
 */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <wincrypt.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define TRAILER_SIZE 96
#define TITLE L"Audio Mixer Setup"

static const char MAGIC[16] = "AMIXSETUPv1";

static void message(const wchar_t *text, UINT flags) { MessageBoxW(NULL, text, TITLE, flags | MB_OK | MB_SETFOREGROUND); }

/* where the PE file's own data ends: before the Authenticode signature when there is one */
static int64_t data_end(HANDLE f, int64_t size) {
  unsigned char hdr[4096]; DWORD got = 0;
  SetFilePointer(f, 0, NULL, FILE_BEGIN);
  if (!ReadFile(f, hdr, sizeof hdr, &got, NULL) || got < 0x100 || hdr[0] != 'M' || hdr[1] != 'Z') return size;
  uint32_t pe = *(uint32_t *)(hdr + 0x3c);
  if (pe + 0x100 > got || memcmp(hdr + pe, "PE\0\0", 4) != 0) return size;
  unsigned char *opt = hdr + pe + 24;
  uint16_t magic = *(uint16_t *)opt;
  unsigned char *dd = opt + (magic == 0x20b ? 112 : 96);          /* data directories */
  uint32_t secOff = *(uint32_t *)(dd + 4 * 8), secLen = *(uint32_t *)(dd + 4 * 8 + 4);   /* entry 4 = certificate table (file offset) */
  if (secLen && secOff && (int64_t)secOff < size) return secOff;
  return size;
}

static int sha256_of_file_range(HANDLE f, int64_t start, int64_t len, HANDLE out, unsigned char digest[32]) {
  HCRYPTPROV prov = 0; HCRYPTHASH h = 0; int ok = 0;
  static unsigned char buf[1 << 20];
  if (!CryptAcquireContextW(&prov, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return 0;
  if (!CryptCreateHash(prov, CALG_SHA_256, 0, 0, &h)) { CryptReleaseContext(prov, 0); return 0; }
  LARGE_INTEGER pos; pos.QuadPart = start;
  if (!SetFilePointerEx(f, pos, NULL, FILE_BEGIN)) goto done;
  for (int64_t left = len; left > 0;) {
    DWORD want = left > (int64_t)sizeof buf ? (DWORD)sizeof buf : (DWORD)left, got = 0, wrote = 0;
    if (!ReadFile(f, buf, want, &got, NULL) || got != want) goto done;
    if (!CryptHashData(h, buf, got, 0)) goto done;
    if (out != INVALID_HANDLE_VALUE && (!WriteFile(out, buf, got, &wrote, NULL) || wrote != got)) goto done;
    left -= got;
  }
  DWORD n = 32;
  ok = CryptGetHashParam(h, HP_HASHVAL, digest, &n, 0) && n == 32;
done:
  CryptDestroyHash(h); CryptReleaseContext(prov, 0);
  return ok;
}

static int install_dir(wchar_t *dir, DWORD cap) {
  HKEY k; DWORD type = 0, n = cap * sizeof(wchar_t);
  REGSAM sam = KEY_READ;
#ifdef REQUIRE_X64
  sam |= KEY_WOW64_64KEY;
#endif
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Audio Mixer", 0, sam, &k) != ERROR_SUCCESS) return 0;
  LONG r = RegQueryValueExW(k, L"InstallDir", NULL, &type, (BYTE *)dir, &n);
  RegCloseKey(k);
  return r == ERROR_SUCCESS && type == REG_SZ;
}

static int file_exists(const wchar_t *path) { DWORD a = GetFileAttributesW(path); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }

static int run(wchar_t *cmdline, int newConsole, DWORD *code);

/* The native audio module (Audify: ASIO / WASAPI) is built with Visual C++ and needs the Microsoft Visual C++ runtime. When it is missing,
 * install it with winget (Microsoft's own package manager, package Microsoft.VCRedist.2015+.x64 / .x86). A failure is not fatal: the mixer still
 * runs with browser audio, and the installer says where to get the runtime. */
static void ensure_vc_runtime(void) {
  wchar_t sys[MAX_PATH], dll[MAX_PATH + 40], line[512];
  if (!GetWindowsDirectoryW(sys, MAX_PATH)) return;
#ifdef REQUIRE_X64
  /* a 32-bit program sees System32 redirected: Sysnative is the real 64-bit folder */
  _snwprintf(dll, MAX_PATH + 40, L"%ls\\Sysnative\\vcruntime140_1.dll", sys);
  const wchar_t *id = L"Microsoft.VCRedist.2015+.x64";
#else
  _snwprintf(dll, MAX_PATH + 40, L"%ls\\System32\\vcruntime140.dll", sys);   /* 32-bit Windows: native; on 64-bit the x86 runtime sits in SysWOW64, which a 32-bit program sees as System32 */
  const wchar_t *id = L"Microsoft.VCRedist.2015+.x86";
#endif
  dll[MAX_PATH + 39] = 0;
  if (file_exists(dll)) return;
  _snwprintf(line, 512, L"winget.exe install --id %ls -e --silent --accept-package-agreements --accept-source-agreements", id);
  line[511] = 0;
  DWORD code = 0;
  if (!run(line, 0, &code) || code != 0)
    message(L"The Microsoft Visual C++ runtime that the native audio module (ASIO) needs could not be installed automatically.\n\nAudio Mixer is installed and works with browser audio. For ASIO / WASAPI audio install it from https://aka.ms/vs/17/release/vc_redist.x64.exe (x86: vc_redist.x86.exe).", MB_ICONINFORMATION);
}

static int run(wchar_t *cmdline, int newConsole, DWORD *code) {
  STARTUPINFOW si; PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof si); si.cb = sizeof si;
  if (!CreateProcessW(NULL, cmdline, NULL, NULL, FALSE, newConsole ? CREATE_NEW_CONSOLE : 0, NULL, NULL, &si, &pi)) return 0;
  if (code) { WaitForSingleObject(pi.hProcess, INFINITE); GetExitCodeProcess(pi.hProcess, code); }
  CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
  return 1;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
  (void)inst; (void)prev; (void)cmd; (void)show;
  int argc = 0; wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  int quiet = 0, passive = 0, scan = 0, uninstall = 0, novc = 0;
  int noplug = 0, nohelp = 0, notools = 0, noshort = 0, desktop = 0, noauto = 0;
  wchar_t extra[2048] = L"";
  for (int i = 1; argv && i < argc; i++) {
    if (!_wcsicmp(argv[i], L"/quiet") || !_wcsicmp(argv[i], L"/S") || !_wcsicmp(argv[i], L"/qn") || !_wcsicmp(argv[i], L"-quiet")) quiet = 1;
    else if (!_wcsicmp(argv[i], L"/passive")) passive = 1;
    else if (!_wcsicmp(argv[i], L"/scan")) scan = 1;
    else if (!_wcsicmp(argv[i], L"/novcredist")) novc = 1;
    else if (!_wcsicmp(argv[i], L"/noplugins")) noplug = 1;
    else if (!_wcsicmp(argv[i], L"/nohelpers")) nohelp = 1;
    else if (!_wcsicmp(argv[i], L"/notools")) notools = 1;
    else if (!_wcsicmp(argv[i], L"/noshortcuts")) noshort = 1;
    else if (!_wcsicmp(argv[i], L"/desktop")) desktop = 1;
    else if (!_wcsicmp(argv[i], L"/noautostart")) noauto = 1;
    else if (!_wcsicmp(argv[i], L"/uninstall") || !_wcsicmp(argv[i], L"/remove")) uninstall = 1;
    else if (!wcscmp(argv[i], L"/?") || !_wcsicmp(argv[i], L"/help")) {
      message(L"Audio Mixer setup\n\n/quiet  silent install\n/passive  progress only\n/uninstall  remove Audio Mixer\n/scan  run the verification scan after installing\n/novcredist  skip the Visual C++ runtime check\n/noplugins  no VST plugin host\n/nohelpers  no native device helpers\n/notools  no tool shortcuts\n/noshortcuts  no Start Menu shortcuts\n/desktop  add a desktop shortcut\n/noautostart  do not start at login\nPROPERTY=value  passed to Windows Installer (for example INSTALLDIR=\"D:\\Audio Mixer\")", MB_ICONINFORMATION);
      return 0;
    } else {
      /* quote property values that contain spaces: NAME=value with spaces -> NAME="value" */
      wchar_t item[600]; const wchar_t *eq = wcschr(argv[i], L'=');
      if (eq && wcschr(eq, L' ')) _snwprintf(item, 600, L" %.*ls=\"%ls\"", (int)(eq - argv[i]), argv[i], eq + 1);
      else _snwprintf(item, 600, wcschr(argv[i], L' ') ? L" \"%ls\"" : L" %ls", argv[i]);
      item[599] = 0;
      if (wcslen(extra) + wcslen(item) < 2000) wcscat(extra, item);
    }
  }

  /* feature switches -> ADDLOCAL (only when one was used; the default install keeps the MSI defaults) */
  if (noplug || nohelp || notools || noshort || desktop || noauto) {
    wchar_t add[300] = L" ADDLOCAL=Main";
    if (!noshort) wcscat(add, L",Shortcuts");
    if (!noshort && !notools) wcscat(add, L",Tools");
    if (!noplug) wcscat(add, L",PluginHost");
    if (!nohelp) wcscat(add, L",WinHelpers");
    if (!noauto) wcscat(add, L",Autostart");
    if (desktop) wcscat(add, L",Desktop");
    if (!wcsstr(extra, L"ADDLOCAL=") && wcslen(extra) + wcslen(add) < 2000) wcscat(extra, add);
  }

  /* open ourselves and find the payload */
  wchar_t self[MAX_PATH * 2];
  if (!GetModuleFileNameW(NULL, self, MAX_PATH * 2)) { message(L"Cannot find the setup file.", MB_ICONERROR); return 1; }
  HANDLE f = CreateFileW(self, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
  if (f == INVALID_HANDLE_VALUE) { message(L"Cannot read the setup file.", MB_ICONERROR); return 1; }
  LARGE_INTEGER sz; GetFileSizeEx(f, &sz);
  int64_t end = data_end(f, sz.QuadPart);
  unsigned char tr[TRAILER_SIZE]; DWORD got = 0; LARGE_INTEGER pos; pos.QuadPart = end - TRAILER_SIZE;
  if (end < TRAILER_SIZE || !SetFilePointerEx(f, pos, NULL, FILE_BEGIN) || !ReadFile(f, tr, TRAILER_SIZE, &got, NULL) || got != TRAILER_SIZE || memcmp(tr, MAGIC, 16) != 0) {
    message(L"This setup file is damaged (no installer package inside). Download it again and check its SHA-256.", MB_ICONERROR); CloseHandle(f); return 1;
  }
  uint64_t psize = *(uint64_t *)(tr + 16);
  unsigned char *want = tr + 24; char product[41]; memcpy(product, tr + 56, 40); product[40] = 0;
  wchar_t code[48]; MultiByteToWideChar(CP_ACP, 0, product, -1, code, 48);

  DWORD exitCode = 0; wchar_t line[4096];
  if (uninstall) {
    CloseHandle(f);
    _snwprintf(line, 4096, L"msiexec.exe /x %ls%ls%ls", code, quiet ? L" /qn" : passive ? L" /passive" : L" /qb", extra);
    line[4095] = 0;
    if (!run(line, 0, &exitCode)) { message(L"Cannot start Windows Installer (msiexec).", MB_ICONERROR); return 1; }
    return (int)exitCode;
  }

#ifdef REQUIRE_X64
  { BOOL wow = FALSE; typedef BOOL (WINAPI *IW)(HANDLE, PBOOL);
    IW fn = (IW)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process");
    if (sizeof(void *) == 4 && !(fn && fn(GetCurrentProcess(), &wow) && wow)) { message(L"This setup is for 64-bit Windows. Use the x86 installer (.msi) on 32-bit Windows.", MB_ICONERROR); CloseHandle(f); return 1; } }
#endif

  /* copy the payload to a temp file while hashing it, and refuse a payload that does not match the hash stored at build time */
  wchar_t tmpdir[MAX_PATH], msi[MAX_PATH + 64];
  GetTempPathW(MAX_PATH, tmpdir);
  _snwprintf(msi, MAX_PATH + 64, L"%lsAudioMixer-%lu.msi", tmpdir, (unsigned long)GetTickCount());
  HANDLE out = CreateFileW(msi, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, NULL);
  if (out == INVALID_HANDLE_VALUE) { message(L"Cannot write to the temporary folder.", MB_ICONERROR); CloseHandle(f); return 1; }
  unsigned char digest[32];
  int ok = sha256_of_file_range(f, end - TRAILER_SIZE - (int64_t)psize, (int64_t)psize, out, digest) && memcmp(digest, want, 32) == 0;
  CloseHandle(out); CloseHandle(f);
  if (!ok) { DeleteFileW(msi); message(L"The installer package failed its SHA-256 check: the file is damaged or was changed. Download it again from the official release and compare its SHA-256.", MB_ICONERROR); return 1; }

  if (!novc) ensure_vc_runtime();
  _snwprintf(line, 4096, L"msiexec.exe /i \"%ls\"%ls%ls", msi, quiet ? L" /qn" : passive ? L" /passive" : L"", extra);
  line[4095] = 0;
  if (!run(line, 0, &exitCode)) { DeleteFileW(msi); message(L"Cannot start Windows Installer (msiexec).", MB_ICONERROR); return 1; }
  DeleteFileW(msi);
  if ((exitCode == 0 || exitCode == 3010) && scan) {
    wchar_t dir[MAX_PATH];
    if (install_dir(dir, MAX_PATH)) {
      _snwprintf(line, 4096, L"\"%lsruntime\\node.exe\" \"%lsclient\\cli.js\" verify --scan --pause", dir, dir);
      line[4095] = 0;
      run(line, 1, NULL);
    }
  }
  return (int)exitCode;
}
