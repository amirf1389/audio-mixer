#include "net/midi.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#elif defined(AM_HAVE_ALSA)
#include <alsa/asoundlib.h>
#endif

namespace am::net {

static std::int64_t nowNs() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

int midiDataBytes(std::uint8_t st) { const int t = st & 0xf0; if (st < 0x80 || st >= 0xf0) return 0; return (t == 0xc0 || t == 0xd0) ? 1 : 2; }

void MidiParser::feed(const std::uint8_t* p, std::size_t n, const MidiSink& sink) {
  for (std::size_t i = 0; i < n; ++i) {
    const std::uint8_t b = p[i];
    if (b >= 0xf8) continue;                                  // real-time: may come anywhere, changes nothing
    if (b & 0x80) {
      if (b == 0xf0) { sysex_ = true; status_ = 0; continue; }
      if (b == 0xf7) { sysex_ = false; continue; }
      sysex_ = false; have_ = 0; status_ = b < 0xf0 ? b : 0;   // system common cancels running status
      continue;
    }
    if (sysex_ || !status_) continue;
    d_[have_++] = b;
    if (have_ >= midiDataBytes(status_)) { MidiMsg m; m.status = status_; m.d1 = d_[0]; m.d2 = have_ > 1 ? d_[1] : 0; have_ = 0; if (sink) sink(m); }
  }
}

double midiFaderDb(int v) { if (v <= 0) return -120.0; if (v >= 127) return 10.0; return v <= 100 ? -60.0 + 60.0 * (v - 1) / 99.0 : 10.0 * (v - 100) / 27.0; }

static MidiAction act(MidiAction::Kind k, int strip, const MidiMsg& m) {
  MidiAction a; a.kind = k; a.strip = strip;
  switch (k) {
    case MidiAction::Fader: case MidiAction::Master: a.value = midiFaderDb(m.d2); break;
    case MidiAction::Pan: a.value = m.d2 == 64 ? 0.0 : std::clamp((m.d2 - 64) / 63.0, -1.0, 1.0); break;
    case MidiAction::Mute: case MidiAction::Solo: a.value = 1; if (m.type() == 0xb0 ? m.d2 < 64 : m.d2 == 0) a.kind = MidiAction::None; break;   // a button: act on press, not on release
    default: break;
  }
  return a;
}

MidiAction MidiMap::apply(const MidiMsg& m) const {
  const int type = m.type() == 0x80 ? 0x90 : m.type();
  {
    std::lock_guard<std::mutex> g(m_);
    for (const Entry& e : e_) if (e.type == type && e.channel == m.channel() && e.number == m.d1) { MidiMsg x = m; if (m.type() == 0x80) x.d2 = 0; return act(e.kind, e.strip, x); }
  }
  if (m.type() == 0xb0) {
    if (m.d1 == 7) return m.channel() == 15 ? act(MidiAction::Master, 0, m) : act(MidiAction::Fader, m.channel(), m);
    if (m.d1 == 10 && m.channel() != 15) return act(MidiAction::Pan, m.channel(), m);
  } else if (m.type() == 0x90 && m.d2 > 0) {
    if (m.d1 < 32) return act(MidiAction::Mute, m.d1, m);
    if (m.d1 < 64) return act(MidiAction::Solo, m.d1 - 32, m);
  }
  return {};
}

void MidiMap::learn(MidiAction::Kind kind, int strip) { std::lock_guard<std::mutex> g(m_); lk_ = kind; ls_ = strip; pending_ = kind != MidiAction::None; }
bool MidiMap::offer(const MidiMsg& m) {
  if (!pending_.load()) return false;
  const int type = m.type();
  if (type != 0xb0 && !(type == 0x90 && m.d2 > 0)) return false;
  std::lock_guard<std::mutex> g(m_);
  if (!pending_.load()) return false;
  const bool button = lk_ == MidiAction::Mute || lk_ == MidiAction::Solo;
  if (!button && type != 0xb0) return false;                 // a fader or pan needs a continuous control
  e_.erase(std::remove_if(e_.begin(), e_.end(), [&](const Entry& e) { return (e.type == type && e.channel == m.channel() && e.number == m.d1) || (e.kind == lk_ && e.strip == ls_); }), e_.end());
  e_.push_back({type, m.channel(), m.d1, lk_, ls_}); pending_ = false; return true;
}
std::string MidiMap::save() const { std::lock_guard<std::mutex> g(m_); std::ostringstream o; for (const Entry& e : e_) o << e.type << ',' << e.channel << ',' << e.number << ',' << static_cast<int>(e.kind) << ',' << e.strip << ';'; return o.str(); }
void MidiMap::load(const std::string& s) {
  std::lock_guard<std::mutex> g(m_); e_.clear(); std::istringstream in(s); std::string item;
  while (std::getline(in, item, ';')) { int t, c, n, k, st; if (std::sscanf(item.c_str(), "%d,%d,%d,%d,%d", &t, &c, &n, &k, &st) == 5 && (t == 0xb0 || t == 0x90) && c >= 0 && c < 16 && n >= 0 && n < 128 && k > 0 && k <= MidiAction::Master && st >= 0 && st < 1024 && e_.size() < 512) e_.push_back({t, c, n, static_cast<MidiAction::Kind>(k), st}); }
}
void MidiMap::clear() { std::lock_guard<std::mutex> g(m_); e_.clear(); pending_ = false; }
int MidiMap::learned() const { std::lock_guard<std::mutex> g(m_); return static_cast<int>(e_.size()); }

// ---- RTP-MIDI payload: [B J Z P LEN(4)] or [B J Z P LEN(12)], then the list: (delta time) command (delta time command)*
bool rtpMidiParse(const std::uint8_t* p, std::size_t n, const MidiSink& sink) {
  if (!p || n < 1) return false;
  const bool big = p[0] & 0x80, z = p[0] & 0x20; std::size_t len = p[0] & 0x0f, o = 1;
  if (big) { if (n < 2) return false; len = (len << 8) | p[1]; o = 2; }
  if (o + len > n) return false;
  const std::size_t end = o + len; std::uint8_t status = 0; bool first = true;
  while (o < end) {
    if (!first || z) { int k = 0; while (o < end && k < 4) { const bool more = p[o] & 0x80; ++o; ++k; if (!more) break; } if (o >= end) break; }   // delta time: 1 .. 4 bytes
    first = false;
    if (p[o] & 0x80) {
      status = p[o++];
      if (status == 0xf0 || status == 0xf7) { while (o < end && !(p[o] & 0x80)) ++o; if (o < end) ++o; status = 0; continue; }   // SysEx (segments): skipped
      if (status >= 0xf8) { status = 0; continue; }
      if (status >= 0xf0) { const int skip = status == 0xf2 ? 2 : ((status == 0xf1 || status == 0xf3) ? 1 : 0); o += static_cast<std::size_t>(skip); status = 0; continue; }
    }
    if (!status) return false;
    const int need = midiDataBytes(status);
    if (o + static_cast<std::size_t>(need) > end) return false;
    MidiMsg m; m.status = status; m.d1 = p[o]; m.d2 = need > 1 ? p[o + 1] : 0; o += static_cast<std::size_t>(need);
    if ((m.d1 | m.d2) & 0x80) return false;
    if (sink) sink(m);
  }
  return true;
}

// ---- the session: 0xFFFF + 'IN' invitation / 'OK' accepted / 'NO' refused / 'BY' end / 'CK' clock sync (all big-endian)
static std::uint32_t be32(const std::uint8_t* p) { return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) | (static_cast<std::uint32_t>(p[2]) << 8) | p[3]; }
static void put32(std::uint8_t* p, std::uint32_t v) { p[0] = static_cast<std::uint8_t>(v >> 24); p[1] = static_cast<std::uint8_t>(v >> 16); p[2] = static_cast<std::uint8_t>(v >> 8); p[3] = static_cast<std::uint8_t>(v); }

