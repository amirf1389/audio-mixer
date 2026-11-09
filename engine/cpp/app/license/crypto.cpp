// See crypto.hpp. The P-256 arithmetic is plain 256-bit integers in four 64-bit limbs with double-and-add modular multiplication: slow
// (a verification takes a few milliseconds) but short, obviously correct and free of platform intrinsics; a licence is checked once at start.
#include "crypto.hpp"

#include <cstring>

namespace am::crypto {

// ---------------------------------------------------------------- SHA-256
namespace {
inline std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline std::uint32_t rotl(std::uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
const std::uint32_t K256[64] = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
std::vector<std::uint8_t> pad(const std::uint8_t* data, std::size_t n) {
  std::vector<std::uint8_t> m(data, data + n); m.push_back(0x80);
  while (m.size() % 64 != 56) m.push_back(0);
  const std::uint64_t bits = static_cast<std::uint64_t>(n) * 8; for (int i = 7; i >= 0; --i) m.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
  return m;
}
}  // namespace

std::array<std::uint8_t, 32> sha256(const std::uint8_t* data, std::size_t n) {
  std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  const std::vector<std::uint8_t> m = pad(data, n);
  for (std::size_t off = 0; off < m.size(); off += 64) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) w[i] = (std::uint32_t(m[off + 4 * i]) << 24) | (std::uint32_t(m[off + 4 * i + 1]) << 16) | (std::uint32_t(m[off + 4 * i + 2]) << 8) | m[off + 4 * i + 3];
    for (int i = 16; i < 64; ++i) { const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10); w[i] = w[i - 16] + s0 + w[i - 7] + s1; }
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
      const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25), ch = (e & f) ^ (~e & g), t1 = hh + S1 + ch + K256[i] + w[i];
      const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22), maj = (a & b) ^ (a & c) ^ (b & c), t2 = S0 + maj;
      hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }
  std::array<std::uint8_t, 32> out{}; for (int i = 0; i < 8; ++i) { out[4 * i] = std::uint8_t(h[i] >> 24); out[4 * i + 1] = std::uint8_t(h[i] >> 16); out[4 * i + 2] = std::uint8_t(h[i] >> 8); out[4 * i + 3] = std::uint8_t(h[i]); }
  return out;
}

// ---------------------------------------------------------------- SHA-1 / HMAC / TOTP
std::array<std::uint8_t, 20> sha1(const std::uint8_t* data, std::size_t n) {
  std::uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  const std::vector<std::uint8_t> m = pad(data, n);
  for (std::size_t off = 0; off < m.size(); off += 64) {
    std::uint32_t w[80];
    for (int i = 0; i < 16; ++i) w[i] = (std::uint32_t(m[off + 4 * i]) << 24) | (std::uint32_t(m[off + 4 * i + 1]) << 16) | (std::uint32_t(m[off + 4 * i + 2]) << 8) | m[off + 4 * i + 3];
    for (int i = 16; i < 80; ++i) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
      std::uint32_t f, k;
      if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; } else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; } else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; } else { f = b ^ c ^ d; k = 0xCA62C1D6; }
      const std::uint32_t t = rotl(a, 5) + f + e + k + w[i]; e = d; d = c; c = rotl(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }
  std::array<std::uint8_t, 20> out{}; for (int i = 0; i < 5; ++i) { out[4 * i] = std::uint8_t(h[i] >> 24); out[4 * i + 1] = std::uint8_t(h[i] >> 16); out[4 * i + 2] = std::uint8_t(h[i] >> 8); out[4 * i + 3] = std::uint8_t(h[i]); }
  return out;
}

std::array<std::uint8_t, 20> hmacSha1(const std::uint8_t* key, std::size_t keyLen, const std::uint8_t* msg, std::size_t msgLen) {
  std::uint8_t k[64] = {0};
  if (keyLen > 64) { const auto h = sha1(key, keyLen); std::memcpy(k, h.data(), 20); } else std::memcpy(k, key, keyLen);
  std::vector<std::uint8_t> inner(64 + msgLen); for (int i = 0; i < 64; ++i) inner[static_cast<std::size_t>(i)] = k[i] ^ 0x36; std::memcpy(inner.data() + 64, msg, msgLen);
  const auto ih = sha1(inner.data(), inner.size());
  std::uint8_t outer[84]; for (int i = 0; i < 64; ++i) outer[i] = k[i] ^ 0x5c; std::memcpy(outer + 64, ih.data(), 20);
  return sha1(outer, 84);
}

