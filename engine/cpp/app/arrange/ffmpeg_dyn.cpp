// FFmpeg's decoders, used only when its shared libraries sit next to the program (Windows: avformat-61.dll, avcodec-61.dll, avutil-59.dll,
// swresample-5.dll; Linux: the same .so files, or AMIX_FFMPEG_DIR). They are opened at run time and never linked in: the program starts
// and runs without them, and anyone may replace them with their own build (what the LGPL asks of a closed program). Only the headers are
// used at compile time, for the structures; a build without them (no third_party/ffmpeg-*) answers "not available".
#include "decode.hpp"

#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

#if defined(AMIX_FFMPEG_HEADERS)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

namespace am::arr {

#if !defined(AMIX_FFMPEG_HEADERS)
bool readFfmpegLib(const std::string&, Audio&, std::string& err) { err = "FFmpeg is not part of this build"; return false; }
bool ffmpegAvailable() { return false; }
#else
namespace {
struct Lib {
  bool tried = false, ok = false; std::string why;
  decltype(&avformat_open_input) open_input = nullptr; decltype(&avformat_find_stream_info) find_info = nullptr; decltype(&av_find_best_stream) best = nullptr;
  decltype(&av_read_frame) read_frame = nullptr; decltype(&avformat_close_input) close_input = nullptr;
  decltype(&avcodec_find_decoder) find_decoder = nullptr; decltype(&avcodec_alloc_context3) alloc_ctx = nullptr; decltype(&avcodec_parameters_to_context) par_to_ctx = nullptr;
  decltype(&avcodec_open2) open2 = nullptr; decltype(&avcodec_send_packet) send = nullptr; decltype(&avcodec_receive_frame) receive = nullptr; decltype(&avcodec_free_context) free_ctx = nullptr;
  decltype(&av_packet_alloc) pkt_alloc = nullptr; decltype(&av_packet_unref) pkt_unref = nullptr; decltype(&av_packet_free) pkt_free = nullptr;
  decltype(&av_frame_alloc) frame_alloc = nullptr; decltype(&av_frame_free) frame_free = nullptr; decltype(&av_channel_layout_default) layout_default = nullptr; decltype(&av_channel_layout_uninit) layout_uninit = nullptr;
  decltype(&swr_alloc_set_opts2) swr_opts = nullptr; decltype(&swr_init) swr_init_ = nullptr; decltype(&swr_convert) swr_convert_ = nullptr; decltype(&swr_free) swr_free_ = nullptr;
};
#ifdef _WIN32
using Handle = HMODULE;
Handle openLib(const char* name) {   // the program's own folder only: never the current directory or PATH
  wchar_t exe[MAX_PATH] = {}; const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH); std::wstring dir(exe, n); const auto s = dir.find_last_of(L"\\/"); dir = s == std::wstring::npos ? L"" : dir.substr(0, s + 1);
  std::wstring w; for (const char* p = name; *p; ++p) w.push_back(static_cast<wchar_t>(*p));
  return LoadLibraryExW((dir + w).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH); }
void* sym(Handle h, const char* n) { return h ? reinterpret_cast<void*>(GetProcAddress(h, n)) : nullptr; }
const char* kNames[4] = {"avutil-59.dll", "swresample-5.dll", "avcodec-61.dll", "avformat-61.dll"};
#else
using Handle = void*;
Handle openLib(const char* name) { const char* d = std::getenv("AMIX_FFMPEG_DIR"); if (d && *d) { if (Handle h = dlopen((std::string(d) + "/" + name).c_str(), RTLD_NOW | RTLD_LOCAL)) return h; }
  return dlopen(name, RTLD_NOW | RTLD_LOCAL); }
void* sym(Handle h, const char* n) { return h ? dlsym(h, n) : nullptr; }
const char* kNames[4] = {"libavutil.so.59", "libswresample.so.5", "libavcodec.so.61", "libavformat.so.61"};
#endif
Lib& lib() {
  static Lib L; if (L.tried) return L; L.tried = true;
  Handle u = openLib(kNames[0]), r = openLib(kNames[1]), c = openLib(kNames[2]), f = openLib(kNames[3]);
  if (!u || !r || !c || !f) { L.why = "FFmpeg is not installed next to the program"; return L; }
#define GET(field, h, name) L.field = reinterpret_cast<decltype(L.field)>(sym(h, name)); if (!L.field) { L.why = std::string("FFmpeg lacks ") + name; return L; }
  GET(open_input, f, "avformat_open_input") GET(find_info, f, "avformat_find_stream_info") GET(best, f, "av_find_best_stream") GET(read_frame, f, "av_read_frame") GET(close_input, f, "avformat_close_input")
  GET(find_decoder, c, "avcodec_find_decoder") GET(alloc_ctx, c, "avcodec_alloc_context3") GET(par_to_ctx, c, "avcodec_parameters_to_context") GET(open2, c, "avcodec_open2")
  GET(send, c, "avcodec_send_packet") GET(receive, c, "avcodec_receive_frame") GET(free_ctx, c, "avcodec_free_context") GET(pkt_alloc, c, "av_packet_alloc") GET(pkt_unref, c, "av_packet_unref") GET(pkt_free, c, "av_packet_free")
  GET(frame_alloc, u, "av_frame_alloc") GET(frame_free, u, "av_frame_free") GET(layout_default, u, "av_channel_layout_default") GET(layout_uninit, u, "av_channel_layout_uninit")
  GET(swr_opts, r, "swr_alloc_set_opts2") GET(swr_init_, r, "swr_init") GET(swr_convert_, r, "swr_convert") GET(swr_free_, r, "swr_free")
#undef GET
  L.ok = true; return L;
}
// THE MPEG-H EXTENSION (amfb_mpegh): Fraunhofer's decoder in its own library, present only where its patent licences are held
struct Mpegh { bool tried = false, ok = false; void* (*open)(int, const std::uint8_t*, std::uint32_t) = nullptr; int (*feed)(void*, const std::uint8_t*, std::uint32_t, std::uint64_t) = nullptr;
  int (*get)(void*, float*, int, int*, int*) = nullptr; int (*flush)(void*) = nullptr; void (*close)(void*) = nullptr; };
Mpegh& mpegh() {
  static Mpegh M; if (M.tried) return M; M.tried = true;
#ifdef _WIN32
  Handle h = openLib("amfb_mpegh.dll");
#else
  Handle h = openLib("amfb_mpegh.so"); if (!h) h = openLib("libamfb_mpegh.so");
#endif
  if (!h) return M;
  M.open = reinterpret_cast<decltype(M.open)>(sym(h, "amfb_mpegh_open")); M.feed = reinterpret_cast<decltype(M.feed)>(sym(h, "amfb_mpegh_feed")); M.get = reinterpret_cast<decltype(M.get)>(sym(h, "amfb_mpegh_get"));
  M.flush = reinterpret_cast<decltype(M.flush)>(sym(h, "amfb_mpegh_flush")); M.close = reinterpret_cast<decltype(M.close)>(sym(h, "amfb_mpegh_close"));
  M.ok = M.open && M.feed && M.get && M.flush && M.close; return M;
}
// an MPEG-H stream: FFmpeg reads the access units out of the file, the extension decodes them to stereo (CICP 2)
bool decodeMpegh(Lib& F, AVFormatContext* fmt, int si, Audio& out, std::string& err) {
  Mpegh& M = mpegh(); if (!M.ok) { err = "MPEG-H audio: the MPEG-H extension is not installed (it needs its own patent licence)"; return false; }
  const AVCodecParameters* par = fmt->streams[si]->codecpar; const bool inBand = par->codec_tag == MKTAG('m', 'h', 'm', '1');
  void* d = M.open(2, inBand ? nullptr : par->extradata, inBand ? 0u : static_cast<std::uint32_t>(par->extradata_size)); if (!d) { err = "the MPEG-H decoder would not start"; return false; }
  const AVRational tb = fmt->streams[si]->time_base; AVPacket* pkt = F.pkt_alloc(); std::vector<float> buf(3072 * 24 * 2); out = Audio{}; int ch = 2, rate = 48000;
  auto take = [&]() { int n; while ((n = M.get(d, buf.data(), 3072, &ch, &rate)) > 0) for (int i = 0; i < n; ++i) { out.L.push_back(buf[static_cast<std::size_t>(i) * ch]); out.R.push_back(buf[static_cast<std::size_t>(i) * ch + (ch > 1 ? 1 : 0)]); } };
  while (F.read_frame(fmt, pkt) >= 0) {
    if (pkt->stream_index == si) { const std::uint64_t ns = pkt->pts == AV_NOPTS_VALUE ? 0 : static_cast<std::uint64_t>(static_cast<double>(pkt->pts) * tb.num / tb.den * 1e9); M.feed(d, pkt->data, static_cast<std::uint32_t>(pkt->size), ns); take(); }
    F.pkt_unref(pkt); if (out.L.size() > 48000ull * 60 * 60 * 3) break; }
  M.flush(d); take(); M.close(d); F.pkt_free(&pkt); out.rate = rate;
  if (out.L.empty()) { err = "the MPEG-H stream gave no audio"; return false; }
  return true;
}
}  // namespace

