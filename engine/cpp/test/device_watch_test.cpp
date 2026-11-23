// HOT-PLUG WHILE CAPTURING, on a simulated device layer (no Windows audio here): a fake system of endpoints with its fingerprint and
// notifications, the null backend as the running capture stream (DeviceConfig::nullLoseAfterFrames = the device pulled out under it).
// Covers: listing during an active capture, a USB card added / the open one removed during capture, recovery after the device is
// invalidated, a device held in exclusive mode still listed (in use), the open ASIO driver never probed by a rescan.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "audiomixer/device_watch.hpp"
#include "audiomixer/runtime.hpp"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

namespace {
// the simulated system: endpoints by name, one may be held exclusively by "another program"
struct SimSystem {
  struct Ep { std::string id, name; bool exclusiveHeld = false; };
  std::vector<Ep> eps; std::function<void()> notifier;
  std::string fingerprint() const { std::string f; for (const auto& e : eps) f += e.id + "|"; return f; }
  // listing as addWasapi does it: an endpoint held exclusively answers AUDCLNT_E_DEVICE_IN_USE to the format probe, and stays listed
  std::vector<am::DeviceInfo> list() const { std::vector<am::DeviceInfo> v; for (const auto& e : eps) { am::DeviceInfo d; d.backend = "wasapi"; d.id = e.id; d.name = e.name; d.outChannels = 2;
      const long probe = e.exclusiveHeld ? static_cast<long>(0x8889000Aul) : 0; if (am::classifyStreamError(probe) == am::StreamFault::InUse) d.probeError = "in use: held in exclusive mode"; v.push_back(d); } return v; }
  std::vector<std::string> names() const { std::vector<std::string> n; for (const auto& e : eps) n.push_back(e.name); return n; }
  void plug(const std::string& id, const std::string& name) { eps.push_back({id, name, false}); if (notifier) notifier(); }
  void unplug(const std::string& id) { for (auto it = eps.begin(); it != eps.end(); ++it) if (it->id == id) { eps.erase(it); break; } if (notifier) notifier(); }
};
bool waitFor(const std::function<bool()>& f, int ms) { for (int i = 0; i < ms / 5; ++i) { if (f()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(5)); } return f(); }
}  // namespace