std::uint32_t totp(const std::vector<std::uint8_t>& secret, std::int64_t unixSeconds, int digits, int stepSeconds) {
  const std::uint64_t counter = static_cast<std::uint64_t>(unixSeconds / (stepSeconds > 0 ? stepSeconds : 30));
  std::uint8_t c[8]; for (int i = 0; i < 8; ++i) c[i] = static_cast<std::uint8_t>(counter >> (8 * (7 - i)));
  const auto h = hmacSha1(secret.data(), secret.size(), c, 8);
  const int o = h[19] & 0x0f;
  const std::uint32_t bin = ((std::uint32_t(h[o]) & 0x7f) << 24) | (std::uint32_t(h[o + 1]) << 16) | (std::uint32_t(h[o + 2]) << 8) | h[o + 3];
  std::uint32_t mod = 1; for (int i = 0; i < digits; ++i) mod *= 10;
  return bin % mod;
}
bool verifyTotp(const std::vector<std::uint8_t>& secret, std::uint32_t code, std::int64_t unixSeconds, int digits, int stepSeconds, int window) {
  for (int w = -window; w <= window; ++w) if (totp(secret, unixSeconds + static_cast<std::int64_t>(w) * stepSeconds, digits, stepSeconds) == code) return true;
  return false;
}

// ---------------------------------------------------------------- encodings
std::string hex(const std::uint8_t* p, std::size_t n, bool upper) { static const char* lo = "0123456789abcdef"; static const char* up = "0123456789ABCDEF"; const char* a = upper ? up : lo; std::string s; for (std::size_t i = 0; i < n; ++i) { s += a[p[i] >> 4]; s += a[p[i] & 15]; } return s; }

bool base64urlDecode(const std::string& s, std::vector<std::uint8_t>& out) {
  out.clear(); std::uint32_t acc = 0; int bits = 0;
  for (char ch : s) {
    int v;
    if (ch >= 'A' && ch <= 'Z') v = ch - 'A'; else if (ch >= 'a' && ch <= 'z') v = ch - 'a' + 26; else if (ch >= '0' && ch <= '9') v = ch - '0' + 52;
    else if (ch == '-' || ch == '+') v = 62; else if (ch == '_' || ch == '/') v = 63; else if (ch == '=' || ch == '\n' || ch == '\r' || ch == ' ') continue; else return false;
    acc = (acc << 6) | static_cast<std::uint32_t>(v); bits += 6;
    if (bits >= 8) { bits -= 8; out.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xff)); }
  }
  return true;
}
std::string base64url(const std::uint8_t* p, std::size_t n) {
  static const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"; std::string s; std::uint32_t acc = 0; int bits = 0;
  for (std::size_t i = 0; i < n; ++i) { acc = (acc << 8) | p[i]; bits += 8; while (bits >= 6) { bits -= 6; s += a[(acc >> bits) & 63]; } }
  if (bits) s += a[(acc << (6 - bits)) & 63];
  return s;
}
bool base32Decode(const std::string& s, std::vector<std::uint8_t>& out) {
  out.clear(); std::uint32_t acc = 0; int bits = 0;
  for (char ch : s) {
    int v;
    if (ch >= 'A' && ch <= 'Z') v = ch - 'A'; else if (ch >= 'a' && ch <= 'z') v = ch - 'a'; else if (ch >= '2' && ch <= '7') v = ch - '2' + 26; else if (ch == '=' || ch == ' ' || ch == '-') continue; else return false;
    acc = (acc << 5) | static_cast<std::uint32_t>(v); bits += 5;
    if (bits >= 8) { bits -= 8; out.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xff)); }
  }
  return !out.empty();
}

