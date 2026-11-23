#include "net/aoip.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

namespace am::net {

static std::int64_t nowNs() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static constexpr char kSapGroup[] = "239.255.255.255";   // the administratively scoped SAP group AES67 uses
static constexpr int kSapPort = 9875;
struct FineTimer {   // Windows sleeps in 15 ms steps unless asked otherwise; 1 ms packets need 1 ms sleeps
#ifdef _WIN32
  FineTimer() { timeBeginPeriod(1); } ~FineTimer() { timeEndPeriod(1); }
#endif
};

// ---------------------------------------------------------------------------------------------------------------- sender
namespace { constexpr int kOwnSlots = 8; std::atomic<std::uint32_t> gOwn[kOwnSlots]; std::atomic<bool> gOwnUsed[kOwnSlots]; }
bool isOwnStream(std::uint32_t ssrc) noexcept { for (int i = 0; i < kOwnSlots; ++i) if (gOwnUsed[i].load(std::memory_order_acquire) && gOwn[i].load(std::memory_order_relaxed) == ssrc) return true; return false; }
bool Sender::start(const StreamDesc& d, const std::string& iface, bool announce, std::string& err) {
  stop();
  if (d.channels < 1 || d.channels > 64 || (d.bits != 16 && d.bits != 24) || d.rate < 8000 || d.port < 1 || d.port > 65535) { err = "stream format out of range"; return false; }
  std::uint8_t ip[4]; if (!parseIpv4(d.address, ip)) { err = "not an IPv4 address: " + d.address; return false; }
  if (static_cast<std::size_t>(d.framesPerPacket()) * static_cast<std::size_t>(d.channels) * static_cast<std::size_t>(d.bits / 8) > 1440) { err = "too many channels for one packet at this packet time"; return false; }
  d_ = d; iface_ = iface; announce_ = announce;
  if (d_.origin.empty() || d_.origin == "0.0.0.0") { d_.origin = !iface.empty() ? iface : routeSource(d.address); if (d_.origin.empty()) d_.origin = "0.0.0.0"; }
  sock_ = udpOpen(err); if (sock_ == kNoSock) return false;
  udpMulticastOut(sock_, iface_, d_.ttl, true);
  if (announce_) { std::string e2; sap_ = udpOpen(e2); if (sap_ != kNoSock) udpMulticastOut(sap_, iface_, d_.ttl, true); }
  cap_ = static_cast<std::size_t>(d_.rate);   // one second of frames
  ring_.assign(cap_ * static_cast<std::size_t>(d_.channels), 0.0f); w_ = 0; r_ = 0; packets_ = 0; dropped_ = 0; mismatch_ = false;
  run_ = true; th_ = std::thread([this] { loop(); });
  return true;
}

void Sender::stop() {
  if (!run_.exchange(false)) { if (th_.joinable()) th_.join(); return; }
  if (th_.joinable()) th_.join();
  if (slot_ >= 0) { gOwnUsed[slot_].store(false, std::memory_order_release); slot_ = -1; }
  if (sap_ != kNoSock) { const auto bye = sapBuild(d_, true); udpSendTo(sap_, kSapGroup, kSapPort, bye.data(), bye.size()); }
  udpClose(sock_); udpClose(sap_);
}

SendStats Sender::stats() const { SendStats s; s.running = run_.load(); s.packets = packets_.load(); s.droppedFrames = dropped_.load(); s.rateMismatch = mismatch_.load(); return s; }

void Sender::onBlock(const float* const* strips, int nStrips, const float* L, const float* R, std::size_t frames, double sampleRate) {
  if (!run_.load(std::memory_order_relaxed)) return;
  if (std::fabs(sampleRate - d_.rate) > 0.5) { mismatch_.store(true, std::memory_order_relaxed); return; }
  const std::uint64_t w = w_.load(std::memory_order_relaxed), r = r_.load(std::memory_order_acquire);
  const std::size_t space = cap_ - static_cast<std::size_t>(w - r), n = std::min(frames, space), ch = static_cast<std::size_t>(d_.channels);
  if (n < frames) dropped_.fetch_add(frames - n, std::memory_order_relaxed);
  for (std::size_t i = 0; i < n; ++i) {
    float* f = &ring_[static_cast<std::size_t>((w + i) % cap_) * ch];
    if (ch == 1) { f[0] = 0.5f * (L[i] + R[i]); continue; }
    f[0] = L[i]; f[1] = R[i];
    for (std::size_t c = 2; c < ch; ++c) { const int s = static_cast<int>(c) - 2; f[c] = (s < nStrips && strips[s]) ? strips[s][i] : 0.0f; }   // channels 3.. are the strips before their faders
  }
  w_.store(w + n, std::memory_order_release);
}

void Sender::loop() {
  FineTimer fine; std::mt19937 rng(static_cast<unsigned>(nowNs()));
  RtpHeader h; h.pt = static_cast<std::uint8_t>(d_.pt); h.seq = static_cast<std::uint16_t>(rng()); h.ts = static_cast<std::uint32_t>(rng()); h.ssrc = static_cast<std::uint32_t>(rng());
  for (int i = 0; i < kOwnSlots && slot_ < 0; ++i) { bool f = false; if (gOwnUsed[i].compare_exchange_strong(f, true)) { gOwn[i].store(h.ssrc); slot_ = i; } }   // known before the first packet leaves
  const std::size_t fpp = static_cast<std::size_t>(d_.framesPerPacket()), ch = static_cast<std::size_t>(d_.channels);
  std::vector<float> frame(fpp * ch); std::vector<std::uint8_t> pkt(kRtpHeader + fpp * ch * static_cast<std::size_t>(d_.bits / 8));
  const std::int64_t stepNs = static_cast<std::int64_t>(1e9 * static_cast<double>(fpp) / d_.rate);
  std::int64_t next = nowNs(), nextSap = next;
  while (run_.load()) {
    const std::int64_t now = nowNs();
    if (sap_ != kNoSock && now >= nextSap) { const auto a = sapBuild(d_, false); udpSendTo(sap_, kSapGroup, kSapPort, a.data(), a.size()); nextSap = now + 30'000'000'000LL; }
    if (now - next > 50'000'000) next = now;                       // far behind (the program was stopped for a moment): start the pacing again
    const std::uint64_t r = r_.load(std::memory_order_relaxed);
    if (w_.load(std::memory_order_acquire) - r >= fpp && now >= next) {
      for (std::size_t i = 0; i < fpp; ++i) std::memcpy(&frame[i * ch], &ring_[static_cast<std::size_t>((r + i) % cap_) * ch], ch * sizeof(float));
      r_.store(r + fpp, std::memory_order_release);
      rtpWrite(pkt.data(), h); pcmEncode(frame.data(), fpp, d_.channels, d_.bits, pkt.data() + kRtpHeader);
      udpSendTo(sock_, d_.address, d_.port, pkt.data(), pkt.size());
      ++h.seq; h.ts += static_cast<std::uint32_t>(fpp); packets_.fetch_add(1, std::memory_order_relaxed); next += stepNs;
      continue;                                                    // a late wake-up sends what is due without sleeping in between
    }
    std::this_thread::sleep_for(std::chrono::microseconds(400));
  }
}

// -------------------------------------------------------------------------------------------------------------- receiver
bool Receiver::start(const StreamDesc& d, const std::string& iface, int latencyMs, std::string& err) {
  stop();
  if (d.channels < 1 || d.channels > 64 || (d.bits != 16 && d.bits != 24) || d.rate < 8000 || d.port < 0 || d.port > 65535) { err = "stream format out of range"; return false; }
  d_ = d; latencyMs_ = std::clamp(latencyMs, 5, 500);
  sock_ = udpOpen(err); if (sock_ == kNoSock) return false;
  const bool multi = isMulticast(d.address);
  // a multicast stream is taken on every interface's port; a direct one only on the address asked for ("" or "0.0.0.0" = any)
  const std::string bindTo = multi ? std::string() : ((d.address == "0.0.0.0") ? std::string() : (d.address.empty() ? std::string() : d.address));
  if (!udpBind(sock_, bindTo, d.port, err)) { udpClose(sock_); return false; }
  if (multi && !udpJoin(sock_, d.address, iface, err)) { udpClose(sock_); return false; }
  port_ = udpLocalPort(sock_); udpTimeout(sock_, 100);
  cap_ = static_cast<std::uint64_t>(d_.rate) * 2;   // two seconds
  ring_.assign(static_cast<std::size_t>(cap_) * static_cast<std::size_t>(d_.channels), 0.0f);
  w_ = 0; r_ = 0; frac_ = 0; fillAvg_ = 0; primed_ = false; packets_ = 0; lost_ = 0; late_ = 0; under_ = 0; resync_ = 0; self_ = 0; lastNs_ = 0; fromIp_ = 0; fillMs_ = 0;
  run_ = true; th_ = std::thread([this] { loop(); });
  return true;
}

void Receiver::stop() { run_ = false; if (th_.joinable()) th_.join(); udpClose(sock_); }

RecvStats Receiver::stats() const {
  RecvStats s; s.running = run_.load(); s.packets = packets_.load(); s.lost = lost_.load(); s.late = late_.load(); s.underruns = under_.load(); s.resyncs = resync_.load(); s.selfLoop = self_.load(); s.fillMs = fillMs_.load();
  const std::int64_t last = lastNs_.load(); s.idleMs = last ? static_cast<int>((nowNs() - last) / 1'000'000) : -1;
  if (const std::uint32_t ip = fromIp_.load()) s.from = ipToString(ip);
  return s;
}

void Receiver::loop() {
  std::vector<std::uint8_t> buf(65536); std::vector<float> pcm;
  const std::size_t ch = static_cast<std::size_t>(d_.channels), bytes = ch * static_cast<std::size_t>(d_.bits / 8);
  bool have = false; std::uint16_t expect = 0; std::uint32_t ssrc = 0;
  auto push = [&](const float* src, std::size_t frames) {          // src nullptr = silence (lost packets keep their time)
    const std::uint64_t w = w_.load(std::memory_order_relaxed), r = r_.load(std::memory_order_acquire);
    const std::uint64_t space = cap_ - 1 - std::min<std::uint64_t>(cap_ - 1, w - r); const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(frames, space));
    for (std::size_t i = 0; i < n; ++i) { float* f = &ring_[static_cast<std::size_t>((w + i) % cap_) * ch]; if (src) std::memcpy(f, src + i * ch, ch * sizeof(float)); else std::memset(f, 0, ch * sizeof(float)); }
    w_.store(w + n, std::memory_order_release);
  };
  while (run_.load()) {
    Peer from; const int n = udpRecv(sock_, buf.data(), buf.size(), &from);
    if (n < 0) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); continue; }
    if (n == 0) continue;
    RtpHeader h; std::size_t off = 0, len = 0;
    if (!rtpParse(buf.data(), static_cast<std::size_t>(n), h, off, len) || len == 0 || len % bytes != 0) continue;
    const std::size_t frames = len / bytes;
    if (rejectOwn_.load(std::memory_order_relaxed) && isOwnStream(h.ssrc)) { self_.fetch_add(1, std::memory_order_relaxed); continue; }   // our own send looped back (multicast loopback, same group/port): never mixed in again
    if (have && h.ssrc != ssrc) { const std::int64_t idle = nowNs() - lastNs_.load(); if (idle < 500'000'000) continue; have = false; }   // a second sender on the same address is ignored while the first one is alive
    if (!have) { have = true; ssrc = h.ssrc; expect = h.seq; resync_.fetch_add(1); }
    const std::int16_t diff = static_cast<std::int16_t>(h.seq - expect);
    if (diff < 0) { if (diff > -200) { late_.fetch_add(1); continue; } expect = h.seq; resync_.fetch_add(1); }   // late or doubled: dropped; far off: the sender restarted
    else if (diff > 0) { if (diff <= 100) { lost_.fetch_add(static_cast<std::uint64_t>(diff)); push(nullptr, frames * static_cast<std::size_t>(diff)); } else resync_.fetch_add(1); }
    pcm.resize(frames * ch); pcmDecode(buf.data() + off, frames, d_.channels, d_.bits, pcm.data()); push(pcm.data(), frames);
    expect = static_cast<std::uint16_t>(h.seq + 1); packets_.fetch_add(1, std::memory_order_relaxed); lastNs_.store(nowNs()); fromIp_.store(from.ip);
  }
}

