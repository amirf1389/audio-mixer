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
void Measure::cancel() { io_.cancel(); poll_.stop(); set("idle", "cancelled"); }

bool Measure::runSweep(int strip, double seconds, double f1, double f2, double levelDb, int ppo) {
  if (!engine_->running()) { set("idle", "Start the engine on CONNECTIONS first: the sweep plays on the master and is recorded from the strip."); return false; }
  if (io_.busy()) return false;
  const double fs = engine_->sampleRate(); seconds = std::clamp(seconds, 0.5, 30.0); f1 = std::clamp(f1, 5.0, fs * 0.4); f2 = std::clamp(f2, f1 * 2, fs * 0.45); ppo_ = std::clamp(ppo, 1, 96);
  sweep_ = m::makeSweep(fs, f1, f2, seconds, std::pow(10.0, std::clamp(levelDb, -60.0, 0.0) / 20)); stepped_ = false; lastFs_ = fs;
  io_.arm(sweep_.signal, strip); poll_.start(); set("measuring", QStringLiteral("log sweep %1 - %2 Hz, %3 s, on the master; recording CH %4").arg(f1, 0, 'f', 0).arg(f2, 0, 'f', 0).arg(seconds, 0, 'f', 1).arg(strip + 1)); return true;
}
bool Measure::runStepped(int strip, int ppo, double f1, double f2, double levelDb) {
  if (!engine_->running()) { set("idle", "Start the engine on CONNECTIONS first."); return false; }
  if (io_.busy()) return false;
  const double fs = engine_->sampleRate(); ppo_ = std::clamp(ppo, 1, 96); f1 = std::clamp(f1, 10.0, fs * 0.4); f2 = std::clamp(f2, f1, fs * 0.45); stepFreqs_.clear();
  for (double f = f1; f <= f2 * 1.0000001 && stepFreqs_.size() < 480; f *= std::pow(2.0, 1.0 / ppo_)) stepFreqs_.push_back(f);   // 480 tones of 0.25 s = 2 minutes at most
  const double amp = std::pow(10.0, std::clamp(levelDb, -60.0, 0.0) / 20); std::vector<float> sig; sig.reserve(static_cast<std::size_t>(fs * stepSec_ * static_cast<double>(stepFreqs_.size() + 1)));
  for (double f : stepFreqs_) { std::vector<float> t = m::tone(fs, f, stepSec_, amp); const std::size_t fade = static_cast<std::size_t>(fs * 0.005); for (std::size_t i = 0; i < fade && i < t.size(); ++i) { const float g = static_cast<float>(i) / static_cast<float>(fade); t[i] *= g; t[t.size() - 1 - i] *= g; } sig.insert(sig.end(), t.begin(), t.end()); }
  sig.resize(sig.size() + static_cast<std::size_t>(fs * 0.3), 0.f); stepped_ = true; lastFs_ = fs;
  io_.arm(std::move(sig), strip); poll_.start(); set("measuring", QStringLiteral("stepped sine, %1 tones (%2 per octave), recording CH %3").arg(stepFreqs_.size()).arg(ppo_).arg(strip + 1)); return true;
}
void Measure::finish() {
  lastRec_ = io_.recorded();
  if (!stepped_) { analyseSweep(lastRec_, lastFs_); return; }
  // each tone's own quarter second, read from where the loudest tone says the path delay is (up to 100 ms)
  const std::size_t seg = static_cast<std::size_t>(lastFs_ * stepSec_); m::Response r; std::vector<double> thd, thdn;
  for (std::size_t k = 0; k < stepFreqs_.size(); ++k) { const std::size_t from = k * seg + seg / 4; if (from + seg / 2 > lastRec_.size()) break; std::vector<float> x(lastRec_.begin() + static_cast<long>(from), lastRec_.begin() + static_cast<long>(from + seg / 2 + seg / 8));
    const m::Stepped s = m::analyseTone(x, lastFs_, stepFreqs_[k]); r.freq.push_back(stepFreqs_[k]); r.magDb.push_back(s.amplitude > 1e-9 ? 20 * std::log10(s.amplitude) : -180); r.phaseDeg.push_back(0); thd.push_back(s.thdPercent); thdn.push_back(s.thdnPercent); }
  resp_ = r; publish(r, thd, thdn, "stepped"); decay_.clear(); set("done", QStringLiteral("stepped sine: %1 points").arg(r.freq.size()));
}
void Measure::analyseSweep(const std::vector<float>& rec, double fs) {
  double pk = 0; for (float v : rec) pk = std::max(pk, std::fabs(static_cast<double>(v)));
  if (pk < 1e-6) { result_.clear(); decay_.clear(); set("done", "The recording is silent: route an input to that strip (ROUTING) and connect the microphone or the loopback cable."); return; }
  ir_ = m::deconvolve(rec, sweep_); ir_.fs = fs; resp_ = m::response(ir_, ppo_, sweep_.f1, sweep_.f2); const std::vector<double> thd = m::thdPercent(ir_, resp_.freq);
  publish(resp_, thd, {}, "sweep");
  const m::Decay d = m::decay(ir_, 2.0); QVariantMap o; const std::size_t step = std::max<std::size_t>(1, d.etcDb.size() / 600); QVariantList etc, sch;
  for (std::size_t i = 0; i < d.etcDb.size(); i += step) { double mx = -240; for (std::size_t j = i; j < std::min(d.etcDb.size(), i + step); ++j) mx = std::max(mx, d.etcDb[j]); etc << mx; sch << d.schroederDb[i]; }
  const std::size_t base = static_cast<std::size_t>(sweep_.leadSec * fs) + sweep_.inverse.size() - 1;
  o["etc"] = etc; o["schroeder"] = sch; o["t20"] = d.t20; o["t30"] = d.t30; o["topt"] = d.topt; o["toptRange"] = d.toptRange; o["seconds"] = d.seconds; o["delayMs"] = ir_.peak >= base ? static_cast<double>(ir_.peak - base) / fs * 1000 : 0.0; decay_ = o;
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
bool Measure::loadCalibration(const QString& path) {
  QFile f(path); if (!f.open(QIODevice::ReadOnly) || f.size() > 4 * 1024 * 1024) { set(state_, "cannot read the calibration file"); return false; }
  m::Calibration c; if (!m::parseCalibration(f.readAll().toStdString(), c)) { set(state_, "not a calibration file (lines of: frequency dB)"); return false; }
  cal_ = c; set(state_, QStringLiteral("microphone calibration loaded: %1 points").arg(c.freq.size())); return true;
}
