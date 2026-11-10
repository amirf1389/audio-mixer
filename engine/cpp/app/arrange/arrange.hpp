// ARRANGE: a DAW-style arrangement without recording, ported from the web console (index.html @ html-app-final, window.arrange).
//   Audio files (WAV) are placed as clips on tracks along a timeline (bars / beats at a tempo, or seconds). Each track has gain, pan, mute,
//   solo and a volume automation lane (points in dB, linear between). Markers mark positions. The transport plays the arrangement through the
//   mixer's master chain (Runtime::ExtraSource: summed before master gain / limiter / meters). EXPORT renders the whole mix offline to a
//   24-bit WAV. Nothing is recorded here: files come in by path, the microphone is never captured.
//   The project (tracks, clips, automation, markers, tempo) is JSON; clips reference files by path and are re-loaded next time (a clip whose
//   file cannot be read is MISSING and silent). Pure functions over the model, so everything is testable without a device.
#pragma once
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "audiomixer/runtime.hpp"
#include "daemon/json.hpp"

namespace am::arr {

// ---- audio files
struct Audio { double rate = 48000; std::vector<float> L, R; double duration() const { return rate > 0 ? static_cast<double>(L.size()) / rate : 0; } };
bool readWav(const std::string& path, Audio& out, std::string& err);                 // PCM 8 / 16 / 24 / 32, float 32 / 64, any channel count (mono duplicated, > 2 takes the first two)
bool writeWav24(const std::string& path, const std::vector<float>& L, const std::vector<float>& R, double rate, std::string& err);
std::vector<float> resampleLinear(const std::vector<float>& x, double from, double to);

// ---- the model
struct AutoPoint { double t = 0, v = 0; };                                            // seconds, dB (-60 .. +6)
struct Track { int id = 0; std::string name; double db = 0, pan = 0; bool mute = false, solo = false; int color = 0; std::vector<AutoPoint> autoPts; };
struct Clip { int id = 0, track = 0; std::string file; double start = 0, offset = 0, duration = 0, db = 0; };   // audible: [start, start + duration - offset)
struct Marker { double t = 0; std::string name; };
struct Project { double bpm = 120; int snap = 4; bool loop = false; double zoom = 40; std::vector<Track> tracks; std::vector<Clip> clips; std::vector<Marker> markers; };

double clampd(double v, double a, double b);
double beatSec(double bpm);
double snapTime(double t, double bpm, int div);                                        // div 0 none, 1 bar, 4 beat, 8, 16
int barOf(double t, double bpm);
double autoAt(const std::vector<AutoPoint>& pts, double t);
std::vector<AutoPoint> addPoint(std::vector<AutoPoint> pts, double t, double v);
double dbToGain(double db);
double trackGain(const Track& t, bool anySolo);
void panGains(double pan, double& l, double& r);
double clipEnd(const Clip& c);
bool placeClip(Project& p, int clipId, double start);                                  // false when there is no room: left where it was
bool splitClip(Project& p, int clipId, double t, int newId);
double projectEnd(const Project& p);
bool anySolo(const Project& p);
int nextId(const Project& p);

json::Value toJson(const Project& p);
Project fromJson(const json::Value& v);                                                // field by field, clamped (the file is not trusted)

// ---- rendering
using Buffers = std::map<std::string, std::shared_ptr<const Audio>>;                   // file path -> decoded audio
// renders [from, to) seconds of the project at `rate` into L / R (appended); automation per sample, clips summed, solo / mute applied
void render(const Project& p, const Buffers& b, double from, double to, double rate, std::vector<float>& L, std::vector<float>& R);
bool exportWav(const Project& p, const Buffers& b, double rate, const std::string& path, std::string& err, double* seconds = nullptr);

// the realtime player: an ExtraSource for the Runtime. The UI thread swaps a project snapshot; the audio thread renders from its position.
class Player : public Runtime::ExtraSource {
 public:
  void setProject(const Project& p, const Buffers& b);       // a copy for the audio thread (swapped atomically)
  void play(double fromSeconds);
  void stop();
  void seek(double seconds);
  bool playing() const { return playing_.load(); }
  double position() const { return pos_.load(); }           // seconds
  void setLoop(bool on) { loop_.store(on); }
  void render(float* L, float* R, std::size_t frames, double sampleRate) override;
 private:
  struct Snap { Project p; Buffers b; double end = 0; };
  std::shared_ptr<const Snap> snap_; std::mutex m_;           // m_ guards the swap on the UI side; the audio thread copies the shared_ptr under the same mutex (short, no allocation)
  std::atomic<bool> playing_{false}, loop_{false}; std::atomic<double> pos_{0};
  std::vector<float> tl_, tr_;
};

}  // namespace am::arr
