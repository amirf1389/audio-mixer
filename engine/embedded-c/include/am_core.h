/* Audio Mixer core (embedded C, C99): the measuring and mixing code of Audio Mixer without malloc, stdio, threads or operating system calls.
 *
 * Everything lives in structs that the caller provides (static, stack or in another struct) and is sized at compile time, so it runs on a microcontroller / DSP
 * as well as in a phone, in WebAssembly (the page uses it: engine/wasm) and in the Apple wrappers (engine/apple). The only dependency is <math.h> (sqrt, log10, pow, tan,
 * sin, cos); define AMC_NO_LIBM and the few functions are built in (freestanding builds such as WebAssembly without a C library).
 *
 * The numbers are those of the page and of the bridge (the reference code in index.html and bridge/levels.js, see engine/README.md):
 *   amc_meter_*      peak / RMS / peak hold of a block with CLIP detection (3 samples in a row at the threshold, or one at full scale), latched, events 250 ms apart count once
 *   amc_loudness_*   ITU-R BS.1770-4 / EBU R128: K-weighting at any sample rate, momentary (400 ms), short-term (3 s), gated integrated, loudness range, 4x true peak
 *   amc_phase        stereo phase correlation (with stereo width and polarity inverts)
 *   amc_scope_calc   oscilloscope sweep: trigger, length and readings (Vpp, RMS, crest, frequency)
 *   amc_vu_*         VU needle ballistics
 *   amc_s16meter_*   the bridge's meter for interleaved 16-bit PCM
 *   amc_mixer_*      32 mono strips (gain, pan, mute, solo) -> stereo master (gain, limiter), with a meter on every strip and the master, loudness and phase
 * Sample data is float (-1..1). Levels are linear unless the name says _db. "Minus infinity" is -INFINITY. */
#ifndef AM_CORE_H
#define AM_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef AMC_MAX_STRIPS
#define AMC_MAX_STRIPS 32            /* strips of the mixer (1..AMC_MAX_STRIPS) */
#endif
#ifndef AMC_LUFS_MAX_BLOCKS
#define AMC_LUFS_MAX_BLOCKS 6000     /* gating blocks kept for the integrated loudness: 6000 x 100 ms = 10 minutes (the page keeps 6 hours: raise it where there is RAM) */
#endif
#ifndef AMC_CHUNK
#define AMC_CHUNK 512                /* samples per inner loop: the stack use of the mixer and the loudness meter */
#endif
#define AMC_S16_MAX_CH 64            /* channels of the Int16 meter */

#define AMC_VERSION 1                /* the version of this API: the numbers (and the test vectors) change only with it */

/* ── level meter with clip detection ── */
typedef struct { double lin; int run; double hold_ms; double merge_ms; double lit_ms; } amc_clip_cfg_t;   /* linear threshold, samples in a row, peak hold, merge window, badge time */
void amc_clip_cfg_init(amc_clip_cfg_t *c, double threshold_db);   /* -12..0 dBFS; the others take the page's values: 3 samples, 1400 ms, 250 ms, 1500 ms */

typedef struct {
  double pk, rms;                    /* smoothed peak and RMS (fast attack, slow release) */
  double hold, hold_at;              /* peak hold and when it was set (ms) */
  double clip_at, clip_pk;           /* last clip (ms), highest peak of a clip */
  uint32_t clip_n;                   /* clip events since the last clear */
  int latched;                       /* a clip happened and was not cleared */
} amc_meter_t;
void amc_meter_init(amc_meter_t *m);
void amc_meter_clear_clip(amc_meter_t *m);
int amc_meter_lit(const amc_meter_t *m, double now_ms, const amc_clip_cfg_t *c);

/* one reading window may come in pieces: begin, accumulate (any number of times), end */
typedef struct { double pk, ss; size_t n; int run, max_run; } amc_window_t;
void amc_window_begin(amc_window_t *w);
void amc_window_add(amc_window_t *w, const float *x, size_t n, const amc_clip_cfg_t *c);
void amc_meter_end(amc_meter_t *m, const amc_window_t *w, double now_ms, const amc_clip_cfg_t *c);
void amc_meter_push(amc_meter_t *m, const float *x, size_t n, double now_ms, const amc_clip_cfg_t *c);   /* begin + add + end */