int main() {
  // 1. ENUMERATE DURING AN ACTIVE CAPTURE: the real listing and fingerprint run on another thread while the stream delivers blocks
  { am::Runtime rt(4); am::DeviceConfig c; c.backend = "null"; c.frames = 128; c.inChannels = 2; c.outChannels = 2; std::string err;
    CHECK(rt.open(c, err) && rt.start(err));
    CHECK(waitFor([&] { return rt.snapshot().blocks > 4; }, 2000));
    std::vector<am::DeviceInfo> got; std::string fp; std::thread t([&] { got = am::listDevices(); fp = am::deviceFingerprint(); (void)fp; }); t.join();
    bool hasNull = false; for (const auto& d : got) hasNull |= d.backend == "null"; CHECK(hasNull);
    const auto except = am::listDevicesExcept(true, "Some ASIO driver"); CHECK(except.size() == got.size());   // nothing of that name here: the same list
    const std::uint64_t b = rt.snapshot().blocks; CHECK(waitFor([&] { return rt.snapshot().blocks > b + 4; }, 2000));   // the stream kept running through the scan
    CHECK(!rt.deviceLost()); rt.stop(); }

  // 2. HOT-PLUG ADD / REMOVE DURING CAPTURE: the monitor asks for a rescan on a changed fingerprint or a system notification
  { SimSystem sys; am::DeviceMonitor mon; sys.notifier = [&] { mon.notify(); };
    sys.eps = {{"{spk}", "Speakers (Realtek)", false}, {"{mic}", "Microphone (Realtek)", false}};
    CHECK(!mon.feed(sys.fingerprint()));                       // the first poll only learns the baseline
    CHECK(!mon.feed(sys.fingerprint()));                       // nothing changed, nothing said: no rescan
    auto before = sys.names(); sys.plug("{usb}", "USB Audio CODEC");
    CHECK(mon.notified()); CHECK(mon.feed(sys.fingerprint()));  // plugged in while capturing: rescan
    auto d = am::diffDeviceNames(before, sys.names(), {"Speakers (Realtek)", "Microphone (Realtek)"});
    CHECK(d.added.size() == 1 && d.added[0] == "USB Audio CODEC" && d.gone.empty() && !d.openDeviceGone);
    CHECK(!mon.feed(sys.fingerprint()));                       // said once
    mon.notify(); CHECK(mon.feed(sys.fingerprint()));          // the default device changed (same endpoints): a notification alone rescans
    before = sys.names(); sys.unplug("{usb}"); CHECK(mon.feed(sys.fingerprint()));
    d = am::diffDeviceNames(before, sys.names(), {"USB Audio CODEC", ""}); CHECK(d.gone.size() == 1 && d.openDeviceGone);   // the open one went: fail over
    am::DeviceMonitor polled; polled.feed("a|"); CHECK(polled.feed("a|b|"));   // no notification at all (registration failed): the poll still sees it
    // the real notification watch: inert off Windows, never throws, unregisters cleanly
    { std::atomic<int> n{0}; am::DeviceChangeWatch w([&] { ++n; }); (void)w.active(); } }

  // 3. DEVICE INVALIDATED -> RECOVERY: the simulated device disappears under the running stream; it is reported, blocks stop, a reopen runs
  { am::Runtime rt(4); am::DeviceConfig c; c.backend = "null"; c.frames = 128; c.inChannels = 2; c.outChannels = 2; c.nullLoseAfterFrames = 128 * 20; std::string err;
    CHECK(rt.open(c, err) && rt.start(err));
    CHECK(waitFor([&] { return rt.deviceLost(); }, 3000));
    const std::uint64_t b = rt.snapshot().blocks; std::this_thread::sleep_for(std::chrono::milliseconds(60)); CHECK(rt.snapshot().blocks == b);   // dead: no more blocks
    rt.stop(); c.nullLoseAfterFrames = 0;                       // recovery: the next device (here the same simulated one, back) opens
    CHECK(rt.open(c, err) && rt.start(err)); CHECK(!rt.deviceLost());
    CHECK(waitFor([&] { return rt.snapshot().blocks > b + 4; }, 2000)); rt.stop(); }
  CHECK(am::classifyStreamError(static_cast<long>(0x88890004ul)) == am::StreamFault::Lost);   // AUDCLNT_E_DEVICE_INVALIDATED
  CHECK(am::classifyStreamError(static_cast<long>(0x88890010ul)) == am::StreamFault::Lost);   // the audio service restarted
  CHECK(am::classifyStreamError(0) == am::StreamFault::None);
  CHECK(am::classifyStreamError(static_cast<long>(0x80004005ul)) == am::StreamFault::Other);  // E_FAIL: not taken for a lost device

  // 4. HELD IN EXCLUSIVE MODE: still listed, marked in use; the open ASIO driver is listed without loading it
  { SimSystem sys; sys.eps = {{"{spk}", "Speakers", false}, {"{dac}", "USB DAC", true}};
    const auto l = sys.list(); CHECK(l.size() == 2); CHECK(l[0].probeError.empty()); CHECK(l[1].probeError.find("in use") != std::string::npos); }
  CHECK(am::classifyStreamError(static_cast<long>(0x8889000Aul)) == am::StreamFault::InUse);
  CHECK(am::asioDriverToSkip(true, "asio", "", "Focusrite USB ASIO") == "Focusrite USB ASIO");
  CHECK(am::asioDriverToSkip(true, "asio", "Focusrite USB ASIO", "-") == "Focusrite USB ASIO");
  CHECK(am::asioDriverToSkip(false, "asio", "", "Focusrite USB ASIO").empty());   // stopped: every driver may be probed
  CHECK(am::asioDriverToSkip(true, "wasapi", "", "{spk}").empty());

  if (g_fail) { std::fprintf(stderr, "device_watch_test: %d failure(s)\n", g_fail); return 1; }
  std::printf("device_watch_test: listing during capture, hot-plug add/remove during capture, invalidated device recovery, exclusive in-use listing\ndevice_watch_test: OK\n");
  return 0;
}