void Receiver::pull(float* const* out, int channels, std::size_t frames, double sampleRate) {
  auto silence = [&] { for (int c = 0; c < channels; ++c) std::memset(out[c], 0, frames * sizeof(float)); };
  if (!run_.load(std::memory_order_relaxed) || sampleRate <= 0) { silence(); return; }
  const double base = d_.rate / sampleRate, target = std::max(d_.rate * latencyMs_ / 1000.0, 2.0 * base * static_cast<double>(frames));   // never less than two blocks: the card takes a block at a time
  std::uint64_t r = r_.load(std::memory_order_relaxed); const std::uint64_t w = w_.load(std::memory_order_acquire);
  double avail = static_cast<double>(w - r) - frac_;
  if (!primed_) {
    if (avail < target) { fillMs_.store(avail * 1000.0 / d_.rate, std::memory_order_relaxed); silence(); return; }
    r = w - static_cast<std::uint64_t>(target); frac_ = 0; avail = target; fillAvg_ = target; primed_ = true;     // start exactly one buffer behind the newest sample
  }
  if (avail > 3 * target + base * static_cast<double>(frames) + 64) { r = w - static_cast<std::uint64_t>(target); frac_ = 0; avail = target; fillAvg_ = target; resync_.fetch_add(1, std::memory_order_relaxed); }   // far too much queued (a stall): jump back to the target
  fillAvg_ += (avail - fillAvg_) * 0.02;
  // follow the sender's clock: read a little faster when the buffer runs high, slower when it runs low (0.3 % at most: about 5 cents)
  const double ratio = base * (1.0 + std::clamp((fillAvg_ - target) / target * 0.004, -0.003, 0.003));
  if (avail < ratio * static_cast<double>(frames) + 4) { under_.fetch_add(1, std::memory_order_relaxed); primed_ = false; r_.store(r, std::memory_order_release); fillMs_.store(avail * 1000.0 / d_.rate, std::memory_order_relaxed); silence(); return; }
  const int nc = std::min(channels, d_.channels); double pos = frac_;
  const bool exact = ratio == 1.0 && pos == 0.0;
  for (std::size_t i = 0; i < frames; ++i) {
    const std::uint64_t k = r + static_cast<std::uint64_t>(pos);
    if (exact) { for (int c = 0; c < nc; ++c) out[c][i] = at(k, c); }
    else {
      const float t = static_cast<float>(pos - std::floor(pos)); const std::uint64_t km = k > 0 ? k - 1 : 0;
      for (int c = 0; c < nc; ++c) {                                   // 4-point Hermite
        const float y0 = at(km, c), y1 = at(k, c), y2 = at(k + 1, c), y3 = at(k + 2, c);
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        out[c][i] = ((c3 * t + c2) * t + c1) * t + y1;
      }
    }
    pos += ratio;
  }
  for (int c = nc; c < channels; ++c) std::memset(out[c], 0, frames * sizeof(float));
  const double whole = std::floor(pos); r += static_cast<std::uint64_t>(whole); frac_ = pos - whole;
  r_.store(r, std::memory_order_release); fillMs_.store((avail - ratio * static_cast<double>(frames)) * 1000.0 / d_.rate, std::memory_order_relaxed);
}

