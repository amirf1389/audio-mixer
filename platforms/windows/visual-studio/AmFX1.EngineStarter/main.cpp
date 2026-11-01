// A first program against the AmF X1 audio engine: makes a 440 Hz tone and prints what the engine measures.
#include "audiomixer/engine.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

int main() {
    const double fs = 48000.0;
    const std::size_t n = 4800;
    std::vector<float> left(n), right(n);
    for (std::size_t i = 0; i < n; ++i) {
        const float v = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979 * 440.0 * static_cast<double>(i) / fs));
        left[i] = v;
        right[i] = v;
    }
    const auto phase = am::phaseCorrelation(left.data(), right.data(), n);
    const auto scope = am::scopeSweep(left.data(), n, fs, am::ScopeConfig{});
    std::printf("phase correlation %.3f, peak %.3f, scope frequency %.1f Hz\n", phase.corr, phase.peak, scope.freq);
    return 0;
}
