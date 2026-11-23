// VOLUME CONTROL beside the Windows volume mixer: the step / dB arithmetic of the volume keys, where a key goes (the console's master or
// the Windows slider), and the PER-APP VOLUME list (Windows' audio sessions: one slider and one mute per program, as the Windows volume
// mixer shows them).
//
// The session list is split in two so it can be tested without Windows:
//   SessionLayer   the system side: lists the sessions, sets a program's volume / mute, and reports what changed (Windows: the documented
//                  IAudioSessionManager2 / ISimpleAudioVolume / IAudioSessionEvents interfaces of the default output; elsewhere nothing).
//                  Its reports come on whatever thread the system uses: the owner marshals them (the console: to its interface thread).
//   SessionList    what the console shows: the last list, the diff of a fresh one against it, and ECHO SUPPRESSION. A volume the console
//                  set itself comes back from Windows as an event: it is marked "ours" (Windows hands back the event context the console
//                  passed, the memory layer a flag), and a value equal to the one the console is still waiting for is taken as the echo
//                  too, so a slider being dragged is never pulled back by its own past values.
// No polling: the list is read once, then again only when Windows says a session came or went.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace am::vol {

// ---- STEP / dB ------------------------------------------------------------------------------------------------------------------------
constexpr double kMasterMinDb = -60.0, kMasterMaxDb = 10.0;   // the console's master fader (-60 = off)
/** the step setting: 1, 2 or 3 dB (anything else becomes the nearest of them) */
int clampStep(int stepDb);
/** the master after one key press: dir +1 / -1, on a grid of `stepDb` (an off-grid value goes to the next grid line in that direction),
 *  kept within -60 .. +10. From off (-60) the first step up lands on the first grid line above it. */
double stepDb(double db, int dir, int stepDb, double lo = kMasterMinDb, double hi = kMasterMaxDb);
/** the Windows slider (0..1) after one key press: 2 % per dB of the step (1 dB = 2 %, 3 dB = 6 %), on that grid, within 0..1 */
float stepScalar(float level, int dir, int stepDb);
/** a Windows slider position as the dB it is read as (the endpoint's audio taper is not exposed: 20*log10, 0 -> -inf as -120) */
double scalarToDb(float level);
float dbToScalar(double db);
/** the master's readout: "-∞" at -60 and below, else "+3.0" / "-12.5" */
std::string dbText(double db);

// ---- KEYS -----------------------------------------------------------------------------------------------------------------------------
enum class Target { Console = 0, Windows = 1 };   // what the volume keys move
enum class Key { Up, Down, Mute };
enum class Source { Media, Hotkey };              // the keyboard's own volume keys / the console's hotkeys (Ctrl+Alt+Up/Down/M)
struct KeyConfig { Target target = Target::Console; int stepDb = 2; bool mediaKeys = false, hotkeys = true; };
struct Action { Target target = Target::Console; int dir = 0; bool toggleMute = false; };
/** what a key press does. false: not ours (the media keys while they are not taken: Windows and the other programs keep them) */
bool route(const KeyConfig& cfg, Key key, Source src, Action& out);

// ---- PER-APP SESSIONS -----------------------------------------------------------------------------------------------------------------
struct Session {
  std::string id;          // Windows' session instance id (stable while the program plays)
  std::uint32_t pid = 0;
  std::string name;        // the display name, else the program's file name without .exe
  std::string icon;        // the session's icon path, else the program's own path ("" when unknown)
  float volume = 1.f;      // 0..1, as the Windows volume mixer shows it
  bool muted = false;
  bool system = false;     // "System sounds"
  bool operator==(const Session& o) const { return id == o.id && pid == o.pid && name == o.name && icon == o.icon && volume == o.volume && muted == o.muted && system == o.system; }
};

class SessionLayer {
 public:
  virtual ~SessionLayer() = default;
  virtual bool available() const = 0;
  virtual std::vector<Session> list() = 0;
  virtual bool setVolume(const std::string& id, float volume) = 0;   // marked as the console's own change (its echo comes back with ours = true)
  virtual bool setMute(const std::string& id, bool muted) = 0;
  /** one program's volume / mute moved (ours: the console's own change coming back). Any thread. */
  std::function<void(const std::string& id, float volume, bool muted, bool ours)> onVolume;
  /** a session came or went: read the list again. Any thread. */
  std::function<void()> onListChanged;
};
/** this computer's sessions: Windows' audio sessions of the default output; elsewhere a layer that has none (available() false) */
std::unique_ptr<SessionLayer> makeSystemLayer();

/** sessions kept in memory: the tests, and the screenshots' demo programs. Moves made with poke() look like a change in the Windows mixer. */
class MemoryLayer final : public SessionLayer {
 public:
  bool available() const override { return true; }
  std::vector<Session> list() override { std::lock_guard<std::mutex> g(m_); ++lists; return s_; }
  bool setVolume(const std::string& id, float v) override;
  bool setMute(const std::string& id, bool m) override;
  void add(const Session& s);
  void remove(const std::string& id);
  void poke(const std::string& id, float v, bool m);   // someone moved it in the Windows mixer
  int lists = 0, sets = 0;
 private:
  std::mutex m_; std::vector<Session> s_;
};

class SessionList {
 public:
  struct Diff { std::vector<Session> added; std::vector<std::string> removed; std::vector<Session> changed; bool empty() const { return added.empty() && removed.empty() && changed.empty(); } };
  /** a fresh list from the layer: what changed against the one held (system sounds first, then by name); the fresh one is kept */
  Diff update(std::vector<Session> fresh);
  /** the console sets a program's volume / mute: kept as pending until Windows reports that value back */
  void noteSet(const std::string& id, float volume);
  void noteMute(const std::string& id, bool muted);
  /** a report from the layer: true when the shown list changed (not an echo of the console's own change, not the same value again) */
  bool report(const std::string& id, float volume, bool muted, bool ours);
  const std::vector<Session>& sessions() const { return s_; }
  const Session* find(const std::string& id) const;
  static void sortSessions(std::vector<Session>& v);
 private:
  std::vector<Session> s_;
  struct Pending { float volume = -1; int mute = -1; };
  std::map<std::string, Pending> pending_;
};

/** a remote's request, checked: the id must be listed, the volume a number within 0..100 (percent) */
bool validVolumeRequest(const SessionList& list, const std::string& id, double percent, std::string& err);

}  // namespace am::vol
