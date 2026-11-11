// NETWORK AUDIO, the wire formats (no sockets here, so every function can be tested on its own):
//
//   RTP (RFC 3550)            the 12-byte header, CSRC list / header extension / padding skipped on receive
//   L16 / L24 (RFC 3551/3190) linear PCM, big-endian, interleaved: the payload of AES67, RAVENNA and SMPTE ST 2110-30
//   SDP (RFC 4566)            the description of one stream, with the AES67 attributes (ptime, ts-refclk, mediaclk)
//   SAP (RFC 2974)            the announcement that carries an SDP to 239.255.255.255:9875 (how AES67 streams are found on a network;
//                             devices of other makers in their AES67 mode announce the same way)
//
// The audio profile is AES67: 48 kHz, L24 or L16, 1 ms packets by default. This is the open standard; no maker's own protocol is
// implemented or imitated here.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace am::net {

struct RtpHeader { std::uint8_t pt = 97; bool marker = false; std::uint16_t seq = 0; std::uint32_t ts = 0, ssrc = 0; };
constexpr std::size_t kRtpHeader = 12;
std::size_t rtpWrite(std::uint8_t* dst, const RtpHeader& h);   // writes 12 bytes
// false when the packet is not RTP version 2 or is cut short. off / len: where the payload is.
bool rtpParse(const std::uint8_t* p, std::size_t n, RtpHeader& h, std::size_t& off, std::size_t& len);

// floats (-1 .. +1, clipped) <-> big-endian interleaved PCM. bits: 16 or 24. dst must hold frames * channels * bits / 8 bytes.
void pcmEncode(const float* interleaved, std::size_t frames, int channels, int bits, std::uint8_t* dst);
void pcmDecode(const std::uint8_t* src, std::size_t frames, int channels, int bits, float* interleaved);

struct StreamDesc {
  std::string name = "AmF X1";      // s=
  std::string origin = "0.0.0.0";   // the sender's address (o= and the SAP origin)
  std::string address = "239.69.0.1";   // where the packets go (c=): a multicast group or one receiver
  int port = 5004, channels = 2, bits = 24, rate = 48000, pt = 97, ttl = 32;
  double ptimeMs = 1;
  std::uint32_t sessionId = 1, sessionVersion = 1;
  std::string refClock;             // a=ts-refclk value as received ("ptp=IEEE1588-2008:..."); we send "local" (no PTP clock here)
  int framesPerPacket() const { const int f = static_cast<int>(rate * ptimeMs / 1000.0 + 0.5); return f < 1 ? 1 : f; }
};
std::string sdpBuild(const StreamDesc& d);
bool sdpParse(const std::string& sdp, StreamDesc& d);   // false when there is no L16 / L24 audio stream in it

std::vector<std::uint8_t> sapBuild(const StreamDesc& d, bool deletion);
bool sapParse(const std::uint8_t* p, std::size_t n, StreamDesc& d, bool& deletion, std::uint16_t& hash);

bool isMulticast(const std::string& ipv4);   // 224.0.0.0 .. 239.255.255.255
bool parseIpv4(const std::string& s, std::uint8_t out[4]);

}  // namespace am::net
