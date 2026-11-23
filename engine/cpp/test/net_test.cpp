// Network audio and MIDI: the wire formats byte for byte, then a real stream through UDP on this machine (sender -> receiver),
// the SAP announcement into the discovery list, a network MIDI session, and the whole path through am::Runtime.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "net/aoip.hpp"
#include "net/midi.hpp"
#include "net/ptp.hpp"

using namespace am;
using namespace am::net;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
static void ms(int n) { std::this_thread::sleep_for(std::chrono::milliseconds(n)); }

static void wire() {
  RtpHeader h; h.pt = 97; h.seq = 0xfffe; h.ts = 0x01020304; h.ssrc = 0xa1b2c3d4; h.marker = true;
  std::uint8_t b[64]; CHECK(rtpWrite(b, h) == 12 && b[0] == 0x80 && b[1] == (0x80 | 97) && b[2] == 0xff && b[3] == 0xfe && b[7] == 4 && b[8] == 0xa1);
  RtpHeader g; std::size_t off = 0, len = 0; b[12] = 1; b[13] = 2; b[14] = 3;
  CHECK(rtpParse(b, 15, g, off, len) && g.seq == h.seq && g.ts == h.ts && g.ssrc == h.ssrc && g.pt == 97 && g.marker && off == 12 && len == 3);
  CHECK(!rtpParse(b, 11, g, off, len));
  { std::uint8_t x[32] = {0}; x[0] = 0x80 | 0x20 | 0x10 | 2; x[20] = 0; x[21] = 0; x[22] = 0; x[23] = 1; x[31] = 2;   // 2 CSRC, an extension of one word, 2 bytes of padding
    CHECK(rtpParse(x, 32, g, off, len) && off == 28 && len == 2); x[0] = 0x40; CHECK(!rtpParse(x, 32, g, off, len)); }
  const float in[6] = {0.0f, 1.0f, -1.0f, 0.5f, -0.25f, 2.0f}; std::uint8_t p[18]; float out[6];
  pcmEncode(in, 3, 2, 24, p); CHECK(p[0] == 0 && p[3] == 0x7f && p[4] == 0xff && p[5] == 0xff && p[6] == 0x80 && p[7] == 0 && p[8] == 0 && p[15] == 0x7f);   // +1 -> 7fffff, -1 -> 800000 (one scale both ways: sample_format.hpp), 2.0 clipped
  pcmDecode(p, 3, 2, 24, out); for (int i = 0; i < 5; ++i) CHECK(std::fabs(out[i] - in[i]) < 2e-7f);
  pcmEncode(in, 3, 2, 16, p); CHECK(p[2] == 0x7f && p[3] == 0xff && p[4] == 0x80 && p[5] == 0x00);
  pcmDecode(p, 3, 2, 16, out); for (int i = 0; i < 5; ++i) CHECK(std::fabs(out[i] - in[i]) < 4e-5f);

  StreamDesc d; d.name = "Stage left"; d.origin = "192.168.1.20"; d.address = "239.69.1.2"; d.port = 5004; d.channels = 8; d.bits = 24; d.rate = 48000; d.sessionId = 77;
  const std::string sdp = sdpBuild(d);
  CHECK(sdp.find("a=rtpmap:97 L24/48000/8\r\n") != std::string::npos && sdp.find("c=IN IP4 239.69.1.2/32\r\n") != std::string::npos && sdp.find("a=ptime:1\r\n") != std::string::npos && sdp.find("m=audio 5004 RTP/AVP 97") != std::string::npos);
  StreamDesc e; CHECK(sdpParse(sdp, e) && e.name == d.name && e.address == d.address && e.port == 5004 && e.channels == 8 && e.bits == 24 && e.rate == 48000 && e.origin == d.origin && e.sessionId == 77 && e.ptimeMs == 1);
  // what an AES67 device of another maker announces: media-level c= line, PTP reference, 2 channels
  const char* other = "v=0\r\no=- 1311738121 1311738121 IN IP4 192.168.1.50\r\ns=Stagebox : 2\r\nt=0 0\r\nm=audio 5004 RTP/AVP 98\r\nc=IN IP4 239.69.83.67/32\r\na=recvonly\r\na=rtpmap:98 L24/48000/2\r\na=ptime:1\r\na=ts-refclk:ptp=IEEE1588-2008:00-1D-C1-FF-FE-12-34-56:0\r\na=mediaclk:direct=142033328\r\n";
  StreamDesc f; CHECK(sdpParse(other, f) && f.name == "Stagebox : 2" && f.address == "239.69.83.67" && f.pt == 98 && f.channels == 2 && f.bits == 24 && f.refClock.rfind("ptp=", 0) == 0);
  StreamDesc bad; CHECK(!sdpParse("v=0\r\nm=video 5004 RTP/AVP 96\r\na=rtpmap:96 raw/90000\r\n", bad) && !sdpParse("v=0\r\nm=audio 5004 RTP/AVP 96\r\na=rtpmap:96 opus/48000/2\r\n", bad));
  const auto sap = sapBuild(d, false); StreamDesc s2; bool del = true; std::uint16_t hash = 0;
  CHECK(sap[0] == 0x20 && sap[4] == 192 && sap[7] == 20 && sapParse(sap.data(), sap.size(), s2, del, hash) && !del && hash != 0 && s2.name == d.name && s2.channels == 8);
  const auto bye = sapBuild(d, true); std::uint16_t hash2 = 0; CHECK(bye[0] == 0x24 && sapParse(bye.data(), bye.size(), s2, del, hash2) && del && hash2 == hash);
  CHECK(isMulticast("239.69.0.1") && !isMulticast("192.168.1.1") && !isMulticast("nonsense"));
  Discovery disc; disc.inject(sap.data(), sap.size()); disc.inject(sap.data(), sap.size()); CHECK(disc.list().size() == 1 && disc.list()[0].d.name == "Stage left");
  disc.inject(bye.data(), bye.size()); CHECK(disc.list().empty());
  CHECK(transportsJson().find("\"aes67\"") != std::string::npos && transportsJson().front() == '[');
}