bool RtpMidi::start(const std::string& name, const std::string& bindIp, int port, MidiSink sink, std::string& err) {
  stop();
  if (port < 1024 || port > 65534) { err = "the port must be between 1024 and 65534"; return false; }
  name_ = name.empty() ? "AmF X1" : name; sink_ = std::move(sink); port_ = port;
  std::mt19937 rng(static_cast<unsigned>(nowNs())); ssrc_ = static_cast<std::uint32_t>(rng()) | 1u;
  ctl_ = udpOpen(err); if (ctl_ == kNoSock) return false;
  dat_ = udpOpen(err); if (dat_ == kNoSock) { udpClose(ctl_); return false; }
  if (!udpBind(ctl_, bindIp, port, err) || !udpBind(dat_, bindIp, port + 1, err)) { udpClose(ctl_); udpClose(dat_); return false; }
  udpTimeout(ctl_, 200); udpTimeout(dat_, 200); msgs_ = 0; { std::lock_guard<std::mutex> g(m_); peers_.clear(); }
  run_ = true; tc_ = std::thread([this] { loop(ctl_, false); }); td_ = std::thread([this] { loop(dat_, true); });
  return true;
}
void RtpMidi::stop() {
  if (run_.exchange(false)) {
    std::vector<PeerS> ps; { std::lock_guard<std::mutex> g(m_); ps = peers_; }
    for (const PeerS& p : ps) if (p.ctlPort) { std::uint8_t b[16] = {0xff, 0xff, 'B', 'Y', 0, 0, 0, 2}; put32(b + 8, 0); put32(b + 12, ssrc_); udpSendTo(ctl_, Peer{p.ip, p.ctlPort}, b, 16); }
  }
  if (tc_.joinable()) tc_.join(); if (td_.joinable()) td_.join();
  udpClose(ctl_); udpClose(dat_);
}
RtpMidiStats RtpMidi::stats() const {
  RtpMidiStats s; s.running = run_.load(); s.port = port_; s.messages = msgs_.load(); const std::int64_t now = nowNs();
  std::lock_guard<std::mutex> g(m_); for (const PeerS& p : peers_) if (now - p.seenNs < 90'000'000'000LL) { ++s.peers; s.lastPeer = p.name.empty() ? ipToString(p.ip) : p.name + " (" + ipToString(p.ip) + ")"; }
  return s;
}

void RtpMidi::handleSession(Sock s, const std::uint8_t* p, int n, const Peer& from) {
  if (n < 4) return;
  const char c0 = static_cast<char>(p[2]), c1 = static_cast<char>(p[3]);
  if (c0 == 'I' && c1 == 'N' && n >= 16) {             // invitation: version, token, their ssrc, name
    const std::uint32_t token = be32(p + 8), theirs = be32(p + 12);
    std::string name; for (int i = 16; i < n && p[i]; ++i) name += static_cast<char>(p[i] >= 0x20 ? p[i] : ' ');
    {
      std::lock_guard<std::mutex> g(m_);
      auto it = std::find_if(peers_.begin(), peers_.end(), [&](const PeerS& x) { return x.ssrc == theirs; });
      if (it == peers_.end()) { if (peers_.size() >= 16) { std::uint8_t no[16] = {0xff, 0xff, 'N', 'O', 0, 0, 0, 2}; put32(no + 8, token); put32(no + 12, ssrc_); udpSendTo(s, from, no, 16); return; } peers_.push_back({from.ip, theirs, name, nowNs(), s == ctl_ ? from.port : static_cast<std::uint16_t>(0)}); }
      else { it->ip = from.ip; it->seenNs = nowNs(); if (!name.empty()) it->name = name; if (s == ctl_) it->ctlPort = from.port; }
    }
    std::vector<std::uint8_t> ok(16 + name_.size() + 1, 0); ok[0] = 0xff; ok[1] = 0xff; ok[2] = 'O'; ok[3] = 'K'; put32(&ok[4], 2); put32(&ok[8], token); put32(&ok[12], ssrc_);
    std::memcpy(&ok[16], name_.data(), name_.size()); udpSendTo(s, from, ok.data(), ok.size());
  } else if (c0 == 'C' && c1 == 'K' && n >= 36) {      // clock sync: answer count 0 with count 1 and our time (100 microsecond units)
    const std::uint32_t theirs = be32(p + 4);
    { std::lock_guard<std::mutex> g(m_); for (PeerS& x : peers_) if (x.ssrc == theirs) x.seenNs = nowNs(); }
    if (p[8] == 0) {
      std::uint8_t r[36]; std::memcpy(r, p, 36); put32(r + 4, ssrc_); r[8] = 1;
      const std::uint64_t t = static_cast<std::uint64_t>(nowNs() / 100'000); put32(r + 20, static_cast<std::uint32_t>(t >> 32)); put32(r + 24, static_cast<std::uint32_t>(t));
      udpSendTo(s, from, r, 36);
    }
  } else if (c0 == 'B' && c1 == 'Y' && n >= 16) {
    const std::uint32_t theirs = be32(p + 12); std::lock_guard<std::mutex> g(m_);
    peers_.erase(std::remove_if(peers_.begin(), peers_.end(), [&](const PeerS& x) { return x.ssrc == theirs; }), peers_.end());
  }
}

void RtpMidi::loop(Sock s, bool data) {
  std::vector<std::uint8_t> buf(2048);
  while (run_.load()) {
    Peer from; const int n = udpRecv(s, buf.data(), buf.size(), &from);
    if (n < 0) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue; }
    if (n < 4) continue;
    if (buf[0] == 0xff && buf[1] == 0xff) { handleSession(s, buf.data(), n, from); continue; }
    if (!data || n < 13 || (buf[0] >> 6) != 2) continue;
    const std::uint32_t theirs = be32(buf.data() + 8); bool known = false;
    { std::lock_guard<std::mutex> g(m_); for (PeerS& x : peers_) if (x.ssrc == theirs && x.ip == from.ip) { known = true; x.seenNs = nowNs(); } }
    if (!known) continue;                                // only peers that went through the invitation are listened to
    std::size_t o = 12 + 4 * static_cast<std::size_t>(buf[0] & 0x0f); if (o >= static_cast<std::size_t>(n)) continue;
    rtpMidiParse(buf.data() + o, static_cast<std::size_t>(n) - o, [this](const MidiMsg& m) { msgs_.fetch_add(1); if (sink_) sink_(m); });
  }
}

// ---- the MIDI inputs of this computer
#ifdef _WIN32
static void CALLBACK midiInCb(HMIDIIN, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR) {
  if (msg != MIM_DATA) return;
  const std::uint8_t b[3] = {static_cast<std::uint8_t>(p1), static_cast<std::uint8_t>(p1 >> 8), static_cast<std::uint8_t>(p1 >> 16)};
  const int need = midiDataBytes(b[0]); if (need > 0) reinterpret_cast<MidiPorts*>(inst)->deliver(b, static_cast<std::size_t>(1 + need));
}
std::vector<MidiPortInfo> midiInputs() {
  std::vector<MidiPortInfo> out; const UINT n = midiInGetNumDevs();
  for (UINT i = 0; i < n; ++i) { MIDIINCAPSA c; if (midiInGetDevCapsA(i, &c, sizeof c) == MMSYSERR_NOERROR) out.push_back({std::to_string(i), c.szPname}); }
  return out;
}
bool MidiPorts::open(const std::string& id, MidiSink sink, std::string& err) {
  close(); sink_ = std::move(sink); parser_.reset(); HMIDIIN h = nullptr;
  if (midiInOpen(&h, static_cast<UINT>(std::atoi(id.c_str())), reinterpret_cast<DWORD_PTR>(&midiInCb), reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION) != MMSYSERR_NOERROR) { err = "the MIDI port cannot be opened (another program may hold it)"; return false; }
  h_ = h; id_ = id; open_ = true; midiInStart(h); return true;
}
void MidiPorts::close() { if (h_) { HMIDIIN h = static_cast<HMIDIIN>(h_); open_ = false; midiInStop(h); midiInReset(h); midiInClose(h); h_ = nullptr; } open_ = false; }
#elif defined(AM_HAVE_ALSA)
std::vector<MidiPortInfo> midiInputs() {
  std::vector<MidiPortInfo> out; int card = -1;
  while (snd_card_next(&card) == 0 && card >= 0) {
    char name[32]; std::snprintf(name, sizeof name, "hw:%d", card); snd_ctl_t* ctl = nullptr; if (snd_ctl_open(&ctl, name, 0) < 0) continue;
    int dev = -1;
    while (snd_ctl_rawmidi_next_device(ctl, &dev) == 0 && dev >= 0) {
      snd_rawmidi_info_t* info; snd_rawmidi_info_alloca(&info); snd_rawmidi_info_set_device(info, static_cast<unsigned>(dev)); snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_INPUT);
      if (snd_ctl_rawmidi_info(ctl, info) == 0) { char id[32]; std::snprintf(id, sizeof id, "hw:%d,%d", card, dev); out.push_back({id, snd_rawmidi_info_get_name(info)}); }
    }
    snd_ctl_close(ctl);
  }
  return out;
}
bool MidiPorts::open(const std::string& id, MidiSink sink, std::string& err) {
  close(); sink_ = std::move(sink); parser_.reset(); snd_rawmidi_t* in = nullptr;
  if (snd_rawmidi_open(&in, nullptr, id.c_str(), SND_RAWMIDI_NONBLOCK) < 0 || !in) { err = "the MIDI port cannot be opened (another program may hold it)"; return false; }
  h_ = in; id_ = id; open_ = true;
  th_ = std::thread([this, in] { std::uint8_t b[256]; while (open_.load()) { const ssize_t n = snd_rawmidi_read(in, b, sizeof b); if (n > 0) deliver(b, static_cast<std::size_t>(n)); else std::this_thread::sleep_for(std::chrono::milliseconds(2)); } });
  return true;
}
void MidiPorts::close() { open_ = false; if (th_.joinable()) th_.join(); if (h_) { snd_rawmidi_close(static_cast<snd_rawmidi_t*>(h_)); h_ = nullptr; } }
#else
std::vector<MidiPortInfo> midiInputs() { return {}; }
bool MidiPorts::open(const std::string&, MidiSink, std::string& err) { err = "no MIDI ports on this system in this build"; return false; }
void MidiPorts::close() { open_ = false; }
#endif
void MidiPorts::deliver(const std::uint8_t* p, std::size_t n) { if (open_.load()) parser_.feed(p, n, sink_); }

}  // namespace am::net
