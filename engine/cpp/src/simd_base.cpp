// The baseline SIMD kernels (see include/audiomixer/simd.hpp): SSE2 on x86-64 (every x86-64 has it), NEON on arm64, plain scalar
// code elsewhere. Compiled with the build's own flags, so they run on every processor the build targets.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "audiomixer/simd.hpp"
#if defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
#include <emmintrin.h>
#define AM_V_SSE2 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define AM_V_NEON 1
#endif

namespace am::simd {
namespace {
#if defined(AM_V_SSE2)
constexpr Level kLevel = Level::Sse2; constexpr const char* kName = "SSE2";
struct V {
  using T = __m128d; using M = __m128d; static constexpr std::size_t L = 2;
  static T load(const double* p) { return _mm_loadu_pd(p); }
  static void store(double* p, T v) { _mm_storeu_pd(p, v); }
  static T set(double v) { return _mm_set1_pd(v); }
  static T add(T a, T b) { return _mm_add_pd(a, b); }
  static T sub(T a, T b) { return _mm_sub_pd(a, b); }
  static T mul(T a, T b) { return _mm_mul_pd(a, b); }
  static T div(T a, T b) { return _mm_div_pd(a, b); }
  static T fma(T a, T b, T c) { return _mm_add_pd(_mm_mul_pd(a, b), c); }   // two roundings, as the scalar code
  static T fnma(T a, T b, T c) { return _mm_sub_pd(c, _mm_mul_pd(a, b)); }
  static T shiftIn(T y, double x) { return _mm_unpacklo_pd(_mm_set_sd(x), y); }
  static double last(T y) { return _mm_cvtsd_f64(_mm_unpackhi_pd(y, y)); }
  static M andm(M a, M b) { return _mm_and_pd(a, b); }
  static T gt(T a, T b) { return _mm_max_pd(a, b); }                          // a > b ? a : b (unordered: b)
  static T absv(T a) { return _mm_and_pd(a, _mm_castsi128_pd(_mm_set1_epi64x(0x7fffffffffffffffll))); }
  static M cmpgt(T a, T b) { return _mm_cmpgt_pd(a, b); }
  static M cmplt(T a, T b) { return _mm_cmplt_pd(a, b); }
  static M cmple(T a, T b) { return _mm_cmple_pd(a, b); }
  static T sel(M m, T a, T b) { return _mm_or_pd(_mm_and_pd(m, a), _mm_andnot_pd(m, b)); }
  static T loadf(const float* p) { return _mm_cvtps_pd(_mm_castsi128_ps(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(p)))); }
  static void storef(float* p, T v) { _mm_storel_epi64(reinterpret_cast<__m128i*>(p), _mm_castps_si128(_mm_cvtpd_ps(v))); }
  static void accf(float* p, T v) { const __m128 cur = _mm_castsi128_ps(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(p))); _mm_storel_epi64(reinterpret_cast<__m128i*>(p), _mm_castps_si128(_mm_add_ps(cur, _mm_cvtpd_ps(v)))); }
  static double hsum(T v) { return _mm_cvtsd_f64(_mm_add_sd(v, _mm_unpackhi_pd(v, v))); }
  static T expField(T x) { const __m128i e = _mm_and_si128(_mm_srli_epi64(_mm_castpd_si128(x), 52), _mm_set1_epi64x(0x7ff)); return _mm_sub_pd(_mm_castsi128_pd(_mm_or_si128(e, _mm_set1_epi64x(0x4330000000000000ll))), _mm_set1_pd(4503599627370496.0)); }
  static T mant1(T x) { return _mm_castsi128_pd(_mm_or_si128(_mm_and_si128(_mm_castpd_si128(x), _mm_set1_epi64x(0x000fffffffffffffll)), _mm_set1_epi64x(0x3ff0000000000000ll))); }
  static T trunc(T x) { return _mm_cvtepi32_pd(_mm_cvttpd_epi32(x)); }      // |x| <= 1000 here
  static T pow2i(T fl) { return _mm_castsi128_pd(_mm_slli_epi64(_mm_castpd_si128(_mm_add_pd(fl, _mm_set1_pd(4503599627370496.0 + 1023.0))), 52)); }
};
#elif defined(AM_V_NEON)
constexpr Level kLevel = Level::Neon; constexpr const char* kName = "NEON";
struct V {
  using T = float64x2_t; using M = uint64x2_t; static constexpr std::size_t L = 2;
  static T load(const double* p) { return vld1q_f64(p); }
  static void store(double* p, T v) { vst1q_f64(p, v); }
  static T set(double v) { return vdupq_n_f64(v); }
  static T add(T a, T b) { return vaddq_f64(a, b); }
  static T sub(T a, T b) { return vsubq_f64(a, b); }
  static T mul(T a, T b) { return vmulq_f64(a, b); }
  static T div(T a, T b) { return vdivq_f64(a, b); }
  static T fma(T a, T b, T c) { return vfmaq_f64(c, a, b); }                  // every arm64 has a fused multiply-add
  static T fnma(T a, T b, T c) { return vfmsq_f64(c, a, b); }
  static T shiftIn(T y, double x) { return vzip1q_f64(vdupq_n_f64(x), y); }
  static double last(T y) { return vgetq_lane_f64(y, 1); }
  static M andm(M a, M b) { return vandq_u64(a, b); }
  static T gt(T a, T b) { return vbslq_f64(vcgtq_f64(a, b), a, b); }
  static T absv(T a) { return vabsq_f64(a); }
  static M cmpgt(T a, T b) { return vcgtq_f64(a, b); }
  static M cmplt(T a, T b) { return vcltq_f64(a, b); }
  static M cmple(T a, T b) { return vcleq_f64(a, b); }
  static T sel(M m, T a, T b) { return vbslq_f64(m, a, b); }
  static T loadf(const float* p) { return vcvt_f64_f32(vld1_f32(p)); }
  static void storef(float* p, T v) { vst1_f32(p, vcvt_f32_f64(v)); }
  static void accf(float* p, T v) { vst1_f32(p, vadd_f32(vld1_f32(p), vcvt_f32_f64(v))); }
  static double hsum(T v) { return vgetq_lane_f64(v, 0) + vgetq_lane_f64(v, 1); }
  static T expField(T x) { return vcvtq_f64_u64(vandq_u64(vshrq_n_u64(vreinterpretq_u64_f64(x), 52), vdupq_n_u64(0x7ff))); }
  static T mant1(T x) { return vreinterpretq_f64_u64(vorrq_u64(vandq_u64(vreinterpretq_u64_f64(x), vdupq_n_u64(0x000fffffffffffffull)), vdupq_n_u64(0x3ff0000000000000ull))); }
  static T trunc(T x) { return vrndq_f64(x); }
  static T pow2i(T fl) { return vreinterpretq_f64_s64(vshlq_n_s64(vaddq_s64(vcvtq_s64_f64(fl), vdupq_n_s64(1023)), 52)); }
};
#else
constexpr Level kLevel = Level::Scalar; constexpr const char* kName = "scalar";
struct V {
  using T = double; using M = bool; static constexpr std::size_t L = 1;
  static T load(const double* p) { return *p; }
  static void store(double* p, T v) { *p = v; }
  static T set(double v) { return v; }
  static T add(T a, T b) { return a + b; }
  static T sub(T a, T b) { return a - b; }
  static T mul(T a, T b) { return a * b; }
  static T div(T a, T b) { return a / b; }
  static T fma(T a, T b, T c) { return a * b + c; }
  static T fnma(T a, T b, T c) { return c - a * b; }
  static T shiftIn(T, double x) { return x; }
  static double last(T y) { return y; }
  static M andm(M a, M b) { return a && b; }
  static T gt(T a, T b) { return a > b ? a : b; }
  static T absv(T a) { return a < 0 ? -a : a; }
  static M cmpgt(T a, T b) { return a > b; }
  static M cmplt(T a, T b) { return a < b; }
  static M cmple(T a, T b) { return a <= b; }
  static T sel(M m, T a, T b) { return m ? a : b; }
  static T loadf(const float* p) { return static_cast<double>(*p); }
  static void storef(float* p, T v) { *p = static_cast<float>(v); }
  static void accf(float* p, T v) { *p += static_cast<float>(v); }
  static double hsum(T v) { return v; }
  static T expField(T x) { std::uint64_t b; std::memcpy(&b, &x, sizeof b); return static_cast<double>((b >> 52) & 0x7ff); }
  static T mant1(T x) { std::uint64_t b; std::memcpy(&b, &x, sizeof b); b = (b & 0x000fffffffffffffull) | 0x3ff0000000000000ull; std::memcpy(&x, &b, sizeof x); return x; }
  static T trunc(T x) { return static_cast<double>(static_cast<std::int64_t>(x)); }
  static T pow2i(T fl) { const std::uint64_t b = static_cast<std::uint64_t>(static_cast<std::int64_t>(fl) + 1023) << 52; double s; std::memcpy(&s, &b, sizeof s); return s; }
};
#endif
inline double sfma(double a, double b, double c) { return a * b + c; }
#include "simd_kernels.inl"
}  // namespace
const Kernels& baseKernels() { return kTable; }
}  // namespace am::simd
