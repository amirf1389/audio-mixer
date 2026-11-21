// The AVX2 + FMA kernels (see include/audiomixer/simd.hpp). The only file of the engine compiled for AVX2 (-mavx2 -mfma, /arch:AVX2);
// simd.cpp calls it only on a processor and an operating system that run AVX2 and FMA. Nothing here may be shared with other files
// (anonymous namespace; no engine or standard-library inline function: see simd_kernels.inl).
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <immintrin.h>
#include "audiomixer/simd.hpp"

namespace am::simd {
namespace {
constexpr Level kLevel = Level::Avx2; constexpr const char* kName = "AVX2+FMA";
struct V {
  using T = __m256d; using M = __m256d; static constexpr std::size_t L = 4;
  static T load(const double* p) { return _mm256_loadu_pd(p); }
  static void store(double* p, T v) { _mm256_storeu_pd(p, v); }
  static T set(double v) { return _mm256_set1_pd(v); }
  static T add(T a, T b) { return _mm256_add_pd(a, b); }
  static T sub(T a, T b) { return _mm256_sub_pd(a, b); }
  static T mul(T a, T b) { return _mm256_mul_pd(a, b); }
  static T div(T a, T b) { return _mm256_div_pd(a, b); }
  static T fma(T a, T b, T c) { return _mm256_fmadd_pd(a, b, c); }
  static T fnma(T a, T b, T c) { return _mm256_fnmadd_pd(a, b, c); }
  static T shiftIn(T y, double x) { return _mm256_blend_pd(_mm256_permute4x64_pd(y, 0x93), _mm256_set1_pd(x), 1); }   // {x, y0, y1, y2}
  static double last(T y) { const __m128d h = _mm256_extractf128_pd(y, 1); return _mm_cvtsd_f64(_mm_unpackhi_pd(h, h)); }
  static M andm(M a, M b) { return _mm256_and_pd(a, b); }
  static T gt(T a, T b) { return _mm256_max_pd(a, b); }                       // a > b ? a : b (unordered: b)
  static T absv(T a) { return _mm256_and_pd(a, _mm256_castsi256_pd(_mm256_set1_epi64x(0x7fffffffffffffffll))); }
  static M cmpgt(T a, T b) { return _mm256_cmp_pd(a, b, _CMP_GT_OQ); }
  static M cmplt(T a, T b) { return _mm256_cmp_pd(a, b, _CMP_LT_OQ); }
  static M cmple(T a, T b) { return _mm256_cmp_pd(a, b, _CMP_LE_OQ); }
  static T sel(M m, T a, T b) { return _mm256_blendv_pd(b, a, m); }
  static T loadf(const float* p) { return _mm256_cvtps_pd(_mm_loadu_ps(p)); }
  static void storef(float* p, T v) { _mm_storeu_ps(p, _mm256_cvtpd_ps(v)); }
  static void accf(float* p, T v) { _mm_storeu_ps(p, _mm_add_ps(_mm_loadu_ps(p), _mm256_cvtpd_ps(v))); }
  static double hsum(T v) { const __m128d s = _mm_add_pd(_mm256_castpd256_pd128(v), _mm256_extractf128_pd(v, 1)); return _mm_cvtsd_f64(_mm_add_sd(s, _mm_unpackhi_pd(s, s))); }
  static T expField(T x) { const __m256i e = _mm256_and_si256(_mm256_srli_epi64(_mm256_castpd_si256(x), 52), _mm256_set1_epi64x(0x7ff)); return _mm256_sub_pd(_mm256_castsi256_pd(_mm256_or_si256(e, _mm256_set1_epi64x(0x4330000000000000ll))), _mm256_set1_pd(4503599627370496.0)); }
  static T mant1(T x) { return _mm256_castsi256_pd(_mm256_or_si256(_mm256_and_si256(_mm256_castpd_si256(x), _mm256_set1_epi64x(0x000fffffffffffffll)), _mm256_set1_epi64x(0x3ff0000000000000ll))); }
  static T trunc(T x) { return _mm256_round_pd(x, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC); }
  static T pow2i(T fl) { return _mm256_castsi256_pd(_mm256_slli_epi64(_mm256_castpd_si256(_mm256_add_pd(fl, _mm256_set1_pd(4503599627370496.0 + 1023.0))), 52)); }
};
inline double sfma(double a, double b, double c) { return _mm_cvtsd_f64(_mm_fmadd_sd(_mm_set_sd(a), _mm_set_sd(b), _mm_set_sd(c))); }
#include "simd_kernels.inl"
}  // namespace
const Kernels& avx2Kernels() { return kTable; }
}  // namespace am::simd
