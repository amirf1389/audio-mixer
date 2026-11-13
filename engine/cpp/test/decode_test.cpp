// The audio file reader against files made by ffmpeg from one reference WAV: FLAC at 16 and 24 bits (stereo and mono, every compression level),
// AIFF, and on this platform's fallback path MP3 / M4A / OGG (lossy: compared by level and timing, not sample by sample).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/wait.h>
#include "arrange/decode.hpp"
using namespace am::arr;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL line %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static bool run(const std::string& cmd) { return std::system(cmd.c_str()) == 0; }
static double peak(const std::vector<float>& v) { double p = 0; for (float x : v) p = std::max(p, static_cast<double>(std::fabs(x))); return p; }
int main(int argc, char** argv) {
  const std::string dir = std::string(argc > 1 ? argv[1] : "/tmp") + "/amfx1-decode-test"; run("rm -rf '" + dir + "' && mkdir -p '" + dir + "'");
  if (!run("ffmpeg -v error -f lavfi -i 'sine=frequency=440:duration=1.3:sample_rate=48000' -f lavfi -i 'sine=frequency=1000:duration=1.3:sample_rate=48000' -filter_complex '[0][1]amerge=inputs=2,volume=0.7' -c:a pcm_s24le '" + dir + "/ref.wav'")) { std::printf("SKIP: ffmpeg is not here\n"); return 0; }
  Audio ref; std::string err; CHECK(readAudio(dir + "/ref.wav", ref, err), "ref: %s", err.c_str());
  struct Case { const char* name; const char* args; bool exact; };
  const Case cases[] = {
    {"s16.flac", "-c:a flac -sample_fmt s16 -compression_level 0", false}, {"s16-c8.flac", "-c:a flac -sample_fmt s16 -compression_level 8", false},
    {"s24.flac", "-c:a flac -sample_fmt s32 -bits_per_raw_sample 24 -compression_level 5", true}, {"s24-c12.flac", "-c:a flac -sample_fmt s32 -bits_per_raw_sample 24 -compression_level 12", true},
    {"mono.flac", "-ac 1 -c:a flac -sample_fmt s16", false}, {"ref.aiff", "-c:a pcm_s24be", true}, {"f32.aiff", "-c:a pcm_f32be -f aiff", true},
    {"lossy.mp3", "-c:a libmp3lame -b:a 192k", false}, {"lossy.m4a", "-c:a aac -b:a 192k", false}, {"lossy.ogg", "-c:a libvorbis -q:a 5", false},
  };
  for (const Case& c : cases) {
    const std::string f = dir + "/" + c.name;
    if (!run("ffmpeg -v error -y -i '" + dir + "/ref.wav' " + c.args + " '" + f + "'")) { std::printf("skip %s (this ffmpeg cannot write it)\n", c.name); continue; }
    Audio a; const bool ok = readAudio(f, a, err);
    CHECK(ok, "%s: %s", c.name, err.c_str()); if (!ok) continue;
    const bool mono = std::string(c.name).rfind("mono", 0) == 0, lossy = std::string(c.name).rfind("lossy", 0) == 0;
    CHECK(a.rate == 48000, "%s rate %.0f", c.name, a.rate);
    if (lossy) { CHECK(std::fabs(a.duration() - 1.3) < 0.08, "%s length %.3f", c.name, a.duration()); CHECK(std::fabs(peak(a.L) - peak(ref.L)) < 0.08, "%s level %.3f vs %.3f", c.name, peak(a.L), peak(ref.L)); std::printf("%-14s ok  %zu frames, peak %.3f\n", c.name, a.L.size(), peak(a.L)); continue; }
    CHECK(a.L.size() == ref.L.size(), "%s length %zu vs %zu", c.name, a.L.size(), ref.L.size());
    double worst = 0; const std::size_t n = std::min(a.L.size(), ref.L.size());
    for (std::size_t i = 0; i < n; ++i) { worst = std::max(worst, std::fabs(static_cast<double>(a.L[i]) - ref.L[i])); if (!mono) worst = std::max(worst, std::fabs(static_cast<double>(a.R[i]) - ref.R[i])); else worst = std::max(worst, std::fabs(static_cast<double>(a.R[i]) - a.L[i])); }
    const double tol = mono ? 0.36 : c.exact ? 1e-6 : 1.0 / 32768.0 + 1e-6;   // mono: the two sides of ref are different tones, so only L == R is checked by the last term
    if (!mono) CHECK(worst <= tol, "%s differs by %.3g (tolerance %.3g)", c.name, worst, tol); else CHECK(worst < 1e-9 || worst <= tol, "%s mono", c.name);
    std::printf("%-14s ok  %zu frames, worst difference %.2g\n", c.name, a.L.size(), worst);
  }
  Audio bad; CHECK(!readAudio(dir + "/nope.flac", bad, err), "a missing file is an error");
  { FILE* f = std::fopen((dir + "/junk.flac").c_str(), "wb"); std::fputs("fLaC this is not audio", f); std::fclose(f); CHECK(!readAudio(dir + "/junk.flac", bad, err), "a broken FLAC is an error, not a crash"); }
  CHECK(isAudioFile("x/Song.FLAC") && isAudioFile("a.m4a") && !isAudioFile("a.txt"), "extensions");
  run("rm -rf '" + dir + "'");
  { // FFmpeg's libraries opened at run time (AMIX_FFMPEG_DIR in the test): the same files decode through them, by level and length
    if (am::arr::ffmpegAvailable()) {
      for (const char* ext : {"mp3", "m4a", "ogg", "opus"}) { const std::string p = std::string("/tmp/amfx1-dec-lib.") + ext;
        const std::string cmd = std::string("ffmpeg -nostdin -v error -y -f lavfi -i sine=frequency=1000:sample_rate=48000:duration=2 -ac 2 ") + (std::string(ext) == "opus" ? "-c:a libopus " : std::string(ext) == "ogg" ? "-c:a libvorbis " : "") + p;
        if (std::system(cmd.c_str()) != 0) { std::printf("skip %s (no encoder here)\n", ext); continue; }
        am::arr::Audio a; std::string err; const bool ok = am::arr::readFfmpegLib(p, a, err); double e = 0; for (float v : a.L) e += v * v;
        const double rms = a.L.empty() ? 0 : std::sqrt(e / a.L.size());
        if (!ok || std::fabs(a.duration() - 2.0) > 0.1 || std::fabs(rms - 0.0628) > 0.006) { std::printf("FAIL FFmpeg library %s: ok %d err '%s' %.2f s rms %.3f\n", ext, ok, err.c_str(), a.duration(), rms); ++fails; }
        else std::printf("FFmpeg library %s: %.2f s at %.0f Hz\n", ext, a.duration(), a.rate);
        std::remove(p.c_str()); }
      { // 5.1 AC-3 with the same tone on all six channels: the stereo downmix is normalised (it was 2.4x too loud, so surround files clipped)
        const std::string p = "/tmp/amfx1-dec-51.ac3";
        if (std::system(("ffmpeg -nostdin -v error -y -f lavfi -i sine=frequency=1000:sample_rate=48000:duration=2 -af \"pan=5.1|c0=c0|c1=c0|c2=c0|c3=c0|c4=c0|c5=c0\" -c:a ac3 " + p).c_str()) == 0) {
          am::arr::Audio a; std::string err; const bool ok = am::arr::readFfmpegLib(p, a, err); double e = 0; for (float v : a.L) e += v * v; const double rms = a.L.empty() ? 0 : std::sqrt(e / a.L.size());
          if (!ok || rms > 0.1 || rms < 0.07) { std::printf("FAIL FFmpeg 5.1 downmix: ok %d rms %.3f (want about 0.088)\n", ok, rms); ++fails; } else std::printf("FFmpeg 5.1 downmix: rms %.3f\n", rms);
          std::remove(p.c_str()); } }
    } else std::printf("FFmpeg libraries not present: library path not tested\n");
  }
  std::printf(fails ? "decode_test: %d FAILED\n" : "decode_test: all passed\n", fails);
  return fails ? 1 : 0;
}
