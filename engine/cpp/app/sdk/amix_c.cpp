// The C SDK over the engine (sdk/c/include/amix.h): thin, allocation-free in the audio path, strings as malloc'd JSON.
#define AMIX_BUILD_DLL
#include "net/aoip.hpp"
#include "net/midi.hpp"
#include <thread>
#include <chrono>
#include "amix.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "audiomixer/engine.hpp"
#include "fx/fx.hpp"
#include <array>
#include <mutex>
#include "audiomixer/runtime.hpp"
#include "license/license.hpp"
#include "paths/paths.hpp"

#ifndef AMIX_VERSION_STRING
#define AMIX_VERSION_STRING "0.1.0"
#endif

namespace {
char* dup(const std::string& s) { char* p = static_cast<char*>(std::malloc(s.size() + 1)); if (p) std::memcpy(p, s.c_str(), s.size() + 1); return p; }
std::string jstr(const std::string& s) { std::string o = "\""; for (char c : s) { switch (c) { case '"': o += "\\\""; break; case '\\': o += "\\\\"; break; case '\n': o += "\\n"; break; default: if (static_cast<unsigned char>(c) < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; } else o += c; } } return o + "\""; }
void fillMeter(amix_meter& m, const am::Snapshot::StripMeter& s) { m.peak = s.pk; m.rms = s.rms; m.hold = s.hold; m.clips = s.clipN; m.comp_gr_db = s.compGrDb; m.gate_db = s.gateDb; m.in_peak = s.inPk; }
void fillMeter(amix_meter& m, const am::MeterState& s) { m.peak = s.pk; m.rms = s.rms; m.hold = s.hold; m.clips = s.clipN; m.comp_gr_db = 0; m.gate_db = 0; m.in_peak = 0; }
std::int64_t nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
}  // namespace

// INSERTS: up to AMIX_MAX_INSERTS rack effects (app/fx) on each strip (before the mixer, on the strip's signal) and on the master (after it).
// The audio thread only try-locks: a block that meets an edit in progress plays without its inserts, it never waits.
struct amix_mixer {
  am::Mixer mixer; std::vector<float> silence; int frames = 0; double fs;
  std::mutex fxM; std::vector<std::array<std::unique_ptr<am::fx::Effect>, AMIX_MAX_INSERTS>> fx;   // [strip] ... [strips] = the master
  std::vector<std::vector<float>> work; std::vector<float> scratch;
  explicit amix_mixer(int n, double rate) : mixer(n, rate), fs(rate), fx(static_cast<std::size_t>(n) + 1) {}
  std::unique_ptr<am::fx::Effect>* slot(int strip, int s) { const int k = strip < 0 ? mixer.strips() : strip; if (k < 0 || k > mixer.strips() || s < 0 || s >= AMIX_MAX_INSERTS) return nullptr; return &fx[static_cast<std::size_t>(k)][static_cast<std::size_t>(s)]; }
};
struct amix_rt { am::Runtime rt; std::string backend; am::net::Aoip net; explicit amix_rt(int n) : rt(n), net(rt) {} };

