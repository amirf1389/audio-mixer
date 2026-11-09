// Self-contained crypto for the licence check: SHA-256, HMAC-SHA1 (TOTP, RFC 6238), base64url / base32, and ECDSA P-256 signature
// VERIFICATION (public-key operations only, no signing, no secret handling, so timing is not a concern). Written out here so the native
// console has no OpenSSL dependency on any platform; checked against the Node tooling's output in test/license_test.cpp.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace am::crypto {

std::array<std::uint8_t, 32> sha256(const std::uint8_t* data, std::size_t n);
std::array<std::uint8_t, 20> sha1(const std::uint8_t* data, std::size_t n);
std::array<std::uint8_t, 20> hmacSha1(const std::uint8_t* key, std::size_t keyLen, const std::uint8_t* msg, std::size_t msgLen);
std::string hex(const std::uint8_t* p, std::size_t n, bool upper = true);

bool base64urlDecode(const std::string& s, std::vector<std::uint8_t>& out);   // padding optional; also accepts standard base64 characters
std::string base64url(const std::uint8_t* p, std::size_t n);
bool base32Decode(const std::string& s, std::vector<std::uint8_t>& out);      // RFC 4648 alphabet, case-insensitive, padding and spaces ignored

// TOTP (RFC 6238, HMAC-SHA1, 30 s steps): the 6- or 8-digit code for `unixSeconds`. verifyTotp accepts the step before and after (clock drift).
std::uint32_t totp(const std::vector<std::uint8_t>& secret, std::int64_t unixSeconds, int digits = 6, int stepSeconds = 30);
bool verifyTotp(const std::vector<std::uint8_t>& secret, std::uint32_t code, std::int64_t unixSeconds, int digits = 6, int stepSeconds = 30, int window = 1);

// ECDSA over P-256 with SHA-256, signature as 64 bytes r||s (IEEE P1363, what Node's dsaEncoding 'ieee-p1363' and WebCrypto produce).
// The public key is the JWK's x and y (32 bytes each, base64url). Returns false for any malformed input.
bool ecdsaP256Verify(const std::vector<std::uint8_t>& x, const std::vector<std::uint8_t>& y, const std::uint8_t* msg, std::size_t msgLen, const std::vector<std::uint8_t>& sig);

}  // namespace am::crypto
