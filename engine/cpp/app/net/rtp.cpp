#include "audiomixer/sample_format.hpp"
#include "net/rtp.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace am::net {

std::size_t rtpWrite(std::uint8_t* d, const RtpHeader& h) {
  d[0] = 0x80; d[1] = static_cast<std::uint8_t>((h.marker ? 0x80 : 0) | (h.pt & 0x7f));
  d[2] = static_cast<std::uint8_t>(h.seq >> 8); d[3] = static_cast<std::uint8_t>(h.seq);
  for (int i = 0; i < 4; ++i) { d[4 + i] = static_cast<std::uint8_t>(h.ts >> (24 - 8 * i)); d[8 + i] = static_cast<std::uint8_t>(h.ssrc >> (24 - 8 * i)); }
  return kRtpHeader;
}

bool rtpParse(const std::uint8_t* p, std::size_t n, RtpHeader& h, std::size_t& off, std::size_t& len) {
  if (!p || n < kRtpHeader || (p[0] >> 6) != 2) return false;
  const bool padding = p[0] & 0x20, ext = p[0] & 0x10; const std::size_t cc = p[0] & 0x0f;
  h.marker = p[1] & 0x80; h.pt = p[1] & 0x7f; h.seq = static_cast<std::uint16_t>((p[2] << 8) | p[3]);
  h.ts = (static_cast<std::uint32_t>(p[4]) << 24) | (static_cast<std::uint32_t>(p[5]) << 16) | (static_cast<std::uint32_t>(p[6]) << 8) | p[7];
  h.ssrc = (static_cast<std::uint32_t>(p[8]) << 24) | (static_cast<std::uint32_t>(p[9]) << 16) | (static_cast<std::uint32_t>(p[10]) << 8) | p[11];
  std::size_t o = kRtpHeader + 4 * cc;
  if (o > n) return false;
  if (ext) { if (o + 4 > n) return false; o += 4 + 4 * static_cast<std::size_t>((p[o + 2] << 8) | p[o + 3]); if (o > n) return false; }
  std::size_t end = n;
  if (padding) { const std::size_t pad = p[n - 1]; if (pad == 0 || pad > n - o) return false; end = n - pad; }
  off = o; len = end - o; return true;
}

void pcmEncode(const float* in, std::size_t frames, int channels, int bits, std::uint8_t* d) {
  const std::size_t n = frames * static_cast<std::size_t>(channels);
  if (bits == 16) {
    for (std::size_t i = 0; i < n; ++i) { const long v = am::fmt::toI16(in[i]); *d++ = static_cast<std::uint8_t>(v >> 8); *d++ = static_cast<std::uint8_t>(v); }
  } else {
    for (std::size_t i = 0; i < n; ++i) { const long v = am::fmt::toI24(in[i]); *d++ = static_cast<std::uint8_t>(v >> 16); *d++ = static_cast<std::uint8_t>(v >> 8); *d++ = static_cast<std::uint8_t>(v); }
  }
}

void pcmDecode(const std::uint8_t* s, std::size_t frames, int channels, int bits, float* out) {
  const std::size_t n = frames * static_cast<std::size_t>(channels);
  if (bits == 16) {
    for (std::size_t i = 0; i < n; ++i, s += 2) out[i] = static_cast<float>(static_cast<std::int16_t>((s[0] << 8) | s[1])) / 32768.0f;
  } else {
    for (std::size_t i = 0; i < n; ++i, s += 3) { std::int32_t v = (s[0] << 16) | (s[1] << 8) | s[2]; if (v & 0x800000) v -= 0x1000000; out[i] = static_cast<float>(static_cast<double>(v) / 8388608.0); }
  }
}

