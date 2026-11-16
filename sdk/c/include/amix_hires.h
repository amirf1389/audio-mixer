/* A mix OS engine: HIGH-RESOLUTION AUDIO, part of the C SDK (libamix / amix.dll), AMIX_API_VERSION 6.
 *
 *   amix_hr_resampler   sample-rate conversion up to 768 kHz and beyond: r8brain-free-src (MIT, built in, 64-bit) or libsoxr
 *                       (LGPL 2.1, the separate amix_soxr library, loaded when it is next to the program); planar double buffers
 *   amix_hr_resample    a whole buffer at once, exactly round(frames * dst / src) frames, time aligned
 *   amix_hr_read        WAV (PCM 8..64-bit, float 32 / 64), RF64 and Wave64 (files over 4 GB), FLAC (dr_wav / dr_flac)
 *   amix_hr_write       WAV 16 / 24 / 32-bit, float 32 / 64 (RF64 by itself over 4 GB); FLAC 16 / 24 / 32-bit (libFLAC 1.5)
 *   amix_hr_quantize    float to integers of 8..32 bits with dither: none, TPDF, or TPDF with 5-tap noise shaping
 *
 * Samples are double, -1 .. +1 full scale: a 32-bit integer source keeps every bit. Buffers returned by the library are malloc'd:
 * free them with amix_free(). 0 = ok, negative = failed with the reason in err (when err is not NULL).
 */
#ifndef AMIX_HIRES_H
#define AMIX_HIRES_H
#include "amix.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AMIX_HR_QUALITY_16BIT   0   /* 136 dB stop band */
#define AMIX_HR_QUALITY_24BIT   1   /* 180 dB (r8brain) / very high quality (soxr) */
#define AMIX_HR_QUALITY_MASTER  2   /* 207 dB (r8brain) / 32-bit quality (soxr) */

/* {"libraries":[{"id","name","version","licence","available","note"}],"resamplers":["r8brain","soxr"],"read":[...],"write":[...],"dither":[...]} */
AMIX_API char* amix_hr_info_json(void);

typedef struct amix_hr_resampler amix_hr_resampler;
/* engine: "auto" (soxr when present, else r8brain), "r8brain" or "soxr" */
AMIX_API amix_hr_resampler* amix_hr_resampler_create(double src_rate, double dst_rate, int channels, const char* engine, int quality, char* err, size_t err_len);
AMIX_API void        amix_hr_resampler_destroy(amix_hr_resampler*);
AMIX_API const char* amix_hr_resampler_engine(const amix_hr_resampler*);   /* "r8brain" / "soxr" */
/* in[c][0..frames); writes up to out_capacity frames per channel into out[c]; returns the frames written (what is ready; the rest is kept
 * for the next call). in = NULL flushes the end of the stream. */
AMIX_API int  amix_hr_resampler_process(amix_hr_resampler*, const double* const* in, int frames, double* const* out, int out_capacity);
AMIX_API int  amix_hr_resampler_pending(const amix_hr_resampler*);   /* frames ready and not yet taken */

/* a whole interleaved buffer: *out is malloc'd (free with amix_free), *out_frames its length */
AMIX_API int amix_hr_resample(const double* interleaved, int64_t frames, int channels, double src_rate, double dst_rate, const char* engine, int quality, double** out, int64_t* out_frames, char* err, size_t err_len);

typedef struct amix_hr_info { double rate; int channels; int64_t frames; int bits; int is_float; char container[8]; } amix_hr_info;
/* returns the interleaved samples (free with amix_free) or NULL */
AMIX_API double* amix_hr_read(const char* path_utf8, amix_hr_info* info, char* err, size_t err_len);
/* format: "wav16" "wav24" "wav32" "wavf32" "wavf64" "flac16" "flac24" "flac32"; dither: "none" "tpdf" "shaped" */
AMIX_API int amix_hr_write(const char* path_utf8, const double* interleaved, int64_t frames, int channels, double rate, const char* format, const char* dither, char* err, size_t err_len);
/* n interleaved samples of `channels` to integers of `bits` (8..32), right aligned in int32 */
AMIX_API int amix_hr_quantize(const double* in, int64_t n, int channels, int bits, const char* dither, uint32_t seed, int32_t* out);

#ifdef __cplusplus
}
#endif
#endif