// ---------------------------------------------------------------- P-256
namespace {
struct U256 { std::uint64_t v[4] = {0, 0, 0, 0}; };   // little-endian limbs
bool isZero(const U256& a) { return !(a.v[0] | a.v[1] | a.v[2] | a.v[3]); }
int cmp(const U256& a, const U256& b) { for (int i = 3; i >= 0; --i) { if (a.v[i] != b.v[i]) return a.v[i] < b.v[i] ? -1 : 1; } return 0; }
bool addRaw(U256& r, const U256& a, const U256& b) { std::uint64_t carry = 0; for (int i = 0; i < 4; ++i) { const std::uint64_t s = a.v[i] + carry; carry = s < carry ? 1 : 0; const std::uint64_t t = s + b.v[i]; carry += t < s ? 1 : 0; r.v[i] = t; } return carry != 0; }
bool subRaw(U256& r, const U256& a, const U256& b) { std::uint64_t borrow = 0; for (int i = 0; i < 4; ++i) { const std::uint64_t d = a.v[i] - b.v[i]; const std::uint64_t b2 = (a.v[i] < b.v[i]) ? 1 : 0; const std::uint64_t e = d - borrow; borrow = b2 + ((d < borrow) ? 1 : 0); r.v[i] = e; } return borrow != 0; }
U256 addMod(const U256& a, const U256& b, const U256& m) { U256 r; const bool c = addRaw(r, a, b); if (c || cmp(r, m) >= 0) { U256 t; subRaw(t, r, m); return t; } return r; }
U256 subMod(const U256& a, const U256& b, const U256& m) { U256 r; if (subRaw(r, a, b)) { U256 t; addRaw(t, r, m); return t; } return r; }
U256 mulMod(const U256& a, const U256& b, const U256& m) {   // double-and-add, 256 steps
  U256 r;
  for (int i = 255; i >= 0; --i) { r = addMod(r, r, m); if ((b.v[i / 64] >> (i % 64)) & 1) r = addMod(r, a, m); }
  return r;
}
U256 powMod(const U256& a, const U256& e, const U256& m) { U256 r; r.v[0] = 1; for (int i = 255; i >= 0; --i) { r = mulMod(r, r, m); if ((e.v[i / 64] >> (i % 64)) & 1) r = mulMod(r, a, m); } return r; }
U256 fromHex(const char* h) { U256 r; std::size_t n = std::strlen(h); for (std::size_t i = 0; i < n; ++i) { const char c = h[n - 1 - i]; const std::uint64_t d = c >= '0' && c <= '9' ? std::uint64_t(c - '0') : c >= 'a' && c <= 'f' ? std::uint64_t(c - 'a' + 10) : std::uint64_t(c - 'A' + 10); r.v[i / 16] |= d << (4 * (i % 16)); } return r; }
bool fromBytes(const std::vector<std::uint8_t>& b, U256& r) { if (b.size() != 32) return false; r = U256(); for (int i = 0; i < 32; ++i) r.v[3 - i / 8] |= std::uint64_t(b[static_cast<std::size_t>(i)]) << (8 * (7 - i % 8)); return true; }
U256 fromBytes32(const std::uint8_t* b) { U256 r; for (int i = 0; i < 32; ++i) r.v[3 - i / 8] |= std::uint64_t(b[i]) << (8 * (7 - i % 8)); return r; }

const U256 P = fromHex("ffffffff00000001000000000000000000000000ffffffffffffffffffffffff");
const U256 N = fromHex("ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551");
const U256 B = fromHex("5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b");
const U256 GX = fromHex("6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296");
const U256 GY = fromHex("4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5");
U256 inv(const U256& a, const U256& m) { U256 e, two; two.v[0] = 2; subRaw(e, m, two); return powMod(a, e, m); }   // a^(m-2): Fermat, m is prime

struct Pt { U256 x, y, z; bool inf = true; };   // Jacobian
Pt dbl(const Pt& p) {
  if (p.inf || isZero(p.y)) return Pt();
  const U256 z2 = mulMod(p.z, p.z, P), y2 = mulMod(p.y, p.y, P);
  const U256 s = mulMod(mulMod(p.x, y2, P), U256{{4, 0, 0, 0}}, P);                                            // 4 x y^2
  const U256 m = mulMod(mulMod(subMod(p.x, z2, P), addMod(p.x, z2, P), P), U256{{3, 0, 0, 0}}, P);               // 3 (x - z^2)(x + z^2)  (a = -3)
  Pt r; r.inf = false;
  r.x = subMod(mulMod(m, m, P), addMod(s, s, P), P);
  r.y = subMod(mulMod(m, subMod(s, r.x, P), P), mulMod(mulMod(y2, y2, P), U256{{8, 0, 0, 0}}, P), P);
  r.z = mulMod(addMod(p.y, p.y, P), p.z, P);
  return r;
}
Pt add(const Pt& p, const Pt& q) {
  if (p.inf) return q;
  if (q.inf) return p;
  const U256 z1s = mulMod(p.z, p.z, P), z2s = mulMod(q.z, q.z, P);
  const U256 u1 = mulMod(p.x, z2s, P), u2 = mulMod(q.x, z1s, P);
  const U256 s1 = mulMod(p.y, mulMod(z2s, q.z, P), P), s2 = mulMod(q.y, mulMod(z1s, p.z, P), P);
  if (cmp(u1, u2) == 0) { if (cmp(s1, s2) == 0) return dbl(p); return Pt(); }
  const U256 h = subMod(u2, u1, P), r = subMod(s2, s1, P), h2 = mulMod(h, h, P), h3 = mulMod(h2, h, P), u1h2 = mulMod(u1, h2, P);
  Pt o; o.inf = false;
  o.x = subMod(subMod(mulMod(r, r, P), h3, P), addMod(u1h2, u1h2, P), P);
  o.y = subMod(mulMod(r, subMod(u1h2, o.x, P), P), mulMod(s1, h3, P), P);
  o.z = mulMod(mulMod(p.z, q.z, P), h, P);
  return o;
}
Pt mul(const Pt& p, const U256& k) { Pt r; for (int i = 255; i >= 0; --i) { r = dbl(r); if ((k.v[i / 64] >> (i % 64)) & 1) r = add(r, p); } return r; }
bool onCurve(const U256& x, const U256& y) {   // y^2 == x^3 - 3x + b
  const U256 y2 = mulMod(y, y, P), x3 = mulMod(mulMod(x, x, P), x, P), three = U256{{3, 0, 0, 0}};
  const U256 rhs = addMod(subMod(x3, mulMod(x, three, P), P), B, P);
  return cmp(y2, rhs) == 0;
}
}  // namespace