static void midi() {
  std::vector<MidiMsg> got; const MidiSink sink = [&](const MidiMsg& m) { got.push_back(m); };
  MidiParser p; const std::uint8_t s[] = {0xb0, 7, 100, 10, 0, 0xf8, 0x90, 60, 0xfe, 127, 0xf0, 1, 2, 3, 0xf7, 0xc1, 5, 0x91, 61};
  p.feed(s, sizeof s, sink);
  CHECK(got.size() == 4 && got[0].status == 0xb0 && got[0].d1 == 7 && got[0].d2 == 100 && got[1].d1 == 10 && got[1].d2 == 0 && got[2].status == 0x90 && got[2].d2 == 127 && got[3].status == 0xc1 && got[3].d1 == 5);
  CHECK(midiFaderDb(0) == -120 && std::fabs(midiFaderDb(100)) < 1e-9 && midiFaderDb(127) == 10 && midiFaderDb(1) == -60 && midiFaderDb(50) < midiFaderDb(51));
  MidiMap map;
  MidiAction a = map.apply({0xb2, 7, 100}); CHECK(a.kind == MidiAction::Fader && a.strip == 2 && std::fabs(a.value) < 1e-9);
  a = map.apply({0xbf, 7, 0}); CHECK(a.kind == MidiAction::Master && a.value == -120);
  a = map.apply({0xb1, 10, 127}); CHECK(a.kind == MidiAction::Pan && a.strip == 1 && a.value == 1.0);
  a = map.apply({0xb1, 10, 64}); CHECK(a.value == 0.0);
  a = map.apply({0x90, 3, 90}); CHECK(a.kind == MidiAction::Mute && a.strip == 3);
  a = map.apply({0x90, 35, 90}); CHECK(a.kind == MidiAction::Solo && a.strip == 3);
  CHECK(map.apply({0x90, 3, 0}).kind == MidiAction::None && map.apply({0x80, 3, 64}).kind == MidiAction::None && map.apply({0xb0, 44, 1}).kind == MidiAction::None);
  map.learn(MidiAction::Fader, 5); CHECK(map.learnPending() && !map.offer({0x90, 1, 100}) && map.offer({0xb0, 44, 9}) && !map.learnPending());
  a = map.apply({0xb0, 44, 127}); CHECK(a.kind == MidiAction::Fader && a.strip == 5 && a.value == 10);
  map.learn(MidiAction::Mute, 7); CHECK(map.offer({0x93, 70, 100}));
  CHECK(map.apply({0x93, 70, 100}).kind == MidiAction::Mute && map.apply({0x93, 70, 100}).strip == 7 && map.apply({0x83, 70, 0}).kind == MidiAction::None);
  MidiMap m2; m2.load(map.save()); CHECK(m2.learned() == 2 && m2.apply({0xb0, 44, 100}).strip == 5); m2.load("garbage;1,2;176,0,300,1,0;"); CHECK(m2.learned() == 0);
  // RTP-MIDI payloads: one command; two with a delta time and running status; the long header; a cut one
  got.clear(); const std::uint8_t a1[] = {0x03, 0x90, 60, 100}; CHECK(rtpMidiParse(a1, sizeof a1, sink) && got.size() == 1 && got[0].d1 == 60);
  got.clear(); const std::uint8_t a2[] = {0x07, 0xb0, 7, 90, 0x81, 0x00, 10, 20}; CHECK(rtpMidiParse(a2, sizeof a2, sink) && got.size() == 2 && got[1].status == 0xb0 && got[1].d1 == 10 && got[1].d2 == 20);
  got.clear(); const std::uint8_t a3[] = {0x80 | 0x20, 0x04, 0x05, 0x80, 61, 0}; CHECK(rtpMidiParse(a3, sizeof a3, sink) && got.size() == 1 && got[0].status == 0x80);
  const std::uint8_t a4[] = {0x05, 0x90, 60}; CHECK(!rtpMidiParse(a4, sizeof a4, sink));
  const std::uint8_t a5[] = {0x03, 0x90, 60}; CHECK(!rtpMidiParse(a5, sizeof a5, sink));

  // a session on this machine: invitation on both ports, clock sync, then a note; a stranger's packet is not listened to
  got.clear(); std::mutex gm; RtpMidi srv; std::string err; int port = 0;
  for (int base = 51004; base < 51104 && !port; base += 2) if (srv.start("Test console", "127.0.0.1", base, [&](const MidiMsg& m) { std::lock_guard<std::mutex> g(gm); got.push_back(m); }, err)) port = base;
  CHECK(port != 0);
  if (port) {
    Sock c = udpOpen(err), d = udpOpen(err); udpBind(c, "127.0.0.1", 0, err); udpBind(d, "127.0.0.1", 0, err); udpTimeout(c, 1000); udpTimeout(d, 1000);
    std::uint8_t inv[24] = {0xff, 0xff, 'I', 'N', 0, 0, 0, 2, 0x11, 0x22, 0x33, 0x44, 0xca, 0xfe, 0xba, 0xbe, 'T', 'a', 'b', 0};
    std::uint8_t r[128];
    udpSendTo(c, "127.0.0.1", port, inv, 20); int n = udpRecv(c, r, sizeof r);
    CHECK(n >= 17 && r[0] == 0xff && r[2] == 'O' && r[3] == 'K' && r[8] == 0x11 && r[11] == 0x44 && std::string(reinterpret_cast<char*>(r + 16)) == "Test console");
    std::uint8_t note[] = {0x80, 0x61, 0, 1, 0, 0, 0, 0, 0xca, 0xfe, 0xba, 0xbe, 0x03, 0x90, 64, 99};
    udpSendTo(d, "127.0.0.1", port + 1, inv, 20); n = udpRecv(d, r, sizeof r); CHECK(n >= 17 && r[2] == 'O');
    std::uint8_t ck[36] = {0xff, 0xff, 'C', 'K', 0xca, 0xfe, 0xba, 0xbe, 0}; ck[19] = 9;
    udpSendTo(d, "127.0.0.1", port + 1, ck, 36); n = udpRecv(d, r, sizeof r); CHECK(n == 36 && r[2] == 'C' && r[8] == 1 && r[19] == 9);
    udpSendTo(d, "127.0.0.1", port + 1, note, sizeof note);
    std::uint8_t stranger[sizeof note]; std::memcpy(stranger, note, sizeof note); stranger[8] = 1; udpSendTo(d, "127.0.0.1", port + 1, stranger, sizeof stranger);
    ms(150); { std::lock_guard<std::mutex> g(gm); CHECK(got.size() == 1 && got[0].status == 0x90 && got[0].d1 == 64 && got[0].d2 == 99); }
    CHECK(srv.stats().peers == 1 && srv.stats().messages == 1 && srv.stats().lastPeer.find("Tab") == 0);
    srv.stop(); n = udpRecv(c, r, sizeof r); CHECK(n == 16 && r[2] == 'B' && r[3] == 'Y');
    udpClose(c); udpClose(d);
  }
  (void)midiInputs();   // must not crash without any MIDI hardware
}

