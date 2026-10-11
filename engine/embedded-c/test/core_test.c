/* Runs engine/tests/vectors.txt against the embedded C core:  core_test <vectors.txt>   (exit 0 = every number within tolerance) */
#include "../include/am_core.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI2 (2.0 * 3.14159265358979323846)
static int failures = 0, checked = 0, cases = 0;

static void gen(const char *spec, double fs, size_t n, int ch, float *out) {
  char buf[128]; char *p[8]; int np = 0; size_t i; char *t;
  strncpy(buf, spec, sizeof buf - 1); buf[sizeof buf - 1] = 0;
  for (t = strtok(buf, ":"); t && np < 8; t = strtok(NULL, ":")) p[np++] = t;
#define NUM(k) atof(p[k])
  for (i = 0; i < n; i++) out[i] = 0;
  if (!strcmp(p[0], "silence")) return;
  if (!strcmp(p[0], "sine")) for (i = 0; i < n; i++) out[i] = (float)(NUM(2) * sin(PI2 * NUM(1) * i / fs));
  else if (!strcmp(p[0], "sinelr")) for (i = 0; i < n; i++) out[i] = (float)((ch ? NUM(4) : NUM(2)) * sin(PI2 * (ch ? NUM(3) : NUM(1)) * i / fs));
  else if (!strcmp(p[0], "inv")) for (i = 0; i < n; i++) out[i] = (float)((ch ? -NUM(2) : NUM(2)) * sin(PI2 * NUM(1) * i / fs));
  else if (!strcmp(p[0], "square")) for (i = 0; i < n; i++) out[i] = (float)(NUM(2) * (sin(PI2 * NUM(1) * i / fs) >= 0 ? 1 : -1));
  else if (!strcmp(p[0], "clipsine")) for (i = 0; i < n; i++) { double v = NUM(2) * sin(PI2 * NUM(1) * i / fs); out[i] = (float)(v > 1 ? 1 : v < -1 ? -1 : v); }
  else if (!strcmp(p[0], "noise")) { uint32_t s = (uint32_t)(long long)NUM(1) + (uint32_t)ch; for (i = 0; i < n; i++) { s = s * 1664525u + 1013904223u; out[i] = (float)(NUM(2) * ((s >> 8) / 16777216.0 * 2 - 1)); } }
  else if (!strcmp(p[0], "alt")) for (i = 0; i < n; i++) out[i] = (float)((((long long)floor(i / (NUM(4) * fs))) % 2 ? NUM(3) : NUM(2)) * sin(PI2 * NUM(1) * i / fs));
  else if (!strcmp(p[0], "fade")) for (i = 0; i < n; i++) { double tt = i / (NUM(4) * fs); if (tt > 1) tt = 1; out[i] = (float)((NUM(2) + (NUM(3) - NUM(2)) * tt) * sin(PI2 * NUM(1) * i / fs)); }
  else { fprintf(stderr, "unknown signal %s\n", spec); exit(2); }
}
static void gen16(const char *spec, double fs, size_t frames, int ch, int16_t *out) {
  float *x = (float *)malloc(frames * sizeof(float)); int c; size_t i;
  for (c = 0; c < ch; c++) { gen(spec, fs, frames, c & 1, x); for (i = 0; i < frames; i++) { double v = (double)x[i] * 32767; out[i * ch + c] = (int16_t)(long)(v + (v >= 0 ? 0.5 : -0.5)); } }
  free(x);
}

static void check(const char *id, const char *what, double got, double want, double tol, int rel) {
  double d = fabs(got - want), lim = rel ? tol * (fabs(want) > 1 ? fabs(want) : 1) : tol;
  checked++;
  if (isinf(want) || isinf(got)) { if (got != want) { failures++; printf("FAIL %s %s: got %g want %g\n", id, what, got, want); } return; }
  if (!(d <= lim)) { failures++; printf("FAIL %s %s: got %.9g want %.9g (tolerance %g)\n", id, what, got, want, lim); }
}

static int split(char *s, char **tok, int max) { int n = 0; char *t; for (t = strtok(s, " \t\r\n"); t && n < max; t = strtok(NULL, " \t\r\n")) tok[n++] = t; return n; }

