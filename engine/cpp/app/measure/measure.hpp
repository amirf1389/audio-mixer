// ACOUSTICS: the measurement engine behind the console's ACOUSTICS extension (and the SDK). Pure C++, no device code: it makes the
// stimulus, takes what was recorded, and computes. Everything in double precision, nothing allocated on an audio thread (the console runs
// it on the UI thread after the capture).
//   Sweep        exponential (logarithmic) sine sweep + its inverse filter; deconvolution to the impulse response (Farina); the harmonic
//                impulse responses sit before the linear one at T ln(k) / ln(f2/f1), which gives THD per frequency from one sweep
//   Stepped sine one frequency at a time: amplitude, THD (harmonics 2..5 by Goertzel), THD+N, and SMPTE IMD (60 Hz + 7 kHz, 4:1)
//   Response     magnitude and phase from the impulse response, smoothed to 1/N octave (N up to 96 points per octave); mic calibration
//                files (frequency dB pairs) corrected in; an SPL offset turns dBFS into dB SPL
//   RTA          1/N-octave bands (N up to 48) from a long FFT with RMS averaging, and level alignment to a reference
//   Time domain  ETC (energy-time curve), Schroeder decay, RT60 as T20 / T30 / Topt (the best-fitting range), delay by cross-correlation
//                against the stimulus or a loopback / acoustic reference
//   Waterfall    rising-window slices of the impulse response (3D data), and a spectrogram (STFT) of any signal
//   Room         the modes of a rectangular room (axial / tangential / oblique up to 400 Hz) and the predicted bass response at a
//                listener for a speaker position, from the room's dimensions and a decay time
//   Auto EQ      parametric filters (peaking, 20..400 Hz by default) that pull the smoothed response to a target curve; exported as a
//                REW / miniDSP / Equalizer APO filter list and as JSON
//   Offline      the same analysis from a recorded sweep in a WAV (the console reads the file; this takes the samples)
#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace am::measure {

struct Sweep { double fs = 48000, f1 = 20, f2 = 20000, seconds = 5; std::vector<float> signal; std::vector<double> inverse; double leadSec = 0.2, tailSec = 1.0; };
Sweep makeSweep(double fs, double f1, double f2, double seconds, double amplitude = 0.5);   // signal: lead silence + sweep + tail silence
struct Impulse { double fs = 48000; std::vector<double> h; std::vector<double> pre; std::size_t peak = 0; std::vector<std::vector<double>> harmonics; std::vector<std::size_t> harmonicAt; };   // h: linear impulse response from the peak on; pre: the samples just before the peak (the band-limited pulse's pre-ringing, used by response()); harmonics[k-2]: the k-th harmonic's response
Impulse deconvolve(const std::vector<float>& recorded, const Sweep& sweep, int harmonics = 5);

struct Response { std::vector<double> freq, magDb, phaseDeg; };   // log-spaced points, pointsPerOctave each octave from fLo to fHi
Response response(const Impulse& ir, int pointsPerOctave = 48, double fLo = 20, double fHi = 20000, double windowSec = 0.5);
std::vector<double> thdPercent(const Impulse& ir, const std::vector<double>& freq);   // THD at each frequency from the harmonic responses (0 where no harmonic data)
struct Calibration { std::vector<double> freq, db; };
bool parseCalibration(const std::string& text, Calibration& out);                         // "freq dB" per line (comments #, *, ;), as mic makers ship them
void applyCalibration(Response& r, const Calibration& c);                                 // subtracts the mic's deviation
std::string formatCalibration(const Calibration& c, const std::string& note = "");            // the same "freq dB" text back (a header line with # first)
void applySplOffset(Response& r, double offsetDb);                                        // dBFS -> dB SPL: offset = SPL of a known reference - its dBFS

struct Stepped { double freq = 1000, amplitude = 0, thdPercent = 0, thdnPercent = 0, imdPercent = 0; };
std::vector<float> tone(double fs, double freq, double seconds, double amplitude = 0.5);
std::vector<float> smpteTones(double fs, double seconds, double amplitude = 0.5);         // 60 Hz + 7 kHz, 4:1
Stepped analyseTone(const std::vector<float>& recorded, double fs, double freq);          // amplitude, THD, THD+N of one tone
double analyseSmpte(const std::vector<float>& recorded, double fs);                       // SMPTE IMD in %

struct RtaOptions { int bandsPerOctave = 48; double fLo = 20, fHi = 20000; std::size_t fftSize = 65536; double averaging = 0.0; };   // averaging 0 = none, 0.9 = slow RMS
struct Rta { std::vector<double> freq, db; };
class RtaAverager { public: Rta analyse(const float* x, std::size_t n, double fs, const RtaOptions& o); void reset() { acc_.clear(); } double alignTo(double referenceDb) const;   // the offset that puts the loudest band at the reference
 private: std::vector<double> acc_; };