// a 1 kHz sine through sender -> UDP -> receiver, fed and pulled in real time by this thread
static void stream(int bits, int txRate, int rxDeviceRate, int channels) {
  std::string err; Receiver rx; rx.setRejectOwn(false); StreamDesc d; d.address = "127.0.0.1"; d.port = 0; d.channels = channels; d.bits = bits; d.rate = txRate;
  CHECK(rx.start(d, "", 20, err)); d.port = rx.port(); CHECK(d.port > 0);
  Sender tx; CHECK(tx.start(d, "", false, err));
  const std::size_t N = 256; std::vector<float> L(N), R(N), s0(N); const float* strips[1] = {s0.data()};
  std::vector<std::vector<float>> ob(16, std::vector<float>(2 * N)); float* out[16]; for (int k = 0; k < 16; ++k) out[k] = ob[static_cast<std::size_t>(k)].data();
  std::vector<float> rec, rec2; double ph = 0; const double w = 2 * M_PI * 1000.0 / txRate;
  auto t0 = std::chrono::steady_clock::now(); const int blocks = 400; const std::size_t M = static_cast<std::size_t>(N * static_cast<double>(rxDeviceRate) / txRate + 0.5);
  for (int b = 0; b < blocks; ++b) {
    for (std::size_t i = 0; i < N; ++i) { L[i] = static_cast<float>(0.5 * std::sin(ph)); R[i] = channels == 1 ? L[i] : -L[i]; s0[i] = 0.25f; ph += w; }
    tx.onBlock(strips, 1, L.data(), R.data(), N, txRate);
    rx.pull(out, 16, M, rxDeviceRate);
    if (b >= 100) { rec.insert(rec.end(), out[0], out[0] + M); rec2.insert(rec2.end(), out[channels > 2 ? 2 : (channels > 1 ? 1 : 0)], out[channels > 2 ? 2 : (channels > 1 ? 1 : 0)] + M); }
    std::this_thread::sleep_until(t0 + std::chrono::microseconds(static_cast<long long>((b + 1) * 1e6 * N / txRate)));
  }
  const RecvStats rs = rx.stats(); const SendStats ss = tx.stats();
  CHECK(ss.packets > 1500 && ss.droppedFrames == 0 && !ss.rateMismatch);
  CHECK(rs.packets > 1500 && rs.lost == 0 && rs.from == "127.0.0.1" && rs.idleMs >= 0 && rs.idleMs < 200);
  if (rs.underruns > 2) std::printf("  note: %llu underruns (a loaded machine)\n", static_cast<unsigned long long>(rs.underruns));
  CHECK(rs.underruns <= 6);
  // the tone that came out: level 0.5 peak (rms 0.3536) at 1 kHz, measured over the stretches that are not silence
  double e = 0; std::size_t n = 0; const double wr = 2 * M_PI * 1000.0 / rxDeviceRate;
  for (std::size_t i = 0; i < rec.size(); ++i) { e += rec[i] * rec[i]; ++n; }
  const double rms = std::sqrt(e / static_cast<double>(n));
  CHECK(rms > 0.33 && rms < 0.36);
  // all of it at 1 kHz, no gaps, no clicks: the 1 kHz level of every 10 ms stretch (10 whole cycles). Measured per stretch, not over the
  // whole take: the receiver follows the sender's clock (up to 0.3 % faster or slower when a loaded machine delivers the packets late),
  // which turns the phase slowly and would make one long measurement read low although every stretch is clean
  if (rs.underruns == 0 && txRate == rxDeviceRate) {
    const std::size_t W = static_cast<std::size_t>(rxDeviceRate / 100); double worst = 1, best = 0;
    for (std::size_t s0i = 0; s0i + W <= rec.size(); s0i += W) { double cr = 0, ci = 0; for (std::size_t i = 0; i < W; ++i) { cr += rec[s0i + i] * std::cos(wr * static_cast<double>(i)); ci += rec[s0i + i] * std::sin(wr * static_cast<double>(i)); }
      const double t = 2 * std::sqrt(cr * cr + ci * ci) / static_cast<double>(W); worst = std::min(worst, t); best = std::max(best, t); }
    if (!(worst > 0.49 && best < 0.51)) std::printf("  tone per 10 ms: %.4f .. %.4f (resyncs %llu, fill %.1f ms)\n", worst, best, static_cast<unsigned long long>(rs.resyncs), rs.fillMs);
    CHECK(worst > 0.49 && best < 0.51); }
  double e2 = 0; for (float v : rec2) e2 += v * v; const double rms2 = std::sqrt(e2 / static_cast<double>(rec2.size()));
  if (channels > 2) CHECK(std::fabs(rms2 - 0.25) < 0.01); else if (channels == 2) CHECK(std::fabs(rms2 - rms) < 0.01);   // channel 3 = strip 1 before its fader; channel 2 = right
  for (int k = channels; k < 16; ++k) for (std::size_t i = 0; i < M; ++i) if (out[k][i] != 0) { CHECK(false); break; }
  CHECK(std::fabs(rs.fillMs - 20) < 15);
  tx.stop(); rx.stop(); CHECK(!tx.running() && !rx.running());
  // nothing arriving: silence, no crash
  rx.pull(out, 16, M, rxDeviceRate); CHECK(out[0][0] == 0);
}

