// VOLUME CONTROL: the key steps (master dB grid, Windows slider grid), where a key goes, and the per-app list against a layer kept in
// memory: the diff of a fresh list, the console's own changes not coming back as moves (no echo), a move in the Windows mixer coming in.
#include "audiomixer/app_volume.hpp"

#include <cmath>
#include <cstdio>

using namespace am::vol;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
static bool near(double a, double b) { return std::fabs(a - b) < 1e-5; }

int main() {
  // steps
  CHECK(clampStep(0) == 1 && clampStep(1) == 1 && clampStep(2) == 2 && clampStep(3) == 3 && clampStep(9) == 3);
  CHECK(near(stepDb(0, 1, 2), 2) && near(stepDb(0, -1, 2), -2) && near(stepDb(-3, -1, 2), -4) && near(stepDb(-3, 1, 2), -2));   // off-grid: to the next line
  CHECK(near(stepDb(-0.5, 1, 1), 0) && near(stepDb(9, 1, 3), 10) && near(stepDb(10, 1, 1), 10));   // clamped at +10
  CHECK(near(stepDb(-60, -1, 2), -60) && near(stepDb(-60, 1, 3), -57) && near(stepDb(-200, 1, 1), -59) && near(stepDb(NAN, 1, 2), -58));   // from off
  CHECK(near(stepDb(-58.5, -1, 3), -60));
  { double d = 0; for (int i = 0; i < 40; ++i) d = stepDb(d, -1, 2); CHECK(near(d, -60)); for (int i = 0; i < 40; ++i) d = stepDb(d, 1, 2); CHECK(near(d, 10)); }
  CHECK(near(stepScalar(0.5f, 1, 1), 0.52) && near(stepScalar(0.5f, -1, 3), 0.48) && near(stepScalar(0.54f, -1, 3), 0.48) && near(stepScalar(0.99f, 1, 2), 1.0) && near(stepScalar(0.01f, -1, 1), 0.0));
  CHECK(near(stepScalar(0.51f, 1, 1), 0.52) && near(stepScalar(0.51f, -1, 1), 0.50));
  CHECK(near(scalarToDb(1.f), 0) && scalarToDb(0.f) == -120.0 && std::fabs(scalarToDb(0.5f) + 6.0206) < 1e-3 && dbToScalar(-120) == 0.f && std::fabs(dbToScalar(-6.0206) - 0.5f) < 1e-4f);
  CHECK(dbText(-60) == "-\xe2\x88\x9e" && dbText(3) == "+3.0" && dbText(-12.5) == "-12.5" && dbText(0.01) == "0.0" && dbText(NAN) == "-\xe2\x88\x9e");

  // keys
  { KeyConfig c; Action a;
    CHECK(!route(c, Key::Up, Source::Media, a));   // media keys stay with Windows unless taken
    CHECK(route(c, Key::Up, Source::Hotkey, a) && a.target == Target::Console && a.dir == 1 && !a.toggleMute);
    CHECK(route(c, Key::Down, Source::Hotkey, a) && a.dir == -1);
    CHECK(route(c, Key::Mute, Source::Hotkey, a) && a.toggleMute && a.dir == 0);
    c.mediaKeys = true; c.target = Target::Windows; CHECK(route(c, Key::Down, Source::Media, a) && a.target == Target::Windows && a.dir == -1);
    c.hotkeys = false; CHECK(!route(c, Key::Up, Source::Hotkey, a) && route(c, Key::Mute, Source::Media, a)); }

  // sessions: diff and no echo
  { MemoryLayer L; SessionList S; int moves = 0, lists = 0;
    L.onVolume = [&](const std::string& id, float v, bool m, bool ours) { if (S.report(id, v, m, ours)) ++moves; };
    L.onListChanged = [&] { ++lists; };
    L.add({"b", 20, "Spotify", "", 0.8f, false, false}); L.add({"a", 10, "Chrome", "", 1.f, false, false}); L.add({"s", 0, "System sounds", "", 0.5f, false, true});
    CHECK(lists == 3);
    SessionList::Diff d = S.update(L.list());
    CHECK(d.added.size() == 3 && d.removed.empty() && d.changed.empty());
    CHECK(S.sessions()[0].system && S.sessions()[1].name == "Chrome" && S.sessions()[2].name == "Spotify");   // system sounds first, then by name
    d = S.update(L.list()); CHECK(d.empty());   // nothing new: nothing to redraw
    // the console moves Spotify: its echo is not a move
    S.noteSet("b", 0.3f); CHECK(L.setVolume("b", 0.3f)); CHECK(moves == 0 && near(S.find("b")->volume, 0.3f));
    // a drag: two sets, the echoes come back late and without our flag (another path): neither pulls the slider back
    S.noteSet("b", 0.4f); S.noteSet("b", 0.45f); CHECK(!S.report("b", 0.45f, false, false)); CHECK(near(S.find("b")->volume, 0.45f) && moves == 0);
    S.noteMute("a", true); CHECK(L.setMute("a", true) && moves == 0 && S.find("a")->muted);
    // someone moves Chrome in the Windows mixer: it comes in once
    L.poke("a", 0.25f, true); CHECK(moves == 1 && near(S.find("a")->volume, 0.25f));
    L.poke("a", 0.25f, true); CHECK(moves == 1);   // the same value again: nothing
    CHECK(!S.report("zz", 0.1f, false, false));    // unknown: the list refresh brings it
    // a program goes, another comes
    L.remove("b"); L.add({"c", 30, "Discord", "", 0.6f, false, false});
    d = S.update(L.list()); CHECK(d.added.size() == 1 && d.added[0].id == "c" && d.removed.size() == 1 && d.removed[0] == "b" && d.changed.empty());
    L.onVolume = nullptr; L.poke("c", 0.9f, false); d = S.update(L.list()); CHECK(d.changed.size() == 1 && d.changed[0].id == "c");
    // remote requests
    std::string err;
    CHECK(validVolumeRequest(S, "c", 50, err) && !validVolumeRequest(S, "nope", 50, err) && !validVolumeRequest(S, "c", 101, err) && !validVolumeRequest(S, "c", -1, err) && !validVolumeRequest(S, "c", NAN, err) && !validVolumeRequest(S, "", 1, err)); }

  // this computer: never throws, a list (empty where there is no Windows)
  { auto sys = makeSystemLayer(); CHECK(sys != nullptr); const auto v = sys->list();
#ifndef _WIN32
    CHECK(!sys->available() && v.empty() && !sys->setVolume("x", 0.5f));
#endif
  }
  if (fails) { std::printf("app_volume: %d failures\n", fails); return 1; }
  std::printf("app_volume: all passed\n"); return 0;
}
