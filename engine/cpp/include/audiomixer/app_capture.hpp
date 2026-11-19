// APP CAPTURE: several programs at once, each as its own console input. Where APP SOUND (app_sound.hpp) makes ONE program the sound
// card's input, this captures up to eight programs beside whatever the sound card delivers: every program gets a slot, and a slot
// gives three inputs a strip can listen to (Runtime::kAppInBase): left, right, and both as one mono channel.
//
// Each slot reads its program through Windows' process loopback (Windows 10 2004 and later) on its own thread and hands the samples
// to the audio thread through a lock-free ring. The program plays on Windows' clock and the console on the sound card's, so the ring
// holds a small cushion (about 30 ms): when it runs empty the slot is silent until the cushion is back (counted as a gap), when it
// grows too far the oldest sound is dropped. It is the sound Windows plays, as the user hears it: nothing is decoded or copied from
// the service, and a protected stream may come through silent.
//
// A slot can also carry a TEST TONE instead of a program (tests, the self-test, a system without process loopback).
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "audiomixer/runtime.hpp"

namespace am::app {

class Capture final : public Runtime::NetInput {
 public:
  static constexpr int kSlots = Runtime::kAppSlots;
  Capture();
  ~Capture() override;
  /** capture the program `pid` (label: what the console shows) at the console's sample rate. Returns the slot (0 .. 7), or -1 with
   *  the reason: every slot taken, the program is already captured, or this system has no process loopback. The stream itself is
   *  opened on the slot's thread: info().state tells how it went. */
  int open(std::uint32_t pid, const std::string& label, double sampleRate, std::string& err, int wantSlot = -1);   // wantSlot: that slot when it is free (a program coming back to the strips that wait for it)
  /** a slot that carries a steady tone (hz) instead of a program */
  int openTest(const std::string& label, double hz, double sampleRate, std::string& err);
  void close(int slot);
  void closeAll();
  /** the console changed its sample rate: every program slot is opened again at the new rate (test tones follow by themselves) */
  void setSampleRate(double sampleRate);

  struct Info {
    bool used = false, test = false;
    std::uint32_t pid = 0; std::string label;
    std::string state;        // "opening" | "live" | "silent" (live, but the program plays nothing) | "failed"
    std::string error;        // why it failed (the program closed, Windows refused ...)
    double peak = 0;          // the loudest sample of the last blocks, 0 .. 1
    std::uint64_t gaps = 0;   // how often the cushion ran empty
  };
  Info info(int slot) const;
  int used() const;

  // the audio thread: channels = Runtime::kAppInputs; channel 2k / 2k + 1 = slot k left / right, 16 + k = slot k mono
  void pull(float* const* ch, int channels, std::size_t frames, double sampleRate) override;
  /** tests: a slot with no thread of its own, filled with feed() as a capture thread would fill it (interleaved L R) */
  int openFeed(const std::string& label, double sampleRate, std::string& err);
  std::size_t feed(int slot, const float* interleaved, std::size_t frames);
  static bool available();   // this system can capture a program (Windows 10 2004 and later)

 private:
  struct Slot;
  std::unique_ptr<Slot> s_[kSlots];
  void run(int slot);
  int freeSlot() const;
};

}  // namespace am::app
