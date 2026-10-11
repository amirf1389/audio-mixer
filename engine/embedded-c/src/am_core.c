/* Audio Mixer core (embedded C, C99): see include/am_core.h. No allocation, no I/O. */
#include "../include/am_core.h"
#include "am_math.h"

#define AMC_PI 3.14159265358979323846
#define AMC_NEG_INF (-AMC_INF)
#define AMC_MIN(a, b) ((a) < (b) ? (a) : (b))
#define AMC_MAX(a, b) ((a) > (b) ? (a) : (b))

static double amc_clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
static double amc_round(double v) { double f = (double)(long long)v; if (v < 0 && f != v) f -= 1.0; return (v + 0.5 >= f + 1.0) ? f + 1.0 : f; }   /* round half up, like Math.round */
static double amc_floor(double v) { double f = (double)(long long)v; return (v < 0 && f != v) ? f - 1.0 : f; }
static double amc_lin_of(double db) { return amc_pow(10.0, db / 20.0); }

/* ───────────── level meter ───────────── */
void amc_clip_cfg_init(amc_clip_cfg_t *c, double threshold_db) {
  c->lin = amc_lin_of(amc_clamp(threshold_db, -12.0, 0.0)); c->run = 3; c->hold_ms = 1400.0; c->merge_ms = 250.0; c->lit_ms = 1500.0;
}
void amc_meter_init(amc_meter_t *m) { amc_memset(m, 0, sizeof *m); }
void amc_meter_clear_clip(amc_meter_t *m) { m->clip_n = 0; m->latched = 0; m->clip_pk = 0; m->clip_at = 0; }
int amc_meter_lit(const amc_meter_t *m, double now_ms, const amc_clip_cfg_t *c) { return now_ms - m->clip_at < c->lit_ms; }

void amc_window_begin(amc_window_t *w) { w->pk = 0; w->ss = 0; w->n = 0; w->run = 0; w->max_run = 0; }
void amc_window_add(amc_window_t *w, const float *x, size_t n, const amc_clip_cfg_t *c) {
  size_t i;
  for (i = 0; i < n; i++) {
    double v = x[i], a = v < 0 ? -v : v;
    if (a > w->pk) w->pk = a;
    w->ss += v * v;
    if (a >= c->lin) { if (++w->run > w->max_run) w->max_run = w->run; } else w->run = 0;
  }
  w->n += n;
}
void amc_meter_end(amc_meter_t *m, const amc_window_t *w, double now_ms, const amc_clip_cfg_t *c) {
  double rms = w->n ? amc_sqrt(w->ss / (double)w->n) : 0.0;
  m->pk = w->pk >= m->pk ? w->pk : m->pk * 0.9 + w->pk * 0.1;          /* fast attack, slow release */
  m->rms = rms >= m->rms ? rms : m->rms * 0.9 + rms * 0.1;
  if (w->pk >= m->hold || now_ms - m->hold_at > c->hold_ms) { m->hold = w->pk; m->hold_at = now_ms; }
  if (w->max_run >= c->run || w->pk >= 1.0) {                           /* clip: a flat top, or full scale; events closer than merge_ms are one */
    if (now_ms - m->clip_at > c->merge_ms) m->clip_n++;
    m->clip_at = now_ms; m->latched = 1;
    if (w->pk > m->clip_pk) m->clip_pk = w->pk;
  }
}
void amc_meter_push(amc_meter_t *m, const float *x, size_t n, double now_ms, const amc_clip_cfg_t *c) {
  amc_window_t w; amc_window_begin(&w); amc_window_add(&w, x, n, c); amc_meter_end(m, &w, now_ms, c);
}

/* ───────────── loudness ───────────── */
static double lk(double ms) { return ms > 0 ? -0.691 + 10.0 * amc_log10(ms) : AMC_NEG_INF; }
static double mean_loud(const double *a, int n) { double s = 0; int i; for (i = 0; i < n; i++) s += a[i]; return n ? lk(s / n) : AMC_NEG_INF; }

