// DEVICE WATCH: sound cards plugged in / pulled out / taken by another program while the console is capturing.
// - DeviceChangeWatch: the system's own notifications (Windows: IMMNotificationClient on its own MTA thread); inert elsewhere.
// - DeviceMonitor: when a full rescan is due (a notification, or the cheap fingerprint changed); usable from the interface thread.
// - classifyStreamError: what a WASAPI error means for a running stream (the device is gone / held by another program).
// - diffDeviceNames / asioDriverToSkip: what changed, and which ASIO driver a rescan must not load (the one this console holds open).
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "audiomixer/runtime.hpp"

namespace am {

enum class StreamFault { None, Lost, InUse, Other };
// hr: an HRESULT. AUDCLNT_E_DEVICE_INVALIDATED / SERVICE_NOT_RUNNING / RESOURCES_INVALIDATED -> Lost; AUDCLNT_E_DEVICE_IN_USE -> InUse
StreamFault classifyStreamError(long hr);

class DeviceMonitor {
 public:
  void notify() { notified_.store(true); }   // any thread (the system's notification thread)
  bool notified() const { return notified_.load(); }
  // the latest fingerprint: true when a full rescan is due (it changed after the first, or a notification came in since the last feed)
  bool feed(const std::string& fingerprint) {
    const bool note = notified_.exchange(false);
    const bool changed = have_ && fingerprint != fp_;
    fp_ = fingerprint; have_ = true;
    return changed || note;
  }
 private:
  std::string fp_; bool have_ = false; std::atomic<bool> notified_{false};
};

struct DeviceDiff { std::vector<std::string> added, gone; bool openDeviceGone = false; };
DeviceDiff diffDeviceNames(const std::vector<std::string>& before, const std::vector<std::string>& after, const std::vector<std::string>& openNames);

// the ASIO driver a rescan must list without loading (ASIO drivers serve one client: probing the open one hangs or steals it); "" = none
std::string asioDriverToSkip(bool running, const std::string& backend, const std::string& inId, const std::string& outId);

// listDevices with one ASIO driver listed unprobed and marked "in use by this console" (skipAsio "" = the same as listDevices(probeAsio))
std::vector<DeviceInfo> listDevicesExcept(bool probeAsio, const std::string& skipAsio);

class DeviceChangeWatch {
 public:
  explicit DeviceChangeWatch(std::function<void()> onChange);   // onChange runs on a system thread: it must only post / set a flag
  ~DeviceChangeWatch();                                         // unregisters; no call after it returns
  DeviceChangeWatch(const DeviceChangeWatch&) = delete; DeviceChangeWatch& operator=(const DeviceChangeWatch&) = delete;
  bool active() const;   // registered with the system (false off Windows, or when registration failed: the poll still runs)
  struct Impl;
 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace am