// ------------------------------------------------------------------------------------------------------------- discovery
bool Discovery::start(const std::string& iface, std::string& err) {
  stop();
  sock_ = udpOpen(err); if (sock_ == kNoSock) return false;
  if (!udpBind(sock_, "", kSapPort, err)) { udpClose(sock_); return false; }
  std::string e2; const bool a = udpJoin(sock_, kSapGroup, iface, e2), b = udpJoin(sock_, "224.2.127.254", iface, e2);   // AES67's group and the global one
  if (!a && !b) { err = e2; udpClose(sock_); return false; }
  udpTimeout(sock_, 200); run_ = true; th_ = std::thread([this] { loop(); });
  return true;
}
void Discovery::stop() { run_ = false; if (th_.joinable()) th_.join(); udpClose(sock_); }
void Discovery::loop() { std::vector<std::uint8_t> buf(8192); while (run_.load()) { const int n = udpRecv(sock_, buf.data(), buf.size()); if (n < 0) std::this_thread::sleep_for(std::chrono::milliseconds(50)); else if (n > 0) inject(buf.data(), static_cast<std::size_t>(n)); } }

void Discovery::inject(const std::uint8_t* sap, std::size_t n) {
  StreamDesc d; bool del = false; std::uint16_t hash = 0;
  if (!sapParse(sap, n, d, del, hash)) return;
  std::lock_guard<std::mutex> g(m_);
  const auto it = std::find_if(e_.begin(), e_.end(), [&](const Entry& e) { return e.hash == hash && e.d.origin == d.origin; });
  if (del) { if (it != e_.end()) e_.erase(it); return; }
  if (it != e_.end()) { it->d = d; it->seenNs = nowNs(); } else if (e_.size() < 256) e_.push_back({d, hash, nowNs()});
}

