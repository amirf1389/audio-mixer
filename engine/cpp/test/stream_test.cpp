// PcmRing (app/pcm_ring.hpp), the lock-free ring between the audio thread and the pipes of --stream:
// order, wrap-around, full and empty, and one thread pushing while another pops (every sample arrives once, in order).
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include "pcm_ring.hpp"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

int main() {
  {
    am::PcmRing r(8);
    CHECK(r.capacity() == 8 && r.size() == 0 && r.space() == 8);
    const std::int16_t a[5] = {1, 2, 3, 4, 5};
    CHECK(r.push(a, 5) == 5 && r.size() == 5);
    std::int16_t b[8] = {};
    CHECK(r.pop(b, 3) == 3 && b[0] == 1 && b[2] == 3);
    const std::int16_t c[6] = {6, 7, 8, 9, 10, 11};
    CHECK(r.push(c, 6) == 6);                       // wraps around the end of the buffer
    CHECK(r.size() == 8 && r.space() == 0);
    CHECK(r.push(c, 1) == 0);                       // full: nothing more fits
    CHECK(r.pop(b, 8) == 8);
    const std::int16_t want[8] = {4, 5, 6, 7, 8, 9, 10, 11};
    for (int i = 0; i < 8; ++i) CHECK(b[i] == want[i]);
    CHECK(r.pop(b, 1) == 0 && r.size() == 0);       // empty
  }
  {
    // one producer, one consumer, a small ring: 200000 samples cross it in order
    am::PcmRing r(97);
    const int total = 200000;
    std::thread producer([&] {
      std::int16_t v = 0; int sent = 0;
      while (sent < total) { if (r.push(&v, 1) == 1) { ++v; ++sent; } }
    });
    std::int16_t expect = 0; int got = 0; bool ordered = true;
    std::vector<std::int16_t> buf(64);
    while (got < total) {
      const std::size_t n = r.pop(buf.data(), buf.size());
      for (std::size_t i = 0; i < n; ++i) { if (buf[i] != expect) ordered = false; ++expect; }
      got += static_cast<int>(n);
    }
    producer.join();
    CHECK(ordered && got == total);
  }
  std::printf(failures ? "stream_test: %d failure(s)\n" : "stream_test: ok\n", failures);
  return failures ? 1 : 0;
}