static void k_pre(double *c, double fs, double f0, double g, double q) {
  double k = amc_tan(AMC_PI * f0 / fs), vh = amc_pow(10.0, g / 20.0), vb = amc_pow(vh, 0.4996667741545416), a0 = 1.0 + k / q + k * k;
  c[0] = (vh + vb * k / q + k * k) / a0; c[1] = 2.0 * (k * k - vh) / a0; c[2] = (vh - vb * k / q + k * k) / a0; c[3] = 2.0 * (k * k - 1.0) / a0; c[4] = (1.0 - k / q + k * k) / a0;
}
static void k_hp(double *c, double fs, double f0, double q) {
  double k = amc_tan(AMC_PI * f0 / fs), a0 = 1.0 + k / q + k * k;
  c[0] = 1.0; c[1] = -2.0; c[2] = 1.0; c[3] = 2.0 * (k * k - 1.0) / a0; c[4] = (1.0 - k / q + k * k) / a0;
}
void amc_loudness_reset(amc_loudness_t *l) {
  l->n_blocks = 0; l->n_shorts = 0; l->integrated = AMC_NEG_INF; l->lra = 0; l->true_peak = AMC_NEG_INF;
}
void amc_loudness_init(amc_loudness_t *l, double fs) {
  int p, k;
  amc_memset(l, 0, sizeof *l);
  l->fs = fs; l->sub_size = (int)amc_round(fs * 0.1);
  k_pre(l->c1, fs, 1681.974450955533, 3.999843853973347, 0.7071752369554196);
  k_hp(l->c2, fs, 38.13547087602444, 0.5003270373238773);
  for (p = 0; p < 3; p++) {                                              /* 4x oversampling: 12-tap windowed sinc, phases 1/4, 2/4, 3/4 */
    double f = (p + 1) / 4.0, sum = 0;
    for (k = -5; k <= 6; k++) { double t = f - k, s = t == 0 ? 1.0 : amc_sin(AMC_PI * t) / (AMC_PI * t), w = 0.5 + 0.5 * amc_cos(AMC_PI * t / 6.0); l->tp[p][k + 5] = s * w; sum += s * w; }
    for (k = 0; k < 12; k++) l->tp[p][k] /= sum;
  }
  l->momentary = l->short_term = AMC_NEG_INF;
  amc_loudness_reset(l);
}
static void gate(amc_loudness_t *l) {
  int i, na = 0, ns = 0; double sum = 0, rel;
  double *abs_b = l->scratch;
  for (i = 0; i < l->n_blocks; i++) if (lk(l->blocks[i]) > -70.0) abs_b[na++] = l->blocks[i];
  if (!na) l->integrated = AMC_NEG_INF;
  else {
    int np = 0; double psum = 0;
    rel = mean_loud(abs_b, na) - 10.0;
    for (i = 0; i < na; i++) if (lk(abs_b[i]) > rel) { psum += abs_b[i]; np++; }
    l->integrated = np ? lk(psum / np) : AMC_NEG_INF;
  }
  for (i = 0; i < l->n_shorts; i++) if (l->shorts[i] > -70.0) { abs_b[ns++] = l->shorts[i]; sum += amc_pow(10.0, (l->shorts[i] + 0.691) / 10.0); }
  if (ns > 1) {                                                          /* loudness range: gates -70 / -20 LU, 10th..95th percentile */
    int nv = 0, j; double r2 = -0.691 + 10.0 * amc_log10(sum / ns) - 20.0;
    for (i = 0; i < ns; i++) if (abs_b[i] > r2) abs_b[nv++] = abs_b[i];
    for (i = 1; i < nv; i++) { double v = abs_b[i]; for (j = i - 1; j >= 0 && abs_b[j] > v; j--) abs_b[j + 1] = abs_b[j]; abs_b[j + 1] = v; }
    if (nv > 1) { int hi = (int)AMC_MIN((double)(nv - 1), amc_floor(0.95 * (nv - 1) + 0.5)), lo = (int)amc_floor(0.10 * (nv - 1) + 0.5); l->lra = AMC_MAX(0.0, abs_b[hi] - abs_b[lo]); }
    else l->lra = 0;
  } else l->lra = 0;
}
static void push_cap(double *a, int *n, double v) {
  if (*n >= AMC_LUFS_MAX_BLOCKS) { amc_memmove(a, a + 1, (size_t)(AMC_LUFS_MAX_BLOCKS - 1) * sizeof *a); *n = AMC_LUFS_MAX_BLOCKS - 1; }
  a[(*n)++] = v;
}
void amc_loudness_feed(amc_loudness_t *l, const float *left, const float *right, size_t n) {
  const float *ch[2]; double e = 0, pk = 0, tpdb; int c;
  ch[0] = left; ch[1] = right;
  for (c = 0; c < 2; c++) {
    const float *x = ch[c]; double *z = l->z + c * 4; double a1s = z[0], a2s = z[1], b1s = z[2], b2s = z[3];
    size_t done = 0;
    while (done < n) {
      size_t m = AMC_MIN((size_t)AMC_CHUNK, n - done), i, mm; float buf[11 + AMC_CHUNK];
      amc_memcpy(buf, l->h[c], 11 * sizeof(float)); amc_memcpy(buf + 11, x + done, m * sizeof(float));
      for (mm = 5; mm < m + 5; mm++) {                                   /* true peak: the samples and 3 interpolated phases between them */
        double a = buf[mm] < 0 ? -(double)buf[mm] : (double)buf[mm]; int p, j;
        if (a > pk) pk = a;
        for (p = 0; p < 3; p++) { double s = 0; for (j = 0; j < 12; j++) s += (double)buf[mm - 5 + j] * l->tp[p][j]; if (s < 0) s = -s; if (s > pk) pk = s; }
      }
      amc_memcpy(l->h[c], buf + m, 11 * sizeof(float));
      for (i = 0; i < m; i++) {                                          /* K-weighting, direct form II transposed */
        double v = x[done + i], y1 = l->c1[0] * v + a1s, y2;
        a1s = l->c1[1] * v - l->c1[3] * y1 + a2s; a2s = l->c1[2] * v - l->c1[4] * y1;
        y2 = l->c2[0] * y1 + b1s; b1s = l->c2[1] * y1 - l->c2[3] * y2 + b2s; b2s = l->c2[2] * y1 - l->c2[4] * y2;
        e += y2 * y2;
      }
      done += m;
    }
    z[0] = a1s; z[1] = a2s; z[2] = b1s; z[3] = b2s;
  }
  l->sub_acc += e; l->sub_n += (int)n; l->samples += n;
  tpdb = pk > 0 ? 20.0 * amc_log10(pk) : AMC_NEG_INF;
  if (tpdb > l->true_peak) l->true_peak = tpdb;
  if (l->sub_n >= l->sub_size) {
    double ms = l->sub_acc / l->sub_n; int i;
    l->sub_acc = 0; l->sub_n = 0;
    if (l->sub_len >= 30) { amc_memmove(l->sub, l->sub + 1, 29 * sizeof(double)); l->sub_len = 29; }
    l->sub[l->sub_len++] = ms;
    if (l->sub_len >= 4) {
      double m4 = 0; for (i = l->sub_len - 4; i < l->sub_len; i++) m4 += l->sub[i];
      m4 /= 4.0; l->momentary = lk(m4); push_cap(l->blocks, &l->n_blocks, m4);
    } else l->momentary = lk(ms);
    if (l->sub_len >= 30) { double s = 0; for (i = 0; i < 30; i++) s += l->sub[i]; l->short_term = lk(s / 30.0); push_cap(l->shorts, &l->n_shorts, l->short_term); }
    else l->short_term = mean_loud(l->sub, l->sub_len);
    gate(l);
  }
}

