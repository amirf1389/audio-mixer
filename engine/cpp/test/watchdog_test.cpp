// WATCHDOG FAILOVER: blocks arriving = nothing; a stall = one restart; three restarts a minute at most, then GIVE UP once.
#include <cstdio>

#include "audiomixer/watchdog.hpp"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

int main() {
  using A = am::Watchdog::Action; am::Watchdog w; std::int64_t t = 0; std::uint64_t b = 0;
  for (int i = 0; i < 100; ++i) { t += 100; b += 5; CHECK(w.feed(true, b, t) == A::None); }                     // ten seconds of sound: nothing
  for (int i = 0; i < 29; ++i) { t += 100; CHECK(w.feed(true, b, t) == A::None); }                             // 2.9 s without a block: not yet
  t += 100; CHECK(w.feed(true, b, t) == A::Restart && w.restarts() == 1);                                      // 3 s: restart
  t += 100; b += 5; CHECK(w.feed(true, b, t) == A::None);                                                      // it came back
  for (int k = 0; k < 2; ++k) { t += 3000; CHECK(w.feed(true, b, t) == A::Restart); }                          // stalls again, twice: restarts 2 and 3
  t += 3000; CHECK(w.feed(true, b, t) == A::GiveUp); t += 3000; CHECK(w.feed(true, b, t) == A::None);          // a fourth inside the minute: give up, said once
  t += 61000; CHECK(w.feed(true, b, t) == A::Restart);                                                         // a minute later it may try again
  CHECK(w.feed(false, b, t + 100) == A::None); t += 10000; CHECK(w.feed(false, b, t) == A::None);             // stopped on purpose: never a restart
  CHECK(w.feed(true, b, t + 100) == A::None);                                                                  // started again: the count begins afresh
  am::Watchdog z; CHECK(z.feed(true, 0, 0) == A::None); CHECK(z.feed(true, 0, 2999) == A::None); CHECK(z.feed(true, 0, 3000) == A::Restart);   // a device that never delivered a block
  if (g_fail) { std::fprintf(stderr, "watchdog_test: %d failure(s)\n", g_fail); return 1; }
  std::printf("watchdog_test: sound arriving = nothing, a 3 s stall = one restart, three a minute at most then GIVE UP once, a stop on purpose is left alone\nwatchdog_test: OK\n"); return 0;
}
