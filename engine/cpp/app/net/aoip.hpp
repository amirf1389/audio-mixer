// NETWORK AUDIO over IP on the open AES67 profile (RTP, L16 / L24, 48 kHz, 1 ms packets; SAP / SDP announcements).
//
//   am::net::Sender      the mixer's master (and, with more than 2 channels, the strips before their faders) as one RTP stream to a
//                        multicast group or to one receiver; announced with SAP so that receivers list it
//   am::net::Receiver    one RTP stream as up to Runtime::kNetInputs extra mixer inputs, through a jitter buffer
//   am::net::Discovery   listens for SAP announcements: the AES67 streams on this network
//   am::net::Aoip        the three of them attached to one am::Runtime
//
// WHAT THIS IS NOT. No PTP (IEEE 1588) clock is run: the sender's media clock is the sound card's, and the receiver follows each
// stream's own clock by resampling a few parts per thousand at most. That plays any AES67 / RAVENNA / ST 2110-30 stream and feeds
// receivers that accept a stream without a PTP reference; equipment that insists on PTP lock may refuse this sender. Other makers'
// own protocols are not implemented: their devices are reached through their AES67 mode. Nothing here is encrypted: the audio
// travels in clear on the local network, and it runs only after it has been switched on.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "audiomixer/runtime.hpp"
#include "net/rtp.hpp"
#include "net/sock.hpp"

namespace am::net {

// SSRCs of the streams this process is sending right now: a receiver drops their packets (its own master coming back = a feedback loop)
bool isOwnStream(std::uint32_t ssrc) noexcept;

struct SendStats { bool running = false; std::uint64_t packets = 0, droppedFrames = 0; bool rateMismatch = false; };
class Sender : public Runtime::Tap {
 public:
  ~Sender() override { stop(); }
  // d.rate must be the mixer's sample rate. iface: the local address to send from ("" = the system's route). announce: SAP every 30 s.
  bool start(const StreamDesc& d, const std::string& iface, bool announce, std::string& err);
  void stop();
  bool running() const { return run_.load(); }
  const StreamDesc& desc() const { return d_; }
  SendStats stats() const;
  void onBlock(const float* const* strips, int nStrips, const float* L, const float* R, std::size_t frames, double sampleRate) override;
 private:
  void loop();
  StreamDesc d_; std::string iface_; bool announce_ = true; Sock sock_ = kNoSock, sap_ = kNoSock;
  std::vector<float> ring_; std::size_t cap_ = 0; std::atomic<std::uint64_t> w_{0}, r_{0};   // frames written / read (single producer: audio thread, single consumer: loop)
  std::atomic<bool> run_{false}, mismatch_{false}; std::atomic<std::uint64_t> packets_{0}, dropped_{0}; std::thread th_; int slot_ = -1;
};

struct RecvStats { bool running = false; std::uint64_t packets = 0, lost = 0, late = 0, underruns = 0, resyncs = 0, selfLoop = 0; double fillMs = 0; int idleMs = -1; std::string from; };
class Receiver : public Runtime::NetInput {
 public:
  ~Receiver() override { stop(); }
  // d: address (a multicast group to join, or anything else to take packets sent straight to this machine), port, channels, bits, rate.
  // latencyMs: the jitter buffer (5 .. 500).
  bool start(const StreamDesc& d, const std::string& iface, int latencyMs, std::string& err);
  void stop();
  bool running() const { return run_.load(); }
  const StreamDesc& desc() const { return d_; }
  void setRejectOwn(bool on) { rejectOwn_ = on; }   // default on: packets of a stream this process sends are dropped (tests of the wire turn it off)
  int port() const { return port_; }   // the port really bound (d.port 0 = any free one)
  RecvStats stats() const;
  void pull(float* const* ch, int channels, std::size_t frames, double sampleRate) override;
 private:
  void loop();
  float at(std::uint64_t frame, int c) const { return ring_[static_cast<std::size_t>(frame % cap_) * static_cast<std::size_t>(d_.channels) + static_cast<std::size_t>(c)]; }
  StreamDesc d_; int latencyMs_ = 20, port_ = 0; Sock sock_ = kNoSock; std::atomic<bool> rejectOwn_{true};
  std::vector<float> ring_; std::uint64_t cap_ = 1; std::atomic<std::uint64_t> w_{0}, r_{0};   // frames (single producer: loop, single consumer: audio thread)
  double frac_ = 0, fillAvg_ = 0; bool primed_ = false;                                       // audio thread only
  std::atomic<bool> run_{false}; std::atomic<std::uint64_t> packets_{0}, lost_{0}, late_{0}, under_{0}, resync_{0}, self_{0}; std::atomic<std::int64_t> lastNs_{0}; std::atomic<std::uint32_t> fromIp_{0};
  std::atomic<double> fillMs_{0}; std::thread th_;
};

struct Found { StreamDesc d; std::uint16_t hash = 0; int ageMs = 0; };
class Discovery {
 public:
  ~Discovery() { stop(); }
  bool start(const std::string& iface, std::string& err);
  void stop();
  bool running() const { return run_.load(); }
  std::vector<Found> list() const;          // newest first; entries not heard for 5 minutes are gone
  void inject(const std::uint8_t* sap, std::size_t n);   // one SAP packet, as if received (tests; packets from another listener)
 private:
  void loop();
  struct Entry { StreamDesc d; std::uint16_t hash; std::int64_t seenNs; };
  Sock sock_ = kNoSock; std::atomic<bool> run_{false}; std::thread th_; mutable std::mutex m_; std::vector<Entry> e_;
};

// every way a stream can travel that this program knows about, and whether this build can do it:
// [{"id","name","available","note"}] as JSON. The ones that need a maker's own kit say so; none of them is imitated.
std::string transportsJson();

class Aoip {
 public:
  explicit Aoip(Runtime& rt) : rt_(rt) {}
  ~Aoip();
  bool sendStart(StreamDesc d, const std::string& iface, bool announce, std::string& err);   // d.rate is set to the runtime's rate; d.origin from the route when empty
  void sendStop();
  bool receiveStart(const StreamDesc& d, const std::string& iface, int latencyMs, std::string& err);
  void receiveStop();
  Sender& sender() { return tx_; }
  Receiver& receiver() { return rx_; }
  Discovery& discovery() { return disc_; }
  std::string statsJson() const;
 private:
  void settle();
  Runtime& rt_; Sender tx_; Receiver rx_; Discovery disc_;
};

}  // namespace am::net
