#include "Measure.h"

#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <cmath>

#include "Engine.h"
#include "arrange/arrange.hpp"
#include "paths/paths.hpp"

namespace m = am::measure;
static QVariantList vl(const std::vector<double>& v) { QVariantList l; l.reserve(static_cast<qsizetype>(v.size())); for (double x : v) l << x; return l; }
static QVariantMap grid(const m::Grid& g) { QVariantMap o; o["freq"] = vl(g.x); o["times"] = vl(g.y); QVariantList rows; for (const auto& r : g.db) rows << QVariant(vl(r)); o["rows"] = rows; return o; }

Measure::Measure(Engine* engine, QObject* parent) : QObject(parent), engine_(engine) {
  engine_->setMeasureIo(&io_, &io_);
  poll_.setInterval(100); connect(&poll_, &QTimer::timeout, this, [this] { if (io_.busy()) { emit changed(); return; } poll_.stop(); finish(); });
}
Measure::~Measure() { io_.cancel(); engine_->setMeasureIo(nullptr, nullptr); }
bool Measure::chime() {
  if (!engine_->running() || io_.busy() || state_ == "measuring") return false;
  const double fs = engine_->sampleRate(); const std::size_t n = static_cast<std::size_t>(fs * 2.6); std::vector<float> sig(n, 0.f);
  const double notes[5] = {261.63, 329.63, 392.00, 493.88, 587.33};   // C4 E4 G4 B4 D5: C major ninth
  for (int k = 0; k < 5; ++k) { const std::size_t start = static_cast<std::size_t>(fs * 0.16 * k);
    for (std::size_t i = start; i < n; ++i) { const double t = static_cast<double>(i - start) / fs, env = std::min(1.0, t / 0.012) * std::exp(-t * 1.5), ph = 2 * 3.14159265358979 * notes[k] * t;
      sig[i] += static_cast<float>(0.028 * env * (std::sin(ph) + 0.35 * std::sin(2 * ph) * std::exp(-t * 3) + 0.12 * std::sin(3 * ph) * std::exp(-t * 5))); } }
  const std::size_t fade = static_cast<std::size_t>(fs * 0.25); for (std::size_t i = 0; i < fade; ++i) sig[n - 1 - i] *= static_cast<float>(i) / static_cast<float>(fade);
  io_.arm(std::move(sig), 0); return true;
}
void Measure::cancel() { io_.cancel(); poll_.stop(); end(); set("idle", "cancelled"); }
// A measurement of a microphone: the strip is recorded as the sound card delivers it (before its trim, EQ, gate, compressor and plug-ins) and is
// muted in the mix while the signal plays, so the microphone does not come back out of the loudspeaker it is measuring. Both are undone at the end.
void Measure::begin(int strip) {
  end(); measStrip_ = strip; info_.clear();
  if (am::Runtime* rt = engine_->runtime()) rt->setTapRawStrip(strip);
  const QVariantList st = engine_->stripState();
  if (strip >= 0 && strip < st.size() && !st[strip].toMap()["mute"].toBool()) { engine_->setMute(strip, true); unmuteAfter_ = true; }
}
void Measure::end() {
  if (measStrip_ < 0) return;
  if (am::Runtime* rt = engine_->runtime()) rt->setTapRawStrip(-1);
  if (unmuteAfter_) engine_->setMute(measStrip_, false);
  unmuteAfter_ = false; measStrip_ = -1;
}
// What the recording itself says: its peak, the noise before the signal started, the distance between them, and what to do about it.
void Measure::describe(const std::vector<float>& rec, double fs, double leadSec, double delayMs) {
  QVariantMap o; QStringList lines; double pk = 0, e = 0; for (float v : rec) { pk = std::max(pk, std::fabs(static_cast<double>(v))); e += static_cast<double>(v) * v; }
  const std::size_t quiet = std::min(rec.size(), static_cast<std::size_t>(fs * leadSec * 0.75)); double ne = 0; for (std::size_t i = 0; i < quiet; ++i) ne += static_cast<double>(rec[i]) * rec[i];
  const double peakDb = pk > 0 ? 20 * std::log10(pk) : -180, rmsDb = e > 0 && !rec.empty() ? 10 * std::log10(e / static_cast<double>(rec.size())) : -180, noiseDb = quiet > 0 && ne > 0 ? 10 * std::log10(ne / static_cast<double>(quiet)) : -180;
  const double snr = rmsDb - noiseDb; const bool clipped = pk >= 0.985; const int strip = measStrip_;
  const QVariantList routes = engine_->inputRoutes(); const int route = strip >= 0 && strip < routes.size() ? routes[strip].toInt() : -1; const int devIn = engine_->ioInfo()["inputs"].toInt();
  const QString input = route < 0 ? QStringLiteral("none") : route >= am::Runtime::kNetInBase ? QStringLiteral("network %1").arg(route - am::Runtime::kNetInBase + 1) : QStringLiteral("input %1").arg(route + 1);
  o["peakDb"] = peakDb; o["rmsDb"] = rmsDb; o["noiseDb"] = noiseDb; o["snrDb"] = snr; o["clipped"] = clipped; o["input"] = input; o["delayMs"] = delayMs; o["strip"] = strip + 1; o["rate"] = fs;
  if (strip < 0) lines << QStringLiteral("a recording made elsewhere · %1 Hz").arg(fs, 0, 'f', 0); else
  lines << QStringLiteral("CH %1 · %2 · %3 Hz · recorded before the channel's EQ and dynamics").arg(strip + 1).arg(input).arg(fs, 0, 'f', 0);
  lines << QStringLiteral("peak %1 dBFS · level %2 dBFS · noise %3 dBFS · signal to noise %4 dB").arg(peakDb, 0, 'f', 1).arg(rmsDb, 0, 'f', 1).arg(noiseDb, 0, 'f', 1).arg(noiseDb <= -179 ? 99.0 : snr, 0, 'f', 0);
  if (strip < 0) {} else if (route < 0) lines << QStringLiteral("This strip has no input: give it one on ROUTING.");
  else if (route < am::Runtime::kNetInBase && route >= devIn) lines << QStringLiteral("The sound card has %1 input(s) but this strip listens to input %2: pick a strip that has a real input, or open a device with a microphone on CONNECTIONS.").arg(devIn).arg(route + 1);
  if (clipped) lines << QStringLiteral("CLIPPED: the input reached full scale. Lower the microphone gain or the sweep level and measure again.");
  else if (pk < 1e-6) lines << QStringLiteral("Silent: no signal arrived. Check the microphone, its gain and phantom power, and that Windows allows this program to use the microphone.");
  else if (peakDb < -50) lines << QStringLiteral("Very quiet: raise the microphone gain or the sweep level (aim for a peak between -20 and -6 dBFS).");
  if (pk >= 1e-6 && noiseDb > -179 && snr < 20) lines << QStringLiteral("Noisy: the signal is less than 20 dB above the room noise; the curve below is not reliable. Play louder or measure in a quieter moment.");
  if (delayMs > 250) lines << QStringLiteral("Long delay (%1 ms): this driver buffers a lot. Timing and decay are still measured from the arrival.").arg(delayMs, 0, 'f', 0);
  o["lines"] = lines; o["ok"] = pk >= 1e-6 && !clipped && (noiseDb <= -179 || snr >= 20); info_ = o;
}

