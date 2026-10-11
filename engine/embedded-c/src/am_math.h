/* The few math functions of the core: libm, or (AMC_NO_LIBM) built in for freestanding builds (WebAssembly without a C library). */
#ifndef AM_MATH_H
#define AM_MATH_H
#ifndef AMC_NO_LIBM
#include <math.h>
#include <string.h>
#define amc_memset memset
#define amc_memcpy memcpy
#define amc_memmove memmove
#define amc_sqrt sqrt
#define amc_log10 log10
#define amc_pow pow
#define amc_tan tan
#define amc_sin sin
#define amc_cos cos
#define AMC_INF INFINITY
#else
#include <stddef.h>
#include <stdint.h>
static void *amc_memset(void *d, int v, size_t n) { unsigned char *p = (unsigned char *)d; while (n--) *p++ = (unsigned char)v; return d; }
static void *amc_memcpy(void *d, const void *s, size_t n) { unsigned char *p = (unsigned char *)d; const unsigned char *q = (const unsigned char *)s; while (n--) *p++ = *q++; return d; }
static void *amc_memmove(void *d, const void *s, size_t n) { unsigned char *p = (unsigned char *)d; const unsigned char *q = (const unsigned char *)s; if (p < q) while (n--) *p++ = *q++; else { p += n; q += n; while (n--) *--p = *--q; } return d; }
#define AMC_INF __builtin_inf()
#define amc_sqrt __builtin_sqrt
static double amc_ln(double x) {           /* x > 0: x = m * 2^e, ln m = 2 atanh((m-1)/(m+1)) */
  union { double d; uint64_t u; } v; int e; double m, t, t2, s;
  v.d = x; e = (int)((v.u >> 52) & 0x7ff) - 1023; v.u = (v.u & 0x000fffffffffffffULL) | 0x3ff0000000000000ULL; m = v.d;
  if (m > 1.4142135623730951) { m *= 0.5; e++; }
  t = (m - 1.0) / (m + 1.0); t2 = t * t;
  s = t * (1.0 + t2 * (1.0 / 3 + t2 * (1.0 / 5 + t2 * (1.0 / 7 + t2 * (1.0 / 9 + t2 * (1.0 / 11 + t2 * (1.0 / 13 + t2 * (1.0 / 15 + t2 * (1.0 / 17)))))))));
  return 2.0 * s + e * 0.6931471805599453;
}
static double amc_log10(double x) { return amc_ln(x) * 0.4342944819032518; }
static double amc_exp(double x) {          /* x = k ln2 + r, exp r by its series */
  double r, term = 1.0, sum = 1.0; int k, i; union { double d; uint64_t u; } v;
  k = (int)(x * 1.4426950408889634 + (x < 0 ? -0.5 : 0.5)); r = x - k * 0.6931471805599453;
  for (i = 1; i < 18; i++) { term *= r / i; sum += term; }
  v.u = (uint64_t)(k + 1023) << 52; return sum * v.d;
}
static double amc_pow(double b, double e) { return amc_exp(e * amc_ln(b)); }   /* b > 0 */
static double amc_red(double x) { double k = (double)(long long)(x * 0.15915494309189535 + (x < 0 ? -0.5 : 0.5)); return x - k * 6.283185307179586; }   /* into -pi..pi */
static double amc_sin(double x) { double t, s, x2; int i; x = amc_red(x); t = s = x; x2 = x * x; for (i = 1; i < 14; i++) { t *= -x2 / ((2 * i) * (2 * i + 1)); s += t; } return s; }
static double amc_cos(double x) { double t = 1.0, s = 1.0, x2; int i; x = amc_red(x); x2 = x * x; for (i = 1; i < 14; i++) { t *= -x2 / ((2 * i - 1) * (2 * i)); s += t; } return s; }
static double amc_tan(double x) { return amc_sin(x) / amc_cos(x); }          /* |x| < 1.5 (the filter frequencies are far below fs / 2) */
#endif
#endif
