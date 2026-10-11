/* WebAssembly build of the embedded C core for the page (and for Node.js): the loudness meter and the scope arithmetic, with fixed buffers that JavaScript fills.
 * Built freestanding (no C library, no imports) by scripts/build-engine.js; the binary is embedded in index.html (script id "engine-wasm"). */
#include "../embedded-c/include/am_core.h"

#define WASM_EXPORT(n) __attribute__((export_name(n)))

#define BLOCK_MAX 8192            /* samples per lufs_feed call */
#define SCOPE_MAX 32768           /* samples per scope_calc call */

static amc_loudness_t loud;
static float in_l[BLOCK_MAX], in_r[BLOCK_MAX], scope_in[SCOPE_MAX];
static amc_scope_t scope_out;

/* the compiler may turn copy / clear loops into these: define them here so that the module imports nothing */
void *memcpy(void *d, const void *s, unsigned long n) { unsigned char *p = (unsigned char *)d; const unsigned char *q = (const unsigned char *)s; while (n--) *p++ = *q++; return d; }
void *memset(void *d, int v, unsigned long n) { unsigned char *p = (unsigned char *)d; while (n--) *p++ = (unsigned char)v; return d; }
void *memmove(void *d, const void *s, unsigned long n) { unsigned char *p = (unsigned char *)d; const unsigned char *q = (const unsigned char *)s; if (p < q) while (n--) *p++ = *q++; else { p += n; q += n; while (n--) *--p = *--q; } return d; }

WASM_EXPORT("version") int version(void) { return AMC_VERSION; }
WASM_EXPORT("block_max") int block_max(void) { return BLOCK_MAX; }
WASM_EXPORT("scope_max") int scope_max(void) { return SCOPE_MAX; }
WASM_EXPORT("left_ptr") float *left_ptr(void) { return in_l; }
WASM_EXPORT("right_ptr") float *right_ptr(void) { return in_r; }
WASM_EXPORT("scope_ptr") float *scope_ptr(void) { return scope_in; }

WASM_EXPORT("lufs_init") void lufs_init(double fs) { amc_loudness_init(&loud, fs); }
WASM_EXPORT("lufs_reset") void lufs_reset(void) { amc_loudness_reset(&loud); }
WASM_EXPORT("lufs_feed") void lufs_feed(int n) { if (n > BLOCK_MAX) n = BLOCK_MAX; if (n > 0) amc_loudness_feed(&loud, in_l, in_r, (size_t)n); }
WASM_EXPORT("lufs_momentary") double lufs_momentary(void) { return loud.momentary; }
WASM_EXPORT("lufs_short_term") double lufs_short_term(void) { return loud.short_term; }
WASM_EXPORT("lufs_integrated") double lufs_integrated(void) { return loud.integrated; }
WASM_EXPORT("lufs_lra") double lufs_lra(void) { return loud.lra; }
WASM_EXPORT("lufs_true_peak") double lufs_true_peak(void) { return loud.true_peak; }

WASM_EXPORT("scope_calc") void scope_calc(int size, double fs, double timebase_ms, double trigger_db, int rising) {
  amc_scope_cfg_t c; c.timebase_ms = timebase_ms; c.trigger_db = trigger_db; c.rising = rising;
  if (size > SCOPE_MAX) size = SCOPE_MAX;
  amc_scope_calc(scope_in, (size_t)size, fs, &c, &scope_out);
}
WASM_EXPORT("scope_n") int scope_n(void) { return (int)scope_out.n; }
WASM_EXPORT("scope_start") int scope_start(void) { return (int)scope_out.start; }
WASM_EXPORT("scope_triggered") int scope_triggered(void) { return scope_out.triggered; }
WASM_EXPORT("scope_p2p") double scope_p2p(void) { return scope_out.p2p; }
WASM_EXPORT("scope_rms") double scope_rms(void) { return scope_out.rms; }
WASM_EXPORT("scope_crest") double scope_crest(void) { return scope_out.crest_db; }
WASM_EXPORT("scope_freq") double scope_freq(void) { return scope_out.freq; }
