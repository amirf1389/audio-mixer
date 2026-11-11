// MIDI for the console: controllers move the mixer.
//
//   MidiParser      a byte stream (a serial MIDI port) -> messages; running status, real-time bytes in between, SysEx skipped
//   MidiMap         which message moves what: the default map is the one every controller can send without set-up
//                     Control Change 7  (channel volume) on MIDI channel n  -> the fader of strip n
//                     Control Change 10 (pan)            on MIDI channel n  -> the pan of strip n
//                     Control Change 1 .. 6 / 8 / 9 ... on channel 16       -> see MidiMap::apply (master fader on CC 7 of channel 16)
//                     Note On, notes 0 .. 31 -> mute of that strip (toggle); notes 32 .. 63 -> solo (toggle)
//                   plus learned entries: "the control I just moved is this fader"
//   RtpMidi         network MIDI, RFC 6295 with the session handshake every implementation uses (invitation, clock sync, end):
//                   this program is a session that a computer, tablet or controller connects to by address and port
//   MidiPorts       the MIDI inputs of this computer (Windows multimedia MIDI; ALSA raw MIDI on Linux)
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "net/sock.hpp"

namespace am::net {

struct MidiMsg { std::uint8_t status = 0, d1 = 0, d2 = 0; int channel() const { return status & 0x0f; } int type() const { return status & 0xf0; } };
using MidiSink = std::function<void(const MidiMsg&)>;   // called on the thread that received the message

class MidiParser {
 public:
  void feed(const std::uint8_t* p, std::size_t n, const MidiSink& sink);
  void reset() { status_ = 0; have_ = 0; sysex_ = false; }
 private:
  std::uint8_t status_ = 0, d_[2] = {0, 0}; int have_ = 0; bool sysex_ = false;
};
int midiDataBytes(std::uint8_t status);   // 1 or 2 for channel messages, 0 otherwise

struct MidiAction { enum Kind { None, Fader, Pan, Mute, Solo, Master } kind = None; int strip = 0; double value = 0; };   // Fader / Master: dB; Pan: -1 .. +1; Mute / Solo: value 1 = toggle
double midiFaderDb(int v);   // 0 -> -120 (off), 1 .. 127 -> -60 .. +10 dB, 100 = 0 dB
class MidiMap {
 public:
  MidiAction apply(const MidiMsg& m) const;
  // the next control moved becomes `kind` of `strip` (Fader, Pan, Mute, Solo, Master). learnPending() is false once it has been taken.
  void learn(MidiAction::Kind kind, int strip);
  bool learnPending() const { return pending_.load(); }
  bool offer(const MidiMsg& m);                     // true when the message was taken as the learned control
  std::string save() const;                         // "type,channel,number,kind,strip;..."
  void load(const std::string& s);
  void clear();
  int learned() const;
 private:
  struct Entry { int type, channel, number; MidiAction::Kind kind; int strip; };
  mutable std::mutex m_; std::vector<Entry> e_; std::atomic<bool> pending_{false}; MidiAction::Kind lk_ = MidiAction::None; int ls_ = 0;
};

// One RTP-MIDI payload (the MIDI command section of RFC 6295, the recovery journal ignored) -> messages. false when it is malformed.
bool rtpMidiParse(const std::uint8_t* payload, std::size_t n, const MidiSink& sink);

struct RtpMidiStats { bool running = false; int port = 0, peers = 0; std::uint64_t messages = 0; std::string lastPeer; };
class RtpMidi {
 public:
  ~RtpMidi() { stop(); }
  // control port `port`, data port `port + 1` (5004 / 5005 by convention). bindIp "" = every interface of this machine.
  bool start(const std::string& name, const std::string& bindIp, int port, MidiSink sink, std::string& err);
  void stop();
  bool running() const { return run_.load(); }
  RtpMidiStats stats() const;
 private:
  void loop(Sock s, bool data);
  void handleSession(Sock s, const std::uint8_t* p, int n, const Peer& from);
  std::string name_; std::uint32_t ssrc_ = 0; int port_ = 0; Sock ctl_ = kNoSock, dat_ = kNoSock; MidiSink sink_;
  std::atomic<bool> run_{false}; std::thread tc_, td_; std::atomic<std::uint64_t> msgs_{0};
  mutable std::mutex m_; struct PeerS { std::uint32_t ip, ssrc; std::string name; std::int64_t seenNs; std::uint16_t ctlPort; }; std::vector<PeerS> peers_;
};

struct MidiPortInfo { std::string id, name; };
std::vector<MidiPortInfo> midiInputs();
class MidiPorts {
 public:
  ~MidiPorts() { close(); }
  bool open(const std::string& id, MidiSink sink, std::string& err);
  void close();
  bool isOpen() const { return open_.load(); }
  const std::string& id() const { return id_; }
  void deliver(const std::uint8_t* p, std::size_t n);   // bytes from the port (used by the system callback)
 private:
  std::string id_; MidiSink sink_; MidiParser parser_; std::atomic<bool> open_{false}; void* h_ = nullptr; std::thread th_;
};

}  // namespace am::net
