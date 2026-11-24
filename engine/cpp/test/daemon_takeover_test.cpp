// The remote service taking over the console's sound card (amixos-daemon on the null backend): it opens no input unless one is named,
// a strip the console had muted (its own MUTE, a fader at -inf, a DCA / MUTE GROUP) stays silent, and the FEEDBACK GUARD mutes a strip
// that howls (a pure tone that grows) while a steady test tone and a voice-like tone with overtones are left alone.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "daemon/daemon.hpp"
#include "daemon/howl.hpp"

using am::json::Value;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

namespace {
constexpr double kPi = 3.14159265358979323846, kFs = 48000;
// what the null device's inputs carry: channel k a sine of freq[k] at amp[k](t) (t in seconds)
struct Signal { std::vector<double> freq; std::function<double(int ch, double t)> amp; };
std::shared_ptr<Signal> g_sig = std::make_shared<Signal>();
void fill(float* const* in, std::size_t frames, std::uint64_t at) {
  const auto s = g_sig; if (!s || !s->amp) { return; }
  for (std::size_t c = 0; c < s->freq.size(); ++c) { if (!in[c]) continue;
    for (std::size_t i = 0; i < frames; ++i) { const double t = static_cast<double>(at + i) / kFs; in[c][i] = static_cast<float>(s->amp(static_cast<int>(c), t) * std::sin(2 * kPi * s->freq[c] * t)); } }
}
double masterDb(am::Daemon& d) { const Value m = d.meters(); const double l = m["master"]["l"].asNumber(0), r = m["master"]["r"].asNumber(0); return 20.0 * std::log10(std::max(l, r) + 1e-6); }   // the meters are linear peaks
Value strip(am::Daemon& d, int i) { return d.state()["strips"].at(static_cast<std::size_t>(i)); }
// detector alone: how many hops until it calls a howl (-1 none) for a signal x(t)
int detect(const std::function<double(double)>& x, double seconds) {
  am::HowlDetector h; std::vector<float> hop(am::HowlDetector::kHop); long n = 0;
  for (int k = 0; k * am::HowlDetector::kHop < seconds * kFs; ++k) { for (float& v : hop) { v = static_cast<float>(x(n / kFs)); ++n; } if (h.push(hop.data(), kFs)) return k; }
  return -1;
}
}  // namespace