static void lossAndRuntime() {
  std::string err;
  { // a lost packet becomes silence of the same length (time is kept), a doubled one is dropped
    Receiver rx; StreamDesc d; d.address = "127.0.0.1"; d.port = 0; d.channels = 1; d.bits = 16; d.rate = 48000; CHECK(rx.start(d, "", 5, err));
    Sock s = udpOpen(err); std::uint8_t pkt[12 + 96]; RtpHeader h; h.ssrc = 5; h.seq = 65534; h.ts = 0;
    for (int i = 0; i < 40; ++i) { if (i == 10) { ++h.seq; continue; } rtpWrite(pkt, h); for (int k = 0; k < 48; ++k) { pkt[12 + 2 * k] = 0x40; pkt[13 + 2 * k] = 0; } udpSendTo(s, "127.0.0.1", rx.port(), pkt, sizeof pkt); if (i == 20) udpSendTo(s, "127.0.0.1", rx.port(), pkt, sizeof pkt); ++h.seq; }
    ms(120); const RecvStats st = rx.stats(); CHECK(st.packets == 39 && st.lost == 1 && st.late == 1);
    std::vector<float> o(1920); float* op[1] = {o.data()}; rx.pull(op, 1, 240, 48000);   // primes at the newest 5 ms (two blocks minimum = 480 frames)
    int zeros = 0, half = 0; for (int i = 0; i < 240; ++i) { if (o[static_cast<std::size_t>(i)] == 0) ++zeros; else if (std::fabs(o[static_cast<std::size_t>(i)] - 0.5f) < 1e-3f) ++half; }
    CHECK(zeros + half == 240 && half > 0);
    udpClose(s);
  }
  { // through the runtime on the null device: the master goes out, comes back on network inputs 1 / 2 into strips 3 / 4 (muted from the master: no loop)
    Runtime rt(4); DeviceConfig cfg; cfg.backend = "null"; cfg.inChannels = 2; cfg.outChannels = 2; cfg.frames = 256;
    cfg.nullInput = [](float* const* in, std::size_t frames, std::uint64_t f0) { for (std::size_t i = 0; i < frames; ++i) { in[0][i] = static_cast<float>(0.25 * std::sin(2 * M_PI * 440.0 * static_cast<double>(f0 + i) / 48000.0)); in[1][i] = 0; } };
    CHECK(rt.open(cfg, err) && rt.start(err));
    rt.setGainDb(0, 0); rt.setPan(0, -1); rt.setLimiter(false); for (int s = 1; s < 4; ++s) rt.setGainDb(s, -120);
    Aoip net(rt); net.receiver().setRejectOwn(false); StreamDesc d; d.address = "127.0.0.1"; d.port = 0; d.channels = 2; d.bits = 24; d.rate = 48000;
    CHECK(net.receiveStart(d, "", 20, err)); d.port = net.receiver().port(); d.name = "Loop";
    CHECK(net.sendStart(d, "", false, err));
    rt.setInputRoute(2, Runtime::kNetInBase); rt.setInputRoute(3, Runtime::kNetInBase + 1);
    ms(700); const Snapshot sn = rt.snapshot();
    CHECK(sn.strips[2].inPk > 0.1 && sn.strips[2].inPk < 0.5);       // the 440 Hz tone, hard left in the master, arrives on network channel 1
    CHECK(sn.strips[3].inPk < 0.01);                                 // and not on channel 2
    const std::string js = net.statsJson(); CHECK(js.find("\"send\":{\"running\":true") != std::string::npos && js.find("\"receive\":{\"running\":true") != std::string::npos && js.find("\"lost\":0,") != std::string::npos);
    CHECK(net.sender().stats().packets > 400 && net.receiver().stats().packets > 400);
    rt.setInputRoute(2, 2); net.sendStop(); net.receiveStop(); CHECK(!net.sender().running() && !net.receiver().running());
    ms(60); rt.stop();
  }
  { Sender tx; StreamDesc d; d.address = "not an address"; CHECK(!tx.start(d, "", false, err) && !err.empty()); d.address = "239.1.2.3"; d.channels = 64; CHECK(!tx.start(d, "", false, err));   // 64 x 24 bit x 48 frames does not fit one packet
    Receiver rx; d.channels = 2; d.bits = 20; CHECK(!rx.start(d, "", 20, err)); }
}

