// see ptp.hpp: a listen-only monitor of the PTP v2 grandmasters on the network
#include "net/ptp.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <tuple>

namespace am::net {

static std::int64_t nowNs() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static std::uint64_t be(const std::uint8_t* p, int n) { std::uint64_t v = 0; for (int i = 0; i < n; ++i) v = (v << 8) | p[i]; return v; }
static std::string idOf(const std::uint8_t* p) { char b[32]; std::snprintf(b, sizeof b, "%02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x", p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]); return b; }

bool parsePtp(const std::uint8_t* p, std::size_t n, PtpPacket& o) {
  if (!p || n < 34) return false;
  if ((p[1] & 0x0F) != 2) return false;                                  // PTP version 2 only
  const int type = p[0] & 0x0F; const std::size_t len = static_cast<std::size_t>(be(p + 2, 2));
  if (len < 34 || len > n) return false;
  o = PtpPacket{}; o.type = type; o.domain = p[4]; o.twoStep = (p[6] & 0x02) != 0; o.sourceId = idOf(p + 20);
  if (type == 0 || type == 8) {                                          // Sync / Follow_Up: the origin timestamp
    if (len < 44) return false;
    o.seconds = be(p + 34, 6); o.nanos = static_cast<std::uint32_t>(be(p + 40, 4)); return o.nanos < 1000000000u;
  }
  if (type == 11) {                                                      // Announce
    if (len < 64) return false;
    o.seconds = be(p + 34, 6); o.nanos = static_cast<std::uint32_t>(be(p + 40, 4));
    o.utcOffset = static_cast<std::int16_t>(be(p + 44, 2)); o.priority1 = p[47]; o.clockClass = p[48]; o.accuracy = p[49]; o.priority2 = p[52];
    o.gmId = idOf(p + 53); o.stepsRemoved = static_cast<int>(be(p + 61, 2)); o.timeSource = p[63]; return true;
  }
  return false;
}

std::string ptpClockName(int c) {
  if (c == 6 || c == 7) return "locked to a primary reference (GPS)";
  if (c == 13 || c == 14) return "locked to an application reference";
  if (c >= 52 && c <= 58) return "holdover (lost its reference)";
  if (c >= 187 && c <= 193) return "holdover";
  if (c == 248) return "free-running (default)";
  if (c == 255) return "follower only";
  return "class " + std::to_string(c);
}

bool PtpMonitor::start(const std::string& iface, std::string& err) {
  stop();
  ev_ = udpOpen(err); gen_ = udpOpen(err); if (ev_ == kNoSock || gen_ == kNoSock) { udpClose(ev_); udpClose(gen_); return false; }
  if (!udpBind(ev_, "", 319, err) || !udpBind(gen_, "", 320, err)) { udpClose(ev_); udpClose(gen_); err = "the PTP ports 319 / 320 are taken (a PTP service is already running here): " + err; return false; }
  std::string e2; if (!udpJoin(ev_, "224.0.1.129", iface, e2) || !udpJoin(gen_, "224.0.1.129", iface, e2)) { udpClose(ev_); udpClose(gen_); err = e2; return false; }
  udpTimeout(ev_, 200); udpTimeout(gen_, 200); run_ = true;
  a_ = std::thread([this] { loop(ev_); }); b_ = std::thread([this] { loop(gen_); });
  return true;
}
void PtpMonitor::stop() { run_ = false; if (a_.joinable()) a_.join(); if (b_.joinable()) b_.join(); udpClose(ev_); udpClose(gen_); }
void PtpMonitor::loop(Sock s) {
  std::vector<std::uint8_t> buf(1500); Peer from;
  while (run_.load()) { const int n = udpRecv(s, buf.data(), buf.size(), &from); if (n < 0) std::this_thread::sleep_for(std::chrono::milliseconds(50)); else if (n > 0) inject(buf.data(), static_cast<std::size_t>(n), ipToString(from.ip)); }
}
void PtpMonitor::inject(const std::uint8_t* p, std::size_t n, const std::string& from) {
  PtpPacket k; if (!parsePtp(p, n, k)) return;
  std::lock_guard<std::mutex> g(mu_); const std::int64_t now = nowNs();
  // an Announce names the grandmaster; Sync / Follow_Up come from the port that leads (the grandmaster, or a boundary clock in between)
  auto find = [&](const std::string& src, int dom) { return std::find_if(e_.begin(), e_.end(), [&](const Entry& e) { return e.m.domain == dom && (e.m.id == src || e.m.from == from); }); };
  if (k.type == 11) {
    auto it = std::find_if(e_.begin(), e_.end(), [&](const Entry& e) { return e.m.id == k.gmId && e.m.domain == k.domain; });
    if (it == e_.end()) { if (e_.size() >= 32) return; e_.push_back({}); it = e_.end() - 1; it->m.id = k.gmId; it->m.domain = k.domain; }
    PtpMaster& m = it->m; m.from = from; m.priority1 = k.priority1; m.priority2 = k.priority2; m.clockClass = k.clockClass; m.accuracy = k.accuracy;
    m.stepsRemoved = k.stepsRemoved; m.utcOffset = k.utcOffset; m.timeSource = k.timeSource; it->seenNs = now;
    return;
  }
  auto it = find(k.sourceId, k.domain); if (it == e_.end()) return;   // sync from a clock that has not announced yet
  if (k.type == 0) { if (!it->firstSyncNs) it->firstSyncNs = now; ++it->syncs; }
  if (k.type == 8 || (k.type == 0 && !k.twoStep)) it->m.ptpSeconds = k.seconds;
  it->seenNs = now;
}
std::vector<PtpMaster> PtpMonitor::masters() const {
  std::lock_guard<std::mutex> g(mu_); const std::int64_t now = nowNs(); std::vector<PtpMaster> v;
  for (const Entry& e : e_) { if (now - e.seenNs > 30'000'000'000LL) continue; PtpMaster m = e.m; m.lastSeenS = static_cast<double>(now - e.seenNs) / 1e9;
    const double span = e.firstSyncNs ? static_cast<double>(now - e.firstSyncNs) / 1e9 : 0; m.syncPerSec = span > 0.5 ? static_cast<double>(e.syncs) / span : 0; v.push_back(m); }
  std::sort(v.begin(), v.end(), [](const PtpMaster& a, const PtpMaster& b) { return std::tie(a.priority1, a.clockClass, a.accuracy, a.priority2, a.id) < std::tie(b.priority1, b.clockClass, b.accuracy, b.priority2, b.id); });   // IEEE 1588 best master order
  return v;
}

}  // namespace am::net
