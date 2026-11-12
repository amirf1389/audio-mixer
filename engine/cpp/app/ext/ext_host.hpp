// Shared by every extension: runs a program function on a thread, collects what it prints, and
// implements the amfb_ext_* calls. The program function gets a quit flag it must watch.
#pragma once
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "ext.h"

namespace amfb_ext {

using ProgramFn = int (*)(int argc, char** argv, std::atomic<bool>& quit, std::function<void(const std::string&)> say);

class Host {
 public:
  explicit Host(const char* name, ProgramFn fn) : name_(name), fn_(fn) {}
  const char* name() const { return name_; }

  int start(int argc, const char* const* argv) {
    stop();
    args_.assign(argv, argv + argc);
    argv_.clear(); for (auto& a : args_) argv_.push_back(a.data()); argv_.push_back(nullptr);
    quit_ = false; running_ = true; code_ = 0;
    std::thread t([this] {
      code_ = fn_(int(args_.size()), argv_.data(), quit_, [this](const std::string& s) { say(s); });
      running_ = false;
    });
    th_ = std::move(t);
    // a program that ends at once (a --list, a bad option) reports its code now
    for (int i = 0; i < 20 && running_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (!running_) { th_.join(); return code_ == 0 ? 0 : code_; }
    return 0;
  }
  int running() const { return running_ ? 1 : 0; }
  void stop() { quit_ = true; if (th_.joinable()) th_.join(); running_ = false; }
  const char* status() { std::lock_guard<std::mutex> l(m_); std::snprintf(statusBuf_, sizeof statusBuf_, "%s", last_.c_str()); return statusBuf_; }
  const char* output() { std::lock_guard<std::mutex> l(m_); outBuf_ = out_; out_.clear(); return outBuf_.c_str(); }
  void say(const std::string& s) {
    std::lock_guard<std::mutex> l(m_);
    out_ += s; if (!s.empty() && s.back() != '\n') out_ += '\n';
    if (out_.size() > 65536) out_.erase(0, out_.size() - 65536);
    std::string line = s; while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    const auto nl = line.rfind('\n'); if (nl != std::string::npos) line = line.substr(nl + 1);
    if (!line.empty()) last_ = line.substr(0, 200);
  }

 private:
  const char* name_; ProgramFn fn_;
  std::vector<std::string> args_; std::vector<char*> argv_;
  std::thread th_; std::atomic<bool> quit_{false}, running_{false}; int code_ = 0;
  std::mutex m_; std::string out_, last_, outBuf_; char statusBuf_[201] = {};
};

// printf-style helper for the program functions
inline std::string fmt(const char* f, ...) {
  char b[1024]; va_list ap; va_start(ap, f); std::vsnprintf(b, sizeof b, f, ap); va_end(ap); return b;
}

}  // namespace amfb_ext

// Put this once in each extension: defines the C calls and a main() for the stand-alone program.
#define AMFB_EXTENSION(NAME, FN)                                                                       \
  static amfb_ext::Host g_host(NAME, FN);                                                              \
  extern "C" {                                                                                         \
  AMFB_EXT_EXPORT const char* amfb_ext_name(void) { return g_host.name(); }                            \
  AMFB_EXT_EXPORT int amfb_ext_start(int argc, const char* const* argv) { return g_host.start(argc, argv); } \
  AMFB_EXT_EXPORT int amfb_ext_running(void) { return g_host.running(); }                              \
  AMFB_EXT_EXPORT const char* amfb_ext_status(void) { return g_host.status(); }                        \
  AMFB_EXT_EXPORT const char* amfb_ext_output(void) { return g_host.output(); }                        \
  AMFB_EXT_EXPORT void amfb_ext_stop(void) { g_host.stop(); }                                          \
  }