bool Measure::runSweep(int strip, double seconds, double f1, double f2, double levelDb, int ppo) {
  if (!engine_->running()) { set("idle", "Start the engine on CONNECTIONS first: the sweep plays on the master and is recorded from the strip."); return false; }
  if (io_.busy()) return false;
  const double fs = engine_->sampleRate(); seconds = std::clamp(seconds, 0.5, 30.0); f1 = std::clamp(f1, 5.0, fs * 0.4); f2 = std::clamp(f2, f1 * 2, fs * 0.45); ppo_ = std::clamp(ppo, 1, 96);
  sweep_ = m::makeSweep(fs, f1, f2, seconds, std::pow(10.0, std::clamp(levelDb, -60.0, 0.0) / 20)); stepped_ = false; lastFs_ = fs;
  begin(strip);
  { std::vector<float> sig = sweep_.signal; sig.resize(sig.size() + static_cast<std::size_t>(fs * 1.0), 0.f);   // one more second of recording: a slow driver's delay must not cut the top of the sweep or the room's decay
    io_.arm(std::move(sig), strip); }
  poll_.start(); set("measuring", QStringLiteral("log sweep %1 - %2 Hz, %3 s, on the master; recording CH %4").arg(f1, 0, 'f', 0).arg(f2, 0, 'f', 0).arg(seconds, 0, 'f', 1).arg(strip + 1)); return true;
}
bool Measure::runStepped(int strip, int ppo, double f1, double f2, double levelDb) {
  if (!engine_->running()) { set("idle", "Start the engine on CONNECTIONS first."); return false; }
  if (io_.busy()) return false;
  const double fs = engine_->sampleRate(); ppo_ = std::clamp(ppo, 1, 96); f1 = std::clamp(f1, 10.0, fs * 0.4); f2 = std::clamp(f2, f1, fs * 0.45); stepFreqs_.clear();
  for (double f = f1; f <= f2 * 1.0000001 && stepFreqs_.size() < 480; f *= std::pow(2.0, 1.0 / ppo_)) stepFreqs_.push_back(f);   // 480 tones of 0.25 s = 2 minutes at most
  const double amp = std::pow(10.0, std::clamp(levelDb, -60.0, 0.0) / 20); std::vector<float> sig; sig.reserve(static_cast<std::size_t>(fs * stepSec_ * static_cast<double>(stepFreqs_.size() + 1)));
  for (double f : stepFreqs_) { std::vector<float> t = m::tone(fs, f, stepSec_, amp); const std::size_t fade = static_cast<std::size_t>(fs * 0.005); for (std::size_t i = 0; i < fade && i < t.size(); ++i) { const float g = static_cast<float>(i) / static_cast<float>(fade); t[i] *= g; t[t.size() - 1 - i] *= g; } sig.insert(sig.end(), t.begin(), t.end()); }
  sig.resize(sig.size() + static_cast<std::size_t>(fs * 0.3), 0.f); stepped_ = true; lastFs_ = fs;
  begin(strip);
  io_.arm(std::move(sig), strip); poll_.start(); set("measuring", QStringLiteral("stepped sine, %1 tones (%2 per octave), recording CH %3").arg(stepFreqs_.size()).arg(ppo_).arg(strip + 1)); return true;
}
void Measure::finish() {
  lastRec_ = io_.recorded();
  if (!stepped_) { analyseSweep(lastRec_, lastFs_); end(); return; }
  describe(lastRec_, lastFs_, 0.0, 0.0); end();
  // each tone's own quarter second, read from where the loudest tone says the path delay is (up to 100 ms)
  const std::size_t seg = static_cast<std::size_t>(lastFs_ * stepSec_); m::Response r; std::vector<double> thd, thdn;
  for (std::size_t k = 0; k < stepFreqs_.size(); ++k) { const std::size_t from = k * seg + seg / 4; if (from + seg / 2 > lastRec_.size()) break; std::vector<float> x(lastRec_.begin() + static_cast<long>(from), lastRec_.begin() + static_cast<long>(from + seg / 2 + seg / 8));
    const m::Stepped s = m::analyseTone(x, lastFs_, stepFreqs_[k]); r.freq.push_back(stepFreqs_[k]); r.magDb.push_back(s.amplitude > 1e-9 ? 20 * std::log10(s.amplitude) : -180); r.phaseDeg.push_back(0); thd.push_back(s.thdPercent); thdn.push_back(s.thdnPercent); }
  resp_ = r; publish(r, thd, thdn, "stepped"); decay_.clear(); set("done", QStringLiteral("stepped sine: %1 points").arg(r.freq.size()));
}
void Measure::analyseSweep(const std::vector<float>& rec, double fs) {
  double pk = 0; for (float v : rec) pk = std::max(pk, std::fabs(static_cast<double>(v)));
  if (pk < 1e-6) { describe(rec, fs, sweep_.leadSec, 0.0); result_.clear(); decay_.clear(); set("done", "The recording is silent: route an input to that strip (ROUTING) and connect the microphone or the loopback cable."); return; }
  ir_ = m::deconvolve(rec, sweep_); ir_.fs = fs; resp_ = m::response(ir_, ppo_, sweep_.f1, sweep_.f2); const std::vector<double> thd = m::thdPercent(ir_, resp_.freq);
  publish(resp_, thd, {}, "sweep");
  const m::Decay d = m::decay(ir_, 2.0); QVariantMap o; const std::size_t step = std::max<std::size_t>(1, d.etcDb.size() / 600); QVariantList etc, sch;
  for (std::size_t i = 0; i < d.etcDb.size(); i += step) { double mx = -240; for (std::size_t j = i; j < std::min(d.etcDb.size(), i + step); ++j) mx = std::max(mx, d.etcDb[j]); etc << mx; sch << d.schroederDb[i]; }
  const std::size_t base = static_cast<std::size_t>(sweep_.leadSec * fs) + sweep_.inverse.size() - 1;
  o["etc"] = etc; o["schroeder"] = sch; o["t20"] = d.t20; o["t30"] = d.t30; o["topt"] = d.topt; o["toptRange"] = d.toptRange; o["seconds"] = d.seconds; o["delayMs"] = ir_.peak >= base ? static_cast<double>(ir_.peak - base) / fs * 1000 : 0.0; decay_ = o;
  describe(rec, fs, sweep_.leadSec, o["delayMs"].toDouble());
  set("done", QStringLiteral("sweep: peak %1 dBFS, delay %2 ms, RT60 (T30) %3 s").arg(20 * std::log10(pk), 0, 'f', 1).arg(o["delayMs"].toDouble(), 0, 'f', 2).arg(d.t30, 0, 'f', 2));
}
void Measure::publish(const m::Response& in, const std::vector<double>& thd, const std::vector<double>& thdn, const QString& kind) {
  m::Response r = in; m::applyCalibration(r, cal_); m::applySplOffset(r, spl_); resp_ = r;
  QVariantMap o; o["freq"] = vl(r.freq); o["mag"] = vl(r.magDb); o["phase"] = vl(r.phaseDeg); o["thd"] = vl(thd); o["thdn"] = vl(thdn); o["kind"] = kind; result_ = o; filters_.clear(); eqCurve_.clear(); eq_.clear();
}
bool Measure::importSweep(const QString& wavPath, double seconds, double f1, double f2, int ppo) {
  am::arr::Audio a; std::string err; if (!am::arr::readWav(wavPath.toStdString(), a, err)) { set("idle", "cannot read the WAV: " + QString::fromStdString(err)); return false; }
  ppo_ = std::clamp(ppo, 1, 96); sweep_ = m::makeSweep(a.rate, f1, std::min(f2, a.rate * 0.45), std::clamp(seconds, 0.5, 30.0), 0.5); stepped_ = false; lastFs_ = a.rate; lastRec_ = a.L;
  analyseSweep(lastRec_, a.rate); if (state_ == "done" && !result_.isEmpty()) msg_ = "offline, " + QFileInfo(wavPath).fileName() + ": " + msg_; emit changed(); return !result_.isEmpty();
}
QString Measure::exportSweep(const QString& wavPath, double seconds, double f1, double f2, double levelDb) {
  const double fs = engine_->running() ? engine_->sampleRate() : 48000; const m::Sweep s = m::makeSweep(fs, f1, std::min(f2, fs * 0.45), std::clamp(seconds, 0.5, 30.0), std::pow(10.0, std::clamp(levelDb, -60.0, 0.0) / 20));
  const QString p = wavPath.isEmpty() ? QString::fromStdString(am::paths::file("sweep.wav")) : wavPath; std::string err;
  if (!am::arr::writeWav24(p.toStdString(), s.signal, s.signal, fs, err)) { set(state_, "cannot write the sweep: " + QString::fromStdString(err)); return {}; }
  set(state_, "sweep written: " + p); return p;
}
QVariantMap Measure::rta(int strip, int bpo, double averaging) {
  io_.setStrip(strip); std::vector<float> x; io_.newest(x, 32768); m::RtaOptions o; o.bandsPerOctave = std::clamp(bpo, 1, 48); o.fftSize = 32768; o.averaging = std::clamp(averaging, 0.0, 0.98);
  lastRta_ = rta_.analyse(x.data(), x.size(), io_.fs(), o); QVariantMap r; std::vector<double> db = lastRta_.db; for (double& v : db) v += spl_; r["freq"] = vl(lastRta_.freq); r["db"] = vl(db); return r;
}
double Measure::alignRta(double referenceDb) { if (lastRta_.db.empty()) return spl_; spl_ = rta_.alignTo(referenceDb); emit changed(); return spl_; }
QVariantMap Measure::waterfall(int slices) { if (ir_.h.empty()) return {}; return grid(m::waterfall(ir_, std::clamp(slices, 4, 48))); }
QVariantMap Measure::spectrogram() { if (lastRec_.size() < 4096) return {}; return grid(m::spectrogram(lastRec_.data(), lastRec_.size(), lastFs_, 2048, std::max<std::size_t>(512, lastRec_.size() / 240))); }
QVariantMap Measure::room(double L, double W, double H, double rt60, double sx, double sy, double sz, double lx, double ly, double lz) {
  m::Room r; r.L = std::clamp(L, 1.0, 60.0); r.W = std::clamp(W, 1.0, 60.0); r.H = std::clamp(H, 1.5, 20.0); r.rt60 = std::clamp(rt60, 0.1, 5.0); r.sx = std::clamp(sx, 0.0, r.L); r.sy = std::clamp(sy, 0.0, r.W); r.sz = std::clamp(sz, 0.0, r.H); r.lx = std::clamp(lx, 0.0, r.L); r.ly = std::clamp(ly, 0.0, r.W); r.lz = std::clamp(lz, 0.0, r.H);
  QVariantList modes; for (const m::Mode& md : m::roomModes(r, 300)) { if (modes.size() >= 40) break; QVariantMap o; o["freq"] = md.freq; o["kind"] = QString::fromLatin1(md.kind); o["n"] = QStringLiteral("%1-%2-%3").arg(md.nx).arg(md.ny).arg(md.nz); modes << o; }
  const m::Response rr = m::roomResponse(r); QVariantMap out; out["modes"] = modes; out["freq"] = vl(rr.freq); out["db"] = vl(rr.magDb); return out;
}
bool Measure::autoEq(const QString& target, double levelDb, double bassLiftDb, double fLo, double fHi, int maxFilters) {
  if (resp_.freq.size() < 8) { set(state_, "Measure first (sweep, stepped sine or an imported WAV): the auto EQ works on the last response."); return false; }
  m::AutoEqOptions o; o.fLo = std::clamp(fLo, 10.0, 20000.0); o.fHi = std::clamp(fHi, o.fLo * 1.5, 22000.0); o.maxFilters = std::clamp(maxFilters, 1, 10);
  double mean = 0; int n = 0; for (std::size_t i = 0; i < resp_.freq.size(); ++i) if (resp_.freq[i] >= o.fLo && resp_.freq[i] <= o.fHi) { mean += resp_.magDb[i]; ++n; } mean = n ? mean / n : 0;
  const m::Response tgt = target == "house" ? m::houseTarget(resp_, mean + levelDb, bassLiftDb) : m::flatTarget(resp_, mean + levelDb);
  eq_ = m::autoEq(resp_, tgt, o); const m::Response fixed = m::applyFilters(resp_, eq_); filters_.clear();
  for (const m::Filter& f : eq_) { QVariantMap e; e["freq"] = f.freq; e["q"] = f.q; e["gain"] = f.gainDb; filters_ << e; }
  QVariantMap c; c["freq"] = vl(resp_.freq); c["target"] = vl(tgt.magDb); c["corrected"] = vl(fixed.magDb); c["errorBefore"] = m::rmsErrorDb(resp_, tgt, o.fLo, o.fHi); c["errorAfter"] = m::rmsErrorDb(fixed, tgt, o.fLo, o.fHi); eqCurve_ = c;
  set(state_, QStringLiteral("auto EQ: %1 filters, error %2 -> %3 dB RMS (%4 - %5 Hz)").arg(eq_.size()).arg(c["errorBefore"].toDouble(), 0, 'f', 1).arg(c["errorAfter"].toDouble(), 0, 'f', 1).arg(o.fLo, 0, 'f', 0).arg(o.fHi, 0, 'f', 0)); return true;
}
QString Measure::exportFilters(const QString& path, bool json) {
  if (eq_.empty()) { set(state_, "no filters yet: run AUTO EQ"); return {}; }
  const QString p = path.isEmpty() ? QString::fromStdString(am::paths::file(json ? "autoeq-filters.json" : "autoeq-filters.txt")) : path; QFile f(p);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { set(state_, "cannot write " + p); return {}; }
  f.write(QByteArray::fromStdString(json ? m::exportFiltersJson(eq_) : m::exportFiltersText(eq_))); set(state_, "filters written: " + p); return p;
}
int Measure::applyFilters(int strip) { int n = 0; for (const m::Filter& f : eq_) { if (n >= 10) break; engine_->setEq(strip, n, 1, f.freq, f.q, f.gainDb); ++n; } set(state_, QStringLiteral("%1 filters set on the EQ of CH %2").arg(n).arg(strip + 1)); return n; }
QString Measure::saveCalibration(const QString& path) {
  if (cal_.freq.size() < 2) { set(state_, "no microphone calibration loaded to save"); return {}; }
  const QString p = path.trimmed().isEmpty() ? QString::fromStdString(am::paths::file("mic-calibration.txt")) : path.trimmed(); QFile f(p);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { set(state_, "cannot write " + p); return {}; }
  f.write(QByteArray::fromStdString(m::formatCalibration(cal_, "saved by AmF X1 ACOUSTICS"))); f.close();
  set(state_, QStringLiteral("microphone calibration saved: %1 points in %2").arg(cal_.freq.size()).arg(p)); return p;
}
bool Measure::roomCorrection(double L, double W, double H, double rt60, double sx, double sy, double sz, double lx, double ly, double lz, int maxFilters, double maxCutDb) {
  m::Room r; r.L = std::clamp(L, 1.0, 60.0); r.W = std::clamp(W, 1.0, 60.0); r.H = std::clamp(H, 1.5, 20.0); r.rt60 = std::clamp(rt60, 0.1, 5.0);
  r.sx = std::clamp(sx, 0.0, r.L); r.sy = std::clamp(sy, 0.0, r.W); r.sz = std::clamp(sz, 0.0, r.H); r.lx = std::clamp(lx, 0.0, r.L); r.ly = std::clamp(ly, 0.0, r.W); r.lz = std::clamp(lz, 0.0, r.H);
  eq_ = m::roomCorrection(r, std::clamp(maxFilters, 1, 10), std::clamp(maxCutDb, 1.0, 24.0), 300); filters_.clear();
  for (const m::Filter& f : eq_) { QVariantMap e; e["freq"] = f.freq; e["q"] = f.q; e["gain"] = f.gainDb; filters_ << e; }
  const m::Response pred = m::roomResponse(r, 48, 20, 400), fixed = m::applyFilters(pred, eq_);
  double mean = 0; int n = 0; for (std::size_t i = 0; i < pred.freq.size(); ++i) if (pred.freq[i] <= 300) { mean += pred.magDb[i]; ++n; } mean = n ? mean / n : 0;
  const m::Response tgt = m::flatTarget(pred, mean);
  QVariantMap c; c["room"] = true; c["freq"] = vl(pred.freq); c["target"] = vl(tgt.magDb); c["corrected"] = vl(fixed.magDb); c["errorBefore"] = m::rmsErrorDb(pred, tgt, 20, 300); c["errorAfter"] = m::rmsErrorDb(fixed, tgt, 20, 300); eqCurve_ = c;
  set(state_, eq_.empty() ? QStringLiteral("room correction: no peak worth cutting in this room") : QStringLiteral("room correction: %1 cuts on the modal peaks, error %2 -> %3 dB RMS (20 - 300 Hz, predicted)").arg(eq_.size()).arg(c["errorBefore"].toDouble(), 0, 'f', 1).arg(c["errorAfter"].toDouble(), 0, 'f', 1));
  return !eq_.empty();
}
bool Measure::loadCalibration(const QString& path) {
  QFile f(path); if (!f.open(QIODevice::ReadOnly) || f.size() > 4 * 1024 * 1024) { set(state_, "cannot read the calibration file"); return false; }
  m::Calibration c; if (!m::parseCalibration(f.readAll().toStdString(), c)) { set(state_, "not a calibration file (lines of: frequency dB)"); return false; }
  cal_ = c; set(state_, QStringLiteral("microphone calibration loaded: %1 points").arg(c.freq.size())); return true;
}
