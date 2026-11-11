// ACOUSTICS (extension): the measurement page's back end on am::measure. A MeasIo sits on the audio thread as both a Runtime::Tap (records
// one strip, pre-fader, and keeps a ring of its newest samples for the RTA) and an ExtraSource (plays the test signal into the master).
// Everything else (deconvolution, RT60, auto EQ ...) runs on the interface thread when the recording is complete. Offline: a recorded sweep WAV.
#pragma once
#include <QObject>
#include <QTimer>
#include <QVariant>
#include <atomic>
#include <vector>

#include "audiomixer/runtime.hpp"
#include "measure/measure.hpp"

class Engine;

class MeasIo : public am::Runtime::Tap, public am::Runtime::ExtraSource {
 public:
  static constexpr std::size_t kRing = 1 << 16;
  MeasIo() { ring_.assign(kRing, 0.f); }
  void arm(std::vector<float> signal, int strip) { active_.store(false); play_ = std::move(signal); rec_.assign(play_.size(), 0.f); pos_.store(0); recPos_.store(0); strip_.store(strip); active_.store(true); }
  void cancel() { active_.store(false); }
  bool busy() const { return active_.load(); }
  double progress() const { return play_.empty() ? 0 : static_cast<double>(recPos_.load()) / static_cast<double>(play_.size()); }
  const std::vector<float>& recorded() const { return rec_; }
  void setStrip(int s) { strip_.store(s); }
  void newest(std::vector<float>& out, std::size_t n) const { n = std::min(n, kRing); out.resize(n); const std::size_t h = head_.load(); for (std::size_t i = 0; i < n; ++i) out[i] = ring_[(h + kRing - n + i) & (kRing - 1)]; }
  double fs() const { return fs_.load(); }
  void onBlock(const float* const* strips, int nStrips, const float* L, const float*, std::size_t frames, double fs) override {
    fs_.store(fs); const int s = strip_.load(); const float* x = s >= 0 && s < nStrips ? strips[s] : L; std::size_t h = head_.load();
    for (std::size_t i = 0; i < frames; ++i) ring_[(h + i) & (kRing - 1)] = x[i];
    head_.store((h + frames) & (kRing - 1));
    if (!active_.load()) return;
    std::size_t p = recPos_.load(); const std::size_t n = std::min(frames, rec_.size() - p); for (std::size_t i = 0; i < n; ++i) rec_[p + i] = x[i]; p += n; recPos_.store(p); if (p >= rec_.size()) active_.store(false);
  }
  void render(float* L, float* R, std::size_t frames, double) override {
    std::size_t p = pos_.load(); const bool on = active_.load();
    for (std::size_t i = 0; i < frames; ++i) { const float v = on && p < play_.size() ? play_[p++] : 0.f; L[i] = v; R[i] = v; }
    pos_.store(p);
  }
 private:
  std::vector<float> play_, rec_, ring_; std::atomic<std::size_t> pos_{0}, recPos_{0}, head_{0}; std::atomic<int> strip_{0}; std::atomic<bool> active_{false}; std::atomic<double> fs_{48000};
};