// PTP: an Announce names the grandmaster (priority, class, domain), Sync + Follow_Up from it are counted; the best master comes first
static void ptp() {
  auto hdr = [](std::vector<std::uint8_t>& p, int type, int len, int domain, std::uint8_t idLast, bool twoStep) {
    p.assign(static_cast<std::size_t>(len), 0); p[0] = static_cast<std::uint8_t>(type); p[1] = 2; p[2] = static_cast<std::uint8_t>(len >> 8); p[3] = static_cast<std::uint8_t>(len); p[4] = static_cast<std::uint8_t>(domain);
    p[6] = twoStep ? 0x02 : 0; for (int i = 0; i < 8; ++i) p[20 + i] = static_cast<std::uint8_t>(0x10 + i); p[27] = idLast; };
  auto announce = [&](std::uint8_t idLast, int prio1, int cls) { std::vector<std::uint8_t> p; hdr(p, 11, 64, 0, idLast, false); p[47] = static_cast<std::uint8_t>(prio1); p[48] = static_cast<std::uint8_t>(cls); p[49] = 0x21; p[52] = 128;
    for (int i = 0; i < 8; ++i) p[53 + i] = static_cast<std::uint8_t>(0x10 + i); p[60] = idLast; p[63] = 0x20; return p; };
  am::net::PtpMonitor mon; std::vector<std::uint8_t> a = announce(0xAA, 128, 248), b = announce(0xBB, 100, 6);
  mon.inject(a.data(), a.size(), "10.0.0.5"); mon.inject(b.data(), b.size(), "10.0.0.9");
  std::vector<std::uint8_t> sy; hdr(sy, 0, 44, 0, 0xBB, true); std::vector<std::uint8_t> fu; hdr(fu, 8, 44, 0, 0xBB, false); fu[39] = 0x10;   // seconds = 16
  for (int i = 0; i < 8; ++i) { mon.inject(sy.data(), sy.size(), "10.0.0.9"); mon.inject(fu.data(), fu.size(), "10.0.0.9"); }
  const auto m = mon.masters();
  CHECK(m.size() == 2); if (m.size() == 2) { CHECK(m[0].priority1 == 100 && m[0].clockClass == 6 && m[0].from == "10.0.0.9" && m[0].id == "10-11-12-13-14-15-16-bb" && m[0].ptpSeconds == 16); CHECK(m[1].clockClass == 248); }
  am::net::PtpPacket k; std::vector<std::uint8_t> bad = a; bad[1] = 1; CHECK(!am::net::parsePtp(bad.data(), bad.size(), k));   // PTP v1 is refused
  CHECK(!am::net::parsePtp(a.data(), 20, k)); CHECK(am::net::ptpClockName(6).find("GPS") != std::string::npos);
}