/* ───────────── phase ───────────── */
void amc_phase(const float *L, const float *R, size_t n, double width_percent, int inv_l, int inv_r, amc_phase_t *out) {
  double width = width_percent / 100.0, sLR = 0, sLL = 0, sRR = 0, peak = 0, den; size_t i;
  for (i = 0; i < n; i++) {
    double l = inv_l ? -(double)L[i] : (double)L[i], r = inv_r ? -(double)R[i] : (double)R[i];
    double mid = (l + r) * 0.5, side = (l - r) * 0.5 * width, a;
    l = mid + side; r = mid - side;
    sLR += l * r; sLL += l * l; sRR += r * r;
    a = AMC_MAX(l < 0 ? -l : l, r < 0 ? -r : r); if (a > peak) peak = a;
  }
  den = amc_sqrt(sLL * sRR);
  out->silent = peak < 0.0005;
  out->corr = (out->silent || den == 0) ? 0.0 : amc_clamp(sLR / den, -1.0, 1.0);
  out->peak = peak; out->rms_l = n ? amc_sqrt(sLL / (double)n) : 0.0; out->rms_r = n ? amc_sqrt(sRR / (double)n) : 0.0;
}

/* ───────────── scope ───────────── */
void amc_scope_calc(const float *A, size_t size, double fs, const amc_scope_cfg_t *cfg, amc_scope_t *out) {
  double tb = cfg->timebase_ms > 0 ? cfg->timebase_ms : 1.0, nn = amc_round(fs * tb * 10.0 / 1000.0), lvl = amc_lin_of(cfg->trigger_db);
  size_t half = size >> 1, N, start, i, from; double mn = AMC_INF, mx = -AMC_INF, ss = 0, pk = 0, rms, hy, t; int triggered = 0;
  N = (size_t)AMC_MAX(64.0, AMC_MIN((double)half, nn)); start = size - N;
  for (i = (size_t)(size >= 2 * N ? size - 2 * N + 1 : 1); i < size - N; i++) {   /* the newest sweep that begins with a crossing and still has N samples after it */
    int hit = cfg->rising ? (A[i - 1] < lvl && A[i] >= lvl) : (A[i - 1] > -lvl && A[i] <= -lvl);
    if (i >= 1 && hit) { start = i; triggered = 1; }
  }
  for (i = start; i < start + N; i++) { double v = A[i], a = v < 0 ? -v : v; if (v < mn) mn = v; if (v > mx) mx = v; ss += v * v; if (a > pk) pk = a; }
  rms = amc_sqrt(ss / (double)N);
  {                                                                        /* frequency: rising zero crossings of the newest 8192 samples, with hysteresis */
    double first = -1, last = -1; int cnt = 0, armed = 0; from = size > 8192 ? size - 8192 : 1; if (from < 1) from = 1;
    hy = AMC_MAX(0.002, pk * 0.05);
    for (i = from; i < size; i++) {
      if (A[i] < -hy) armed = 1;
      else if (armed && A[i - 1] < 0 && A[i] >= 0) { t = (double)(i - 1) + (-(double)A[i - 1]) / ((double)A[i] - (double)A[i - 1] == 0 ? 1.0 : (double)A[i] - (double)A[i - 1]); if (first < 0) first = t; last = t; cnt++; armed = 0; }
    }
    out->freq = cnt >= 2 ? (cnt - 1) * fs / (last - first) : 0.0;
  }
  out->n = N; out->start = start; out->triggered = triggered; out->p2p = (mx - mn) == (mx - mn) && (mx - mn) < AMC_INF ? mx - mn : 0.0; out->rms = rms; out->crest_db = rms > 1e-6 ? 20.0 * amc_log10(pk / rms) : 0.0;
}

