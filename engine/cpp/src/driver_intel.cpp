#include "audiomixer/driver_intel.hpp"
#include "paths/paths.hpp"

#include <atomic>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace am::drv {

namespace {
const char* kPinReason = "Realtek onboard: its ASIO driver is known to be unreliable, so the Windows (WASAPI shared) endpoint of the same chip is used";
const std::pair<const char*, std::regex> kFamilies[] = {
  {"realtek", std::regex("realtek", std::regex::icase)}, {"conexant", std::regex("conexant|synaptics", std::regex::icase)}, {"idt", std::regex("\\bidt\\b|sigmatel|tempo", std::regex::icase)},
  {"via", std::regex("\\bvia\\b|vinyl", std::regex::icase)}, {"cirrus", std::regex("cirrus", std::regex::icase)}, {"intel", std::regex("intel.*(smart sound|display audio)", std::regex::icase)},
  {"amd", std::regex("\\bamd\\b.*(high definition|audio)", std::regex::icase)},
  {"behringer", std::regex("behringer|\\bumc ?[0-9]|\\bum ?2\\b|\\buca ?2[0-9][0-9]|usb audio codec|burr[- ]?brown|\\bpcm ?29[0-9][0-9]", std::regex::icase)},
  {"focusrite", std::regex("focusrite|scarlett", std::regex::icase)}, {"steinberg", std::regex("steinberg|yamaha ur\\b|\\bur[0-9]{2}", std::regex::icase)}, {"presonus", std::regex("presonus|audiobox", std::regex::icase)},
  {"motu", std::regex("\\bmotu\\b", std::regex::icase)}, {"rme", std::regex("\\brme\\b|fireface|babyface", std::regex::icase)}, {"audient", std::regex("audient|\\bid4\\b|\\bid14\\b", std::regex::icase)}, {"m-audio", std::regex("m-audio|m-track", std::regex::icase)},
};
const std::regex kWrappers("asio4all|flexasio|generic low latency|asio2wasapi", std::regex::icase);
const std::regex kLoopback("stereo mix|loopback|what u hear|hands-free|headset", std::regex::icase);
const std::set<std::string> kGeneric = {"asio", "audio", "driver", "usb", "device", "speakers", "microphone", "line", "input", "output", "high", "definition", "the", "and", "for", "generic", "low", "latency", "array", "headphones", "codec", "wdm", "analogue", "analog", "digital", "interface", "sound", "card"};
std::set<std::string> words(const std::string& n) {
  std::set<std::string> out; std::string w;
  auto flush = [&]() { if (w.size() >= 3 && !kGeneric.count(w)) out.insert(w); w.clear(); };
  for (unsigned char c : n) { if (std::isalnum(c)) w += static_cast<char>(std::tolower(c)); else flush(); }
  flush(); return out;
}
std::string lower(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
std::string keyOf(const std::string& name) { std::string k = lower(name); std::string o; bool sp = false; for (char c : k) { if (c == ' ' || c == '\t') { if (!sp && !o.empty()) o += ' '; sp = true; } else { o += c; sp = false; } } while (!o.empty() && o.back() == ' ') o.pop_back(); if (o.size() > 120) o.resize(120); return o; }

// ---- the store: {"v":1,"locks":{key:{name,mode,device,reason,at}},"pins":{key:false}}
struct Store { std::map<std::string, Lock> locks; std::map<std::string, bool> pins; std::string fallback = "all"; bool loaded = false; };
Store g_store;
std::string jsonEsc(const std::string& s) { std::string o = "\""; for (unsigned char c : s) { if (c == '"') o += "\\\""; else if (c == '\\') o += "\\\\"; else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; } else o += static_cast<char>(c); } return o + "\""; }
// a tiny reader for our own file (flat strings / numbers / booleans two levels deep): tolerant, never throws
void load() {
  if (g_store.loaded) return;
  g_store.loaded = true;
  std::ifstream in(lockFile()); if (!in) return; std::stringstream ss; ss << in.rdbuf(); const std::string t = ss.str();
  auto str = [&](const std::string& obj, const char* key) { const std::string k = std::string("\"") + key + "\":"; const std::size_t p = obj.find(k); if (p == std::string::npos) return std::string(); std::size_t i = p + k.size(); if (i >= obj.size()) return std::string(); if (obj[i] == '"') { std::string o; for (++i; i < obj.size() && obj[i] != '"'; ++i) { if (obj[i] == '\\' && i + 1 < obj.size()) { ++i; o += obj[i] == 'n' ? '\n' : obj[i]; } else o += obj[i]; } return o; } std::string o; while (i < obj.size() && (std::isalnum(static_cast<unsigned char>(obj[i])) || obj[i] == '.' || obj[i] == '-')) o += obj[i++]; return o; };
  const std::size_t lp = t.find("\"locks\":{");
  if (lp != std::string::npos) {   // each lock is one flat object: {"name":..,"mode":..,...}
    std::size_t i = lp + 9; int depth = 1;
    while (i < t.size() && depth > 0) {
      if (t[i] == '{') { const std::size_t e = t.find('}', i); if (e == std::string::npos) break; const std::string obj = t.substr(i, e - i + 1); Lock l; l.name = str(obj, "name"); l.mode = str(obj, "mode"); l.device = str(obj, "device"); l.reason = str(obj, "reason"); l.at = std::atoll(str(obj, "at").c_str()); if (!l.name.empty() && !l.mode.empty()) g_store.locks[keyOf(l.name)] = l; i = e + 1; }
      else { if (t[i] == '}') --depth; ++i; }
    }
  }
  { const std::size_t fp = t.find("\"fallback\":\""); if (fp != std::string::npos) { const std::size_t e = t.find('"', fp + 12); if (e != std::string::npos) { const std::string v = t.substr(fp + 12, e - fp - 12); if (v == "all" || v == "dsound" || v == "none") g_store.fallback = v; } } }
  const std::size_t pp = t.find("\"pins\":{");
  if (pp != std::string::npos) { const std::size_t e = t.find('}', pp); std::string body = t.substr(pp + 8, e == std::string::npos ? std::string::npos : e - pp - 8); std::size_t i = 0; while ((i = body.find('"', i)) != std::string::npos) { const std::size_t j = body.find('"', i + 1); if (j == std::string::npos) break; const std::string k = body.substr(i + 1, j - i - 1); const std::size_t c = body.find(':', j); if (c != std::string::npos && body.compare(c + 1, 5, "false") == 0) g_store.pins[k] = false; i = j + 1; } }
}
void save() {
  std::string o = "{\"v\":1,\"locks\":{"; bool first = true;
  for (const auto& [k, l] : g_store.locks) { if (!first) o += ','; first = false; o += jsonEsc(k) + ":{\"name\":" + jsonEsc(l.name) + ",\"mode\":" + jsonEsc(l.mode) + ",\"device\":" + jsonEsc(l.device) + ",\"reason\":" + jsonEsc(l.reason) + ",\"at\":" + std::to_string(l.at) + "}"; }
  o += "},\"pins\":{"; first = true; for (const auto& [k, on] : g_store.pins) { if (on) continue; if (!first) o += ','; first = false; o += jsonEsc(k) + ":false"; } o += "},\"fallback\":" + jsonEsc(g_store.fallback) + "}\n";
  const std::string f = lockFile(); const std::size_t slash = f.find_last_of("/\\");
  if (slash != std::string::npos) {
#ifdef _WIN32
    CreateDirectoryA(f.substr(0, slash).c_str(), nullptr);
#else
    ::mkdir(f.substr(0, slash).c_str(), 0700);
#endif
  }
#ifdef _WIN32
  if (FILE* fp = std::fopen(f.c_str(), "wb")) { std::fwrite(o.data(), 1, o.size(), fp); std::fclose(fp); }
#else
  const int fd = ::open(f.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600); if (fd >= 0) { const auto w = ::write(fd, o.data(), o.size()); (void)w; ::close(fd); }
#endif
}
bool isAsio(const DeviceInfo& d) { return d.backend == "asio"; }
bool isWasapi(const DeviceInfo& d) { return d.backend == "wasapi"; }
bool isDsound(const DeviceInfo& d) { return d.backend == "dsound"; }
bool offers(const DeviceInfo& d, bool in, bool out) { return (!in || d.inChannels > 0) && (!out || d.outChannels > 0); }
}  // namespace

