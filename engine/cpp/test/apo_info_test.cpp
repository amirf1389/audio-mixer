// SYSTEM EFFECTS (APOs): the FX property store parsed from mock property data (no Windows needed)
#include "audiomixer/apo_info.hpp"

#include <cstdio>
#include <cstdlib>

namespace {
int fails = 0;
void expect(bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++fails; } }
}

int main() {
  using namespace am::apo;
  // a Realtek laptop with Dolby on top: stream + mode effects, composite endpoint list, enhancements on
  const std::map<std::string, std::string> props = {
    {"{A45C254E-DF1C-4EFD-8020-67D146A850E0},2", "Speakers (Realtek(R) Audio)"},
    {"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},5", "{AAAAAAAA-1111-2222-3333-444444444444}"},
    {"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},6", "{BBBBBBBB-1111-2222-3333-444444444444}"},
    {"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},15", "{62dc1a93-ae24-464c-a43e-452f824c4250}\n{CCCCCCCC-1111-2222-3333-444444444444}\nnot a clsid"},
  };
  const std::map<std::string, std::string> names = {
    {"{aaaaaaaa-1111-2222-3333-444444444444}", "Realtek Audio Effects Component"},
    {"BBBBBBBB-1111-2222-3333-444444444444", "Dolby Atmos APO"},
    {"cccccccc-1111-2222-3333-444444444444", "Nahimic mirroring APO"},
  };
  Endpoint e = fromProperties("{0.0.0.00000000}.{x}", false, props, names);
  expect(e.name == "Speakers (Realtek(R) Audio)", "friendly name");
  expect(e.effects.size() == 4, "four APOs, the non-CLSID text skipped");
  expect(!e.sysFxDisabled && e.effectsActive(), "enhancements active");
  const auto v = e.vendors();
  expect(v.size() == 4 && v[0] == "Realtek" && v[1] == "Dolby" && v[2] == "Nahimic" && v[3] == "Windows", "makers in order, Windows last");
  expect(e.effects[0].slot == "stream" && e.effects[1].slot == "mode", "slots");
  // the same endpoint with "Disable all enhancements" ticked
  auto off = props; off["{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5"] = "1";
  expect(fromProperties("x", false, off, names).sysFxDisabled && !fromProperties("x", false, off, names).effectsActive(), "Disable_SysFx = 1: not active");
  // only Windows' own APO: not reported as a maker's effect
  const Endpoint w = fromProperties("y", true, {{"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},5", "{62DC1A93-AE24-464C-A43E-452F824C4250}"}}, {});
  expect(w.capture && w.effects.size() == 1 && w.effects[0].vendor == "Windows" && !w.effectsActive(), "Windows-only: not active");
  expect(w.name == "y", "no name: the id");
  expect(vendorOf("", "DTS Sound APO") == "DTS" && vendorOf("", "Waves MaxxAudio") == "Waves" && vendorOf("", "something") == "", "vendor names");
#ifndef _WIN32
  expect(scan().empty(), "no APOs off Windows");
#endif
  std::printf("apo_info_test: %s\n", fails ? "FAILED" : "ok");
  return fails ? EXIT_FAILURE : EXIT_SUCCESS;
}
