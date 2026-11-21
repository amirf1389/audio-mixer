// SIMD dispatch (see include/audiomixer/simd.hpp): what the processor runs, read once; the kernels of the best level it runs.
#include "audiomixer/simd.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
#define AM_X86 1
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

namespace am::simd {
const Kernels& baseKernels();
#ifdef AM_SIMD_AVX2
const Kernels& avx2Kernels();
#endif

namespace {
#ifdef AM_X86
void cpuidex(int leaf, int sub, unsigned r[4]) {
#if defined(_MSC_VER) && !defined(__clang__)
  int v[4]; __cpuidex(v, leaf, sub); for (int i = 0; i < 4; ++i) r[i] = static_cast<unsigned>(v[i]);
#else
  __cpuid_count(leaf, sub, r[0], r[1], r[2], r[3]);
#endif
}
unsigned long long xgetbv0() {   // which register states the operating system saves on a thread switch (XCR0)
#if defined(_MSC_VER) && !defined(__clang__)
  return _xgetbv(0);
#else
  unsigned lo, hi; __asm__ volatile(".byte 0x0f, 0x01, 0xd0" : "=a"(lo), "=d"(hi) : "c"(0)); return (static_cast<unsigned long long>(hi) << 32) | lo;
#endif
}
#endif
CpuFeatures detect() {
  CpuFeatures f;
#ifdef AM_X86
  unsigned r[4]; cpuidex(0, 0, r); const unsigned maxLeaf = r[0];
  cpuidex(1, 0, r); f.sse2 = (r[3] >> 26) & 1; f.sse41 = (r[2] >> 19) & 1; const bool osxsave = (r[2] >> 27) & 1, avx = (r[2] >> 28) & 1, fma = (r[2] >> 12) & 1;
  const unsigned long long xcr = osxsave ? xgetbv0() : 0; const bool ymm = (xcr & 0x6) == 0x6, zmm = (xcr & 0xe6) == 0xe6;   // SSE + AVX state; + opmask / ZMM
  f.avx = avx && ymm; f.fma = fma && ymm;
  if (maxLeaf >= 7) { cpuidex(7, 0, r); f.avx2 = f.avx && ((r[1] >> 5) & 1); f.avx512f = zmm && ((r[1] >> 16) & 1); }
#elif defined(__aarch64__) || defined(_M_ARM64)
  f.neon = true;
#endif
  return f;
}
const Kernels* pick() {
  const char* e = std::getenv("AM_SIMD"); const std::string want = e ? e : "";
  const bool forceBase = want == "base" || want == "sse2" || want == "neon" || want == "scalar";
#ifdef AM_SIMD_AVX2
  if (!forceBase && cpu().avx2 && cpu().fma) return &avx2Kernels();
#else
  (void)forceBase;
#endif
  return &baseKernels();
}
std::atomic<const Kernels*>& active() { static std::atomic<const Kernels*> k{pick()}; return k; }
}  // namespace

const CpuFeatures& cpu() { static const CpuFeatures f = detect(); return f; }
const Kernels& kernels() { return *active().load(std::memory_order_relaxed); }
const Kernels* kernelsFor(Level l) {
  if (l == baseKernels().level) return &baseKernels();
#ifdef AM_SIMD_AVX2
  if (l == Level::Avx2 && cpu().avx2 && cpu().fma) return &avx2Kernels();
#endif
  return nullptr;
}
bool setLevel(Level l) { const Kernels* k = kernelsFor(l); if (!k) return false; active().store(k, std::memory_order_relaxed); return true; }
Level level() { return kernels().level; }
const char* levelName() { return kernels().name; }
const char* describe() {
  static thread_local std::string s;
  const Kernels& b = baseKernels(); s = levelName();
  if (level() != b.level) s += std::string(" (baseline ") + b.name + (cpu().avx512f ? "; AVX-512 present, not used)" : ")");
  else if (kernelsFor(Level::Avx2)) s += " (forced; AVX2+FMA available)";
  return s.c_str();
}

}  // namespace am::simd