std::string family(const std::string& name) { for (const auto& [f, re] : kFamilies) if (std::regex_search(name, re)) return f; return ""; }
bool isWrapper(const std::string& name) { return std::regex_search(name, kWrappers); }
int score(const std::string& a, const std::string& b) {
  if (isWrapper(a) || isWrapper(b)) return 0;
  const std::string fa = family(a); const auto wa = words(a); int s = (!fa.empty() && family(b) == fa) ? 100 : 0;
  for (const auto& w : words(b)) if (wa.count(w)) s += 10;
  return s;
}

const DeviceInfo* twin(const std::string& asioName, const std::vector<DeviceInfo>& devices, bool wantInput, bool wantOutput) {
  if (isWrapper(asioName)) {   // the wrapper drives the Windows default endpoint: that endpoint is its twin, never one owned by a native ASIO device
    std::vector<std::string> natives; for (const auto& d : devices) if (isAsio(d) && !isWrapper(d.name)) natives.push_back(d.name);
    const DeviceInfo* best = nullptr; int bestRank = -1;
    for (const auto& d : devices) {
      if (!isWasapi(d) || !offers(d, wantInput, wantOutput) || std::regex_search(d.name, kLoopback)) continue;
      bool owned = false; for (const auto& n : natives) if (score(n, d.name) >= 100) owned = true;
      const int rank = (owned ? 0 : 2) + ((d.defaultIn || d.defaultOut) ? 1 : 0);
      if (rank > bestRank) { bestRank = rank; best = &d; }
    }
    return best;
  }
  const DeviceInfo* best = nullptr; int bs = 0;
  for (const auto& d : devices) { if (!isWasapi(d) || !offers(d, wantInput, wantOutput) || std::regex_search(d.name, kLoopback)) continue; const int s = score(asioName, d.name); if (s > bs) { bs = s; best = &d; } }
  return bs >= 10 ? best : nullptr;
}

