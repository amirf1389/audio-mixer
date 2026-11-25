// AUTO DCA: a transparent, deterministic channel classifier (fixed rules and thresholds; no network, no model files).
//
// ChannelFeatures listens to one strip's input for a few seconds (add() on the audio thread: two biquads, a few adds a sample, fixed
// storage) and summary() turns it into the numbers below. classifyChannel() scores them against fixed thresholds:
//
//   rmsDb / peakDb / crestDb   level over the window, and peak minus RMS (percussive sources have a high crest)
//   activity                   share of 20 ms frames above -50 dBFS
//   voiceRatio                 energy 300 .. 3400 Hz over all energy (telephone band: where speech lives)
//   lowRatio / highRatio       energy below 300 Hz / above 3400 Hz over all energy
//   centroidHz                 band-energy centroid: the three bands weighted at 150 / 1500 / 7000 Hz
//   zcr                        zero crossings per sample
//   pauseFrac                  share of active-window frames 20 dB or more under the loud frames (the 90th percentile): speech has gaps
//                              between words and syllables, a song keeps going
//   stereoCorr                 correlation with the linked partner strip (a stereo pair that is not mono): music is usually stereo
//
//   SILENT      activity < 2 % or RMS < -60 dBFS: nothing was played (flagged "not needed")
//   VOICE       +2 voiceRatio >= 0.70 (+1 >= 0.50), with voiceRatio >= 0.50: +2 pauseFrac >= 0.15 (+1 >= 0.06), +1 centroid 400 .. 2500 Hz, +1 zcr 0.02 .. 0.20,
//               +1 a microphone input, +2 a voice name (MIC VOX VOICE HOST TALK SPEECH MC PASTOR...), -2 a true stereo pair (corr < 0.9)
//   MUSIC       +2 voiceRatio < 0.50 (+1 < 0.70 with highRatio >= 0.10), +2 pauseFrac < 0.06, +1 highRatio >= 0.12, +1 a true stereo
//               pair, +1 not a microphone (computer / app sound), +2 a music name (MUSIC BGM TRACK SONG DECK PLAYER SPOTIFY...)
//   INSTRUMENT  +1 base, +3 an instrument name (GTR GUITAR BASS KEYS PIANO DRUM KICK SNARE...), +1 lowRatio >= 0.50, +1 crest >= 18 dB,
//               +1 a microphone / line input that is not speech-band (voiceRatio < 0.50)
//   The highest score wins; a tie goes VOICE, then MUSIC, then INSTRUMENT. The reasons are written out so the console can show them.
#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace am {

enum class ChannelClass { Silent = 0, Voice = 1, Music = 2, Instrument = 3 };
inline const char* channelClassName(ChannelClass c) { switch (c) { case ChannelClass::Voice: return "VOICE"; case ChannelClass::Music: return "MUSIC"; case ChannelClass::Instrument: return "INSTRUMENT"; default: return "SILENT"; } }

struct FeatureSummary {
  double seconds = 0, rmsDb = -120, peakDb = -120, crestDb = 0, activity = 0, voiceRatio = 0, lowRatio = 0, highRatio = 0, centroidHz = 0, zcr = 0, pauseFrac = 0;
  double stereoCorr = 2;   // 2 = no partner measured
};