extern "C" {

const char* amix_version(void) { return AMIX_VERSION_STRING; }
int amix_api_version(void) { return AMIX_API_VERSION; }
void amix_free(void* p) { std::free(p); }

char* amix_backends_json(void) { std::string o = "["; bool first = true; for (const auto& b : am::availableBackends()) { if (!first) o += ","; first = false; o += jstr(b); } return dup(o + "]"); }
char* amix_devices_json(void) {
  std::string o = "["; bool first = true;
  for (const am::DeviceInfo& d : am::listDevices()) {
    if (!first) o += ",";
    first = false;
    o += "{\"backend\":" + jstr(d.backend) + ",\"id\":" + jstr(d.id) + ",\"name\":" + jstr(d.name) + ",\"inChannels\":" + std::to_string(d.inChannels) + ",\"outChannels\":" + std::to_string(d.outChannels) + ",\"defaultRate\":" + std::to_string(static_cast<int>(d.defaultRate)) + ",\"defaultIn\":" + (d.defaultIn ? "true" : "false") + ",\"defaultOut\":" + (d.defaultOut ? "true" : "false") + ",\"kind\":" + jstr(d.kind) + "}";
  }
  return dup(o + "]");
}

amix_mixer* amix_mixer_create(int strips, double fs) { return new (std::nothrow) amix_mixer(strips < 1 ? 1 : strips > AMIX_MAX_STRIPS ? AMIX_MAX_STRIPS : strips, fs > 0 ? fs : 48000); }
void amix_mixer_destroy(amix_mixer* m) { delete m; }
int amix_mixer_strips(const amix_mixer* m) { return m ? m->mixer.strips() : 0; }
void amix_mixer_set_gain(amix_mixer* m, int s, double db) { if (m) m->mixer.setGainDb(s, db); }
void amix_mixer_set_pan(amix_mixer* m, int s, double pan) { if (m) m->mixer.setPan(s, pan); }
void amix_mixer_set_mute(amix_mixer* m, int s, int on) { if (m) m->mixer.setMute(s, on != 0); }
void amix_mixer_set_solo(amix_mixer* m, int s, int on) { if (m) m->mixer.setSolo(s, on != 0); }
void amix_mixer_set_master(amix_mixer* m, double db) { if (m) m->mixer.setMasterDb(db); }
void amix_mixer_set_limiter(amix_mixer* m, int on, double ceil, double rel) { if (m) m->mixer.setLimiter(on != 0, ceil, rel); }
void amix_mixer_set_send(amix_mixer* m, int s, int b, double db, int pre) { if (m) m->mixer.setSend(s, b, db, pre != 0); }
void amix_mixer_set_bus_level(amix_mixer* m, int b, double db) { if (m) m->mixer.setBusLevel(b, db); }
void amix_mixer_set_bus_return(amix_mixer* m, int b, double db) { if (m) m->mixer.setBusReturn(b, db); }
void amix_mixer_set_width(amix_mixer* m, double w) { if (m) m->mixer.setWidth(w); }
void amix_mixer_set_spatial(amix_mixer* m, int strip, int on, double az, double el, double dist, double spread) { if (m) m->mixer.setSpatial(strip, on != 0, az, el, dist, spread); }
void amix_mixer_set_depth(amix_mixer* m, int strip, double depth) { if (m) m->mixer.setDepth(strip, depth); }
int amix_mixer_process5(amix_mixer* m, const float* const* in, int frames, float* const* outs) {
  if (!m || frames <= 0 || !outs) return 0;
  for (int c = 0; c < 5; ++c) if (!outs[c]) return 0;
  if (frames > m->frames) { m->silence.assign(static_cast<std::size_t>(frames), 0.f); m->frames = frames; }
  const float* ins[AMIX_MAX_STRIPS]; for (int s = 0; s < m->mixer.strips(); ++s) ins[s] = (in && in[s]) ? in[s] : m->silence.data();
  m->mixer.process5(ins, static_cast<std::size_t>(frames), outs); return frames;
}
int amix_mixer_process(amix_mixer* m, const float* const* in, int frames, float* outL, float* outR) {
  if (!m || frames <= 0 || !outL || !outR) return 0;
  const int n = m->mixer.strips();
  if (frames > m->frames) {   // grows only on the first larger block (the native loop calls with its fixed maximum first)
    m->silence.assign(static_cast<std::size_t>(frames), 0.f); m->scratch.assign(static_cast<std::size_t>(frames), 0.f); m->work.assign(static_cast<std::size_t>(n), std::vector<float>(static_cast<std::size_t>(frames), 0.f)); m->frames = frames; }
  const float* ins[AMIX_MAX_STRIPS]; for (int s = 0; s < n; ++s) ins[s] = (in && in[s]) ? in[s] : m->silence.data();
  std::unique_lock<std::mutex> lk(m->fxM, std::try_to_lock);
  if (lk.owns_lock()) for (int s = 0; s < n; ++s) {   // strip inserts: the strip's signal through each effect (stereo effects: left kept, a mono strip)
    bool any = false; for (const auto& e : m->fx[static_cast<std::size_t>(s)]) if (e && !e->bypass()) { any = true; break; }
    if (!any || !in || !in[s]) continue;
    float* w = m->work[static_cast<std::size_t>(s)].data(); std::copy(in[s], in[s] + frames, w);
    for (const auto& e : m->fx[static_cast<std::size_t>(s)]) if (e && !e->bypass()) { std::copy(w, w + frames, m->scratch.data()); e->process(w, m->scratch.data(), static_cast<std::size_t>(frames)); }
    ins[s] = w; }
  m->mixer.process(ins, static_cast<std::size_t>(frames), outL, outR);
  if (lk.owns_lock()) for (const auto& e : m->fx[static_cast<std::size_t>(n)]) if (e && !e->bypass()) e->process(outL, outR, static_cast<std::size_t>(frames));   // the master rack
  return frames;
}
char* amix_fx_catalog_json(void) {
  std::string j = "["; bool first = true; for (const auto& [k, nm] : am::fx::catalog()) { j += (first ? "" : ",") + std::string("{\"kind\":") + jstr(k) + ",\"name\":" + jstr(nm) + "}"; first = false; }
  return dup(j + "]");
}
int amix_mixer_insert(amix_mixer* m, int strip, int s, const char* kind) {
  if (!m) return -1; std::unique_ptr<am::fx::Effect> e;
  if (kind && *kind) { e = am::fx::make(kind, m->fs); if (!e) return -2; }
  std::lock_guard<std::mutex> g(m->fxM); auto* p = m->slot(strip, s); if (!p) return -1; *p = std::move(e); return 0;   // the old effect dies here, never under the audio thread
}
char* amix_mixer_insert_json(amix_mixer* m, int strip, int s) {
  if (!m) return dup("null"); std::lock_guard<std::mutex> g(m->fxM); auto* p = m->slot(strip, s); if (!p || !*p) return dup("null");
  const am::fx::Effect& e = **p; std::string j = "{\"kind\":" + jstr(e.kind()) + ",\"name\":" + jstr(e.name()) + ",\"bypass\":" + (e.bypass() ? "true" : "false") + ",\"mix\":" + std::to_string(e.mix()) + ",\"params\":[";
  bool first = true; for (const am::fx::Param& q : e.params()) { j += (first ? "" : ",") + std::string("{\"id\":") + jstr(q.id) + ",\"title\":" + jstr(q.title) + ",\"value\":" + std::to_string(q.value) + ",\"default\":" + std::to_string(q.def) + ",\"text\":" + jstr(e.text(q.id, q.value)) + "}"; first = false; }
  return dup(j + "]}");
}
int amix_mixer_insert_set(amix_mixer* m, int strip, int s, const char* id, double v) {
  if (!m || !id) return -1; std::lock_guard<std::mutex> g(m->fxM); auto* p = m->slot(strip, s); if (!p || !*p) return -1;
  const std::string k = id; if (k == "mix") (*p)->setMix(v); else if (k == "bypass") (*p)->setBypass(v >= 0.5); else (*p)->set(k, v < 0 ? 0 : v > 1 ? 1 : v);
  return 0;
}
void amix_mixer_levels(const amix_mixer* m, amix_levels* o) {
  if (!m || !o) return;
  std::memset(o, 0, sizeof *o);
  for (int i = 0; i < m->mixer.strips(); ++i) { const am::Strip& s = m->mixer.strip(i); fillMeter(o->strips[i], s.meter.state()); o->strips[i].in_peak = s.inPk; }
  for (int c = 0; c < 2; ++c) fillMeter(o->master[c], m->mixer.masterMeter(c));
  for (int k = 0; k < AMIX_MAX_BUSES; ++k) fillMeter(o->buses[k], m->mixer.bus(k).meter.state());
  const am::Loudness& l = m->mixer.loudness(); o->momentary_lufs = l.momentary(); o->short_term_lufs = l.shortTerm(); o->integrated_lufs = l.integrated(); o->lra_lu = l.range(); o->true_peak_dbtp = l.truePeak(); o->correlation = m->mixer.phase().corr;
}

amix_rt* amix_rt_create(int strips) { return new (std::nothrow) amix_rt(strips < 1 ? 1 : strips > AMIX_MAX_STRIPS ? AMIX_MAX_STRIPS : strips); }
void amix_rt_destroy(amix_rt* r) { delete r; }
int amix_rt_open(amix_rt* r, const amix_config* c, char* err, size_t errLen) {
  if (err && errLen) err[0] = 0;
  if (!r || !c) return -1;
  am::DeviceConfig cfg; cfg.backend = c->backend && *c->backend ? c->backend : "auto"; cfg.inDevice = c->in_device ? c->in_device : ""; cfg.outDevice = c->out_device ? c->out_device : "";
  cfg.sampleRate = c->sample_rate > 0 ? c->sample_rate : 48000; cfg.frames = c->frames > 0 ? static_cast<unsigned>(c->frames) : 256; cfg.inChannels = c->in_channels > 0 ? c->in_channels : 2; cfg.outChannels = c->out_channels > 0 ? c->out_channels : 2; cfg.exclusive = c->exclusive != 0;
  std::string e; r->rt.stop();
  if (!r->rt.open(cfg, e) || !r->rt.start(e)) { if (err && errLen) { std::strncpy(err, e.c_str(), errLen - 1); err[errLen - 1] = 0; } return -2; }
  r->backend = r->rt.backendName(); return 0;
}
void amix_rt_stop(amix_rt* r) { if (r) r->rt.stop(); }
int amix_rt_running(const amix_rt* r) { return r && r->rt.running() ? 1 : 0; }
const char* amix_rt_backend(const amix_rt* r) { return r ? r->backend.c_str() : ""; }
double amix_rt_sample_rate(const amix_rt* r) { return r ? r->rt.sampleRate() : 0; }
void amix_rt_set_gain(amix_rt* r, int s, double db) { if (r) r->rt.setGainDb(s, db); }
void amix_rt_set_pan(amix_rt* r, int s, double p) { if (r) r->rt.setPan(s, p); }
void amix_rt_set_mute(amix_rt* r, int s, int on) { if (r) r->rt.setMute(s, on != 0); }
void amix_rt_set_solo(amix_rt* r, int s, int on) { if (r) r->rt.setSolo(s, on != 0); }
void amix_rt_set_trim(amix_rt* r, int s, double db) { if (r) r->rt.setTrimDb(s, db); }
void amix_rt_set_master(amix_rt* r, double db) { if (r) r->rt.setMasterDb(db); }
void amix_rt_set_limiter(amix_rt* r, int on, double ceil, double rel) { if (r) r->rt.setLimiter(on != 0, ceil, rel); }
void amix_rt_set_eq(amix_rt* r, int s, int band, int type, double f, double q, double g) { if (r) r->rt.setEqBand(s, band, static_cast<am::dsp::FilterType>(type < 0 ? 0 : type > 8 ? 8 : type), f, q, g); }
void amix_rt_set_compressor(amix_rt* r, int s, int on, double th, double ratio, double knee, double att, double rel, double mk) { if (r) r->rt.setCompressor(s, on != 0, th, ratio, knee, att, rel, mk); }
void amix_rt_set_gate(amix_rt* r, int s, int on, double th, double range, double att, double hold, double rel) { if (r) r->rt.setGate(s, on != 0, th, range, att, hold, rel); }
void amix_rt_set_send(amix_rt* r, int s, int b, double db, int pre) { if (r) r->rt.setSend(s, b, db, pre != 0); }
void amix_rt_set_bus_level(amix_rt* r, int b, double db) { if (r) r->rt.setBusLevel(b, db); }
void amix_rt_set_bus_mute(amix_rt* r, int b, int on) { if (r) r->rt.setBusMute(b, on != 0); }
void amix_rt_set_bus_return(amix_rt* r, int b, double db) { if (r) r->rt.setBusReturn(b, db); }
void amix_rt_set_width(amix_rt* r, double w) { if (r) r->rt.setWidth(w); }
void amix_rt_set_monitor(amix_rt* r, int src, double db, int dim, int mono, int mute) { if (r) r->rt.setMonitor(src, db, dim != 0, mono != 0, mute != 0); }
void amix_rt_set_input_route(amix_rt* r, int s, int in) { if (r) r->rt.setInputRoute(s, in); }
void amix_rt_set_output_route(amix_rt* r, int out, int src) { if (r) r->rt.setOutputRoute(out, src); }
void amix_rt_clear_clips(amix_rt* r) { if (r) r->rt.clearClips(); }
void amix_rt_levels(const amix_rt* r, amix_levels* o) {
  if (!r || !o) return;
  std::memset(o, 0, sizeof *o); const am::Snapshot s = r->rt.snapshot();
  for (int i = 0; i < AMIX_MAX_STRIPS; ++i) fillMeter(o->strips[i], s.strips[i]);
  for (int c = 0; c < 2; ++c) fillMeter(o->master[c], s.master[c]);
  for (int k = 0; k < AMIX_MAX_BUSES; ++k) fillMeter(o->buses[k], s.buses[k]);
  o->monitor_peak[0] = s.monitorPk[0]; o->monitor_peak[1] = s.monitorPk[1]; o->momentary_lufs = s.momentary; o->short_term_lufs = s.shortTerm; o->integrated_lufs = s.integrated; o->lra_lu = s.lra; o->true_peak_dbtp = s.truePeak; o->correlation = s.correlation; o->blocks = s.blocks; o->frames = s.frames; o->xruns = s.xruns;
}

void amix_set_dirs(const char* data, const char* cache, const char* temp) { am::paths::setDirs(data ? data : "", cache ? cache : "", temp ? temp : ""); }
static std::string jsonStr(const std::string& v) { std::string o = "\""; for (char c : v) { if (c == '"' || c == '\\') o += '\\'; o += c; } return o + "\""; }
char* amix_paths_json(void) {
  std::string j = "{\"os\":" + jsonStr(am::paths::osName()) + ",\"program\":" + jsonStr(am::paths::programDir()) + ",\"data\":" + jsonStr(am::paths::dataDir()) + ",\"cache\":" + jsonStr(am::paths::cacheDir()) + ",\"prefetch\":" + jsonStr(am::paths::prefetchDir()) + ",\"temp\":" + jsonStr(am::paths::tempDir()) + ",\"downloads\":" + jsonStr(am::paths::downloadsDir()) + "}";
  char* out = static_cast<char*>(std::malloc(j.size() + 1)); std::memcpy(out, j.c_str(), j.size() + 1); return out;
}
char* amix_license_json(void) {
  const am::lic::Status st = am::lic::status(nowMs());
  return dup("{\"state\":" + jstr(st.state) + ",\"plan\":" + jstr(st.planId) + ",\"planName\":" + jstr(st.planName) + ",\"channels\":" + std::to_string(st.channels) + ",\"source\":" + jstr(st.source) + ",\"expires\":" + std::to_string(st.expires) + ",\"machine\":" + jstr(st.machine) + ",\"message\":" + jstr(st.message) + "}");
}
char* amix_machine_id(void) { return dup(am::lic::machineId()); }

static int netFail(const std::string& e, char* err, size_t n) { if (err && n) std::snprintf(err, n, "%s", e.c_str()); return -1; }
int amix_rt_net_send_start(amix_rt* r, const char* name, const char* address, int port, int channels, int bits, const char* iface, int announce, char* err, size_t n) {
  if (!r || !address) return netFail("no runtime or address", err, n);
  am::net::StreamDesc d; if (name && *name) d.name = name; d.address = address; d.port = port; d.channels = channels; d.bits = bits; std::string e;
  return r->net.sendStart(d, iface ? iface : "", announce != 0, e) ? 0 : netFail(e, err, n);
}
void amix_rt_net_send_stop(amix_rt* r) { if (r) r->net.sendStop(); }
int amix_rt_net_receive_start(amix_rt* r, const char* address, int port, int channels, int bits, int rate, int latency_ms, const char* iface, char* err, size_t n) {
  if (!r || !address) return netFail("no runtime or address", err, n);
  am::net::StreamDesc d; d.address = address; d.port = port; d.channels = channels; d.bits = bits; d.rate = rate; std::string e;
  return r->net.receiveStart(d, iface ? iface : "", latency_ms, e) ? 0 : netFail(e, err, n);
}
void amix_rt_net_receive_stop(amix_rt* r) { if (r) r->net.receiveStop(); }
char* amix_rt_net_stats_json(amix_rt* r) { return dup(r ? r->net.statsJson() : std::string("{}")); }
char* amix_net_discover_json(int wait_ms, const char* iface) {
  am::net::Discovery d; std::string e, o = "[";
  if (d.start(iface ? iface : "", e)) { std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms < 0 ? 0 : (wait_ms > 60000 ? 60000 : wait_ms))); bool first = true;
    for (const auto& f : d.list()) { if (!first) o += ","; first = false; char b[160]; std::snprintf(b, sizeof b, ",\"port\":%d,\"channels\":%d,\"bits\":%d,\"rate\":%d,\"ptimeMs\":%g,", f.d.port, f.d.channels, f.d.bits, f.d.rate, f.d.ptimeMs);
      o += "{\"name\":" + jstr(f.d.name) + ",\"origin\":" + jstr(f.d.origin) + ",\"address\":" + jstr(f.d.address) + b + "\"refClock\":" + jstr(f.d.refClock) + "}"; } }
  return dup(o + "]");
}
char* amix_net_transports_json(void) { return dup(am::net::transportsJson()); }
char* amix_net_addresses_json(void) { std::string o = "["; bool first = true; for (const auto& a : am::net::localAddresses()) { if (!first) o += ","; first = false; o += jstr(a); } return dup(o + "]"); }
void amix_midi_action_default(int status, int d1, int d2, amix_midi_action* out) {
  if (!out) return; static const am::net::MidiMap map;
  const am::net::MidiAction a = map.apply({static_cast<std::uint8_t>(status), static_cast<std::uint8_t>(d1 & 0x7f), static_cast<std::uint8_t>(d2 & 0x7f)});
  out->kind = static_cast<int>(a.kind); out->strip = a.strip; out->value = a.value;
}
char* amix_midi_inputs_json(void) { std::string o = "["; bool first = true; for (const auto& p : am::net::midiInputs()) { if (!first) o += ","; first = false; o += "{\"id\":" + jstr(p.id) + ",\"name\":" + jstr(p.name) + "}"; } return dup(o + "]"); }

}  // extern "C"