const DeviceInfo* dsTwin(const std::string& asioName, const DeviceInfo* w, const std::vector<DeviceInfo>& devices, bool wantInput, bool wantOutput) {
  const DeviceInfo* best = nullptr; int bs = 0;
  for (const auto& d : devices) { if (!isDsound(d) || !offers(d, wantInput, wantOutput)) continue; int sc = 0; if (w && lower(d.name) == lower(w->name)) sc = 1000; else if (w) sc = score(w->name, d.name); if (sc < 10) sc = std::max(sc, score(asioName, d.name)); if (sc > bs) { bs = sc; best = &d; } }
  return bs >= 10 ? best : nullptr;
}
static std::atomic<bool> g_openRouting{false};
bool openRouting() { return g_openRouting.load(); }
void setOpenRouting(bool on) { g_openRouting.store(on); }
std::string fallback() { load(); return g_store.fallback; }
void setFallback(const std::string& f) { load(); if (f == "all" || f == "dsound" || f == "none") { g_store.fallback = f; save(); } }

bool pinRule(const std::string& n) { return !n.empty() && !isWrapper(n) && family(n) == "realtek"; }
Pin pinOf(const std::string& n) { load(); Pin p; p.name = n; p.reason = kPinReason; if (!pinRule(n)) { p.on = false; p.reason.clear(); return p; } const auto it = g_store.pins.find(keyOf(n)); p.on = it == g_store.pins.end() || it->second; return p; }
Pin setPin(const std::string& n, bool on) { load(); if (pinRule(n)) { if (on) g_store.pins.erase(keyOf(n)); else g_store.pins[keyOf(n)] = false; save(); } return pinOf(n); }
std::vector<Pin> pins(const std::vector<DeviceInfo>& devices) { std::vector<Pin> out; for (const auto& d : devices) if (isAsio(d) && pinRule(d.name)) { bool dup = false; for (const auto& p : out) if (p.name == d.name) dup = true; if (!dup) out.push_back(pinOf(d.name)); } return out; }

