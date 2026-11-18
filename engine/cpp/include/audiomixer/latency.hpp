// LOW LATENCY SOUND DETECTION: how long sound takes through the open sound card, said in plain words, with what would lower it.
//   REPORTED: what the driver itself says (ASIO getLatencies; WASAPI the stream latency plus its buffer; DirectSound its buffer periods),
//   or, where a driver says nothing, an estimate from the block size. ROUND TRIP: measured: a click goes out, comes back in through a loop
//   cable (or the microphone near the speaker) and the delay is found by cross-correlation, so it includes the converters and the cable.
//   LEVELS: under 10 ms LOW LATENCY (live monitoring, playing an instrument through the mixer); 10-20 ms OK (playback, light monitoring);
//   over 20 ms HIGH (fine for listening, too slow to monitor yourself).
#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace am::latency {

enum class Level { Low, Ok, High };

struct Input {
  std::string backend;          // "asio", "wasapi", "dsound", "alsa", "coreaudio", "null"
  bool exclusive = false;       // WASAPI exclusive
  double rate = 48000;
  int blockFrames = 256;
  long inFrames = 0, outFrames = 0;   // as the driver reports them (0 = it does not)
  bool hasInput = true;         // an input is open (round trip and monitoring need one)
  bool bluetoothOut = false;    // the output is a Bluetooth device
  bool hybrid = false, fastCores = true;   // a hybrid processor, and whether FAST CORES FOR AUDIO is on
  unsigned long long xruns = 0; double maxBlockUs = 0;   // the engine's own counters: dropouts, worst block time
};

struct Report {
  double inMs = 0, outMs = 0, blockMs = 0, totalMs = 0;   // totalMs: input + output (what a monitored voice is late by)
  bool reported = false;        // the driver said it (true) or it is our estimate (false)
  Level level = Level::High;
  std::string levelName;        // "LOW LATENCY", "OK", "HIGH"
  std::string verdict;          // one sentence
  std::vector<std::string> advice;   // what would lower it or make it steadier, most useful first
};

/** the plain-words report and the advice for one open device (pure: no device is touched) */
Report assess(const Input& in);
Level levelOf(double totalMs);
const char* levelName(Level l);

/** ROUND TRIP: where `sent` (the click as played) appears in `recorded` (the input over the same time), in frames; `confidence` 0..1
 *  (how clearly the click stands out: under 0.3 the result is not trusted). -1 when it is not found. */
long findDelay(const float* sent, std::size_t nSent, const float* recorded, std::size_t nRec, double* confidence);
/** the click the test plays: a short band-limited burst that correlates sharply (deterministic) */
std::vector<float> testClick(double rate);

}  // namespace am::latency
