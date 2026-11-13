// AmF X1 MPEG-H extension: a thin C wrapper around the Fraunhofer FDK MPEG-H decoder (mpeghdec), built as its own library
// (amfb_mpegh.dll / libamfb_mpegh.so). The console never links it: when it finds this file next to the program it opens it at run
// time and hands it the MPEG-H access units FFmpeg reads out of an MP4 / MHAS file. NOT PART OF THE RELEASE until the MPEG-H patent
// licences (Via LA, www.via-la.com) are in place; see scripts/third_party/build-mpeghdec.sh and third_party notices.
#include <mpeghdecoder.h>

#include <cstdint>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#define AMFB_EXPORT extern "C" __declspec(dllexport)
#else
#define AMFB_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {
struct Ctx { HANDLE_MPEGH_DECODER_CONTEXT h = nullptr; std::vector<int32_t> pcm; };
}

// cicp: the target loudspeaker layout (2 = stereo, 6 = 5.1, 13 = 22.2 ...); config: the mhaC box of an "mha1" track (nullptr for "mhm1",
// which carries its configuration in band). Returns nullptr when the decoder will not start.
AMFB_EXPORT void* amfb_mpegh_open(int cicp, const uint8_t* config, uint32_t size) {
  Ctx* c = new Ctx; c->h = mpeghdecoder_init(cicp); if (!c->h) { delete c; return nullptr; }
  if (config && size && mpeghdecoder_setMhaConfig(c->h, config, size) != MPEGH_DEC_OK) { mpeghdecoder_destroy(c->h); delete c; return nullptr; }
  c->pcm.resize(3072 * 24 * 2); return c;
}
// one access unit with its presentation time in nanoseconds; 0 when taken
AMFB_EXPORT int amfb_mpegh_feed(void* p, const uint8_t* au, uint32_t len, uint64_t ptsNs) { Ctx* c = static_cast<Ctx*>(p); return c && mpeghdecoder_process(c->h, au, len, ptsNs) == MPEGH_DEC_OK ? 0 : -1; }
// decoded audio as interleaved float; returns frames written (0 = nothing ready), channels / rate filled in
AMFB_EXPORT int amfb_mpegh_get(void* p, float* out, int maxFrames, int* channels, int* rate) {
  Ctx* c = static_cast<Ctx*>(p); if (!c) return -1; MPEGH_DECODER_OUTPUT_INFO info; std::memset(&info, 0, sizeof info);
  if (mpeghdecoder_getSamples(c->h, c->pcm.data(), static_cast<uint32_t>(c->pcm.size()), &info) != MPEGH_DEC_OK || info.numSamplesPerChannel <= 0) return 0;
  const int ch = info.numChannels, n = info.numSamplesPerChannel < maxFrames ? info.numSamplesPerChannel : maxFrames; if (channels) *channels = ch; if (rate) *rate = info.sampleRate;
  for (int i = 0; i < n * ch; ++i) out[i] = static_cast<float>(c->pcm[static_cast<std::size_t>(i)] / 2147483648.0);
  return n;
}
AMFB_EXPORT int amfb_mpegh_flush(void* p) { Ctx* c = static_cast<Ctx*>(p); return c && mpeghdecoder_flushAndGet(c->h) == MPEGH_DEC_OK ? 0 : -1; }
AMFB_EXPORT void amfb_mpegh_close(void* p) { Ctx* c = static_cast<Ctx*>(p); if (!c) return; mpeghdecoder_destroy(c->h); delete c; }
AMFB_EXPORT const char* amfb_mpegh_notice() { return "MPEG-H 3D Audio decoding by the Fraunhofer FDK MPEG-H Software (mpeghdec). Commercial use needs MPEG-H patent licences (Via LA)."; }
