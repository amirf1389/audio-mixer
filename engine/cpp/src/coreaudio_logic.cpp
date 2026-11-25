// Core Audio negotiation: the pure part (see coreaudio_logic.hpp). Built on every system.
#include "audiomixer/coreaudio_logic.hpp"

#include <algorithm>
#include <cmath>

namespace am::ca {

double pickSampleRate(double wanted, const std::vector<RateRange>& ranges, double current) {
  if (!(wanted > 0) || ranges.empty()) return current > 0 ? current : wanted;
  double best = 0, bestDist = 1e300;
  for (const RateRange& r : ranges) {
    if (wanted >= r.min && wanted <= r.max) return wanted;
    for (const double c : {r.min, r.max}) {
      if (!(c > 0)) continue;
      const double d = std::fabs(c - wanted);
      if (d < bestDist || (d == bestDist && c > best)) { best = c; bestDist = d; }
    }
  }
  return best > 0 ? best : current;
}

bool shouldSetRate(double current, double target, bool settable, bool aggregate) {
  if (!(target > 0) || std::fabs(current - target) < 0.5) return false;
  return settable && !aggregate;
}

unsigned clampBufferFrames(unsigned wanted, unsigned minFrames, unsigned maxFrames) {
  const unsigned w = wanted ? wanted : 256u;
  if (maxFrames < minFrames || maxFrames == 0) return w;
  return std::clamp(w, minFrames, maxFrames);
}

int clampChannels(int wanted, int available) { return available <= 0 || wanted <= 0 ? 0 : std::min(wanted, available); }

bool isAggregateTransport(std::uint32_t t) { return t == fourcc("grup") || t == fourcc("fgrp"); }

std::string kindOfTransport(std::uint32_t t, bool inputOnly) {
  if (t == fourcc("blue") || t == fourcc("blea")) return "bluetooth";
  if (t == fourcc("usb ") || t == fourcc("1394") || t == fourcc("tbol") || t == fourcc("pci ") || t == fourcc("avb ")) return "interface";
  if (isAggregateTransport(t)) return "interface";
  if (t == fourcc("hdmi") || t == fourcc("dprt") || t == fourcc("airp")) return "digital";
  if (t == fourcc("virt")) return "wrapper";
  if (t == fourcc("bltn")) return inputOnly ? "microphone" : "speakers";
  return "unknown";
}

DevicePlan planDevices(const std::string& inId, const std::string& outId, int inChannels, int outChannels, const std::string& defaultIn, const std::string& defaultOut) {
  DevicePlan p;
  p.wantIn = inChannels > 0 && inId != "-"; p.wantOut = outChannels > 0 && outId != "-";
  if (p.wantIn) p.inId = inId.empty() ? defaultIn : inId;
  if (p.wantOut) p.outId = outId.empty() ? defaultOut : outId;
  if (!p.wantIn && !p.wantOut) { p.err = "neither an input nor an output was asked for"; return p; }
  if ((p.wantIn && p.inId.empty()) || (p.wantOut && p.outId.empty())) { p.err = "no such audio device (or no default device)"; return p; }
  if (p.wantIn && p.wantOut && p.inId != p.outId) {
    // both defaults differ (the usual MacBook case: built-in microphone + built-in speakers): run the OUTPUT only rather than refuse,
    // unless the caller named the input itself
    if (inId.empty()) { p.wantIn = false; p.inId.clear(); }
    else { p.err = "the input and the output are different devices: make an aggregate device in Audio MIDI Setup and choose it for both"; return p; }
  }
  p.ok = true; return p;
}

std::string fingerprint(const std::vector<std::string>& ids) { std::string f; for (const std::string& s : ids) { f += "ca:"; f += s; f += '|'; } return f; }

}  // namespace am::ca