class ChannelFeatures {
 public:
  void reset(double fs) {
    fs_ = fs > 0 ? fs : 48000; n_ = 0; e_ = eLow_ = eVoice_ = eHigh_ = 0; pk_ = 0; zc_ = 0; last_ = 0; fE_ = 0; fN_ = 0; frames_ = 0; active_ = 0; xy_ = xx_ = yy_ = 0;
    for (auto& h : hist_) h = 0;
    design(hp300_, 300, true); design(lp3400_, 3400, false); design(lp300_, 300, false); design(hp3400_, 3400, true);
    frameLen_ = static_cast<long>(0.020 * fs_);
  }
  // mono input of one strip; partner (may be null): the linked strip's input of the same block, for the stereo correlation
  void add(const float* x, std::size_t n, const float* partner = nullptr) {
    for (std::size_t i = 0; i < n; ++i) {
      const double v = x ? static_cast<double>(x[i]) : 0.0; const double a = std::fabs(v);
      e_ += v * v; if (a > pk_) pk_ = a; if ((v >= 0) != (last_ >= 0) && a > 1e-5) ++zc_; if (a > 1e-5) last_ = v;
      const double vb = run(lp3400_, run(hp300_, v)), lo = run(lp300_, v), hi = run(hp3400_, v);
      eVoice_ += vb * vb; eLow_ += lo * lo; eHigh_ += hi * hi;
      if (partner) { const double y = partner[i]; xy_ += v * y; xx_ += v * v; yy_ += y * y; }
      fE_ += v * v; if (++fN_ >= frameLen_) { const double db = fE_ > 0 ? 10 * std::log10(fE_ / static_cast<double>(fN_)) : -120; ++frames_; if (db > -50) ++active_;
        hist_[std::clamp(static_cast<int>(db + 100.0), 0, kBins - 1)]++; fE_ = 0; fN_ = 0; }
    }
    n_ += static_cast<long long>(n);
  }
  FeatureSummary summary() const {
    FeatureSummary s; if (n_ <= 0) return s;
    s.seconds = static_cast<double>(n_) / fs_;
    const double ms = e_ / static_cast<double>(n_); s.rmsDb = ms > 1e-12 ? 10 * std::log10(ms) : -120; s.peakDb = pk_ > 1e-6 ? 20 * std::log10(pk_) : -120; s.crestDb = s.peakDb - s.rmsDb;
    s.activity = frames_ ? static_cast<double>(active_) / static_cast<double>(frames_) : 0;
    const double eb = eLow_ + eVoice_ + eHigh_; if (eb > 1e-12) { s.voiceRatio = eVoice_ / eb; s.lowRatio = eLow_ / eb; s.highRatio = eHigh_ / eb; s.centroidHz = (eLow_ * 150 + eVoice_ * 1500 + eHigh_ * 7000) / eb; }
    s.zcr = static_cast<double>(zc_) / static_cast<double>(n_);
    long tot = 0; for (int b = 0; b < kBins; ++b) tot += hist_[b];
    if (tot > 0) {   // the loud frames: the 90th percentile; pauses: the frames of the window 20 dB or more under it
      long acc = 0; int p90 = kBins - 1; for (int b = 0; b < kBins; ++b) { acc += hist_[b]; if (acc >= tot * 9 / 10) { p90 = b; break; } }
      long dips = 0; for (int b = 0; b <= p90 - 20 && b < kBins; ++b) dips += hist_[b];
      s.pauseFrac = static_cast<double>(dips) / static_cast<double>(tot);
    }
    if (xx_ > 1e-9 && yy_ > 1e-9) s.stereoCorr = xy_ / std::sqrt(xx_ * yy_);
    return s;
  }
 private:
  struct Bq { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; };
  void design(Bq& q, double f, bool high) {   // RBJ 2nd order, Q 0.707
    const double w = 2 * 3.14159265358979 * std::min(f, fs_ * 0.45) / fs_, c = std::cos(w), al = std::sin(w) / (2 * 0.70710678), a0 = 1 + al;
    q = Bq{}; if (high) { q.b0 = (1 + c) / 2 / a0; q.b1 = -(1 + c) / a0; } else { q.b0 = (1 - c) / 2 / a0; q.b1 = (1 - c) / a0; } q.b2 = q.b0; q.a1 = -2 * c / a0; q.a2 = (1 - al) / a0;
  }
  static double run(Bq& q, double x) { const double y = q.b0 * x + q.z1; q.z1 = q.b1 * x - q.a1 * y + q.z2; q.z2 = q.b2 * x - q.a2 * y; return y; }
  static constexpr int kBins = 100;   // frame levels -100 .. 0 dBFS in 1 dB bins
  double fs_ = 48000, e_ = 0, eLow_ = 0, eVoice_ = 0, eHigh_ = 0, pk_ = 0, last_ = 0, fE_ = 0, xy_ = 0, xx_ = 0, yy_ = 0; long long n_ = 0, zc_ = 0; long fN_ = 0, frameLen_ = 960, frames_ = 0, active_ = 0;
  long hist_[kBins] = {}; Bq hp300_, lp3400_, lp300_, hp3400_;
};

struct ChannelHints { bool microphone = false; bool linked = false; std::string name; };
struct ChannelVerdict { ChannelClass cls = ChannelClass::Silent; int voice = 0, music = 0, instrument = 0; std::string why; };