/* ───────────── VU ───────────── */
void amc_vu_init(amc_vu_t *v) { amc_memset(v, 0, sizeof *v); }
void amc_vu_step(amc_vu_t *v, double rms, double ref) {
  double db = rms > 0.00003 ? AMC_MAX(-60.0, 20.0 * amc_log10(rms)) : -60.0, target = amc_clamp(db - ref, -20.0, 3.0);
  v->vel = (v->vel + (target - v->needle) * 0.08) * 0.82;                 /* spring and damper: 300 ms integration */
  v->needle += v->vel; v->peak_led = target >= 0.5;
}

/* ───────────── Int16 meter ───────────── */
void amc_s16meter_init(amc_s16meter_t *m, int ch, double clip_db) {
  double lim;
  amc_memset(m, 0, sizeof *m);
  m->ch = ch < 1 ? 1 : ch > AMC_S16_MAX_CH ? AMC_S16_MAX_CH : ch;
  lim = 32768.0 * amc_lin_of(amc_clamp(clip_db, -12.0, 0.0)); lim = (double)(long long)lim == lim ? lim : (double)(long long)lim + 1.0;   /* ceil */
  m->limit = (int32_t)amc_clamp(lim, 1.0, 32767.0);
}
void amc_s16meter_push(amc_s16meter_t *m, const int16_t *x, size_t samples) {
  size_t n = samples / (size_t)m->ch * (size_t)m->ch, i; int c = 0;
  for (i = 0; i < n; i++) {
    int32_t v = x[i], a = v < 0 ? -v : v;
    if (a > m->peak[c]) m->peak[c] = a;
    m->sum_sq[c] += (double)v * (double)v;
    if (a >= m->limit) { m->clips++; if (++m->run[c] == 3) { m->runs++; m->clip_ch[c] = 1; } } else m->run[c] = 0;
    if (a >= 32767) m->clip_ch[c] = 1;
    if (++c == m->ch) c = 0;
  }
  m->frames += (uint32_t)(n / (size_t)m->ch);
}
void amc_s16meter_snapshot(amc_s16meter_t *m, amc_s16snap_t *o) {
  int c, any = 0;
  amc_memset(o, 0, sizeof *o);
  for (c = 0; c < m->ch; c++) {
    o->peak_db[c] = m->peak[c] > 0 ? 20.0 * amc_log10(m->peak[c] / 32768.0) : -90.0;
    o->rms_db[c] = (m->frames && m->sum_sq[c] > 0) ? 20.0 * amc_log10(amc_sqrt(m->sum_sq[c] / m->frames) / 32768.0) : -90.0;
    if (o->peak_db[c] < -90.0) o->peak_db[c] = -90.0;
    if (o->rms_db[c] < -90.0) o->rms_db[c] = -90.0;
    o->clip_ch[c] = m->clip_ch[c]; any |= m->clip_ch[c];
  }
  o->clip = m->clips; o->clip_runs = m->runs; o->frames = m->frames; o->clipping = any;
  amc_memset(m->peak, 0, sizeof m->peak); amc_memset(m->sum_sq, 0, sizeof m->sum_sq); amc_memset(m->clip_ch, 0, sizeof m->clip_ch);   /* the run counters carry over */
  m->frames = 0; m->clips = 0; m->runs = 0;
}

