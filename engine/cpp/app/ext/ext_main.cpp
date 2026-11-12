// The stand-alone program of an extension: start it, print what it says, stop on Ctrl+C.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>
#include "ext.h"

namespace { std::atomic<bool> g_quit{false}; void onSignal(int) { g_quit = true; } }

int main(int argc, char** argv) {
  std::signal(SIGINT, onSignal); std::signal(SIGTERM, onSignal);
  const int rc = amfb_ext_start(argc, const_cast<const char* const*>(argv));
  auto flush = [] { const char* o = amfb_ext_output(); if (o && *o) { std::fputs(o, stdout); std::fflush(stdout); } };
  while (amfb_ext_running() && !g_quit) { flush(); std::this_thread::sleep_for(std::chrono::milliseconds(100)); }
  amfb_ext_stop(); flush();
  return rc;
}