bool ffmpegAvailable() { return lib().ok; }

bool readFfmpegLib(const std::string& path, Audio& out, std::string& err) {
  Lib& F = lib(); if (!F.ok) { err = F.why; return false; }
  AVFormatContext* fmt = nullptr; if (F.open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) { err = "FFmpeg cannot open this file"; return false; }
  bool ok = false; AVCodecContext* cc = nullptr; AVPacket* pkt = nullptr; AVFrame* fr = nullptr; SwrContext* swr = nullptr;
  do {
    if (F.find_info(fmt, nullptr) < 0) { err = "FFmpeg cannot read the stream"; break; }
    int si = F.best(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (si < 0) for (unsigned k = 0; k < fmt->nb_streams; ++k) if (fmt->streams[k]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) { si = static_cast<int>(k); break; }   // MPEG-H: FFmpeg cannot tell its channels or rate (no decoder of its own), so it does not offer it as "best"
    if (si < 0) { err = "no audio in the file"; break; }
    const AVCodecParameters* par = fmt->streams[si]->codecpar;
    if (par->codec_id == AV_CODEC_ID_MPEGH_3D_AUDIO) { ok = decodeMpegh(F, fmt, si, out, err); break; }
    const AVCodec* dec = F.find_decoder(par->codec_id); if (!dec) { err = "no decoder for this kind of audio"; break; }
    cc = F.alloc_ctx(dec); if (!cc || F.par_to_ctx(cc, par) < 0 || F.open2(cc, dec, nullptr) < 0) { err = "the decoder would not start"; break; }
    AVChannelLayout stereo; F.layout_default(&stereo, 2);
    if (F.swr_opts(&swr, &stereo, AV_SAMPLE_FMT_FLT, cc->sample_rate, &cc->ch_layout, cc->sample_fmt, cc->sample_rate, 0, nullptr) < 0 || F.swr_init_(swr) < 0) { F.layout_uninit(&stereo); err = "cannot convert the samples"; break; }
    F.layout_uninit(&stereo);
    pkt = F.pkt_alloc(); fr = F.frame_alloc(); out = Audio{}; out.rate = cc->sample_rate > 0 ? cc->sample_rate : 48000; std::vector<float> tmp; bool bad = false;
    auto drain = [&]() { while (F.receive(cc, fr) == 0) { const int cap = fr->nb_samples + 256; tmp.resize(static_cast<std::size_t>(cap) * 2); std::uint8_t* o[1] = {reinterpret_cast<std::uint8_t*>(tmp.data())};
        const int got = F.swr_convert_(swr, o, cap, const_cast<const std::uint8_t**>(fr->extended_data), fr->nb_samples);
        for (int i = 0; i < got; ++i) { out.L.push_back(tmp[2 * i]); out.R.push_back(tmp[2 * i + 1]); }
        if (out.L.size() > static_cast<std::size_t>(out.rate) * 60 * 60 * 3) { err = "longer than 3 hours"; bad = true; return; } } };
    while (!bad && F.read_frame(fmt, pkt) >= 0) { if (pkt->stream_index == si && F.send(cc, pkt) >= 0) drain(); F.pkt_unref(pkt); }
    if (!bad) { F.send(cc, nullptr); drain(); }
    ok = !bad && !out.L.empty(); if (!ok && !bad) err = "no audio in the file";
  } while (false);
  if (swr) F.swr_free_(&swr); if (fr) F.frame_free(&fr); if (pkt) F.pkt_free(&pkt); if (cc) F.free_ctx(&cc); F.close_input(&fmt);
  return ok;
}
#endif

}  // namespace am::arr
