// Integer <-> float sample conversion, shared by every backend (WASAPI exclusive, ASIO, DirectSound) so all drivers convert the same way:
//  * one scale both ways: 2^(N-1) (int16 / 32768, int24 / 8388608, int32 / 2147483648), so an integer sample that passes the engine
//    untouched comes out as the SAME integer (bit-exact for 16 and 24 bit; float32 holds 24 bits of mantissa exactly)
//  * rounding to nearest (not truncation toward zero, which adds a half-LSB bias and crossover distortion at low level)
//  * the product in double (a float multiply of a 24-bit mantissa by 2^23-1 needs 47 bits)
//  * clamped to the format's range: +1.0 -> 32767 / 8388607 / 2147483647, -1.0 -> the most negative value
// 32-bit integer devices: the engine computes in 32-bit float (24-bit mantissa + exponent: ~144 dB at any level, more on quiet signals);
// the lowest 8 bits of a loud int32 sample are below that and are not carried (that is the float32 mixer, not the conversion).
#pragma once
#include <cmath>
#include <cstdint>

namespace am::fmt {

inline float fromI16(std::int16_t v) { return static_cast<float>(v) * (1.0f / 32768.0f); }
inline float fromI24(std::int32_t v) { return static_cast<float>(v) * (1.0f / 8388608.0f); }                       // v: sign-extended 24-bit
inline float fromI32(std::int32_t v) { return static_cast<float>(static_cast<double>(v) * (1.0 / 2147483648.0)); }
inline std::int32_t unpack24(const std::uint8_t* p) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(p[0]) << 8 | static_cast<std::uint32_t>(p[1]) << 16 | static_cast<std::uint32_t>(p[2]) << 24) >> 8; }
inline void pack24(std::uint8_t* p, std::int32_t v) { p[0] = static_cast<std::uint8_t>(v); p[1] = static_cast<std::uint8_t>(v >> 8); p[2] = static_cast<std::uint8_t>(v >> 16); }

inline std::int64_t toInt(float x, double scale, std::int64_t lo, std::int64_t hi) {
  if (!(x == x)) return 0;   // NaN: silence, never a full-scale click
  const std::int64_t v = std::llrint(static_cast<double>(x) * scale); return v < lo ? lo : v > hi ? hi : v;
}
inline std::int16_t toI16(float x) { return static_cast<std::int16_t>(toInt(x, 32768.0, -32768, 32767)); }
inline std::int32_t toI24(float x) { return static_cast<std::int32_t>(toInt(x, 8388608.0, -8388608, 8388607)); }
inline std::int32_t toI32(float x) { return static_cast<std::int32_t>(toInt(x, 2147483648.0, -2147483647LL - 1, 2147483647)); }

}  // namespace am::fmt