static void do_lufs(char **h, double *e) {
  double fs = atof(h[2]); int block = atoi(h[3]); size_t n = (size_t)(fs * atof(h[4]) + 0.5), i;
  float *l = (float *)malloc(n * sizeof(float)), *r = (float *)malloc(n * sizeof(float)); amc_loudness_t *m = (amc_loudness_t *)malloc(sizeof *m);
  gen(h[5], fs, n, 0, l); gen(h[5], fs, n, 1, r); amc_loudness_init(m, fs);
  for (i = 0; i < n; i += (size_t)block) amc_loudness_feed(m, l + i, r + i, (size_t)block < n - i ? (size_t)block : n - i);
  check(h[1], "momentary", m->momentary, e[0], 0.02, 0); check(h[1], "short-term", m->short_term, e[1], 0.02, 0); check(h[1], "integrated", m->integrated, e[2], 0.02, 0);
  check(h[1], "lra", m->lra, e[3], 0.02, 0); check(h[1], "true peak", m->true_peak, e[4], 0.01, 0);
  free(l); free(r); free(m);
}
static void do_meter(char **h, double *e) {
  double fs = atof(h[2]); int win = atoi(h[3]); size_t n = (size_t)(fs * atof(h[4]) + 0.5), i; float *x = (float *)malloc(n * sizeof(float)); amc_meter_t m; amc_clip_cfg_t c; double t = 0;
  gen(h[6], fs, n, 0, x); amc_clip_cfg_init(&c, atof(h[5])); amc_meter_init(&m);
  for (i = 0; i + (size_t)win <= n; i += (size_t)win) { t += win * 1000.0 / fs; amc_meter_push(&m, x + i, (size_t)win, t, &c); }
  check(h[1], "pk", m.pk, e[0], 1e-6, 1); check(h[1], "rms", m.rms, e[1], 1e-6, 1); check(h[1], "hold", m.hold, e[2], 1e-6, 1); check(h[1], "clipN", m.clip_n, e[3], 0, 0);
  check(h[1], "latched", m.latched, e[4], 0, 0); check(h[1], "clipPk", m.clip_pk, e[5], 1e-6, 1);
  free(x);
}
static void do_phase(char **h, double *e) {
  size_t n = (size_t)atoi(h[2]); float *l = (float *)malloc(n * sizeof(float)), *r = (float *)malloc(n * sizeof(float)); amc_phase_t p;
  gen(h[6], 48000, n, 0, l); gen(h[6], 48000, n, 1, r); amc_phase(l, r, n, atof(h[3]), atoi(h[4]), atoi(h[5]), &p);
  check(h[1], "corr", p.corr, e[0], 1e-5, 0); check(h[1], "peak", p.peak, e[1], 1e-6, 1); check(h[1], "rmsL", p.rms_l, e[2], 1e-6, 1); check(h[1], "rmsR", p.rms_r, e[3], 1e-6, 1);
  free(l); free(r);
}
static void do_scope(char **h, double *e) {
  double fs = atof(h[2]); size_t n = (size_t)atoi(h[3]); float *a = (float *)malloc(n * sizeof(float)); amc_scope_cfg_t c; amc_scope_t s;
  gen(h[7], fs, n, 0, a); c.timebase_ms = atof(h[4]); c.trigger_db = atof(h[5]); c.rising = atoi(h[6]); amc_scope_calc(a, n, fs, &c, &s);
  check(h[1], "N", (double)s.n, e[0], 0, 0); check(h[1], "start", (double)s.start, e[1], 0, 0); check(h[1], "triggered", s.triggered, e[2], 0, 0); check(h[1], "p2p", s.p2p, e[3], 1e-6, 1);
  check(h[1], "rms", s.rms, e[4], 1e-6, 1); check(h[1], "crest", s.crest_db, e[5], 1e-4, 0); check(h[1], "freq", s.freq, e[6], 1e-6, 1);
  free(a);
}
static void do_vu(char **h, double *e) {
  amc_vu_t v; int i, sa = atoi(h[4]), sb = atoi(h[6]); amc_vu_init(&v);
  for (i = 0; i < sa; i++) amc_vu_step(&v, atof(h[3]), atof(h[2]));
  for (i = 0; i < sb; i++) amc_vu_step(&v, atof(h[5]), atof(h[2]));
  check(h[1], "needle", v.needle, e[0], 1e-9, 1); check(h[1], "vel", v.vel, e[1], 1e-9, 1); check(h[1], "led", v.peak_led, e[2], 0, 0);
}
static void do_s16(char **h, double *e) {
  int ch = atoi(h[2]); size_t frames = (size_t)atoi(h[4]), half = frames / 2, a, b; int16_t *x = (int16_t *)malloc(frames * (size_t)ch * sizeof(int16_t)); amc_s16meter_t m; amc_s16snap_t s; int snap, c, k = 0;
  gen16(h[5], 48000, frames, ch, x); amc_s16meter_init(&m, ch, atof(h[3]));
  for (snap = 0; snap < 2; snap++) {
    a = snap ? half : 0; b = snap ? frames : half; amc_s16meter_push(&m, x + a * (size_t)ch, (b - a) * (size_t)ch); amc_s16meter_snapshot(&m, &s);
    for (c = 0; c < ch; c++) check(h[1], "peak", s.peak_db[c], e[k++], 0.06, 0);
    for (c = 0; c < ch; c++) check(h[1], "rms", s.rms_db[c], e[k++], 0.06, 0);
    check(h[1], "clip", s.clip, e[k++], 0, 0); check(h[1], "clipRuns", s.clip_runs, e[k++], 0, 0); check(h[1], "clipping", s.clipping, e[k++], 0, 0); check(h[1], "frames", s.frames, e[k++], 0, 0);
  }
  free(x);
}
static void do_mixer(char **h, FILE *f) {
  double fs = atof(h[2]); int blocks = atoi(h[3]), ns = atoi(h[8]), s, b, k = 0; size_t n = (size_t)blocks * 512, i; static amc_mixer_t m; char line[512], *tok[40], *spec[AMC_MAX_STRIPS]; float *in[AMC_MAX_STRIPS], *ol, *or_; double e[64], pkl = 0, pkr = 0, ssl = 0, ssr = 0; int ne;
  amc_mixer_init(&m, ns, fs); amc_mixer_set_master_db(&m, atof(h[4])); amc_mixer_set_limiter(&m, atoi(h[5]), atof(h[6]), atof(h[7]));
  for (s = 0; s < ns; s++) {
    int nt; if (!fgets(line, sizeof line, f)) exit(2); nt = split(line, tok, 40); (void)nt;
    spec[s] = (char *)malloc(strlen(tok[1]) + 1); strcpy(spec[s], tok[1]); in[s] = (float *)malloc(n * sizeof(float)); gen(spec[s], fs, n, 0, in[s]);
    amc_mixer_set_gain_db(&m, s, atof(tok[2])); amc_mixer_set_pan(&m, s, atof(tok[3])); amc_mixer_set_mute(&m, s, atoi(tok[4])); amc_mixer_set_solo(&m, s, atoi(tok[5]));
  }
  if (!fgets(line, sizeof line, f)) exit(2);
  { char *bar = strchr(line, '|'); ne = split(bar + 1, tok, 64); for (k = 0; k < ne; k++) e[k] = atof(tok[k]); }
  ol = (float *)malloc(n * sizeof(float)); or_ = (float *)malloc(n * sizeof(float));
  for (b = 0; b < blocks; b++) { const float *ptrs[AMC_MAX_STRIPS]; for (s = 0; s < ns; s++) ptrs[s] = in[s] + (size_t)b * 512; amc_mixer_process(&m, ptrs, 512, ol + (size_t)b * 512, or_ + (size_t)b * 512); }
  for (i = 0; i < n; i++) { if (fabs(ol[i]) > pkl) pkl = fabs(ol[i]); if (fabs(or_[i]) > pkr) pkr = fabs(or_[i]); ssl += (double)ol[i] * ol[i]; ssr += (double)or_[i] * or_[i]; }
  k = 0; check(h[1], "peak L", pkl, e[k++], 2e-4, 1); check(h[1], "peak R", pkr, e[k++], 2e-4, 1); check(h[1], "rms L", sqrt(ssl / n), e[k++], 2e-4, 1); check(h[1], "rms R", sqrt(ssr / n), e[k++], 2e-4, 1);
  for (s = 0; s < ns; s++) { check(h[1], "strip pk", m.strip[s].meter.pk, e[k++], 2e-4, 1); check(h[1], "strip rms", m.strip[s].meter.rms, e[k++], 2e-4, 1); check(h[1], "strip hold", m.strip[s].meter.hold, e[k++], 2e-4, 1); check(h[1], "strip clipN", m.strip[s].meter.clip_n, e[k++], 0, 0); }
  for (s = 0; s < 2; s++) { check(h[1], "master pk", m.master[s].pk, e[k++], 2e-4, 1); check(h[1], "master rms", m.master[s].rms, e[k++], 2e-4, 1); check(h[1], "master clipN", m.master[s].clip_n, e[k++], 0, 0); }
  
  check(h[1], "momentary", m.loudness.momentary, e[k++], 0.02, 0); check(h[1], "integrated", m.loudness.integrated, e[k++], 0.02, 0); check(h[1], "true peak", m.loudness.true_peak, e[k++], 0.01, 0);
  for (s = 0; s < ns; s++) { free(in[s]); free(spec[s]); } free(ol); free(or_);
}

