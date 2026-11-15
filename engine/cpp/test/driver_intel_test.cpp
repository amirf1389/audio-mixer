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
  { auto L = drv::ladder(pc[0], pc, true, false, now); CHECK(L.size() == 4 && L[0].mode == "asio" && !L[0].available && L[0].pinned && L[2].mode == "wasapi-shared" && L[2].available && L[2].id == "{r-in}");
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
    // both directions wanted: Windows endpoints are one-directional, so the WASAPI steps take the mic endpoint as input and the speakers endpoint as output
    P = drv::plan("asio", "BEHRINGER USB AUDIO", "BEHRINGER USB AUDIO", pc, now); CHECK(P.size() == 3 && P[0].mode == "asio" && P[1].inDevice == "{um2-in}" && P[1].outDevice == "{um2-out}" && P[2].inDevice == "{um2-in}" && P[2].outDevice == "{um2-out}"); }
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
  // DirectSound step: a dsound device named like the WASAPI twin becomes step 4; the fallback policy "dsound" skips WASAPI, "none" leaves ASIO alone, "all" restores the ladder
  { std::vector<DeviceInfo> um = {dev("asio", "BEHRINGER USB AUDIO", "BEHRINGER USB AUDIO", 2, 2), dev("wasapi", "{um2-in}", "Microphone (USB Audio CODEC)", 2, 0), dev("wasapi", "{um2-out}", "Speakers (USB Audio CODEC)", 0, 2), dev("dsound", "{ds-out}", "Speakers (USB Audio CODEC)", 0, 2), dev("dsound", "{ds-bt}", "Headphones (JBL TUNE Stereo)", 0, 2)};
    auto L = drv::ladder(um[0], um, false, true, now); CHECK(L.size() == 4 && L[3].mode == "dsound" && L[3].available && L[3].id == "{ds-out}");
    auto P = drv::plan("asio", "-", "BEHRINGER USB AUDIO", um, now); CHECK(P.size() == 4 && P[3].mode == "dsound" && P[3].backend == "dsound" && P[3].outDevice == "{ds-out}" && P[3].inDevice == "-");
    drv::setFallback("dsound"); CHECK(drv::fallback() == "dsound"); P = drv::plan("asio", "-", "BEHRINGER USB AUDIO", um, now); CHECK(P.size() == 2 && P[0].mode == "asio" && P[1].mode == "dsound");
    drv::setFallback("none"); P = drv::plan("asio", "-", "BEHRINGER USB AUDIO", um, now); CHECK(P.size() == 1 && P[0].mode == "asio");
    drv::setFallback("bogus"); CHECK(drv::fallback() == "none"); drv::setFallback("all"); P = drv::plan("asio", "-", "BEHRINGER USB AUDIO", um, now); CHECK(P.size() == 4);
    drv::resetCache(); CHECK(drv::fallback() == "all"); }   // and it survives a reload of the store
  // duplex: the UM2 asked for input AND output: the WASAPI steps carry the mic endpoint as input and the speakers endpoint as output (one-directional endpoints)
  { std::vector<DeviceInfo> um = {dev("asio", "BEHRINGER USB AUDIO", "BEHRINGER USB AUDIO", 2, 2), dev("wasapi", "{um2-in}", "Microphone (USB Audio CODEC)", 2, 0), dev("wasapi", "{um2-out}", "Speakers (USB Audio CODEC)", 0, 2), dev("dsound", "{ds-um2}", "Speakers (USB Audio CODEC)", 0, 2), dev("dsound", "{ds-um2-in}", "Microphone (USB Audio CODEC)", 2, 0)};
    auto P = drv::plan("asio", "BEHRINGER USB AUDIO", "BEHRINGER USB AUDIO", um, now);
    CHECK(P.size() == 4 && P[1].mode == "wasapi-exclusive" && P[1].inDevice == "{um2-in}" && P[1].outDevice == "{um2-out}" && P[2].inDevice == "{um2-in}" && P[2].outDevice == "{um2-out}" && P[3].mode == "dsound" && P[3].inDevice == "{ds-um2-in}" && P[3].outDevice == "{ds-um2}");
    std::vector<DeviceInfo> outOnly = {um[0], um[2], um[3]}; P = drv::plan("asio", "BEHRINGER USB AUDIO", "BEHRINGER USB AUDIO", outOnly, now); CHECK(P.size() == 1 && P[0].mode == "asio");   // no input endpoint: the duplex ladder has no WASAPI / DirectSound step
    P = drv::plan("asio", "-", "BEHRINGER USB AUDIO", outOnly, now); CHECK(P.size() == 4 && P[1].inDevice == "-" && P[1].outDevice == "{um2-out}"); }
  { // OPEN ROUTING: what was picked is opened first: the wrapper although a manufacturer driver is present, the Realtek driver instead of its pin
    drv::forget("*"); drv::setPin("Realtek ASIO", true);
    auto P = drv::plan("asio", "ASIO4ALL v2", "ASIO4ALL v2", pc, now); CHECK(!P.empty() && P[0].mode != "asio");          // closed routing: the wrapper is passed over
    P = drv::plan("asio", "Realtek ASIO", "Realtek ASIO", pc, now); CHECK(P[0].pinned && P[0].mode == "wasapi-shared");
    drv::setOpenRouting(true); CHECK(drv::openRouting());
    P = drv::plan("asio", "ASIO4ALL v2", "ASIO4ALL v2", pc, now); CHECK(P[0].mode == "asio" && P[0].outDevice == "ASIO4ALL v2" && P.size() >= 2);
    P = drv::plan("asio", "Realtek ASIO", "Realtek ASIO", pc, now); CHECK(P[0].mode == "asio" && !P[0].pinned && P[0].outDevice == "Realtek ASIO" && P.size() >= 2 && P[1].backend == "wasapi");
    P = drv::plan("asio", "BEHRINGER USB AUDIO", "BEHRINGER USB AUDIO", pc, now); CHECK(P[0].mode == "asio");
    drv::setOpenRouting(false); }
  { // MIXERS as multichannel sound cards: recognised from the ASIO driver's name; an ordinary interface, a wrapper and onboard audio are not mixers
    CHECK(drv::mixerProfile("X-USB ASIO Driver").known && drv::mixerProfile("X-USB ASIO Driver").maker == "Behringer" && drv::mixerProfile("X-USB ASIO Driver").prefix == "X32");
    CHECK(drv::mixerProfile("Yamaha Steinberg USB ASIO").maker == "Yamaha" && drv::mixerProfile("Allen&Heath Qu-16 ASIO").maker == "Allen & Heath" && drv::mixerProfile("ZOOM L-12 ASIO").maker == "Zoom" && drv::mixerProfile("PreSonus StudioLive 16.0.2").maker == "PreSonus" && drv::mixerProfile("Soundcraft USB Audio ASIO").maker == "Soundcraft" && drv::mixerProfile("TASCAM Model 16").maker == "Tascam" && drv::mixerProfile("X-AIR ASIO Driver").model.find("XR18") != std::string::npos);
    CHECK(!drv::mixerProfile("Realtek ASIO").known && !drv::mixerProfile("ASIO4ALL v2").known && !drv::mixerProfile("Focusrite USB ASIO").known && !drv::mixerProfile("").known);
    // UNIVERSAL ASIO DETECTION: maker and type from the driver's name, for any brand
    { struct T { const char* name; const char* type; const char* maker; };
      const T cases[] = {{"BEHRINGER USB AUDIO", "interface", "Behringer"}, {"Focusrite USB ASIO", "interface", "Focusrite"}, {"Yamaha Steinberg USB ASIO", "mixer", "Yamaha"},
        {"ASIO Fireface USB", "interface", "RME"}, {"MOTU M Series", "interface", "MOTU"}, {"Universal Audio Thunderbolt", "interface", "Universal Audio"},
        {"Audient USB Audio ASIO Driver", "interface", "Audient"}, {"SSL USB ASIO Driver", "interface", "Solid State Logic"}, {"AudioBox ASIO Driver", "interface", "PreSonus"},
        {"Pioneer DDJ-FLX4 ASIO", "dj", "Pioneer DJ / AlphaTheta"}, {"ASIO Driver for Line 6 Helix", "guitar", "Line 6"}, {"BOSS KATANA", "guitar", "BOSS"},
        {"Realtek ASIO", "onboard", "Realtek"}, {"ASIO4ALL v2", "wrapper", "ASIO4ALL"}, {"FlexASIO", "wrapper", "FlexASIO"},
        {"Voicemeeter AUX Virtual ASIO", "virtual", "VB-Audio"}, {"Dante Virtual Soundcard (x64)", "network", "Audinate"}, {"X-USB ASIO Driver", "mixer", "Behringer"},
        {"Audio Mixer", "virtual", "AMFB Audio"}, {"Some Unknown Box ASIO", "interface", ""}};
      for (const T& t : cases) { const drv::AsioIdentity a = drv::identifyAsio(t.name);
        if (a.type != t.type || (t.maker[0] && a.maker != t.maker)) { std::fprintf(stderr, "identifyAsio(%s) = %s / %s, want %s / %s\n", t.name, a.type.c_str(), a.maker.c_str(), t.type, t.maker); return 1; } }
      CHECK(drv::identifyAsio("Audio Mixer").own && !drv::identifyAsio("BEHRINGER USB AUDIO").own && !drv::identifyAsio("Some Unknown Box ASIO").known);
      std::printf("driver_intel_test: universal ASIO detection: %d driver names identified\n", static_cast<int>(sizeof cases / sizeof cases[0])); }
    std::vector<DeviceInfo> rig = {dev("asio", "Realtek ASIO", "Realtek ASIO", 2, 2), dev("asio", "X-USB ASIO Driver", "X-USB ASIO Driver", 32, 32), dev("wasapi", "{x}", "Line (X-USB)", 2, 0), dev("null", "", "null", 32, 32)};
    const auto M = drv::mixers(rig); CHECK(M.size() == 1 && M[0]->name == "X-USB ASIO Driver" && M[0]->inChannels == 32); }
  { // RESCUE after every planned step failed: the Realtek ASIO driver, Realtek Windows shared (RWS), the default endpoints
    auto R = drv::rescue(pc, true, true, {});
    CHECK(R.size() >= 2 && R[0].mode == "realtek-asio" && R[0].backend == "asio" && R[0].inDevice == "Realtek ASIO" && R[0].outDevice == "Realtek ASIO" && !R[0].exclusive);
    CHECK(R[1].mode == "rws" && R[1].backend == "wasapi" && R[1].inDevice == "{r-in}" && R[1].outDevice == "{r-out}");   // the microphone array, never the Stereo Mix loopback
    for (const auto& a : R) CHECK(a.mode != "wasapi-shared");                                                            // the defaults here are the Realtek pair: not tried twice
    drv::Attempt done; done.backend = "wasapi"; done.inDevice = "{r-in}"; done.outDevice = "{r-out}"; R = drv::rescue(pc, true, true, {done}); CHECK(R.size() == 1 && R[0].mode == "realtek-asio");
    R = drv::rescue(pc, false, true, {}); CHECK(R[0].inDevice == "-" && R[1].inDevice == "-" && R[1].outDevice == "{r-out}");
    std::vector<DeviceInfo> usb = {dev("asio", "BEHRINGER USB AUDIO", "BEHRINGER USB AUDIO", 2, 2), dev("wasapi", "{um2-out}", "Speakers (USB Audio CODEC)", 0, 2, false, true), dev("dsound", "{ds-out}", "Speakers (USB Audio CODEC)", 0, 2, false, true), dev("null", "", "null", 32, 32)};
    R = drv::rescue(usb, true, true, {}); CHECK(R.size() == 2 && R[0].mode == "wasapi-shared" && R[0].outDevice == "{um2-out}" && R[0].inDevice == "-" && R[1].mode == "dsound" && R[1].outDevice == "{ds-out}");   // no Realtek here: no Realtek step
    CHECK(drv::rescue({dev("null", "", "null", 32, 32)}, true, true, {}).empty()); }
  std::printf("driver_intel_test: hardware match, Realtek pin (default on, opt-out saved), ASIO4ALL rule, ladder, locks, plan OK\n");
  return 0;
}
