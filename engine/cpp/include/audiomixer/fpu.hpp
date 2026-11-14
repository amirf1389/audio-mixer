// DENORMALS OFF on the audio thread: as a reverb tail, a filter or a delay line decays towards zero its numbers become "denormal" (smaller
// than the smallest normal float), and x86 processors compute those up to 100x slower: a quiet tail can spike the CPU and drop out.
// Flush-to-zero + denormals-are-zero (SSE MXCSR bits 15 and 6) turn them into exact zeros instead; inaudible (below -1500 dBFS).
// Called at the top of every audio callback (cheap: two instructions), because the setting is per thread.
#pragma once
#if defined(__SSE__) || defined(_M_X64) || defined(_M_AMD64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#include <xmmintrin.h>
#define AM_HAS_MXCSR 1
#endif

namespace am {
inline void denormalsOff() {
#ifdef AM_HAS_MXCSR
  _mm_setcsr(_mm_getcsr() | 0x8040u);
#endif
}
// for code that runs on someone else's thread (a plug-in inside a DAW): denormals off for this call, the host's setting restored after
class ScopedNoDenormals {
 public:
#ifdef AM_HAS_MXCSR
  ScopedNoDenormals() : saved_(_mm_getcsr()) { _mm_setcsr(saved_ | 0x8040u); }
  ~ScopedNoDenormals() { _mm_setcsr(saved_); }
 private:
  unsigned saved_;
#else
  ScopedNoDenormals() {}
#endif
 public:
  ScopedNoDenormals(const ScopedNoDenormals&) = delete; ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;
};
}  // namespace am