/* ───────────── mixer ───────────── */
void amc_mixer_set_gain_db(amc_mixer_t *m, int s, double db) {
  if (s < 0 || s >= m->n_strips) return;
  db = db > 10.0 ? 10.0 : db; m->strip[s].gain_db = db; m->strip[s].gain_lin = db <= -60.0 ? 0.0 : amc_lin_of(db);
}
void amc_mixer_set_pan(amc_mixer_t *m, int s, double pan) { if (s >= 0 && s < m->n_strips) m->strip[s].pan = amc_clamp(pan, -1.0, 1.0); }
void amc_mixer_set_mute(amc_mixer_t *m, int s, int on) { if (s >= 0 && s < m->n_strips) m->strip[s].mute = on != 0; }
void amc_mixer_set_solo(amc_mixer_t *m, int s, int on) { if (s >= 0 && s < m->n_strips) m->strip[s].solo = on != 0; }
void amc_mixer_set_master_db(amc_mixer_t *m, double db) { db = db > 10.0 ? 10.0 : db; m->master_db = db; m->master_lin = db <= -60.0 ? 0.0 : amc_lin_of(db); }
void amc_mixer_set_limiter(amc_mixer_t *m, int on, double ceiling_db, double release_ms) {
  m->limiter_on = on != 0; m->limiter_ceiling_lin = amc_lin_of(amc_clamp(ceiling_db, -24.0, 0.0)); m->limiter_release = release_ms > 1.0 ? release_ms : 1.0;
}
void amc_mixer_set_clip_db(amc_mixer_t *m, double db) { amc_clip_cfg_init(&m->clip, db); }
double amc_mixer_strip_peak(const amc_mixer_t *m, int s) { return (s >= 0 && s < m->n_strips) ? m->strip[s].meter.pk : 0.0; }
double amc_mixer_master_peak(const amc_mixer_t *m, int ch) { return m->master[ch ? 1 : 0].pk; }
void amc_mixer_init(amc_mixer_t *m, int strips, double fs) {
  int i;
  amc_memset(m, 0, sizeof *m);
  m->n_strips = strips < 1 ? 1 : strips > AMC_MAX_STRIPS ? AMC_MAX_STRIPS : strips; m->fs = fs;
  for (i = 0; i < m->n_strips; i++) { amc_meter_init(&m->strip[i].meter); m->strip[i].gain_lin = 1.0; }
  m->master_lin = 1.0; m->limiter_gain = 1.0; m->limiter_ceiling_lin = amc_lin_of(-0.5); m->limiter_release = 100.0;
  amc_meter_init(&m->master[0]); amc_meter_init(&m->master[1]);
  amc_clip_cfg_init(&m->clip, -0.1); amc_loudness_init(&m->loudness, fs);
}
void amc_mixer_process(amc_mixer_t *m, const float *const *in, size_t frames, float *out_l, float *out_r) {
  amc_window_t sw[AMC_MAX_STRIPS], mw[2]; int s, any_solo = 0; size_t done = 0;
  double rel = 1.0 - amc_pow(2.718281828459045, -1.0 / (m->limiter_release * 0.001 * m->fs));    /* one-pole release: time constant = limiter_release */
  for (s = 0; s < m->n_strips; s++) { amc_window_begin(&sw[s]); if (m->strip[s].solo) any_solo = 1; }
  amc_window_begin(&mw[0]); amc_window_begin(&mw[1]);
  while (done < frames) {
    size_t n = AMC_MIN((size_t)AMC_CHUNK, frames - done), i; float tmp[AMC_CHUNK];
    for (i = 0; i < n; i++) { out_l[done + i] = 0.0f; out_r[done + i] = 0.0f; }
    for (s = 0; s < m->n_strips; s++) {
      amc_strip_t *st = &m->strip[s]; double g = st->gain_lin, ang = (st->pan + 1.0) * AMC_PI * 0.25, gl = amc_cos(ang), gr = amc_sin(ang); int audible = !st->mute && (!any_solo || st->solo);
      for (i = 0; i < n; i++) tmp[i] = (float)((double)in[s][done + i] * g);                       /* post-fader signal: what the strip meter shows */
      amc_window_add(&sw[s], tmp, n, &m->clip);
      if (audible) for (i = 0; i < n; i++) { out_l[done + i] += (float)(tmp[i] * gl); out_r[done + i] += (float)(tmp[i] * gr); }
    }
    for (i = 0; i < n; i++) {
      double l = (double)out_l[done + i] * m->master_lin, r = (double)out_r[done + i] * m->master_lin;
      if (m->limiter_on) {
        double pk = AMC_MAX(l < 0 ? -l : l, r < 0 ? -r : r), need = pk > m->limiter_ceiling_lin ? m->limiter_ceiling_lin / pk : 1.0;
        if (need < m->limiter_gain) m->limiter_gain = need; else m->limiter_gain += (1.0 - m->limiter_gain) * rel;
        if (m->limiter_gain > need) m->limiter_gain = need;                                        /* instant attack: never above what this sample needs */
        l *= m->limiter_gain; r *= m->limiter_gain;
      }
      out_l[done + i] = (float)l; out_r[done + i] = (float)r;
    }
    amc_window_add(&mw[0], out_l + done, n, &m->clip); amc_window_add(&mw[1], out_r + done, n, &m->clip);
    amc_loudness_feed(&m->loudness, out_l + done, out_r + done, n);
    amc_phase(out_l + done, out_r + done, n, 100.0, 0, 0, &m->phase);
    done += n;
  }
  m->time_ms += (double)frames * 1000.0 / m->fs;
  for (s = 0; s < m->n_strips; s++) amc_meter_end(&m->strip[s].meter, &sw[s], m->time_ms, &m->clip);
  amc_meter_end(&m->master[0], &mw[0], m->time_ms, &m->clip); amc_meter_end(&m->master[1], &mw[1], m->time_ms, &m->clip);
}