struct Decay { std::vector<double> etcDb, schroederDb; double t20 = 0, t30 = 0, topt = 0, toptRange = 0; double seconds = 0; };
Decay decay(const Impulse& ir, double seconds = 2.0);                                     // ETC (dB), Schroeder (dB), RT60 estimates (0 when the decay is too short / noisy)
double delaySeconds(const std::vector<float>& recorded, const std::vector<float>& reference, double fs, double maxSeconds = 1.0);   // cross-correlation lag of recorded against reference (seconds, >= 0)

struct Grid { std::vector<double> x, y; std::vector<std::vector<double>> db; };          // y rows of x columns (waterfall: x = frequency, y = time)
Grid waterfall(const Impulse& ir, int slices = 24, double windowSec = 0.3, double stepSec = 0.02, int pointsPerOctave = 24, double fLo = 20, double fHi = 2000);
Grid spectrogram(const float* x, std::size_t n, double fs, std::size_t fftSize = 2048, std::size_t hop = 512, int pointsPerOctave = 24, double fLo = 20, double fHi = 20000);

struct Room { double L = 5, W = 4, H = 2.7; double rt60 = 0.5; double sx = 1, sy = 1, sz = 1.2; double lx = 2.5, ly = 3, lz = 1.2; };   // metres; speaker (s) and listener (l)
struct Mode { int nx, ny, nz; double freq; const char* kind; };
std::vector<Mode> roomModes(const Room& r, double fMax = 400, double c = 343);
Response roomResponse(const Room& r, int pointsPerOctave = 24, double fLo = 20, double fHi = 400, double c = 343);   // predicted bass response at the listener (0 dB = mean)

struct Filter { double freq, q, gainDb; };
struct AutoEqOptions { int maxFilters = 10; double fLo = 20, fHi = 400; double maxBoostDb = 6, maxCutDb = 15, minDb = 1.0; double qMin = 0.7, qMax = 10; };
std::vector<Filter> autoEq(const Response& measured, const Response& target, const AutoEqOptions& o);   // target: same freq grid (use flatTarget / houseTarget)
/** ROOM CORRECTION from the room's prediction (roomResponse): CUTS only, on the modal peaks (a null is a cancellation that no EQ can fill, so
 *  nothing is boosted), each no narrower than the mode's own bandwidth (about 2.2 / RT60 Hz), at most maxCutDb, below fHi (the modal region) */
std::vector<Filter> roomCorrection(const Room& r, int maxFilters = 6, double maxCutDb = 12, double fHi = 300);
Response flatTarget(const Response& like, double levelDb);
Response houseTarget(const Response& like, double levelDb, double bassLiftDb = 4, double kneeHz = 120);   // a gentle bass lift below the knee
Response applyFilters(const Response& r, const std::vector<Filter>& f);
double rmsErrorDb(const Response& a, const Response& b, double fLo, double fHi);
std::string exportFiltersText(const std::vector<Filter>& f);                                // "Filter 1: ON PK Fc 63.0 Hz Gain -4.0 dB Q 4.00": REW / miniDSP / Equalizer APO
std::string exportFiltersJson(const std::vector<Filter>& f);
double peakingDbAt(const Filter& f, double freq, double fs = 48000);                       // the filter's own response, for drawing

/** AI EQ AUTO of the control room (DUAL LISTEN): the monitor's octave bands (dBFS, band k at 31.25 * 2^k Hz) to at most maxFilters gentle
 *  peaking filters (Auto EQ above, on a 12-points-per-octave grid, against a flat octave-band target = a pink balance at the bands' mean).
 *  Bands below -90 dBFS are left out; nothing to hear = no filters. */
std::vector<Filter> monitorAutoEq(const double* bandDb, int bands, int maxFilters = 8, double maxBoostDb = 4, double maxCutDb = 8);
/** The monitor's EQ mode and what DUAL LISTEN captures: engage() keeps the mode and filters there were and runs AI EQ AUTO; release() puts
 *  them back exactly. reset() clears the AI EQ's filters (it learns again). */
struct MonitorEqMode {
  enum Mode { Off = 0, Manual = 1, AiAuto = 2 };
  Mode mode = Off; std::vector<Filter> filters;
  bool captured = false; Mode savedMode = Off; std::vector<Filter> savedFilters;
  void engage() { if (!captured) { savedMode = mode; savedFilters = filters; captured = true; } mode = AiAuto; filters.clear(); }
  void release() { if (captured) { mode = savedMode; filters = savedFilters; captured = false; savedFilters.clear(); } }
  void reset() { if (mode == AiAuto) filters.clear(); }
  void learn(const double* bandDb, int bands, int maxFilters = 8) { if (mode == AiAuto) filters = monitorAutoEq(bandDb, bands, maxFilters); }
  bool eqOn() const { return mode != Off && !filters.empty(); }
};

}  // namespace am::measure
