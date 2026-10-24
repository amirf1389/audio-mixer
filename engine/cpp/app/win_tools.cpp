// Windows-only helpers of audiomixer-native, so one .exe can set a machine up:
//   --install-driver     writes the ASIO driver DLL that is embedded in this .exe (AM_EMBED_ASIO, see scripts/build-native.js) to
//                        %ProgramFiles%\Audio Mixer ASIO\ and registers it (DllRegisterServer): "Audio Mixer" then shows up in every ASIO host. Needs an administrator prompt.
//   --uninstall-driver   unregisters it and deletes the DLL
//   --install-flexasio FILE --sha256 HEX
//                        runs a FlexASIO installer (Inno Setup, https://github.com/dechamps/FlexASIO/releases) silently, but only when the SHA-256 of FILE matches
// Each prints what it did and returns the exit code (0 = done).
#ifdef _WIN32
#include <windows.h>
#include <olectl.h>
#include <wincrypt.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "win_tools.hpp"

#ifdef AM_EMBED_ASIO
#include "asio_blob.inc"          // static const unsigned char kAsioDll[]; static const unsigned kAsioDllSize; static const char kAsioDllName[]
#endif

namespace am {
namespace {
std::wstring widen(const std::string& s) { if (s.empty()) return std::wstring(); const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0); std::wstring w(static_cast<std::size_t>(n), L'\0'); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n); w.resize(static_cast<std::size_t>(n) - 1); return w; }

std::wstring driverDir() {
  wchar_t pf[MAX_PATH] = {0}; if (!GetEnvironmentVariableW(L"ProgramFiles", pf, MAX_PATH)) wcscpy(pf, L"C:\\Program Files");
  return std::wstring(pf) + L"\\Audio Mixer ASIO";
}

#ifdef AM_EMBED_ASIO
typedef HRESULT(__stdcall* RegFn)();
int callRegister(const std::wstring& dll, const char* fn) {
  HMODULE m = LoadLibraryW(dll.c_str());
  if (!m) { std::printf("cannot load %ls (error %lu)\n", dll.c_str(), static_cast<unsigned long>(GetLastError())); return 1; }
  RegFn f = reinterpret_cast<RegFn>(reinterpret_cast<void*>(GetProcAddress(m, fn)));
  const HRESULT r = f ? f() : E_NOTIMPL; FreeLibrary(m);
  if (FAILED(r)) { std::printf("%s failed (0x%08lx)%s\n", fn, static_cast<unsigned long>(r), r == SELFREG_E_CLASS ? ": run this from an administrator prompt (the driver is registered in HKLM)" : ""); return 1; }
  return 0;
}
int installDriver() {
  const std::wstring dir = driverDir(), dll = dir + L"\\" + widen(kAsioDllName);
  CreateDirectoryW(dir.c_str(), nullptr);
  HANDLE h = CreateFileW(dll.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) { std::printf("cannot write %ls (error %lu): run this from an administrator prompt\n", dll.c_str(), static_cast<unsigned long>(GetLastError())); return 1; }
  DWORD w = 0; const BOOL ok = WriteFile(h, kAsioDll, kAsioDllSize, &w, nullptr); CloseHandle(h);
  if (!ok || w != kAsioDllSize) { std::printf("cannot write %ls\n", dll.c_str()); return 1; }
  if (callRegister(dll, "DllRegisterServer")) return 1;
  std::printf("ASIO driver \"Audio Mixer\" installed: %ls (choose it in any ASIO program; audiomixer-native --list shows it)\n", dll.c_str());
  return 0;
}
int uninstallDriver() {
  const std::wstring dir = driverDir(), dll = dir + L"\\" + widen(kAsioDllName);
  if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) { std::printf("no driver installed at %ls\n", dll.c_str()); return 0; }
  if (callRegister(dll, "DllUnregisterServer")) return 1;
  DeleteFileW(dll.c_str()); RemoveDirectoryW(dir.c_str());
  std::printf("ASIO driver \"Audio Mixer\" removed\n");
  return 0;
}
#else
int installDriver() { std::printf("this audiomixer-native was built without the embedded ASIO driver (build it with scripts/build-native.js)\n"); return 1; }
int uninstallDriver() { return installDriver(); }
#endif

bool sha256Of(const std::wstring& file, std::string& hex) {
  HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  HCRYPTPROV prov = 0; HCRYPTHASH hash = 0; bool ok = false;
  if (CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) && CryptCreateHash(prov, CALG_SHA_256, 0, 0, &hash)) {
    std::vector<BYTE> buf(1 << 16); DWORD n = 0; ok = true;
    while (ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &n, nullptr) && n > 0) if (!CryptHashData(hash, buf.data(), n, 0)) { ok = false; break; }
    BYTE d[32]; DWORD len = 32;
    if (ok && CryptGetHashParam(hash, HP_HASHVAL, d, &len, 0) && len == 32) { static const char* hx = "0123456789abcdef"; hex.clear(); for (BYTE b : d) { hex += hx[b >> 4]; hex += hx[b & 15]; } } else ok = false;
  }
  if (hash) CryptDestroyHash(hash);
  if (prov) CryptReleaseContext(prov, 0);
  CloseHandle(h);
  return ok;
}
int installFlexAsio(const std::string& file, const std::string& want) {
  if (want.size() != 64) { std::printf("--install-flexasio needs --sha256 <64 hex characters> of the installer you checked on https://github.com/dechamps/FlexASIO/releases\n"); return 2; }
  std::string got; const std::wstring wf = widen(file);
  if (!sha256Of(wf, got)) { std::printf("cannot read %s\n", file.c_str()); return 1; }
  std::string lw = want; for (auto& c : lw) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (got != lw) { std::printf("SHA-256 mismatch: the file is %s, expected %s; nothing was run\n", got.c_str(), lw.c_str()); return 1; }
  std::wstring cmd = L"\"" + wf + L"\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART";
  STARTUPINFOW si; std::memset(&si, 0, sizeof si); si.cb = sizeof si; PROCESS_INFORMATION pi; std::memset(&pi, 0, sizeof pi);
  if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) { std::printf("cannot run the installer (error %lu)\n", static_cast<unsigned long>(GetLastError())); return 1; }
  WaitForSingleObject(pi.hProcess, 10 * 60 * 1000); DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
  std::printf(code == 0 ? "FlexASIO installed (checksum verified); audiomixer-native --list shows it\n" : "the FlexASIO installer exited with %lu\n", static_cast<unsigned long>(code));
  return code == 0 ? 0 : 1;
}
}  // namespace

bool runWinTool(int argc, char** argv, int& rc) {
  std::string flex, sha;
  for (int i = 1; i < argc; ++i) { const std::string a = argv[i]; if (a == "--install-flexasio" && i + 1 < argc) flex = argv[++i]; else if (a == "--sha256" && i + 1 < argc) sha = argv[++i]; }
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--install-driver") { rc = installDriver(); return true; }
    if (a == "--uninstall-driver") { rc = uninstallDriver(); return true; }
    if (a == "--install-flexasio") { rc = installFlexAsio(flex, sha); return true; }
  }
  return false;
}
}  // namespace am
#endif
