/* A mix OS DSP core for WebAssembly: freestanding C (no libc), built with plain clang --target=wasm32. The browser / Node side gets a
 * small mixer: up to 16 strips with gain, pan, mute, solo, a 4-band parametric EQ per strip (peaking / shelves / high-pass), a master
 * with a brick-wall limiter, and peak meters. Buffers live in the module's memory: write a strip's input at amix_in(strip), call
 * amix_process(frames), read amix_out(0) / amix_out(1). Same algorithms as the native engine (engine/cpp), kept in one file so the
 * web console can run the mix locally; the full engine (buses, loudness, plug-ins, devices) stays native.
 */
#define AMIX_W_MAX_STRIPS 16
#define AMIX_W_MAX_FRAMES 4096
#define AMIX_W_BANDS 4
#define PI 3.14159265358979323846

typedef struct { double b0, b1, b2, a1, a2, z1, z2; int on; } Biquad;
typedef struct { double gain, pan, gl, gr; int mute, solo; Biquad eq[AMIX_W_BANDS]; float peak; } Strip;

static float g_in[AMIX_W_MAX_STRIPS][AMIX_W_MAX_FRAMES];
static float g_out[2][AMIX_W_MAX_FRAMES];
static Strip g_strip[AMIX_W_MAX_STRIPS];
static int g_strips = 2; static double g_fs = 48000, g_master = 1.0, g_limGain = 1.0, g_limCeil = 0.944, g_limRel = 0.0; static int g_limOn = 1; static float g_masterPeak[2];

/* ---- a few libm functions, freestanding ---- */
static double w_fabs(double x) { return x < 0 ? -x : x; }
static double w_exp(double x) { if (x > 700) x = 700; if (x < -700) return 0; int n = (int)(x / 0.6931471805599453); if (x < 0) n--; double r = x - n * 0.6931471805599453, s = 1, t = 1; for (int i = 1; i < 18; ++i) { t *= r / i; s += t; } double p = 1; double b = 2; int k = n < 0 ? -n : n; while (k) { if (k & 1) p *= b; b *= b; k >>= 1; } return n < 0 ? s / p : s * p; }
static double w_log(double x) { if (x <= 0) return -1e300; int e = 0; while (x > 1.4142135623730951) { x *= 0.5; ++e; } while (x < 0.7071067811865476) { x *= 2; --e; } double y = (x - 1) / (x + 1), y2 = y * y, s = 0, t = y; for (int i = 1; i < 40; i += 2) { s += t / i; t *= y2; } return 2 * s + e * 0.6931471805599453; }
static double w_pow(double b, double e) { return b <= 0 ? 0 : w_exp(e * w_log(b)); }
static double w_sin(double x) { while (x > PI) x -= 2 * PI; while (x < -PI) x += 2 * PI; double x2 = x * x, t = x, s = x; for (int i = 1; i < 12; ++i) { t *= -x2 / ((2 * i) * (2 * i + 1)); s += t; } return s; }
static double w_cos(double x) { return w_sin(x + PI / 2); }
static double w_sqrt(double x) { if (x <= 0) return 0; double r = x; for (int i = 0; i < 40; ++i) r = 0.5 * (r + x / r); return r; }
static double db2lin(double db) { return w_pow(10.0, db / 20.0); }

/* RBJ cookbook biquads: type 0 off, 1 peaking, 2 low shelf, 3 high shelf, 5 high-pass, 4 low-pass */
static void design(Biquad* q, int type, double f, double Q, double gdb) {
  q->on = type != 0; if (!q->on) return;
  const double A = w_pow(10.0, gdb / 40.0), w0 = 2 * PI * f / g_fs, cw = w_cos(w0), sw = w_sin(w0), alpha = sw / (2 * (Q > 0.05 ? Q : 0.05));
  double b0, b1, b2, a0, a1, a2;
  switch (type) {
    case 1: b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A; break;
    case 2: { const double s = 2 * w_sqrt(A) * alpha; b0 = A * ((A + 1) - (A - 1) * cw + s); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - s); a0 = (A + 1) + (A - 1) * cw + s; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - s; break; }
    case 3: { const double s = 2 * w_sqrt(A) * alpha; b0 = A * ((A + 1) + (A - 1) * cw + s); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - s); a0 = (A + 1) - (A - 1) * cw + s; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - s; break; }
    case 4: b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
    default: b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;   /* 5: high-pass */
  }
  q->b0 = b0 / a0; q->b1 = b1 / a0; q->b2 = b2 / a0; q->a1 = a1 / a0; q->a2 = a2 / a0;
}
static inline float tick(Biquad* q, float x) { const double y = q->b0 * x + q->z1; q->z1 = q->b1 * x - q->a1 * y + q->z2; q->z2 = q->b2 * x - q->a2 * y; return (float)y; }
static void pan(Strip* s) { const double ang = (s->pan + 1.0) * PI * 0.25; s->gl = w_cos(ang); s->gr = w_sin(ang); }