bool parseIpv4(const std::string& s, std::uint8_t out[4]) {
  unsigned a, b, c, d; char tail;
  if (std::sscanf(s.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 || a > 255 || b > 255 || c > 255 || d > 255) return false;
  out[0] = static_cast<std::uint8_t>(a); out[1] = static_cast<std::uint8_t>(b); out[2] = static_cast<std::uint8_t>(c); out[3] = static_cast<std::uint8_t>(d); return true;
}
bool isMulticast(const std::string& ip) { std::uint8_t a[4]; return parseIpv4(ip, a) && a[0] >= 224 && a[0] <= 239; }

static std::string oneLine(std::string s) { for (char& c : s) if (c == '\r' || c == '\n') c = ' '; return s.empty() ? std::string("-") : s; }

std::string sdpBuild(const StreamDesc& d) {
  std::ostringstream o;
  o << "v=0\r\n"
    << "o=- " << d.sessionId << " " << d.sessionVersion << " IN IP4 " << d.origin << "\r\n"
    << "s=" << oneLine(d.name) << "\r\n"
    << "c=IN IP4 " << d.address; if (isMulticast(d.address)) o << "/" << d.ttl; o << "\r\n"
    << "t=0 0\r\n"
    << "m=audio " << d.port << " RTP/AVP " << d.pt << "\r\n"
    << "a=rtpmap:" << d.pt << " L" << d.bits << "/" << d.rate << "/" << d.channels << "\r\n"
    << "a=recvonly\r\n";
  char pt[32]; std::snprintf(pt, sizeof pt, "%g", d.ptimeMs);
  o << "a=ptime:" << pt << "\r\n"
    << "a=ts-refclk:" << (d.refClock.empty() ? std::string("local") : d.refClock) << "\r\n"   // no PTP clock is run here: the media clock is the sender's own
    << "a=mediaclk:sender\r\n";
  return o.str();
}

bool sdpParse(const std::string& sdp, StreamDesc& d) {
  std::istringstream in(sdp); std::string line; int pt = -1; bool audio = false, map = false; int media = 0;
  d.refClock.clear();
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (line.size() < 2 || line[1] != '=') continue;
    const std::string v = line.substr(2);
    switch (line[0]) {
      case 's': d.name = v; break;
      case 'o': { std::istringstream f(v); std::string user, nt, at, addr; unsigned long long id = 0, ver = 0; f >> user >> id >> ver >> nt >> at >> addr; d.sessionId = static_cast<std::uint32_t>(id); d.sessionVersion = static_cast<std::uint32_t>(ver); if (!addr.empty()) d.origin = addr; break; }
      case 'c': { std::istringstream f(v); std::string nt, at, addr; f >> nt >> at >> addr; const auto slash = addr.find('/'); if (slash != std::string::npos) { d.ttl = std::atoi(addr.c_str() + slash + 1); addr.resize(slash); } if (!addr.empty() && media <= 1) d.address = addr; break; }   // the session's, or the first audio stream's own
      case 'm': { ++media; if (audio) break; std::istringstream f(v); std::string media, proto; int port = 0, p = -1; f >> media >> port >> proto >> p; if (media == "audio" && port > 0 && p >= 0) { audio = true; d.port = port; pt = p; d.pt = p; } break; }
      case 'a': {
        if (v.rfind("rtpmap:", 0) == 0 && audio && !map) {
          int p = -1, bits = 0, rate = 0, ch = 1; char enc[16] = {0};
          const int got = std::sscanf(v.c_str(), "rtpmap:%d %15[^/]/%d/%d", &p, enc, &rate, &ch);
          if (got >= 3 && p == pt && (enc[0] == 'L' || enc[0] == 'l')) { bits = std::atoi(enc + 1); if ((bits == 16 || bits == 24) && rate > 0) { d.bits = bits; d.rate = rate; d.channels = got >= 4 ? ch : 1; map = true; } }
        } else if (v.rfind("ptime:", 0) == 0) { const double p = std::atof(v.c_str() + 6); if (p > 0) d.ptimeMs = p; }
        else if (v.rfind("ts-refclk:", 0) == 0) d.refClock = v.substr(10);
        break;
      }
      default: break;
    }
  }
  return audio && map && d.channels >= 1 && d.channels <= 64;
}

static std::uint16_t sapHash(const StreamDesc& d) {   // stable for one session: receivers match the deletion to the announcement by it
  std::uint32_t h = 2166136261u; const std::string k = d.origin + "|" + std::to_string(d.sessionId);
  for (unsigned char c : k) { h ^= c; h *= 16777619u; }
  const std::uint16_t r = static_cast<std::uint16_t>(h ^ (h >> 16)); return r ? r : 1;
}

std::vector<std::uint8_t> sapBuild(const StreamDesc& d, bool deletion) {
  std::vector<std::uint8_t> o; const std::uint16_t hash = sapHash(d); std::uint8_t ip[4] = {0, 0, 0, 0}; parseIpv4(d.origin, ip);
  o.push_back(static_cast<std::uint8_t>(0x20 | (deletion ? 0x04 : 0)));   // version 1, IPv4, announce / delete, not encrypted, not compressed
  o.push_back(0); o.push_back(static_cast<std::uint8_t>(hash >> 8)); o.push_back(static_cast<std::uint8_t>(hash));
  o.insert(o.end(), ip, ip + 4);
  static const char type[] = "application/sdp"; o.insert(o.end(), type, type + sizeof type);   // with its terminating zero
  const std::string sdp = sdpBuild(d); o.insert(o.end(), sdp.begin(), sdp.end());
  return o;
}

bool sapParse(const std::uint8_t* p, std::size_t n, StreamDesc& d, bool& deletion, std::uint16_t& hash) {
  if (!p || n < 8 || (p[0] >> 5) != 1) return false;
  if (p[0] & 0x03) return false;                       // encrypted or compressed: not read
  const bool v6 = p[0] & 0x10; deletion = p[0] & 0x04; hash = static_cast<std::uint16_t>((p[2] << 8) | p[3]);
  std::size_t o = 4 + (v6 ? 16 : 4) + 4 * static_cast<std::size_t>(p[1]);
  if (o >= n) return false;
  if (!v6) { char b[20]; std::snprintf(b, sizeof b, "%u.%u.%u.%u", p[4], p[5], p[6], p[7]); d.origin = b; }
  if (n - o >= 3 && !(p[o] == 'v' && p[o + 1] == '=')) {   // an optional payload type string, zero-terminated
    const void* z = std::memchr(p + o, 0, n - o); if (!z) return false;
    const std::string type(reinterpret_cast<const char*>(p + o));
    if (type != "application/sdp") return false;
    o = static_cast<std::size_t>(static_cast<const std::uint8_t*>(z) - p) + 1;
  }
  if (o >= n) return false;
  const std::string keepOrigin = d.origin;
  if (!sdpParse(std::string(reinterpret_cast<const char*>(p + o), n - o), d)) return deletion;   // a deletion may carry only the o= line
  if (d.origin.empty() || d.origin == "0.0.0.0") d.origin = keepOrigin;
  return true;
}

}  // namespace am::net
