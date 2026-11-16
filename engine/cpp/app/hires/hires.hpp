// HIGH-RESOLUTION AUDIO: the engine's hi-res toolkit, on four open-source libraries (licences in licenses/THIRD-PARTY-SOURCES.md):
//   r8brain-free-src 6.5 (MIT)       sample-rate conversion, 64-bit internal, up to 768 kHz and beyond; built in
//   libsoxr 0.1.3 (LGPL 2.1)         the SoX resampler; a separate library (amix_soxr.dll / libamix_soxr.so) loaded at run time,
//                                    so it can be replaced by the user (LGPL); without it the r8brain path is used
//   dr_wav 0.14.6 / dr_flac 0.13.4   reading WAV (PCM 8-64 bit, float 32/64, RF64 and Wave64 for files over 4 GB) and FLAC
//   (public domain / MIT-0)
//   libFLAC 1.5.0 (BSD, Xiph)        writing FLAC: 16, 24 and 32-bit, any rate FLAC allows (1 Hz .. 1,048,575 Hz)
// Audio is passed as double, interleaved, -1 .. +1 full scale: a 32-bit integer source keeps every bit (a float keeps 24).
// Writing to an integer word length goes through dither (none, TPDF, or TPDF with 5-tap noise shaping).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace am::hr {

struct AudioInfo { double rate = 0; int channels = 0; std::int64_t frames = 0; int bits = 0; bool isFloat = false; std::string container; };   // container: "wav", "rf64", "w64", "flac"
struct Audio { AudioInfo info; std::vector<double> samples; };   // interleaved

// ---- what this build has
struct LibInfo { std::string id, name, version, licence; bool available = false; std::string note; };
std::vector<LibInfo> libraries();
std::string infoJson();

// ---- sample-rate conversion. engine: "auto" (soxr when its library is present, else r8brain), "r8brain", "soxr".
// quality: 0 = 16-bit grade (136 dB stop band), 1 = 24-bit grade (180 dB), 2 = mastering (r8brain 207 dB / soxr very high quality)
class Resampler {
 public:
  static std::unique_ptr<Resampler> make(double srcRate, double dstRate, int channels, const std::string& engine, int quality, std::string& err);
  virtual ~Resampler() = default;
  // streaming: in[c][0..frames), out[c] receives what is ready (appended); flush() pushes the tail out at the end of the stream
  virtual void process(const double* const* in, int frames, std::vector<std::vector<double>>& out) = 0;
  virtual void flush(std::vector<std::vector<double>>& out) = 0;
  virtual const char* engine() const = 0;
  int channels() const { return ch_; } double ratio() const { return dst_ / src_; }
 protected:
  double src_ = 0, dst_ = 0; int ch_ = 0;
};
// a whole buffer at once, the output exactly round(frames * dst / src) frames long and time aligned (the filter delay removed)
bool resample(const std::vector<double>& interleaved, int channels, double srcRate, double dstRate, const std::string& engine, int quality, std::vector<double>& out, std::string& err, std::string* used = nullptr);
std::vector<float> resampleMono(const std::vector<float>& x, double from, double to, int quality = 1);   // for the console's own float buffers
bool soxrAvailable();

// ---- files
bool read(const std::string& path, Audio& out, std::string& err);   // WAV / RF64 / W64 / FLAC
// format: wav16 wav24 wav32 wavf32 wavf64 (RF64 by itself when the data passes 4 GB), flac16 flac24 flac32; dither: none tpdf shaped
bool write(const std::string& path, const std::vector<double>& interleaved, int channels, double rate, const std::string& format, const std::string& dither, std::string& err, int flacLevel = 5);
std::vector<std::string> writeFormats();
// the same formats written block by block (a recording of any length): make, write blocks of interleaved frames, close
class Writer {
 public:
  static std::unique_ptr<Writer> make(const std::string& path, int channels, double rate, const std::string& format, const std::string& dither, std::string& err, std::uint64_t expectedFrames = 0, int flacLevel = 5);
  ~Writer();
  bool write(const double* interleaved, std::size_t frames);
  bool close(std::string* err = nullptr);   // finishes the header; false when a write or the close failed
  std::uint64_t frames() const;
 private:
  Writer(); struct Impl; std::unique_ptr<Impl> i_;
};

// ---- dither: in (-1 .. 1) -> integers of `bits` (8 .. 32), one state per channel (interleaved)
class Ditherer {
 public:
  Ditherer(int channels, int bits, const std::string& mode, std::uint32_t seed = 0x2545F491u);
  std::int32_t next(double x, int channel);
 private:
  int ch_, bits_; int mode_; double scale_, lo_, hi_; std::uint32_t rng_; std::vector<double> err_;   // 5 past errors per channel
  double rnd();
};

}  // namespace am::hr
