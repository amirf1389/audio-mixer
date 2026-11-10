// Driver intelligence on synthetic device lists: the UM2 twin problem, the Realtek pin (default on, opt-out saved), ASIO4ALL held back next to a
// native driver and first when alone, the ladder, locks (learn / retry / forget) and the start plan.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "audiomixer/driver_intel.hpp"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)
using namespace am;

static DeviceInfo dev(const char* backend, const char* id, const char* name, int in, int out, bool defIn = false, bool defOut = false) { DeviceInfo d; d.backend = backend; d.id = id; d.name = name; d.inChannels = in; d.outChannels = out; d.defaultIn = defIn; d.defaultOut = defOut; return d; }

int main(int argc, char** argv) {
  const std::string dir = std::string(argc > 1 ? argv[1] : "/tmp") + "/amixos-drv-test"; const std::string f = dir + "/driver-lock.json";
  setenv("AMIXOS_DRIVER_LOCK_FILE", f.c_str(), 1); std::remove(f.c_str()); drv::resetCache();
  const std::int64_t now = 1762700000000LL;

  // hardware match
  CHECK(drv::family("BEHRINGER USB AUDIO") == "behringer" && drv::family("Microphone (USB Audio CODEC)") == "behringer" && drv::family("Speakers (Realtek High Definition Audio)") == "realtek" && drv::family("Focusrite USB ASIO") == "focusrite");
  CHECK(drv::score("BEHRINGER USB AUDIO", "Microphone (USB Audio CODEC)") >= 100 && drv::score("Focusrite USB ASIO", "Speakers (Realtek High Definition Audio)") == 0);
  CHECK(drv::isWrapper("ASIO4ALL v2") && drv::isWrapper("FlexASIO") && !drv::isWrapper("Realtek ASIO") && drv::score("ASIO4ALL v2", "Speakers (Realtek High Definition Audio)") == 0);

  // a laptop: Realtek onboard with its ASIO driver + ASIO4ALL + a UM2
  std::vector<DeviceInfo> pc = {
    dev("asio", "Realtek ASIO", "Realtek ASIO", 2, 2), dev("asio", "ASIO4ALL v2", "ASIO4ALL v2", 2, 2), dev("asio", "BEHRINGER USB AUDIO", "BEHRINGER USB AUDIO", 2, 2),
    dev("wasapi", "{r-out}", "Speakers (Realtek High Definition Audio)", 0, 2, false, true), dev("wasapi", "{r-in}", "Microphone Array (Realtek High Definition Audio)", 2, 0, true, false),
    dev("wasapi", "{um2-in}", "Microphone (USB Audio CODEC)", 2, 0), dev("wasapi", "{um2-out}", "Speakers (USB Audio CODEC)", 0, 2), dev("wasapi", "{mix}", "Stereo Mix (Realtek High Definition Audio)", 2, 0), dev("null", "", "null", 32, 32)};
  const DeviceInfo* t = drv::twin("BEHRINGER USB AUDIO", pc, true, false); CHECK(t && t->id == "{um2-in}");
  t = drv::twin("BEHRINGER USB AUDIO", pc, false, true); CHECK(t && t->id == "{um2-out}");
  t = drv::twin("Realtek ASIO", pc, true, false); CHECK(t && t->id == "{r-in}");   // never the Stereo Mix loopback
  t = drv::twin("ASIO4ALL v2", pc, false, true); CHECK(t && t->id == "{r-out}");   // every endpoint here belongs to a native-ASIO device: the Windows default wins (as the bridge does)

  // Realtek pin: default on, the ladder goes to WASAPI shared, the ASIO step is marked pinned
  { auto L = drv::ladder(pc[0], pc, true, false, now); CHECK(L.size() == 3 && L[0].mode == "asio" && !L[0].available && L[0].pinned && L[2].mode == "wasapi-shared" && L[2].available && L[2].id == "{r-in}");
    auto P = drv::plan("asio", "Realtek ASIO", "-", pc, now); CHECK(!P.empty() && P[0].mode == "wasapi-shared" && P[0].pinned && P[0].inDevice == "{r-in}" && P[0].outDevice == "-");
    CHECK(drv::pins(pc).size() == 1 && drv::pins(pc)[0].on);
    drv::setPin("Realtek ASIO", false); CHECK(!drv::pinOf("Realtek ASIO").on);
    drv::resetCache(); CHECK(!drv::pinOf("Realtek ASIO").on);   // the opt-out survives a restart
    P = drv::plan("asio", "Realtek ASIO", "-", pc, now); CHECK(P[0].mode == "asio" && !P[0].pinned && P.size() == 3 && P[1].mode == "wasapi-exclusive" && P[2].mode == "wasapi-shared");
    drv::setPin("Realtek ASIO", true); CHECK(drv::pinOf("Realtek ASIO").on); }
  // ASIO4ALL next to a native driver: held back; alone: first
  { auto L = drv::ladder(pc[1], pc, true, true, now); CHECK(!L[0].available && L[0].why.find("wrapper") != std::string::npos);
    std::vector<DeviceInfo> laptop = {dev("asio", "ASIO4ALL v2", "ASIO4ALL v2", 2, 2), dev("wasapi", "{r-out}", "Speakers (Realtek High Definition Audio)", 0, 2, false, true), dev("wasapi", "{r-in}", "Microphone Array (Realtek High Definition Audio)", 2, 0, true, false)};
    auto L2 = drv::ladder(laptop[0], laptop, false, true, now); CHECK(L2[0].available && L2[2].available && L2[2].id == "{r-out}");
    auto P = drv::plan("asio", "-", "ASIO4ALL v2", laptop, now); CHECK(P[0].mode == "asio" && P[0].outDevice == "ASIO4ALL v2" && P[0].inDevice == "-"); }
  // the UM2: ASIO first, then exclusive, then shared on the CODEC endpoints
  { auto P = drv::plan("asio", "BEHRINGER USB AUDIO", "-", pc, now); CHECK(P.size() == 3 && P[0].mode == "asio" && P[1].mode == "wasapi-exclusive" && P[1].exclusive && P[1].inDevice == "{um2-in}" && P[2].mode == "wasapi-shared");
    // both directions wanted: the twin must offer both; the CODEC endpoints are split in / out, so there is none -> only the ASIO step
    P = drv::plan("asio", "BEHRINGER USB AUDIO", "BEHRINGER USB AUDIO", pc, now); CHECK(P.size() == 1 && P[0].mode == "asio"); }
  // locks: a fallback that worked is tried first next time; a lower lock is retried after 6 h; forget clears
  { drv::learn("BEHRINGER USB AUDIO", "wasapi-shared", "Microphone (USB Audio CODEC)", "ASIO busy: another program holds the driver", now);
    drv::Lock l; CHECK(drv::getLock("BEHRINGER USB AUDIO", l, now + 1000) && l.mode == "wasapi-shared");
    auto P = drv::plan("asio", "BEHRINGER USB AUDIO", "-", pc, now + 1000); CHECK(P[0].mode == "wasapi-shared" && P[0].fromLock && P[0].inDevice == "{um2-in}" && P[1].mode == "asio");
    CHECK(!drv::getLock("BEHRINGER USB AUDIO", l, now + drv::kRetryMs + 1)); P = drv::plan("asio", "BEHRINGER USB AUDIO", "-", pc, now + drv::kRetryMs + 1); CHECK(P[0].mode == "asio");
    drv::resetCache(); CHECK(drv::getLock("BEHRINGER USB AUDIO", l, now + 1000) && l.reason.find("busy") != std::string::npos);   // persisted
    drv::learn("BEHRINGER USB AUDIO", "asio", "BEHRINGER USB AUDIO", "clean run", now + 2000); CHECK(drv::getLock("BEHRINGER USB AUDIO", l, now + 10 * drv::kRetryMs) && l.mode == "asio");   // an ASIO lock never expires
    CHECK(drv::locks().size() == 1 && drv::forget("BEHRINGER USB AUDIO") == 1 && drv::locks().empty()); }
  // not ASIO: the plain request
  { auto P = drv::plan("wasapi", "{r-in}", "{r-out}", pc, now); CHECK(P.size() == 1 && P[0].backend == "wasapi" && P[0].inDevice == "{r-in}");
    P = drv::plan("null", "", "", pc, now); CHECK(P.size() == 1 && P[0].backend == "null"); }
  std::remove(f.c_str());
  std::printf("driver_intel_test: hardware match, Realtek pin (default on, opt-out saved), ASIO4ALL rule, ladder, locks, plan OK\n");
  return 0;
}