/* ── loudness (BS.1770-4) ── */
typedef struct {
  double fs; int sub_size;
  double c1[5], c2[5];               /* K-weighting stages: b0 b1 b2 a1 a2 */
  double z[8]; float h[2][11]; double tp[3][12];
  double sub_acc; int sub_n; uint64_t samples;
  double sub[30]; int sub_len;       /* the last 3 s in 100 ms blocks (mean square, both channels summed) */
  double blocks[AMC_LUFS_MAX_BLOCKS]; int n_blocks;   /* 400 ms gating blocks, one per 100 ms */
  double shorts[AMC_LUFS_MAX_BLOCKS]; int n_shorts;   /* short-term values, for the loudness range */
  double scratch[AMC_LUFS_MAX_BLOCKS];                /* working space of the gating (so that several meters can run in several threads) */
  double momentary, short_term, integrated, lra, true_peak;   /* LUFS, LUFS, LUFS, LU, dBTP */
} amc_loudness_t;
void amc_loudness_init(amc_loudness_t *l, double sample_rate);
void amc_loudness_reset(amc_loudness_t *l);        /* integrated, range and true peak start over */
void amc_loudness_feed(amc_loudness_t *l, const float *left, const float *right, size_t n);

/* ── phase correlation ── */
typedef struct { double corr, peak, rms_l, rms_r; int silent; } amc_phase_t;
void amc_phase(const float *l, const float *r, size_t n, double width_percent, int invert_l, int invert_r, amc_phase_t *out);   /* corr -1..1; 0 when silent */

/* ── oscilloscope ── */
typedef struct { double timebase_ms; double trigger_db; int rising; } amc_scope_cfg_t;     /* ms per division (10 divisions), trigger level in dB, edge */
typedef struct { size_t n, start; int triggered; double p2p, rms, crest_db, freq; } amc_scope_t;   /* sweep length and first sample, readings of channel A */
void amc_scope_calc(const float *a, size_t size, double sample_rate, const amc_scope_cfg_t *cfg, amc_scope_t *out);

/* ── VU needle ── */
typedef struct { double needle, vel; int peak_led; } amc_vu_t;
void amc_vu_init(amc_vu_t *v);
void amc_vu_step(amc_vu_t *v, double rms, double ref_dbfs);   /* once per display frame; ref: 0 VU = ref dBFS (-18 EBU, -20 SMPTE) */

/* ── Int16 interleaved meter (the bridge's native streams) ── */
typedef struct {
  int ch; int32_t limit; int32_t peak[AMC_S16_MAX_CH]; double sum_sq[AMC_S16_MAX_CH]; int32_t run[AMC_S16_MAX_CH];
  uint32_t frames, clips, runs; uint8_t clip_ch[AMC_S16_MAX_CH];
} amc_s16meter_t;
typedef struct { double peak_db[AMC_S16_MAX_CH], rms_db[AMC_S16_MAX_CH]; uint32_t clip, clip_runs, frames; int clipping; uint8_t clip_ch[AMC_S16_MAX_CH]; } amc_s16snap_t;
void amc_s16meter_init(amc_s16meter_t *m, int channels, double clip_db);
void amc_s16meter_push(amc_s16meter_t *m, const int16_t *interleaved, size_t samples);   /* samples = frames x channels; a partial last frame is ignored */
void amc_s16meter_snapshot(amc_s16meter_t *m, amc_s16snap_t *out);                      /* levels since the last snapshot, then start over (the clip runs carry over) */

/* ── mixer ── */
typedef struct { double gain_db, gain_lin, pan; int mute, solo; amc_meter_t meter; } amc_strip_t;
typedef struct {
  double fs, time_ms; int n_strips;
  amc_strip_t strip[AMC_MAX_STRIPS];
  double master_db, master_lin;
  int limiter_on; double limiter_ceiling_lin, limiter_gain, limiter_release;   /* brick-wall peak limiter on the master: instant attack, release in ms */
  amc_clip_cfg_t clip;
  amc_meter_t master[2];
  amc_loudness_t loudness;
  amc_phase_t phase;
} amc_mixer_t;
void amc_mixer_init(amc_mixer_t *m, int strips, double sample_rate);
void amc_mixer_set_gain_db(amc_mixer_t *m, int strip, double db);   /* -60 and below = silence, +10 at most */
void amc_mixer_set_pan(amc_mixer_t *m, int strip, double pan);       /* -1 left .. +1 right, constant power (-3 dB in the middle) */
void amc_mixer_set_mute(amc_mixer_t *m, int strip, int on);
void amc_mixer_set_solo(amc_mixer_t *m, int strip, int on);          /* with any solo on, only the solo strips are heard */
void amc_mixer_set_master_db(amc_mixer_t *m, double db);
void amc_mixer_set_limiter(amc_mixer_t *m, int on, double ceiling_db, double release_ms);
void amc_mixer_set_clip_db(amc_mixer_t *m, double db);
/* readings for languages that cannot index the structs (Swift imports C arrays as tuples) */
double amc_mixer_strip_peak(const amc_mixer_t *m, int strip);
double amc_mixer_master_peak(const amc_mixer_t *m, int channel);   /* 0 left, 1 right */
/* inputs[strip] -> out_l / out_r (frames samples each); the meters, loudness and phase are updated once per call */
void amc_mixer_process(amc_mixer_t *m, const float *const *inputs, size_t frames, float *out_l, float *out_r);

#ifdef __cplusplus
}
#endif
#endif