// the feedback that hissed on the NETWORK page: the receiver on the same group/port as our own send got the master back,
// patched it onto strips that feed the master again. Own packets are now dropped: tone in -> no return; silence in -> exact silence out.
static void selfLoop() {
  std::string err; Runtime rt(4); DeviceConfig cfg; cfg.backend = "null"; cfg.inChannels = 2; cfg.outChannels = 2; cfg.frames = 256;
  cfg.nullInput = [](float* const* in, std::size_t frames, std::uint64_t) { for (std::size_t i = 0; i < frames; ++i) { in[0][i] = 0; in[1][i] = 0; } };
  CHECK(rt.open(cfg, err) && rt.start(err)); rt.setLimiter(false); for (int s = 0; s < 4; ++s) rt.setGainDb(s, 0);   // every strip at 0 dB into the master: a real loop if anything came back
  Aoip net(rt); StreamDesc d; d.address = "127.0.0.1"; d.port = 0; d.channels = 2; d.bits = 16; d.rate = 48000;
  CHECK(net.receiveStart(d, "", 20, err)); d.port = net.receiver().port(); CHECK(net.sendStart(d, "", false, err));
  rt.setInputRoute(2, Runtime::kNetInBase); rt.setInputRoute(3, Runtime::kNetInBase + 1);
  ms(700); const Snapshot sn = rt.snapshot(); const RecvStats rs = net.receiver().stats();
  CHECK(rs.selfLoop > 100 && rs.packets == 0);                                       // own packets seen, none mixed in
  CHECK(sn.strips[2].inPk == 0 && sn.strips[3].inPk == 0);                           // silence in -> silence out: no noise floor at all (< -90 dBFS), no denormals
  float L[256], R[256]; const float* st[1] = {L}; for (int i = 0; i < 256; ++i) { L[i] = 1e-39f; R[i] = 0; }   // denormal input is flushed by the 16 bit wire
  Receiver rx2; rx2.setRejectOwn(false); StreamDesc d2 = d; d2.port = 0; CHECK(rx2.start(d2, "", 5, err)); d2.port = rx2.port(); Sender tx2; CHECK(tx2.start(d2, "", false, err));
  std::vector<float> o(512); float* op[1] = {o.data()}; for (int b = 0; b < 60; ++b) { tx2.onBlock(st, 1, L, R, 256, 48000); ms(5); }
  rx2.pull(op, 1, 256, 48000); for (float v : o) { CHECK(v == 0 || std::fabs(v) < 3.2e-5f); CHECK(v == 0 || std::fpclassify(v) == FP_NORMAL); }   // below -90 dBFS, never subnormal
  tx2.stop(); rx2.stop(); net.sendStop(); net.receiveStop(); ms(60); rt.stop();
}

int main() {
  wire(); midi(); ptp();
  stream(24, 48000, 48000, 2); stream(16, 48000, 48000, 1); stream(24, 48000, 48000, 4); stream(24, 44100, 48000, 2);
  lossAndRuntime(); selfLoop();
  if (fails) { std::printf("net_test: %d FAILED\n", fails); return 1; }
  std::printf("net_test: ok\n"); return 0;
}