int main(int argc, char **argv) {
  FILE *f = fopen(argc > 1 ? argv[1] : "vectors.txt", "r"); char line[1024], *tok[40], *bar; double e[64]; int nt, ne, i;
  if (!f) { fprintf(stderr, "cannot open the vectors file\n"); return 2; }
  while (fgets(line, sizeof line, f)) {
    if (line[0] == '#' || line[0] == '\n' || !strncmp(line, "strip ", 6) || !strncmp(line, "expect", 6)) continue;
    bar = strchr(line, '|'); ne = 0;
    if (bar) { *bar = 0; { char tmp[1024]; char *tt[64]; strcpy(tmp, bar + 1); ne = split(tmp, tt, 64); for (i = 0; i < ne; i++) e[i] = atof(tt[i]); } }
    nt = split(line, tok, 40); if (nt < 2) continue; cases++;
    if (!strcmp(tok[0], "lufs")) do_lufs(tok, e); else if (!strcmp(tok[0], "meter")) do_meter(tok, e); else if (!strcmp(tok[0], "phase")) do_phase(tok, e);
    else if (!strcmp(tok[0], "scope")) do_scope(tok, e); else if (!strcmp(tok[0], "vu")) do_vu(tok, e); else if (!strcmp(tok[0], "s16")) do_s16(tok, e); else if (!strcmp(tok[0], "mixer")) do_mixer(tok, f);
    else { fprintf(stderr, "unknown case %s\n", tok[0]); return 2; }
  }
  fclose(f);
  printf("embedded C core: %d cases, %d numbers checked, %d failed\n", cases, checked, failures);
  return failures ? 1 : 0;
}