int main() {
  // ---- the --open command: no input unless one is named
  { am::Daemon::OpenArgs a; a.backend = "wasapi"; a.out = "{spk}"; Value c = am::Daemon::openCommand(a);
    CHECK(c["inChannels"].asInt(-1) == 0 && c["in"].asString("x").empty() && c["out"].asString() == "{spk}");
    a.in = ""; c = am::Daemon::openCommand(a); CHECK(c["inChannels"].asInt(-1) == 0);   // --open without --in: none (it used to be the default microphone)
    a.in = "default"; a.inChannels = 2; c = am::Daemon::openCommand(a); CHECK(c["in"].asString("x").empty() && c["inChannels"].asInt() == 2);
    a.in = "{mic}"; c = am::Daemon::openCommand(a); CHECK(c["in"].asString() == "{mic}" && c["inChannels"].asInt() == 2); }

  // ---- the detector: a growing pure tone is a howl; a steady tone, a held note with overtones and silence are not
  { const int grow = detect([](double t) { return std::pow(10.0, (-50 + 40 * t) / 20.0) * std::sin(2 * kPi * 1000 * t); }, 3.0);
    CHECK(grow > 0 && grow * am::HowlDetector::kHop / kFs < 1.2);
    CHECK(detect([](double t) { return 0.1 * std::sin(2 * kPi * 1000 * t); }, 4.0) < 0);   // -20 dBFS test tone, steady
    CHECK(detect([](double t) { const double a = std::pow(10.0, (-50 + 40 * t) / 20.0); return a * (std::sin(2 * kPi * 220 * t) + 0.5 * std::sin(2 * kPi * 440 * t) + 0.3 * std::sin(2 * kPi * 660 * t)); }, 2.0) < 0);   // a voice: overtones
    CHECK(detect([](double) { return 0.0; }, 1.0) < 0);
    CHECK(detect([](double t) { return std::min(0.9, std::pow(10.0, (-40 + 60 * t) / 20.0)) * std::sin(2 * kPi * 2500 * t); }, 3.0) > 0); }

  const std::string seed = "daemon_takeover_seed.json";
  // ---- a seed whose input is none: the service opens no input
  { { std::ofstream f(seed); f << R"({"strips":[{"name":"MIC","db":0,"mute":false}]})"; }
    am::Daemon::Options o; o.token = "0123456789abcdef0123"; o.strips = 4; o.nullInput = fill; am::Daemon d(o); std::string err;
    CHECK(d.loadSeed(seed, err));
    am::Daemon::OpenArgs a; a.backend = "null"; a.in = "-"; d.control(am::Daemon::openCommand(a), err); CHECK(err.empty());
    CHECK(d.status()["inChannels"].asInt(-1) == 0); }

  // ---- muted in the console: stays muted (MUTE, fader at -inf arriving as null, a DCA / MUTE GROUP); the mix stays silent
  { { std::ofstream f(seed); f << R"({"strips":[{"name":"MIC","db":0,"mute":true},{"name":"LINE","db":null,"mute":false},{"name":"VOX","db":0,"mute":false,"groupMute":true},{"name":"DCA","db":0,"groupDb":-60}],"master":{"db":0},
                                   "dcas":[{"name":"VOCALS","db":-60,"mute":true,"members":[2]}]})"; }
    g_sig = std::make_shared<Signal>(Signal{{1000, 1500, 700, 300}, [](int, double) { return 0.1; }});   // -20 dBFS, steady: the guard leaves it alone
    am::Daemon::Options o; o.token = "0123456789abcdef0123"; o.strips = 4; o.nullInput = fill; am::Daemon d(o); std::string err;
    CHECK(d.loadSeed(seed, err));
    am::Daemon::OpenArgs a; a.backend = "null"; a.in = "default"; a.inChannels = 4; d.control(am::Daemon::openCommand(a), err); CHECK(err.empty());
    CHECK(d.status()["inChannels"].asInt() == 4);
    CHECK(strip(d, 0)["mute"].asBool() && strip(d, 1)["db"].asNumber(0) <= -119.9 && !strip(d, 2)["mute"].asBool());
    CHECK(d.state()["dcas"].at(0)["mute"].asBool());
    double loud = -120; for (int k = 0; k < 40; ++k) { std::this_thread::sleep_for(std::chrono::milliseconds(30)); d.feedbackCheck(); loud = std::max(loud, masterDb(d)); }
    std::printf("muted in the console: master %.1f dBFS\n", loud);
    CHECK(loud < -60);
    CHECK(strip(d, 0)["mute"].asBool() && d.feedbackJson()["count"].asInt() == 0);   // a steady tone is not a howl
    // the measurement itself works: the console lifts the group on VOX, and it is heard
    bool pok = false; d.control(Value::parse(R"({"op":"group","strip":2,"mute":false,"db":0})", pok), err); CHECK(pok); CHECK(err.empty());
    double up = -120; for (int k = 0; k < 30; ++k) { std::this_thread::sleep_for(std::chrono::milliseconds(30)); up = std::max(up, masterDb(d)); }
    std::printf("group lifted: master %.1f dBFS\n", up); CHECK(up > -40); }

  // ---- the FEEDBACK GUARD: a mic strip that howls (grows on one frequency) is muted and reported
  { { std::ofstream f(seed); f << R"({"strips":[{"name":"MIC","db":0,"mute":false},{"name":"LINE","db":0,"mute":false}]})"; }
    g_sig = std::make_shared<Signal>(Signal{{1000, 440}, [](int ch, double t) { return ch == 0 ? std::min(0.8, std::pow(10.0, (-50 + 30 * t) / 20.0)) : 0.05 * (1 + 0.3 * std::sin(2 * kPi * 3 * t)); }});
    am::Daemon::Options o; o.token = "0123456789abcdef0123"; o.strips = 2; o.nullInput = fill; am::Daemon d(o); std::string err;
    CHECK(d.loadSeed(seed, err));
    am::Daemon::OpenArgs a; a.backend = "null"; a.in = "default"; a.inChannels = 2; d.control(am::Daemon::openCommand(a), err); CHECK(err.empty());
    const unsigned long long v0 = d.stateVersion(); int muted = 0; auto t0 = std::chrono::steady_clock::now();
    while (!muted && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(4)) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); muted += d.feedbackCheck(); }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const Value fb = d.feedbackJson(); std::printf("howl muted after %.2f s: %s\n", secs, fb.dump().c_str());
    CHECK(muted == 1 && strip(d, 0)["mute"].asBool() && !strip(d, 1)["mute"].asBool());
    CHECK(fb["count"].asInt() == 1 && fb["strip"].asInt() == 0 && std::fabs(fb["hz"].asNumber() - 1000) < 30 && fb["text"].asString().find("FEEDBACK on CH 1") == 0);
    CHECK(d.stateVersion() > v0 && d.state()["feedback"]["count"].asInt() == 1);
    for (int k = 0; k < 20; ++k) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); d.feedbackCheck(); }
    CHECK(d.feedbackJson()["count"].asInt() == 1); }   // muted: not counted again
  std::remove(seed.c_str());
  std::printf("daemon takeover: ok\n");
  return 0;
}
