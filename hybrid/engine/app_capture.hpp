// Windows only: captures what one program plays (and the programs it started) with the system's own
// process-loopback API (Windows 10 2004 and newer) and writes it, stereo float at the mixer's rate,
// into a ring. It listens to the sound a program sends to the speakers; it opens no file and no
// stream of that program, and content the system marks as protected arrives silent.
#pragma once
#ifdef _WIN32
#include <atomic>
#include <string>
#include <thread>
#include "../core/spsc_ring.hpp"

namespace amfb {

class AppCapture {
 public:
  AppCapture(std::wstring exeName, SpscRing<float>& ring, double rate) : exe_(std::move(exeName)), ring_(ring), rate_(rate) {}
  ~AppCapture() { stop(); }
  void start();
  void stop();
  bool capturing() const { return capturing_.load(); }
  const std::wstring& exe() const { return exe_; }

 private:
  void run();
  bool session(unsigned long pid);            // true = the program ended normally, look for it again
  std::wstring exe_; SpscRing<float>& ring_; double rate_;
  std::thread th_; std::atomic<bool> quit_{false}, capturing_{false};
  void* wake_ = nullptr;
};

}  // namespace amfb
#endif