std::vector<Step> ladder(const DeviceInfo& asio, const std::vector<DeviceInfo>& devices, bool wantInput, bool wantOutput, std::int64_t nowMs) {
  (void)nowMs;
  bool nativeAsio = false; for (const auto& d : devices) if (isAsio(d) && !isWrapper(d.name)) nativeAsio = true;
  const bool open = openRouting(); const bool wrapper = isWrapper(asio.name), wrapperOk = wrapper && (!nativeAsio || open);
  // one twin per direction: an input endpoint when an input is wanted, an output endpoint when an output is wanted (both for a duplex start)
  const DeviceInfo* wIn = wantInput ? twin(asio.name, devices, true, false) : nullptr; const DeviceInfo* wOut = wantOutput ? twin(asio.name, devices, false, true) : nullptr;
  const bool twinsOk = (!wantInput || wIn) && (!wantOutput || wOut); const DeviceInfo* w = wantOutput ? wOut : wIn;
  const Pin pin = pinOf(asio.name); const bool pinned = pin.on && pinRule(asio.name) && w != nullptr && !open;
  std::vector<Step> out;
  { Step s; s.step = 1; s.mode = "asio"; s.backend = "asio"; s.id = asio.id; s.name = asio.name; s.available = offers(asio, wantInput, wantOutput) && !pinned && (!wrapper || wrapperOk); s.pinned = pinned;
    if (!offers(asio, wantInput, wantOutput)) s.why = "the ASIO driver has no channels in this direction"; else if (pinned) s.why = std::string("pinned: ") + kPinReason; else if (wrapper && !wrapperOk) s.why = "a wrapper (ASIO4ALL-style): a device with its own ASIO driver is present, so that driver is used first"; out.push_back(s); }
  auto wasapiStep = [&](int n, const char* mode, bool excl) { Step s; s.step = n; s.mode = mode; s.backend = "wasapi"; s.exclusive = excl; if (w) { s.id = w->id; s.name = w->name; } if (wIn) s.inId = wIn->id; if (wOut) s.outId = wOut->id; s.available = twinsOk && w; if (!s.available) s.why = std::string("no Windows ") + (wantInput && !wIn ? "input" : "output") + " endpoint of this hardware"; out.push_back(s); };
  wasapiStep(2, "wasapi-exclusive", true); wasapiStep(3, "wasapi-shared", false);
  const DeviceInfo* dsIn = wantInput ? dsTwin(asio.name, wIn, devices, true, false) : nullptr; const DeviceInfo* dsOut = wantOutput ? dsTwin(asio.name, wOut, devices, false, true) : nullptr;
  const bool dsOk = (!wantInput || dsIn) && (!wantOutput || dsOut); const DeviceInfo* ds = wantOutput ? dsOut : dsIn;
  { Step s; s.step = 4; s.mode = "dsound"; s.backend = "dsound"; if (ds) { s.id = ds->id; s.name = ds->name; } if (dsIn) s.inId = dsIn->id; if (dsOut) s.outId = dsOut->id; s.available = dsOk && ds; if (!s.available) s.why = "no DirectSound device of this hardware"; out.push_back(s); }
  // the fallback policy: "all" = WASAPI exclusive, WASAPI shared, then DirectSound; "dsound" = DirectSound only (WASAPI is optional, never a lock); "none" = ASIO only
  const std::string fb = fallback();
  for (Step& s : out) { if (fb == "none" && s.step > 1 && s.available) { s.available = false; s.why = "fallback policy: ASIO only"; } if (fb == "dsound" && (s.step == 2 || s.step == 3) && s.available) { s.available = false; s.why = "fallback policy: DirectSound, not WASAPI"; } }
  return out;
}

std::string lockFile() { if (const char* f = std::getenv("AMIXOS_DRIVER_LOCK_FILE")) return f; return am::paths::file("driver-lock.json"); }
void resetCache() { g_store = Store(); }
bool getLock(const std::string& name, Lock& out, std::int64_t nowMs) { load(); const auto it = g_store.locks.find(keyOf(name)); if (it == g_store.locks.end()) return false; if (it->second.mode != "asio" && nowMs - it->second.at > kRetryMs) return false; out = it->second; return true; }
Lock learn(const std::string& name, const std::string& mode, const std::string& device, const std::string& reason, std::int64_t nowMs) {
  load(); const std::string k = keyOf(name); const auto it = g_store.locks.find(k);
  if (it != g_store.locks.end() && it->second.mode == mode && it->second.device == device && nowMs - it->second.at < 60000) return it->second;
  Lock l; l.name = name.substr(0, 120); l.mode = mode; l.device = device.substr(0, 120); l.reason = reason.substr(0, 200); l.at = nowMs; g_store.locks[k] = l; save(); return l;
}
int forget(const std::string& name) { load(); if (name == "*" || name.empty()) { const int n = static_cast<int>(g_store.locks.size()); g_store.locks.clear(); save(); return n; } const int n = static_cast<int>(g_store.locks.erase(keyOf(name))); save(); return n; }
std::vector<Lock> locks() { load(); std::vector<Lock> out; for (const auto& [k, l] : g_store.locks) out.push_back(l); return out; }

