// Windows-only helpers of audiomixer-native, so one .exe can set a machine up:
//   --install-driver     writes the ASIO driver DLL that is embedded in this .exe (AM_EMBED_ASIO, see scripts/build-native.js) to
//                        %ProgramFiles%\Audio Mixer ASIO\ and registers it (DllRegisterServer): "Audio Mixer" then shows up in every ASIO host. Needs an administrator prompt.
//   --uninstall-driver   unregisters it and deletes the DLL
//   --install-flexasio FILE --sha256 HEX
//                        runs a FlexASIO installer (Inno Setup, https://github.com/dechamps/FlexASIO/releases) silently, but only when the SHA-256 of FILE matches
//   --update [--check] [--install-driver] [--url MANIFEST]
//                        self-update: fetches the signed update manifest (ECDSA P-256 / SHA-256, checked here against the project's public key, no Node.js), and when a newer
//                        native program exists for this system downloads it, checks its SHA-256, and replaces this running .exe in place (the old one stays as <name>.old; a running
//                        .exe can be renamed but not overwritten). --install-driver then runs the NEW program's embedded driver install. --check only reports.
// Each prints what it did and returns the exit code (0 = done).
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <olectl.h>
#include <wincrypt.h>
#include <winhttp.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "update_logic.hpp"
#include "win_tools.hpp"

#ifdef AM_EMBED_UPDATE_KEY
#include "update_key.inc"         // static const unsigned char kUpdateKeyX[32], kUpdateKeyY[32]: the project's public key (bridge/license-public.json)
#endif
#ifndef AM_VERSION
#define AM_VERSION "0.0.0"
#endif
#ifdef AM_EMBED_ASIO
#include "asio_blob.inc"          // static const unsigned char kAsioDll[]; static const unsigned kAsioDllSize; static const char kAsioDllName[]
#endif

namespace am {
namespace {
std::wstring widen(const std::string& s) { if (s.empty()) return std::wstring(); const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0); std::wstring w(static_cast<std::size_t>(n), L'\0'); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n); w.resize(static_cast<std::size_t>(n) - 1); return w; }

#ifdef AM_EMBED_ASIO
std::wstring driverDir() {
  wchar_t pf[MAX_PATH] = {0}; if (!GetEnvironmentVariableW(L"ProgramFiles", pf, MAX_PATH)) wcscpy(pf, L"C:\\Program Files");
  return std::wstring(pf) + L"\\Audio Mixer ASIO";
}
#endif

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

// ───────── self-update ─────────
std::string narrow(const std::wstring& w) { if (w.empty()) return std::string(); const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr); std::string s(static_cast<std::size_t>(n), '\0'); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr); s.resize(static_cast<std::size_t>(n) - 1); return s; }

// HTTPS GET of an allowed host (redirects are followed by hand, each target is checked), at most maxBytes
bool httpGet(const std::string& first, std::vector<unsigned char>& out, std::size_t maxBytes, std::string& err) {
  std::string url = first;
  for (int hop = 0; hop < 6; ++hop) {
    if (!upd::hostAllowed(url)) { err = "the address is not on the list of hosts the releases are served from: " + url; return false; }
    const std::wstring w = widen(url); wchar_t host[256] = {0}, path[2048] = {0}, extra[2048] = {0};
    URL_COMPONENTSW uc; std::memset(&uc, 0, sizeof uc); uc.dwStructSize = sizeof uc;
    uc.lpszHostName = host; uc.dwHostNameLength = 255; uc.lpszUrlPath = path; uc.dwUrlPathLength = 2047; uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 2047;
    if (!WinHttpCrackUrl(w.c_str(), 0, 0, &uc)) { err = "bad address"; return false; }
    HINTERNET ses = WinHttpOpen(L"audio-mixer-native-update", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { err = "WinHttpOpen failed"; return false; }
    HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0), req = con ? WinHttpOpenRequest(con, L"GET", (std::wstring(path) + extra).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    auto close = [&]() { if (req) WinHttpCloseHandle(req); if (con) WinHttpCloseHandle(con); WinHttpCloseHandle(ses); };
    if (!req) { close(); err = "cannot open the request"; return false; }
    DWORD pol = WINHTTP_OPTION_REDIRECT_POLICY_NEVER; WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &pol, sizeof pol);
    WinHttpSetTimeouts(req, 15000, 15000, 30000, 120000);
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(req, nullptr)) { err = "no answer (error " + std::to_string(static_cast<unsigned long>(GetLastError())) + "): is there an internet connection?"; close(); return false; }
    DWORD status = 0, sz = sizeof status; WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    if (status >= 300 && status < 400) {
      wchar_t loc[2048] = {0}; DWORD ls = sizeof loc - sizeof(wchar_t);
      const bool got = WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, loc, &ls, WINHTTP_NO_HEADER_INDEX) != 0; close();
      if (!got) { err = "redirect without a target"; return false; }
      const std::string l = narrow(loc); url = l.compare(0, 1, "/") == 0 ? "https://" + upd::hostOf(url) + l : l; continue;
    }
    if (status != 200) { close(); err = "the server answered " + std::to_string(static_cast<unsigned long>(status)); return false; }
    out.clear(); DWORD avail = 0;
    while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
      if (out.size() + avail > maxBytes) { close(); err = "the download is larger than allowed"; return false; }
      const std::size_t at = out.size(); out.resize(at + avail); DWORD got = 0;
      if (!WinHttpReadData(req, out.data() + at, avail, &got)) { close(); err = "the download was interrupted"; return false; }
      out.resize(at + got);
    }
    close(); return true;
  }
  err = "too many redirects"; return false;
}

