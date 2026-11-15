// THE NETWORK CLOCK (PTP, IEEE 1588-2008 version 2): AES67 and every Dante device in its AES67 mode set their media clock from a PTP
// grandmaster on the network. This monitor LISTENS only (multicast 224.0.1.129, event port 319 and general port 320): it decodes the
// Announce messages (who the grandmaster is, its domain, priority and clock quality) and counts the Sync / Follow_Up messages, so the
// console can show whether a network clock is there and which device leads it. It never sends, never takes part in the best-master
// election and does not set this computer's clock.
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "net/sock.hpp"

namespace am::net {

struct PtpMaster {
  std::string id;                 // the grandmaster's clock identity, "xx-xx-xx-xx-xx-xx-xx-xx"
  std::string from;               // the IP address the announcements come from
  int domain = 0, priority1 = 128, priority2 = 128, clockClass = 248, accuracy = 0xFE, stepsRemoved = 0, utcOffset = 0, timeSource = 0xA0;
  double syncPerSec = 0;          // Sync messages per second seen from it (0: announced, no sync heard)
  double lastSeenS = 0;           // seconds since its last message
  std::uint64_t ptpSeconds = 0;   // the last precise time it sent (Follow_Up or one-step Sync), seconds since 1970 TAI
};

// one PTP v2 packet decoded (also used by the tests): type 0 Sync, 8 Follow_Up, 11 Announce; false for anything else / malformed
struct PtpPacket { int type = -1, domain = 0; std::string sourceId; bool twoStep = false; std::uint64_t seconds = 0; std::uint32_t nanos = 0;
                   int priority1 = 0, priority2 = 0, clockClass = 0, accuracy = 0, stepsRemoved = 0, utcOffset = 0, timeSource = 0; std::string gmId; };
bool parsePtp(const std::uint8_t* p, std::size_t n, PtpPacket& out);
std::string ptpClockName(int clockClass);       // "locked to GPS / primary reference", "free-running" ...

class PtpMonitor {
 public:
  ~PtpMonitor() { stop(); }
  bool start(const std::string& iface, std::string& err);   // iface "" = the system's choice
  void stop();
  bool running() const { return run_.load(); }
  std::vector<PtpMaster> masters() const;     // the grandmasters heard in the last 30 s, the best (lowest priority1, class, ...) first
  void inject(const std::uint8_t* p, std::size_t n, const std::string& from);   // a packet as if received (tests)
 private:
  void loop(Sock s);
  struct Entry { PtpMaster m; std::int64_t seenNs = 0, firstSyncNs = 0; std::uint64_t syncs = 0; };
  Sock ev_ = kNoSock, gen_ = kNoSock; std::atomic<bool> run_{false}; std::thread a_, b_; mutable std::mutex mu_; std::vector<Entry> e_;
};

}  // namespace am::net