bool ecdsaP256Verify(const std::vector<std::uint8_t>& xb, const std::vector<std::uint8_t>& yb, const std::uint8_t* msg, std::size_t msgLen, const std::vector<std::uint8_t>& sig) {
  U256 qx, qy; if (!fromBytes(xb, qx) || !fromBytes(yb, qy) || sig.size() != 64) return false;
  if (cmp(qx, P) >= 0 || cmp(qy, P) >= 0 || !onCurve(qx, qy)) return false;
  const U256 r = fromBytes32(sig.data()), s = fromBytes32(sig.data() + 32);
  if (isZero(r) || isZero(s) || cmp(r, N) >= 0 || cmp(s, N) >= 0) return false;
  const auto h = sha256(msg, msgLen); U256 e = fromBytes32(h.data()); if (cmp(e, N) >= 0) { U256 t; subRaw(t, e, N); e = t; }
  const U256 w = inv(s, N), u1 = mulMod(e, w, N), u2 = mulMod(r, w, N);
  Pt G; G.inf = false; G.x = GX; G.y = GY; G.z.v[0] = 1;
  Pt Q; Q.inf = false; Q.x = qx; Q.y = qy; Q.z.v[0] = 1;
  const Pt R = add(mul(G, u1), mul(Q, u2));
  if (R.inf) return false;
  const U256 zi = inv(R.z, P), zi2 = mulMod(zi, zi, P); U256 x = mulMod(R.x, zi2, P);
  if (cmp(x, N) >= 0) { U256 t; subRaw(t, x, N); x = t; }
  return cmp(x, r) == 0;
}

}  // namespace am::crypto
