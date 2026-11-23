// SECURE ASIO LOADING (host side; no driver is changed). An ASIO driver is an in-process COM DLL: whatever it does runs inside this
// program. So a driver is only ever loaded when
//   * it is registered under HKLM\SOFTWARE\ASIO (64-bit or 32-bit view) with a well-formed CLSID, and
//   * HKLM\SOFTWARE\Classes\CLSID\{clsid}\InprocServer32 (machine-wide only: HKCU registrations, which any user process can write and
//     which CoCreateInstance would prefer, are ignored) names an existing DLL with an absolute local path inside a trusted folder
//     (System32 / SysWOW64, Program Files, Program Files (x86)), after a SAFE expansion of a few system variables only;
//   * the DLL is then loaded from exactly that verified path (LoadLibraryEx + DllGetClassObject), never through the COM lookup.
// Its Authenticode signature is checked offline (no revocation download that could block): an unsigned or invalid driver is still
// listed, marked "unsigned", and is not loaded (not even probed) until the user confirmed it once.
// A driver is selected only by a NAME from this enumerated list: a path, a DLL name or a CLSID that is not in the list (from a settings
// file, the remote protocol or the command line) selects nothing.
// A driver that crashes or hangs in a guarded call is marked FAULTED for this run; the runtime then falls back to WASAPI.
// Everything here except the Windows registry / WinVerifyTrust glue (backend_asio.cpp) is plain C++, tested on every system with a mock.
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace am {
namespace asiotrust {

enum class Sig { Valid, Unsigned, Invalid, Unknown };
const char* sigName(Sig s);   // "signed" | "unsigned" | "invalid signature" | "unknown"

// What the registry says about one driver, raw (name and CLSID from SOFTWARE\ASIO, the InprocServer32 default value as stored).
struct RegEntry { std::string name, clsid, inproc; bool expandSz = false; int bits = 64; };

// The machine as the enumeration sees it (the real one on Windows; a mock in the tests).
struct Env {
  virtual ~Env() = default;
  virtual std::vector<RegEntry> entries(int bits) = 0;                    // HKLM\SOFTWARE\ASIO in that view, with HKLM's InprocServer32
  virtual bool getVar(const std::string& name, std::string& value) = 0;   // the value of a system environment variable
  virtual bool fileExists(const std::string& path) = 0;
  virtual Sig signature(const std::string& path) = 0;                     // offline Authenticode check
};

struct Driver { std::string name, clsid, path; int bits = 64; Sig sig = Sig::Unknown; };
struct Rejected { std::string name, reason; };

// a CLSID in registry form: {8-4-4-4-12 hex digits}
bool validClsid(const std::string& s);
// %VAR% expansion limited to the system folders (SystemRoot, windir, ProgramFiles, ProgramFiles(x86), ProgramW6432, CommonProgramFiles,
// CommonProgramFiles(x86), CommonProgramW6432, SystemDrive). Any other variable (USERPROFILE, TEMP, APPDATA ...), an unclosed '%', or a
// value that itself holds '%' fails: false.
bool expandSafe(const std::string& raw, const std::function<bool(const std::string&, std::string&)>& getVar, std::string& out);
// An absolute local path, cleaned: '/' -> '\', quotes removed, no "..", no UNC (\\server), no device path (\\?\, \\.\), no relative path,
// no ':' after the drive (alternate data streams). false = refused (reason set).
bool normalizePath(const std::string& raw, std::string& out, std::string& reason);
// The trusted folders, from the same safe variables: <SystemRoot>\System32, <SystemRoot>\SysWOW64, Program Files, Program Files (x86).
std::vector<std::string> trustedRoots(const std::function<bool(const std::string&, std::string&)>& getVar);
// path (normalized) lies inside one of the roots (case-insensitive, whole folder names)
bool insideTrusted(const std::string& path, const std::vector<std::string>& roots);

// A name or text a driver reports, made safe to show and to store: valid UTF-8 (bad bytes -> '?'), no control characters, trimmed,
// at most maxBytes bytes (never cut inside a character). Empty input -> fallback.
std::string sanitizeText(const char* data, std::size_t len, std::size_t maxBytes, const char* fallback = "");
inline std::string sanitizeText(const std::string& s, std::size_t maxBytes, const char* fallback = "") { return sanitizeText(s.data(), s.size(), maxBytes, fallback); }
constexpr std::size_t kMaxDriverName = 64, kMaxChannelName = 32, kMaxErrorText = 124;

// Driver-reported numbers, checked before use
constexpr long kMaxChannels = 512, kMaxBuffer = 32768;
long clampChannels(long n);   // < 0 -> 0, > kMaxChannels -> kMaxChannels
struct BufferSizes { long mn = 0, mx = 0, pref = 0, gran = 0; };
// a sane set from getBufferSize: 1 <= min <= max <= kMaxBuffer, min <= preferred <= max, granularity -1 (powers of two), 0 (only min==max
// meaningful) or 1..max; false when nothing usable came back (the stream is then refused)
bool validateBuffers(BufferSizes& b);
bool validRate(double r);     // finite, 8000 .. 1100000 Hz
long clampLatency(long frames);   // 0 .. 1 << 20

// The enumeration: every registered driver that passes the checks; the others go to `rejected` with the reason.
std::vector<Driver> enumerate(Env& env, std::vector<Rejected>* rejected = nullptr);
// The driver a request means. Only names (or CLSIDs) of drivers in `list` are accepted: exact name (case-insensitive) first, then exact
// CLSID, then a part of a name. Anything that looks like a path or a DLL (\ / : .dll), empty or over-long text selects nothing (nullptr).
// want == "" picks the first driver.
const Driver* select(const std::vector<Driver>& list, const std::string& want, std::string* why = nullptr);

// FAULTED drivers (crashed or hung in a guarded call): kept for this run of the program, by CLSID
void markFaulted(const std::string& clsid, const std::string& what);
bool isFaulted(const std::string& clsid);
std::string faultText(const std::string& clsid);
void clearFaulted();
// UNSIGNED drivers the user confirmed (by CLSID + path, so a swapped DLL needs a new confirmation); the console restores them at start
void allowUnsigned(const std::string& clsid, const std::string& path, bool allow = true);
bool unsignedAllowed(const std::string& clsid, const std::string& path);
// may the driver be loaded now? (signed, or confirmed; and not faulted) why = the reason when not
bool loadAllowed(const Driver& d, std::string* why = nullptr);

}  // namespace asiotrust
}  // namespace am
