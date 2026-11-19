// APP CAPTURE: several programs beside the sound card, each on its own strips. The slots, their three inputs (left, right, mono), the
// cushion between the program's clock and the console's (silence until it is there, a counted gap when it runs empty, the oldest sound
// dropped when it grows too far), and a strip of the running console that listens to a slot. No Windows needed: the slots are fed here.
#include "audiomixer/app_capture.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

using am::Runtime; using am::app::Capture;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

int main() {
  CHECK(Runtime::kAppInBase == 272 && Runtime::kAppSlots == 8 && Runtime::kAppInputs == 24 && Runtime::isAppInput(272) && Runtime::isAppInput(295) && !Runtime::isAppInput(296) && !Runtime::isAppInput(271));
  CHECK(Runtime::kAppInBase >= Runtime::kNetInBase + Runtime::kNetInputs);   // the two ranges do not overlap
  const std::size_t N = 256; std::vector<std::vector<float>> buf(Runtime::kAppInputs, std::vector<float>(N, 9.f)); std::vector<float*> ch; for (auto& b : buf) ch.push_back(b.data());
  auto peak = [&](int c) { float p = 0; for (float v : buf[static_cast<std::size_t>(c)]) p = std::max(p, std::fabs(v)); return p; };
  { Capture cap; std::string err;
    cap.pull(ch.data(), Runtime::kAppInputs, N, 48000); for (int c = 0; c < Runtime::kAppInputs; ++c) CHECK(peak(c) == 0.f);   // nothing captured: every input silent
    // a slot fed by hand: left 0.5, right -0.25
    const int k = cap.openFeed("Player", 48000, err); CHECK(k == 0 && cap.used() == 1 && cap.info(0).used && cap.info(0).label == "Player" && cap.info(0).state == "live" && !cap.info(1).used);
    std::vector<float> in(2 * N); for (std::size_t i = 0; i < N; ++i) { in[2 * i] = 0.5f; in[2 * i + 1] = -0.25f; }
    cap.pull(ch.data(), Runtime::kAppInputs, N, 48000);                            // the audio thread meets the new slot (anything left in it from an earlier program is thrown away here)
    CHECK(cap.feed(0, in.data(), N) == N); cap.pull(ch.data(), Runtime::kAppInputs, N, 48000);
    CHECK(peak(0) == 0.f && peak(1) == 0.f && peak(16) == 0.f);                    // one block is not the cushion yet (30 ms = 1440 frames): silence, no click
    for (int b = 0; b < 6; ++b) cap.feed(0, in.data(), N);                         // 7 blocks = 1792 frames waiting
    cap.pull(ch.data(), Runtime::kAppInputs, N, 48000);
    CHECK(std::fabs(buf[0][0] - 0.5f) < 1e-6f && std::fabs(buf[1][N - 1] + 0.25f) < 1e-6f && std::fabs(buf[16][10] - 0.125f) < 1e-6f);   // left, right, mono = (L + R) / 2
    CHECK(peak(2) == 0.f && peak(17) == 0.f && cap.info(0).gaps == 0 && cap.info(0).peak > 0.4);                                             // the other slots stay silent
    for (int b = 0; b < 6; ++b) { cap.pull(ch.data(), Runtime::kAppInputs, N, 48000); CHECK(std::fabs(buf[0][0] - 0.5f) < 1e-6f); }        // the rest of what was fed
    cap.pull(ch.data(), Runtime::kAppInputs, N, 48000); CHECK(peak(0) == 0.f && cap.info(0).gaps == 1);                                     // run empty: silence and one gap
    cap.feed(0, in.data(), N); cap.pull(ch.data(), Runtime::kAppInputs, N, 48000); CHECK(peak(0) == 0.f && cap.info(0).gaps == 1);         // and it waits for the cushion again
    // far too much waiting (the program's clock runs ahead): the oldest is dropped, back to the cushion
    std::vector<float> ramp(2 * 48000); for (std::size_t i = 0; i < 48000; ++i) ramp[2 * i] = ramp[2 * i + 1] = static_cast<float>(i) / 48000.f;
    CHECK(cap.feed(0, ramp.data(), 40000) > 30000); cap.pull(ch.data(), Runtime::kAppInputs, N, 48000);
    CHECK(buf[0][0] > 0.5f);                                                       // it plays from near the end of what was fed, not from the start
    // eight slots, no ninth; a closed slot is free again and silent
    for (int i = 1; i < 8; ++i) CHECK(cap.openFeed("P" + std::to_string(i), 48000, err) == i);
    CHECK(cap.openFeed("ninth", 48000, err) == -1 && err.find("eight") != std::string::npos && cap.used() == 8);
    cap.close(3); CHECK(cap.used() == 7 && !cap.info(3).used && cap.openTest("Tone", 1000, 48000, err) == 3 && cap.info(3).test);
    cap.pull(ch.data(), Runtime::kAppInputs, N, 48000); CHECK(std::fabs(peak(6) - 0.25f) < 0.01f && std::fabs(peak(7) - 0.25f) < 0.01f && std::fabs(peak(19) - 0.25f) < 0.01f);   // slot 3: inputs 6, 7 and mono 19
    cap.closeAll(); CHECK(cap.used() == 0); cap.pull(ch.data(), Runtime::kAppInputs, N, 48000); CHECK(peak(6) == 0.f);
    // a real program: off Windows (and on Windows before 2004) it is refused with a reason, and nothing is left behind
    if (!Capture::available()) { CHECK(cap.open(1234, "Spotify", 48000, err) == -1 && err.find("Windows") != std::string::npos && cap.used() == 0); }
    CHECK(cap.open(0, "nothing", 48000, err) == -1); }
  { // THE RUNNING CONSOLE: two programs beside the device's own inputs; strips listen to them; closing one silences only its strips
    Capture cap; std::string err; Runtime rt(16); am::DeviceConfig cfg; cfg.backend = "null"; cfg.sampleRate = 48000; cfg.frames = 256; cfg.inChannels = 2; cfg.outChannels = 2;
    cfg.nullInput = [](float* const* x, std::size_t n, std::uint64_t) { for (std::size_t i = 0; i < n; ++i) { x[0][i] = 0.5f; x[1][i] = 0.5f; } };
    if (rt.open(cfg, err) && rt.start(err)) {
      const int a = cap.openTest("Spotify", 440, 48000, err), b = cap.openTest("Browser", 880, 48000, err); CHECK(a == 0 && b == 1);
      rt.setAppInput(&cap);
      rt.setInputRoute(4, Runtime::kAppInBase + 16 + a);            // CH 5: program A, mono
      rt.setInputRoute(5, Runtime::kAppInBase + 2 * b);             // CH 6 / 7: program B, left / right
      rt.setInputRoute(6, Runtime::kAppInBase + 2 * b + 1);
      rt.setInputRoute(7, Runtime::kAppInBase + 2 * 5);             // CH 8: a slot nothing is on
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      const am::Snapshot s = rt.snapshot();
      CHECK(s.strips[0].inPk > 0.4 && s.strips[1].inPk > 0.4);      // the device's inputs still arrive
      CHECK(std::fabs(s.strips[4].inPk - 0.25) < 0.02 && std::fabs(s.strips[5].inPk - 0.25) < 0.02 && std::fabs(s.strips[6].inPk - 0.25) < 0.02);
      CHECK(s.strips[7].inPk == 0.0 && s.strips[8].inPk == 0.0);
      cap.close(b); std::this_thread::sleep_for(std::chrono::milliseconds(300));
      const am::Snapshot t = rt.snapshot(); CHECK(t.strips[5].inPk == 0.0 && std::fabs(t.strips[4].inPk - 0.25) < 0.02);
      rt.setAppInput(nullptr); std::this_thread::sleep_for(std::chrono::milliseconds(200)); CHECK(rt.snapshot().strips[4].inPk == 0.0);   // detached: silence, never old samples
      rt.stop();
    } else CHECK(false); }
  std::printf(fails ? "app_capture_test: %d failed\n" : "app_capture_test: 8 slots x (left, right, mono), cushion / gap / trim, two programs on a running console, refused without process loopback\n", fails);
  return fails ? 1 : 0;
}