inline bool nameHas(const std::string& name, const char* const* words) {
  std::string u; for (char c : name) u += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  for (const char* const* w = words; *w; ++w) { const std::string k = *w; std::size_t p = u.find(k);
    while (p != std::string::npos) { const bool l = p == 0 || !std::isalpha(static_cast<unsigned char>(u[p - 1])), r = p + k.size() >= u.size() || !std::isalpha(static_cast<unsigned char>(u[p + k.size()]));
      if (l && (r || k.size() >= 4)) return true;
      p = u.find(k, p + 1); } }
  return false;
}

inline ChannelVerdict classifyChannel(const FeatureSummary& f, const ChannelHints& h) {
  static const char* const kVoice[] = {"MIC", "VOX", "VOCAL", "VOICE", "HOST", "TALK", "SPEECH", "MC", "PASTOR", "SPEAKER", "LAPEL", "HEADSET", "GUEST", "PRESENTER", "ANNOUNCER", "CALL", nullptr};
  static const char* const kMusic[] = {"MUSIC", "BGM", "TRACK", "SONG", "DECK", "PLAYER", "SPOTIFY", "YOUTUBE", "PLAYBACK", "BACKING", "MEDIA", "JINGLE", "PC", nullptr};
  static const char* const kInstr[] = {"GTR", "GUITAR", "BASS", "KEYS", "PIANO", "DRUM", "KICK", "SNARE", "HAT", "TOM", "OH", "SYNTH", "ORGAN", "VIOLIN", "SAX", "TRUMPET", "BRASS", "STRINGS", "PERC", "CAJON", "UKULELE", nullptr};
  ChannelVerdict v; char b[200];
  std::snprintf(b, sizeof b, "rms %.0f dB, active %.0f%%, voice band %.0f%%, low %.0f%%, high %.0f%%, centroid %.0f Hz, zcr %.3f, pauses %.0f%%, crest %.0f dB",
                f.rmsDb, f.activity * 100, f.voiceRatio * 100, f.lowRatio * 100, f.highRatio * 100, f.centroidHz, f.zcr, f.pauseFrac * 100, f.crestDb);
  v.why = b;
  if (f.activity < 0.02 || f.rmsDb < -60) { v.cls = ChannelClass::Silent; v.why = "no signal (" + v.why + ")"; return v; }
  const bool trueStereo = h.linked && f.stereoCorr <= 1.0 && f.stereoCorr < 0.9;
  int vo = 0, mu = 0, in = 1;
  vo += f.voiceRatio >= 0.70 ? 2 : f.voiceRatio >= 0.50 ? 1 : 0; if (f.voiceRatio >= 0.50) vo += f.pauseFrac >= 0.15 ? 2 : f.pauseFrac >= 0.06 ? 1 : 0;
  vo += f.centroidHz >= 400 && f.centroidHz <= 2500 ? 1 : 0; vo += f.zcr >= 0.02 && f.zcr <= 0.20 ? 1 : 0; vo += h.microphone ? 1 : 0; vo += nameHas(h.name, kVoice) ? 2 : 0; vo -= trueStereo ? 2 : 0;
  mu += f.voiceRatio < 0.50 ? 2 : (f.voiceRatio < 0.70 && f.highRatio >= 0.10 ? 1 : 0); mu += f.pauseFrac < 0.06 ? 2 : 0; mu += f.highRatio >= 0.12 ? 1 : 0;
  mu += trueStereo ? 1 : 0; mu += h.microphone ? 0 : 1; mu += nameHas(h.name, kMusic) ? 2 : 0;
  in += nameHas(h.name, kInstr) ? 3 : 0; in += f.lowRatio >= 0.50 ? 1 : 0; in += f.crestDb >= 18 ? 1 : 0; in += h.microphone && f.voiceRatio < 0.50 ? 1 : 0;
  v.voice = vo; v.music = mu; v.instrument = in;
  v.cls = vo >= mu && vo >= in ? ChannelClass::Voice : mu >= in ? ChannelClass::Music : ChannelClass::Instrument;
  std::snprintf(b, sizeof b, "voice %d, music %d, instrument %d: ", vo, mu, in); v.why = b + v.why;
  return v;
}

}  // namespace am
