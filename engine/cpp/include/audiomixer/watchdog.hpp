// WATCHDOG FAILOVER: the decision only, kept apart from any device so it can be tested. The audio thread counts its blocks; the watchdog
// is fed that count a few times a second. Blocks arriving: all is well. No new block for stallMs while the engine says it is running: the
// device has stalled (a driver that stopped calling, a USB device that hung): RESTART. At most maxTries restarts inside windowMs: after
// that it says GIVE UP once and waits, so a dead device is not restarted for ever. A new block clears the stall.
#pragma once
#include <cstdint>
#include <deque>

namespace am {

class Watchdog {
 public:
  enum class Action { None, Restart, GiveUp };
  std::int64_t stallMs = 3000, windowMs = 60000; int maxTries = 3;
  void reset() { known_ = false; gaveUp_ = false; tries_.clear(); }
  int restarts() const { return static_cast<int>(tries_.size()); }
  Action feed(bool running, std::uint64_t blocks, std::int64_t nowMs) {
    if (!running) { known_ = false; return Action::None; }   // stopped on purpose: nothing to watch
    if (!known_ || blocks != last_) { known_ = true; last_ = blocks; movedMs_ = nowMs; gaveUp_ = false; return Action::None; }
    if (nowMs - movedMs_ < stallMs) return Action::None;
    while (!tries_.empty() && nowMs - tries_.front() > windowMs) tries_.pop_front();
    if (static_cast<int>(tries_.size()) >= maxTries) { if (gaveUp_) return Action::None; gaveUp_ = true; return Action::GiveUp; }
    tries_.push_back(nowMs); movedMs_ = nowMs; return Action::Restart;   // the next stall is counted from this restart
  }
 private:
  bool known_ = false, gaveUp_ = false; std::uint64_t last_ = 0; std::int64_t movedMs_ = 0; std::deque<std::int64_t> tries_;
};

}  // namespace am