// ECDSA P-256 over SHA-256(payload), signature = r || s (64 bytes), the project's public key
bool verifySignature(const std::string& payload, const std::vector<unsigned char>& sig, std::string& err) {
#ifndef AM_EMBED_UPDATE_KEY
  (void)payload; (void)sig; err = "this program was built without the project's public key"; return false;
#else
  if (sig.size() != 64) { err = "the signature is not 64 bytes"; return false; }
  BCRYPT_ALG_HANDLE sha = nullptr, ec = nullptr; BCRYPT_HASH_HANDLE h = nullptr; BCRYPT_KEY_HANDLE key = nullptr; bool ok = false; BYTE digest[32];
  if (BCryptOpenAlgorithmProvider(&sha, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 && BCryptCreateHash(sha, &h, nullptr, 0, nullptr, 0, 0) >= 0
      && BCryptHashData(h, reinterpret_cast<PUCHAR>(const_cast<char*>(payload.data())), static_cast<ULONG>(payload.size()), 0) >= 0 && BCryptFinishHash(h, digest, 32, 0) >= 0
      && BCryptOpenAlgorithmProvider(&ec, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) >= 0) {
    std::vector<BYTE> blob(sizeof(BCRYPT_ECCKEY_BLOB) + 64); BCRYPT_ECCKEY_BLOB* hd = reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(blob.data()); hd->dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC; hd->cbKey = 32;
    std::memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB), kUpdateKeyX, 32); std::memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB) + 32, kUpdateKeyY, 32);
    if (BCryptImportKeyPair(ec, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key, blob.data(), static_cast<ULONG>(blob.size()), 0) >= 0)
      ok = BCryptVerifySignature(key, nullptr, digest, 32, const_cast<PUCHAR>(sig.data()), 64, 0) >= 0;
  }
  if (key) BCryptDestroyKey(key);
  if (h) BCryptDestroyHash(h);
  if (ec) BCryptCloseAlgorithmProvider(ec, 0);
  if (sha) BCryptCloseAlgorithmProvider(sha, 0);
  if (!ok) err = "the update information is not signed by the Audio Mixer publisher; ignored";
  return ok;
#endif
}

bool writeFile(const std::wstring& f, const std::vector<unsigned char>& b) {
  HANDLE h = CreateFileW(f.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr); if (h == INVALID_HANDLE_VALUE) return false;
  DWORD w = 0; const BOOL ok = WriteFile(h, b.data(), static_cast<DWORD>(b.size()), &w, nullptr); CloseHandle(h); return ok && w == b.size();
}