class Measure : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString state READ state NOTIFY changed)        // idle | measuring | done
  Q_PROPERTY(QString message READ message NOTIFY changed)
  Q_PROPERTY(QVariantMap info READ info NOTIFY changed)       // the last recording: {peakDb, noiseDb, snrDb, clipped, input, lines: [text]}
  Q_PROPERTY(double progress READ progress NOTIFY changed)
  Q_PROPERTY(QVariantMap result READ result NOTIFY changed)   // {freq, mag, phase, thd, thdn, kind}
  Q_PROPERTY(QVariantMap decay READ decay NOTIFY changed)     // {etc, schroeder, t20, t30, topt, toptRange, delayMs, seconds}
  Q_PROPERTY(QVariantList filters READ filters NOTIFY changed)   // auto EQ: [{freq, q, gain}]
  Q_PROPERTY(QVariantMap eqCurve READ eqCurve NOTIFY changed)    // {freq, target, corrected, errorBefore, errorAfter}
  Q_PROPERTY(bool calibrated READ calibrated NOTIFY changed)
  Q_PROPERTY(double splOffset READ splOffset WRITE setSplOffset NOTIFY changed)   // dB added to every reading: dBFS -> dB SPL from an SPL meter / calibrator
 public:
  explicit Measure(Engine* engine, QObject* parent = nullptr);
  ~Measure() override;
  QString state() const { return state_; } QString message() const { return msg_; } QVariantMap info() const { return info_; } double progress() const { return io_.progress(); }
  QVariantMap result() const { return result_; } QVariantMap decay() const { return decay_; } QVariantList filters() const { return filters_; } QVariantMap eqCurve() const { return eqCurve_; }
  bool calibrated() const { return cal_.freq.size() >= 2; } double splOffset() const { return spl_; } void setSplOffset(double v) { spl_ = v; emit changed(); }
  Q_INVOKABLE bool runSweep(int strip, double seconds, double f1, double f2, double levelDb, int pointsPerOctave);
  Q_INVOKABLE bool runStepped(int strip, int pointsPerOctave, double f1, double f2, double levelDb);
  Q_INVOKABLE void cancel();
  Q_INVOKABLE bool chime();   // the boot chord on the master: a major ninth, note by note and then together, soft (-20 dBFS), 2.6 s; false when the engine is not running or a measurement is
  Q_INVOKABLE bool importSweep(const QString& wavPath, double seconds, double f1, double f2, int pointsPerOctave);   // OFFLINE: a recording of the same sweep made elsewhere
  Q_INVOKABLE QString exportSweep(const QString& wavPath, double seconds, double f1, double f2, double levelDb);     // the sweep as a WAV, to play on another system
  Q_INVOKABLE QVariantMap rta(int strip, int bandsPerOctave, double averaging);          // {freq, db} of the strip's newest samples (live)
  Q_INVOKABLE double alignRta(double referenceDb);                                       // sets splOffset so the loudest band reads referenceDb
  Q_INVOKABLE QVariantMap waterfall(int slices);                                         // {freq, times, rows} from the last impulse response
  Q_INVOKABLE QVariantMap spectrogram();                                                 // {freq, times, rows} of the last recording
  Q_INVOKABLE QVariantMap room(double L, double W, double H, double rt60, double sx, double sy, double sz, double lx, double ly, double lz);   // {modes, freq, db}
  Q_INVOKABLE bool autoEq(const QString& target, double levelDb, double bassLiftDb, double fLo, double fHi, int maxFilters);   // target: flat | house; on the last response
  Q_INVOKABLE QString exportFilters(const QString& path, bool json);                     // REW / miniDSP / Equalizer APO text, or JSON; returns the path ("" on failure)
  Q_INVOKABLE int applyFilters(int strip);                                               // the filters onto the strip's EQ bands (peaking); returns how many
  Q_INVOKABLE bool loadCalibration(const QString& path);
  Q_INVOKABLE void clearCalibration() { cal_ = am::measure::Calibration(); emit changed(); }
 signals:
  void changed();
 private:
  void set(const QString& s, const QString& m) { state_ = s; msg_ = m; emit changed(); }
  void finish();
  void analyseSweep(const std::vector<float>& rec, double fs);
  void publish(const am::measure::Response& r, const std::vector<double>& thd, const std::vector<double>& thdn, const QString& kind);
  Engine* engine_; MeasIo io_; QTimer poll_; QString state_ = "idle", msg_; QVariantMap result_, decay_, eqCurve_; QVariantList filters_;
  am::measure::Sweep sweep_; am::measure::Impulse ir_; am::measure::Response resp_; am::measure::Calibration cal_; std::vector<am::measure::Filter> eq_; am::measure::RtaAverager rta_; am::measure::Rta lastRta_;
  void begin(int strip); void end(); void describe(const std::vector<float>& rec, double fs, double leadSec, double delayMs);
  QVariantMap info_; int measStrip_ = -1; bool unmuteAfter_ = false;
  std::vector<float> lastRec_; double lastFs_ = 48000, spl_ = 0; int ppo_ = 48; bool stepped_ = false; std::vector<double> stepFreqs_; double stepSec_ = 0.25;
};