std::vector<Attempt> plan(const std::string& backend, const std::string& inId, const std::string& outId, const std::vector<DeviceInfo>& devices, std::int64_t nowMs) {
  std::vector<Attempt> out;
  const bool wantIn = inId != "-", wantOut = outId != "-";
  const DeviceInfo* asio = nullptr;
  if (backend == "asio") { const std::string want = !outId.empty() && outId != "-" ? outId : inId; for (const auto& d : devices) if (isAsio(d) && (want.empty() || d.id == want || d.name == want)) { asio = &d; break; } }
  if (!asio) { Attempt a; a.mode = backend; a.backend = backend; a.inDevice = inId; a.outDevice = outId; a.why = "as asked"; out.push_back(a); return out; }
  const std::vector<Step> steps = ladder(*asio, devices, wantIn, wantOut, nowMs);
  auto attemptOf = [&](const Step& s) { Attempt a; a.mode = s.mode; a.backend = s.backend; a.exclusive = s.exclusive; a.pinned = s.pinned; a.inDevice = wantIn ? (s.inId.empty() ? s.id : s.inId) : "-"; a.outDevice = wantOut ? (s.outId.empty() ? s.id : s.outId) : "-"; a.why = s.why; return a; };
  // the pin first, then a learned lock, then the ladder in order (each step once)
  std::vector<std::string> done;
  auto push = [&](const Step& s, const std::string& why, bool fromLock) { if (std::find(done.begin(), done.end(), s.mode) != done.end()) return; done.push_back(s.mode); Attempt a = attemptOf(s); if (!why.empty()) a.why = why; a.fromLock = fromLock; out.push_back(a); };
  if (steps[0].pinned && steps[2].available) { push(steps[2], std::string("pinned: ") + kPinReason, false); out.back().pinned = true; }
  else if (steps[0].pinned && steps.size() > 3 && steps[3].available) { push(steps[3], std::string("pinned: ") + kPinReason, false); out.back().pinned = true; }
  Lock l; if (getLock(asio->name, l, nowMs)) { for (const Step& s : steps) if (s.mode == l.mode && s.available && (s.mode == "asio" || s.name == l.device)) push(s, "locked: " + l.reason, true); }
  for (const Step& s : steps) if (s.available) push(s, "", false);
  if (out.empty()) { Attempt a; a.mode = "asio"; a.backend = "asio"; a.inDevice = inId; a.outDevice = outId; a.why = "no alternative known for this interface"; out.push_back(a); }
  return out;
}