int selfUpdate(bool checkOnly, bool thenDriver, const std::string& urlArg) {
  const std::string manifestUrl = urlArg.empty() ? "https://raw.githubusercontent.com/amirf1389/audio-mixer/main/releases/update.json" : urlArg;
  std::vector<unsigned char> body; std::string err;
  std::printf("audiomixer-native v%s: checking %s\n", AM_VERSION, manifestUrl.c_str());
  if (!httpGet(manifestUrl, body, 1 << 20, err)) { std::printf("cannot read the update information: %s\n", err.c_str()); return 1; }
  const std::string env(body.begin(), body.end()); std::string payload, sigText, latest, files, entry;
  if (!upd::getString(env, "payload", payload) || !upd::getString(env, "signature", sigText)) { std::printf("the update information is not in the expected format\n"); return 1; }
  if (!verifySignature(payload, upd::b64url(sigText), err)) { std::printf("%s\n", err.c_str()); return 1; }
  if (!upd::getString(payload, "version", latest) || !upd::getObject(payload, "files", files)) { std::printf("the update information has no version / files\n"); return 1; }
#ifdef _WIN64
  const char* key = "win-x64-native";
#else
  const char* key = "win-x86-native";
#endif
  if (upd::cmpVersion(latest, AM_VERSION) <= 0) { std::printf("up to date (published: v%s)\n", latest.c_str()); return 0; }
  if (!upd::getObject(files, key, entry)) { std::printf("v%s is published, but without a native program for this system (%s); get the setup from the release page\n", latest.c_str(), key); return 1; }
  std::string url, sha, name; upd::getString(entry, "url", url); upd::getString(entry, "sha256", sha); upd::getString(entry, "name", name);
  if (!upd::hostAllowed(url) || !upd::isHex64(sha)) { std::printf("the published entry for %s is not usable\n", key); return 1; }
  std::printf("v%s is available (this is v%s): %s\n", latest.c_str(), AM_VERSION, name.c_str());
  if (checkOnly) return 0;
  std::vector<unsigned char> exe;
  if (!httpGet(url, exe, 200u << 20, err)) { std::printf("download failed: %s\n", err.c_str()); return 1; }
  if (exe.size() < 4096 || exe[0] != 'M' || exe[1] != 'Z') { std::printf("the download is not a Windows program; nothing was changed\n"); return 1; }
  wchar_t selfBuf[MAX_PATH * 2] = {0}; if (!GetModuleFileNameW(nullptr, selfBuf, MAX_PATH * 2)) { std::printf("cannot find this program's path\n"); return 1; }
  const std::wstring self = selfBuf, neu = self + L".new", old = self + L".old";
  if (!writeFile(neu, exe)) { std::printf("cannot write %ls (is the folder writable? run from an administrator prompt)\n", neu.c_str()); return 1; }
  std::string got; std::string want = sha; for (auto& c : want) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (!sha256Of(neu, got) || got != want) { DeleteFileW(neu.c_str()); std::printf("SHA-256 mismatch: the download was discarded and nothing was changed\n"); return 1; }
  DeleteFileW(old.c_str());                                                       // the one kept by the previous update
  if (!MoveFileExW(self.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(neu.c_str()); std::printf("cannot move this program aside (error %lu)\n", static_cast<unsigned long>(GetLastError())); return 1; }
  if (!MoveFileExW(neu.c_str(), self.c_str(), 0)) { MoveFileExW(old.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING); std::printf("cannot put the new program in place; the old one was restored\n"); return 1; }
  std::printf("updated to v%s (checksum verified). The previous program is kept as %ls\n", latest.c_str(), old.c_str());
  if (!thenDriver) return 0;
  std::wstring cmd = L"\"" + self + L"\" --install-driver"; STARTUPINFOW si; std::memset(&si, 0, sizeof si); si.cb = sizeof si; PROCESS_INFORMATION pi; std::memset(&pi, 0, sizeof pi);
  if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) { std::printf("updated, but cannot start the new program to install its driver (error %lu)\n", static_cast<unsigned long>(GetLastError())); return 1; }
  WaitForSingleObject(pi.hProcess, 2 * 60 * 1000); DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
  return code == 0 ? 0 : 1;
}
}  // namespace

bool runWinTool(int argc, char** argv, int& rc) {
  std::string flex, sha, murl; bool upd = false, chk = false, drv = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--install-flexasio" && i + 1 < argc) flex = argv[++i]; else if (a == "--sha256" && i + 1 < argc) sha = argv[++i]; else if (a == "--url" && i + 1 < argc) murl = argv[++i];
    else if (a == "--update") upd = true; else if (a == "--check") chk = true; else if (a == "--install-driver") drv = true;
  }
  if (upd) { rc = selfUpdate(chk, drv, murl); return true; }                  // --update [--check] [--install-driver]
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