std::vector<Found> Discovery::list() const {
  std::vector<Found> out; const std::int64_t now = nowNs();
  { std::lock_guard<std::mutex> g(m_); for (const Entry& e : e_) { const std::int64_t age = (now - e.seenNs) / 1'000'000; if (age < 300'000) out.push_back({e.d, e.hash, static_cast<int>(age)}); } }
  std::sort(out.begin(), out.end(), [](const Found& a, const Found& b) { return a.ageMs < b.ageMs; });
  return out;
}

// ------------------------------------------------------------------------------------------------------------------ Aoip
std::string transportsJson() {
  return "["
    "{\"id\":\"aes67\",\"name\":\"AES67 (RTP, L24 / L16)\",\"available\":true,\"note\":\"Open standard. Also carries RAVENNA and SMPTE ST 2110-30 streams, and reaches devices of other makers through their AES67 mode. No PTP clock: the receiver follows each stream's own clock.\"},"
    "{\"id\":\"sap\",\"name\":\"SAP / SDP stream discovery\",\"available\":true,\"note\":\"Lists the AES67 streams announced on this network and announces ours.\"},"
    "{\"id\":\"rtpmidi\",\"name\":\"Network MIDI (RTP-MIDI, RFC 6295)\",\"available\":true,\"note\":\"A session other computers and tablets connect to by address.\"},"
    "{\"id\":\"midi\",\"name\":\"MIDI ports of this computer\",\"available\":true,\"note\":\"USB and DIN controllers through the system's own MIDI interface.\"},"
    "{\"id\":\"srt\",\"name\":\"SRT\",\"available\":false,\"note\":\"Needs the open-source SRT library built into the program; it is not in this build.\"},"
    "{\"id\":\"dante\",\"name\":\"Dante\",\"available\":false,\"note\":\"A protocol owned by its maker and licensed to manufacturers; its protocol is not implemented here. Dante reaches this console through Audinate's Dante Virtual Soundcard or Dante Via (a sound card) or the device's AES67 mode: see DANTE + CLOCK.\"},"
    "{\"id\":\"ndi\",\"name\":\"NDI\",\"available\":false,\"note\":\"Needs its maker's SDK and runtime under their licence; not in this build.\"}"
    "]";
}

Aoip::~Aoip() { sendStop(); receiveStop(); disc_.stop(); }
void Aoip::settle() {   // the audio thread may still be inside the block that saw the old pointer
  if (!rt_.running()) return;
  const double sr = rt_.sampleRate() > 0 ? rt_.sampleRate() : 48000.0;
  std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(2000.0 * std::max(64u, rt_.blockFrames()) / sr) + 5));
}
bool Aoip::sendStart(StreamDesc d, const std::string& iface, bool announce, std::string& err) {
  sendStop(); d.rate = static_cast<int>(rt_.sampleRate() + 0.5);
  if (!tx_.start(d, iface, announce, err)) return false;
  rt_.setNetTap(&tx_); return true;
}
void Aoip::sendStop() { if (!tx_.running()) return; rt_.setNetTap(nullptr); settle(); tx_.stop(); }
bool Aoip::receiveStart(const StreamDesc& d, const std::string& iface, int latencyMs, std::string& err) {
  receiveStop();
  if (!rx_.start(d, iface, latencyMs, err)) return false;
  rt_.setNetInput(&rx_); return true;
}
void Aoip::receiveStop() { if (!rx_.running()) return; rt_.setNetInput(nullptr); settle(); rx_.stop(); }

static std::string esc(const std::string& s) { std::string o; for (char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += c; } else if (static_cast<unsigned char>(c) < 0x20) o += ' '; else o += c; } return o; }
std::string Aoip::statsJson() const {
  const SendStats t = tx_.stats(); const RecvStats r = rx_.stats(); char b[900];
  std::snprintf(b, sizeof b,
    "{\"send\":{\"running\":%s,\"name\":\"%s\",\"address\":\"%s\",\"port\":%d,\"channels\":%d,\"bits\":%d,\"rate\":%d,\"packets\":%llu,\"droppedFrames\":%llu,\"rateMismatch\":%s},"
    "\"receive\":{\"running\":%s,\"address\":\"%s\",\"port\":%d,\"channels\":%d,\"bits\":%d,\"rate\":%d,\"packets\":%llu,\"lost\":%llu,\"late\":%llu,\"underruns\":%llu,\"fillMs\":%.1f,\"idleMs\":%d,\"from\":\"%s\"}}",
    t.running ? "true" : "false", esc(tx_.desc().name).c_str(), esc(tx_.desc().address).c_str(), tx_.desc().port, tx_.desc().channels, tx_.desc().bits, tx_.desc().rate,
    static_cast<unsigned long long>(t.packets), static_cast<unsigned long long>(t.droppedFrames), t.rateMismatch ? "true" : "false",
    r.running ? "true" : "false", esc(rx_.desc().address).c_str(), rx_.port(), rx_.desc().channels, rx_.desc().bits, rx_.desc().rate,
    static_cast<unsigned long long>(r.packets), static_cast<unsigned long long>(r.lost), static_cast<unsigned long long>(r.late), static_cast<unsigned long long>(r.underruns), r.fillMs, r.idleMs, r.from.c_str());
  return b;
}

}  // namespace am::net
