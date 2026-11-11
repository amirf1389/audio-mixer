// DRIVER INTELLIGENCE, ported from the web console's bridge/driver-lock.js + bridge/hw-match.js into the native device layer.
//   HARDWARE MATCH: which DeviceInfo entries are the same hardware through different drivers (an ASIO driver and its WASAPI endpoint). A chip /
//   product family wins first (Behringer UM2 is "BEHRINGER USB AUDIO" via ASIO but "Microphone (USB Audio CODEC)" via Windows), then shared words.
//   LADDER, best first, for one interface: ASIO (the device's own driver; a wrapper such as ASIO4ALL only when no device here has its own ASIO
//   driver) -> WASAPI EXCLUSIVE (the same hardware's endpoint, DeviceConfig::exclusive) -> WASAPI SHARED. The native engine has no DirectSound / MME.
//   REALTEK PIN: onboard Realtek's ASIO driver is known to be unreliable; by default its ladder goes straight to the chip's WASAPI endpoint and the
//   ASIO driver is never opened. UNPIN lets it go first again; the choice is saved. An in-app routing choice only: no driver file, registry key,
//   Windows setting or scheduled task is touched.
//   AI DRIVER LOCK: when a start had to fall back (ASIO busy, refused, silent), the step that worked is LOCKED for that interface and tried first
//   next time; a lock on a lower step is retried after kRetryMs; FORGET clears it. A clean ASIO run locks ASIO.
// Store: <data>/driver-lock.json (am::paths, per OS) (AMIXOS_DRIVER_LOCK_FILE): device names and modes only, nothing personal. Pure functions over device lists,
// so everything is testable without hardware (test/driver_intel_test.cpp).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "audiomixer/runtime.hpp"

namespace am::drv {

constexpr std::int64_t kRetryMs = 6LL * 3600 * 1000;

// hardware match
std::string family(const std::string& name);              // "realtek", "behringer", "focusrite" ... or ""
bool isWrapper(const std::string& name);                   // ASIO4ALL, FlexASIO, Generic Low Latency, ASIO2WASAPI
int score(const std::string& a, const std::string& b);     // 100 same family, +10 per shared meaningful word; 0 = not the same hardware
// the same hardware through WASAPI (the best match; for a wrapper the default endpoint not owned by a native-ASIO device)
const DeviceInfo* twin(const std::string& asioName, const std::vector<DeviceInfo>& devices, bool wantInput, bool wantOutput);

// id / name: the output endpoint when an output is wanted, else the input one (what the UI shows); inId / outId: the two endpoints of the step
// (Windows endpoints are one-directional: an interface is "Microphone (USB Audio CODEC)" + "Speakers (USB Audio CODEC)", so a duplex start needs both)
struct Step { int step = 0; std::string mode; std::string backend; std::string id; std::string name; std::string inId, outId; bool available = false; bool exclusive = false; bool pinned = false; std::string why; };
struct Pin { std::string name; bool on = true; std::string reason; };
struct Lock { std::string name; std::string mode; std::string device; std::string reason; std::int64_t at = 0; bool pinned = false; };

bool pinRule(const std::string& asioName);                 // the built-in rule applies (Realtek, not a wrapper)
Pin pinOf(const std::string& asioName);                     // the pin's state (default on)
Pin setPin(const std::string& asioName, bool on);
std::vector<Pin> pins(const std::vector<DeviceInfo>& devices);

// the ladder for one ASIO device over the device list; wantInput / wantOutput say which directions the step must offer
// The fallback policy after ASIO: "all" (WASAPI exclusive, WASAPI shared, DirectSound), "dsound" (DirectSound only: WASAPI is optional, never a lock), "none" (ASIO only). Kept in the lock file.
// OPEN ROUTING (off by default here; the console switches it on): the ASIO driver that was picked is the one that is opened first, whatever it
// is. A wrapper (ASIO4ALL, FlexASIO) is opened even when a device with its own ASIO driver is present, and the manufacturer's driver of an
// onboard Realtek is opened instead of being pinned to its Windows endpoint. The steps after it stay as the fallback when it does not start.
bool openRouting();
void setOpenRouting(bool on);
std::string fallback();
void setFallback(const std::string& policy);
std::vector<Step> ladder(const DeviceInfo& asio, const std::vector<DeviceInfo>& devices, bool wantInput, bool wantOutput, std::int64_t nowMs);

// the lock store
bool getLock(const std::string& name, Lock& out, std::int64_t nowMs);
Lock learn(const std::string& name, const std::string& mode, const std::string& device, const std::string& reason, std::int64_t nowMs);
int forget(const std::string& name);                       // "*" = all; the number removed
std::vector<Lock> locks();
std::string lockFile();
void resetCache();                                           // tests: re-read the file

// one start attempt the engine should make, in order
struct Attempt { std::string mode; std::string backend; std::string inDevice; std::string outDevice; bool exclusive = false; std::string why; bool fromLock = false; bool pinned = false; };
// What to try for a start on `backend` / `in` / `out`: for an ASIO device the ladder (pin and lock applied), else the plain request.
std::vector<Attempt> plan(const std::string& backend, const std::string& inId, const std::string& outId, const std::vector<DeviceInfo>& devices, std::int64_t nowMs);
// RESCUE: what to try when every step of the plan failed, so that the console does not stay without input and output. Read from the device
// list only (nothing is installed, registered or changed), in this order:
//   "realtek-asio"  the onboard Realtek's own ASIO driver, opened directly even when the Realtek pin would normally keep it closed
//   "rws"           Realtek Windows Shared: the Realtek endpoints through WASAPI shared (no lock; other programs keep their sound)
//   "wasapi-shared" the system's default endpoints through WASAPI shared
//   "dsound"        the system's default endpoints through DirectSound
// A step is left out when the list has no such device or `tried` already holds the same backend and devices.
std::vector<Attempt> rescue(const std::vector<DeviceInfo>& devices, bool wantInput, bool wantOutput, const std::vector<Attempt>& tried);

}  // namespace am::drv
