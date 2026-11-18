// 1000 CUSTOM SAMPLE PADS: what each pad plays, its name, gain and colour, kept in ONE file (<data>/pads.json) that the console and the
// remote service both read and write, so the Windows console, the phone and the web remote show and play the same pads. A pad plays
//   "sample:N"  one of the 100 built-in samples (am::samples, N 1..100)
//   "sfx:N"     one of the 1000 sound effects (am::sfx, N 1..1000)
//   "file:PATH" an audio file on the computer (WAV, FLAC, AIFF; MP3 / M4A ... where FFmpeg is present), the first 30 s, at the device's rate
//   ""          nothing (an empty pad)
// Out of the box pads 1-100 play the built-in samples and pads 101-1000 the first 900 sound effects; the file stores only what the user
// changed. Another program saving the file is seen by reloadIfChanged() (size + time), so both sides stay in step without a socket.
#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "daemon/json.hpp"

namespace am::pads {

constexpr int kPads = 1000, kColors = 8;
constexpr double kMaxFileSeconds = 30.0;

struct Pad { std::string source, name; double gainDb = 0; int color = 0; bool operator==(const Pad& o) const { return source == o.source && name == o.name && gainDb == o.gainDb && color == o.color; } };

Pad defaultPad(int n);                                  // n 1..1000
bool validSource(const std::string& source, bool allowFiles);
std::string sourceName(const std::string& source);      // "Kick Deep", "Laser 17", the file's name without extension, "" for empty
/** the sound of a source at [rate]: rendered (sample / sfx) or decoded, cut to kMaxFileSeconds and resampled (file) */
bool loadClip(const std::string& source, double rate, std::vector<float>& L, std::vector<float>& R, std::string& err);

class Bank {
 public:
  explicit Bank(std::string path);                     // "" keeps the pads in memory only (tests)
  Pad at(int n) const;                                  // n 1..1000 (out of range: an empty pad)
  /** change one pad; name "" takes the source's own name; gain clamped -40..+12 dB, colour 0..7. allowFiles false: "file:" refused (remotes) */
  bool set(int n, Pad p, bool allowFiles, std::string& err);
  bool resetPad(int n);                                 // back to its default
  void resetAll();
  int version() const;                                  // goes up with every change (and with a reload that changed something)
  json::Value toJson(int from = 1, int to = kPads) const;   // {version, count, pads:[{n, source, name, gainDb, color, custom}]}
  bool reloadIfChanged();                               // true when the file was changed by another program and read again
  bool save(std::string& err);
  const std::string& path() const { return path_; }
 private:
  bool readFile();
  mutable std::mutex m_; std::string path_; std::vector<Pad> pads_; int version_ = 1; std::uintmax_t size_ = 0; long long mtime_ = 0;
};

}  // namespace am::pads