__attribute__((export_name("amix_init"))) void amix_init(int strips, double fs) {
  g_strips = strips < 1 ? 1 : strips > AMIX_W_MAX_STRIPS ? AMIX_W_MAX_STRIPS : strips; g_fs = fs > 0 ? fs : 48000;
  for (int s = 0; s < AMIX_W_MAX_STRIPS; ++s) { Strip* st = &g_strip[s]; st->gain = 1; st->pan = 0; st->mute = 0; st->solo = 0; st->peak = 0; pan(st); for (int b = 0; b < AMIX_W_BANDS; ++b) { st->eq[b].on = 0; st->eq[b].z1 = st->eq[b].z2 = 0; } }
  g_master = 1; g_limGain = 1; g_limCeil = db2lin(-0.5); g_limRel = 1.0 - w_exp(-1.0 / (0.1 * g_fs)); g_limOn = 1; g_masterPeak[0] = g_masterPeak[1] = 0;
}
__attribute__((export_name("amix_in"))) float* amix_in(int strip) { return g_in[strip < 0 ? 0 : strip >= AMIX_W_MAX_STRIPS ? AMIX_W_MAX_STRIPS - 1 : strip]; }
__attribute__((export_name("amix_out"))) float* amix_out(int ch) { return g_out[ch ? 1 : 0]; }
__attribute__((export_name("amix_max_frames"))) int amix_max_frames(void) { return AMIX_W_MAX_FRAMES; }
__attribute__((export_name("amix_set_gain"))) void amix_set_gain(int s, double db) { if (s >= 0 && s < g_strips) g_strip[s].gain = db <= -60 ? 0 : db2lin(db > 10 ? 10 : db); }
__attribute__((export_name("amix_set_pan"))) void amix_set_pan(int s, double p) { if (s >= 0 && s < g_strips) { g_strip[s].pan = p < -1 ? -1 : p > 1 ? 1 : p; pan(&g_strip[s]); } }
__attribute__((export_name("amix_set_mute"))) void amix_set_mute(int s, int on) { if (s >= 0 && s < g_strips) g_strip[s].mute = on != 0; }
__attribute__((export_name("amix_set_solo"))) void amix_set_solo(int s, int on) { if (s >= 0 && s < g_strips) g_strip[s].solo = on != 0; }
__attribute__((export_name("amix_set_eq"))) void amix_set_eq(int s, int band, int type, double f, double q, double gdb) { if (s >= 0 && s < g_strips && band >= 0 && band < AMIX_W_BANDS) design(&g_strip[s].eq[band], type, f, q, gdb); }
__attribute__((export_name("amix_set_master"))) void amix_set_master(double db) { g_master = db <= -60 ? 0 : db2lin(db > 10 ? 10 : db); }
__attribute__((export_name("amix_set_limiter"))) void amix_set_limiter(int on, double ceilDb, double relMs) { g_limOn = on != 0; g_limCeil = db2lin(ceilDb > 0 ? 0 : ceilDb); g_limRel = 1.0 - w_exp(-1.0 / ((relMs > 1 ? relMs : 1) * 0.001 * g_fs)); }
__attribute__((export_name("amix_peak"))) float amix_peak(int s) { return s >= 0 && s < g_strips ? g_strip[s].peak : 0.f; }
__attribute__((export_name("amix_master_peak"))) float amix_master_peak(int ch) { return g_masterPeak[ch ? 1 : 0]; }
__attribute__((export_name("amix_process"))) int amix_process(int frames) {
  if (frames <= 0) return 0; if (frames > AMIX_W_MAX_FRAMES) frames = AMIX_W_MAX_FRAMES;
  int anySolo = 0; for (int s = 0; s < g_strips; ++s) anySolo |= g_strip[s].solo;
  for (int i = 0; i < frames; ++i) { g_out[0][i] = 0; g_out[1][i] = 0; }
  for (int s = 0; s < g_strips; ++s) {
    Strip* st = &g_strip[s]; float pk = 0; const int audible = !st->mute && (!anySolo || st->solo);
    for (int i = 0; i < frames; ++i) {
      float x = g_in[s][i];
      for (int b = 0; b < AMIX_W_BANDS; ++b) if (st->eq[b].on) x = tick(&st->eq[b], x);
      x = (float)(x * st->gain); const float a = w_fabs(x); if (a > pk) pk = a;
      if (audible) { g_out[0][i] += (float)(x * st->gl); g_out[1][i] += (float)(x * st->gr); }
    }
    st->peak = pk;
  }
  float mpk[2] = {0, 0};
  for (int i = 0; i < frames; ++i) {
    double l = g_out[0][i] * g_master, r = g_out[1][i] * g_master;
    if (g_limOn) { const double p = w_fabs(l) > w_fabs(r) ? w_fabs(l) : w_fabs(r), need = p > g_limCeil ? g_limCeil / p : 1.0; if (need < g_limGain) g_limGain = need; else g_limGain += (1.0 - g_limGain) * g_limRel; if (g_limGain > need) g_limGain = need; l *= g_limGain; r *= g_limGain; }
    g_out[0][i] = (float)l; g_out[1][i] = (float)r; if (w_fabs(l) > mpk[0]) mpk[0] = (float)w_fabs(l); if (w_fabs(r) > mpk[1]) mpk[1] = (float)w_fabs(r);
  }
  g_masterPeak[0] = mpk[0]; g_masterPeak[1] = mpk[1];
  return frames;
}
