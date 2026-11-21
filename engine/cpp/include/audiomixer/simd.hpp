// SIMD KERNELS with RUNTIME DISPATCH: the engine's hot loops (biquad cascades, the compressor's dB maths, gain + peak, the meters, the
// pan sum, the 4x true-peak detector) exist once per instruction-set level, and the fastest level this processor runs is chosen once,
// at start-up, from CPUID (x86) or the architecture (arm64). The build itself stays generic (no -march): the binaries run on any
// x86-64, and only the AVX2 kernels' own file is compiled for AVX2 + FMA.
//   x86-64: SSE2 (every x86-64; the baseline) or AVX2 + FMA (Intel Haswell 2013+, AMD Excavator / Zen and later)
//   arm64 (Windows on ARM, Android arm64-v8a, Apple): NEON (always present)
//   anything else: plain scalar code
// The baseline kernels compute exactly what the scalar code did (the same operations in the same order; only the meter's sum of squares
// and the true-peak dot products are added in a different order). The AVX2 + FMA kernels round once per multiply-add, so they differ
// from the baseline in the last bits (well under 1e-6 relative; test/simd_test.cpp checks every kernel against the scalar reference).
// One level for the whole process: every thread (the FAST worker pool too) runs the same kernels, so multi-core output stays
// bit-identical to one thread. AM_SIMD=base (or sse2 / neon / scalar) in the environment forces the baseline, for comparisons.
// This header only declares: no inline function of the engine is compiled into the AVX2 file (see src/simd_kernels.inl).
#pragma once
#include <cstddef>
#include <cstdint>

namespace am::simd {

enum class Level : std::uint8_t { Scalar, Sse2, Neon, Avx2 };

// the compressor's settings and state, as the kernel reads and writes them (dsp::Compressor keeps one)
struct CompK {
  double thr = -20, ratio = 4, knee = 6, makeup = 0, att = 0.01, rel = 0.001, det = 0.05, kneeLin = 0.07;   // dB, dB, dB, dB, coefficients, linear
  double env = 0, gr = 0, gLinAt = 1e300, gLin = 1;                                                      // state
};

struct Kernels {
  Level level;
  const char* name;
  // biquad sections in series (each points at {b0, b1, b2, a1, a2, z1, z2}, transposed direct form II), in place
  void (*cascade)(double* const* sec, int count, double* x, std::size_t n);
  // the feed-forward compressor (dsp::Compressor::tick for every sample), in place
  void (*compressor)(CompK* k, double* x, std::size_t n);
  // out[i] = float(in[i] * g); returns the largest |in[i]| (0 for none; a NaN is not counted)
  double (*gainPeak)(const float* in, std::size_t n, double g, float* out);
  // a meter window: *pk = max(*pk, |x|), *ss += x^2, the clip run counters (a sample >= clipLin extends the run, any other ends it)
  void (*meter)(const float* x, std::size_t n, double clipLin, double* pk, double* ss, int* run, int* maxRun);
  // L[i] += float(t[i] * gl), R[i] += float(t[i] * gr)
  void (*panAdd)(const float* t, std::size_t n, double gl, double gr, float* L, float* R);
  // the 4x true-peak detector over a block: h = 24-sample history (each sample twice), *w its write index, c the 3 x 12 polyphase taps;
  // pk[i] = the peak around the sample kDelay (6) behind x[i], delayed[i] = that sample
  void (*truePeak)(double* h, int* w, const double* c, const double* x, std::size_t n, double* pk, double* delayed);
};

struct CpuFeatures { bool sse2 = false, sse41 = false, avx = false, avx2 = false, fma = false, avx512f = false, neon = false; };

const CpuFeatures& cpu();                  // what this processor (and its operating system) runs
const Kernels& kernels();                  // the active level (chosen once; setLevel changes it)
const Kernels* kernelsFor(Level l);        // nullptr when this processor or this build cannot run it
bool setLevel(Level l);                    // tests and benches: switch every caller to that level (false = not available); not while audio runs
Level level();
const char* levelName();                   // "AVX2+FMA", "SSE2", "NEON", "scalar"
const char* describe();                    // for the SYSTEM panel: "AVX2+FMA (baseline SSE2; AVX-512 present, not used)"

}  // namespace am::simd