MixerProfile mixerProfile(const std::string& name) {
  struct Row { const char* re; const char* maker; const char* model; const char* prefix; const char* note; };
  static const Row rows[] = {
    {"x-?usb|\\bx32\\b", "Behringer", "X32 family (X-USB card)", "X32", "32 channels to the computer, 32 returns; set the card's routing on the desk (CARD OUT)"},
    {"\\bm32\\b|dn32", "Midas", "M32 family", "M32", "32 channels to the computer, 32 returns; set CARD OUT on the desk"},
    {"x-?air|\\bxr ?1[268]\\b|\\bx18\\b", "Behringer", "X AIR XR18 / X18", "XR", "18 channels each way over USB"},
    {"\\bmr ?18\\b", "Midas", "MR18", "MR", "18 channels each way over USB"},
    {"\\bwing\\b", "Behringer", "WING", "WING", "48 channels each way over USB"},
    {"umc ?(1820|404|204|202)|uphoria", "Behringer", "U-PHORIA interface", "UMC", "a multichannel interface"},
    {"yamaha steinberg|\\btf ?[135]\\b|tf-?rack|\\bmg ?[0-9]+xu|\\bag0[36]|\\bdm[37]\\b", "Yamaha", "TF / DM / MG-XU / AG", "YAM", "channel count depends on the model and its USB mode"},
    {"soundcraft|\\bui ?24|signature .*mtk|\\bnotepad", "Soundcraft", "Ui24R / Signature MTK / Notepad", "SC", "MTK and Ui24R send every channel over USB"},
    {"allen ?& ?heath|\\bqu-?(16|24|32|pac|sb)|\\bsq-?[567]\\b|\\bcq-?(12|18|20)|zedi?-?[0-9]", "Allen & Heath", "Qu / SQ / CQ / ZEDi", "AH", "Qu and SQ send 32 channels over USB-B"},
    {"studiolive|presonus", "PreSonus", "StudioLive", "SL", "every channel plus the main mix over USB"},
    {"livetrak|\\bzoom l-?(8|12|20)", "Zoom", "LiveTrak L-8 / L-12 / L-20", "ZOOM", "switch the desk to AUDIO INTERFACE mode"},
    {"model ?(12|16|24)\\b|tascam", "Tascam", "Model 12 / 16 / 24", "TAS", "every channel plus the stereo mix over USB"},
    {"onyx|profx|\\bdl ?(16|32)", "Mackie", "Onyx / ProFX / DL", "MK", "channel count depends on the model"},
    {"\\btouchmix|qsc", "QSC", "TouchMix", "TM", "channel count depends on the model"},
    {"\\brodecaster|r[oø]de", "RODE", "RODECaster", "RC", "multitrack mode sends every channel"},
  };
  MixerProfile p; if (name.empty() || isWrapper(name)) return p;
  for (const Row& r : rows) if (std::regex_search(name, std::regex(r.re, std::regex::icase))) { p.known = true; p.maker = r.maker; p.model = r.model; p.prefix = r.prefix; p.note = r.note; return p; }
  return p;
}
std::vector<const DeviceInfo*> mixers(const std::vector<DeviceInfo>& devices) {
  std::vector<const DeviceInfo*> out; for (const auto& d : devices) if (isAsio(d) && mixerProfile(d.name).known) out.push_back(&d); return out;
}

std::vector<Attempt> rescue(const std::vector<DeviceInfo>& devices, bool wantInput, bool wantOutput, const std::vector<Attempt>& tried) {
  std::vector<Attempt> out;
  auto add = [&](const Attempt& a) {
    if (a.inDevice == "-" && a.outDevice == "-") return;
    for (const std::vector<Attempt>* list : std::initializer_list<const std::vector<Attempt>*>{&tried, &out}) for (const Attempt& t : *list) if (t.backend == a.backend && t.inDevice == a.inDevice && t.outDevice == a.outDevice && t.exclusive == a.exclusive) return;
    out.push_back(a);
  };
  // one endpoint per direction of `backend`: Realtek only when asked, never a loopback, the system default first
  auto endpoint = [&](const char* backend, bool input, bool realtekOnly) -> std::string {
    const DeviceInfo* best = nullptr; int br = -1;
    for (const auto& d : devices) {
      if (d.backend != backend || (input ? d.inChannels : d.outChannels) <= 0 || std::regex_search(d.name, kLoopback) || d.id.rfind("loopback:", 0) == 0) continue;
      if (realtekOnly && family(d.name) != "realtek") continue;
      const int r = (input ? d.defaultIn : d.defaultOut) ? 2 : 1; if (r > br) { br = r; best = &d; }
    }
    return best ? best->id : std::string("-");
  };
  for (const auto& d : devices) if (isAsio(d) && !isWrapper(d.name) && family(d.name) == "realtek") {
    Attempt a; a.mode = "realtek-asio"; a.backend = "asio"; a.inDevice = wantInput && d.inChannels > 0 ? d.id : "-"; a.outDevice = wantOutput && d.outChannels > 0 ? d.id : "-";
    a.why = "rescue: the Realtek ASIO driver found on this computer, opened directly"; add(a);
  }
  struct Row { const char* mode; const char* backend; bool realtek; const char* why; };
  for (const Row& r : {Row{"rws", "wasapi", true, "rescue: Realtek through Windows shared mode (no lock)"}, Row{"wasapi-shared", "wasapi", false, "rescue: the system's default endpoints, Windows shared mode"}, Row{"dsound", "dsound", false, "rescue: the system's default endpoints, DirectSound"}}) {
    Attempt a; a.mode = r.mode; a.backend = r.backend; a.inDevice = wantInput ? endpoint(r.backend, true, r.realtek) : "-"; a.outDevice = wantOutput ? endpoint(r.backend, false, r.realtek) : "-"; a.why = r.why; add(a);
  }
  return out;
}

}  // namespace am::drv
