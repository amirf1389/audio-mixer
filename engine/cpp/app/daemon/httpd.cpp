#include "httpd.hpp"

#include <cstring>
#include <sstream>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
#define CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#define CLOSESOCK ::close
#endif

namespace am::httpd {
namespace {

void initSockets() {
#ifdef _WIN32
  static bool done = false; if (!done) { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); done = true; }
#endif
}
bool sendAll(int fd, const char* p, std::size_t n) { while (n) { const auto k = ::send(fd, p, static_cast<int>(n), 0); if (k <= 0) return false; p += k; n -= static_cast<std::size_t>(k); } return true; }
bool recvExact(int fd, unsigned char* p, std::size_t n) { while (n) { const auto k = ::recv(fd, reinterpret_cast<char*>(p), static_cast<int>(n), 0); if (k <= 0) return false; p += k; n -= static_cast<std::size_t>(k); } return true; }
std::string lower(std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
std::string trim(const std::string& s) { std::size_t a = 0, b = s.size(); while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a; while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b; return s.substr(a, b - a); }

// read a request head (up to 16 KB) and its Content-Length body (up to 64 KB)
bool readRequest(int fd, Request& r, std::string& err) {
  std::string head; unsigned char c;
  while (head.size() < 16384) { if (!recvExact(fd, &c, 1)) { err = "closed"; return false; } head += static_cast<char>(c); if (head.size() >= 4 && head.compare(head.size() - 4, 4, "\r\n\r\n") == 0) break; }
  if (head.size() < 4 || head.compare(head.size() - 4, 4, "\r\n\r\n") != 0) { err = "head too large"; return false; }
  std::istringstream is(head); std::string line; std::getline(is, line); line = trim(line);
  std::istringstream rl(line); std::string target, ver; rl >> r.method >> target >> ver;
  if (r.method.empty() || target.empty() || target[0] != '/') { err = "bad request line"; return false; }
  const std::size_t q = target.find('?'); r.path = target.substr(0, q); r.query = q == std::string::npos ? "" : target.substr(q + 1);
  while (std::getline(is, line)) { line = trim(line); if (line.empty()) break; const std::size_t k = line.find(':'); if (k == std::string::npos) continue; r.headers[lower(trim(line.substr(0, k)))] = trim(line.substr(k + 1)); }
  const std::string cl = r.header("content-length");
  if (!cl.empty()) { const long n = std::strtol(cl.c_str(), nullptr, 10); if (n < 0 || n > 65536) { err = "body too large"; return false; } r.body.resize(static_cast<std::size_t>(n)); if (n && !recvExact(fd, reinterpret_cast<unsigned char*>(&r.body[0]), static_cast<std::size_t>(n))) { err = "short body"; return false; } }
  return true;
}
std::string statusText(int s) { switch (s) { case 200: return "OK"; case 204: return "No Content"; case 400: return "Bad Request"; case 401: return "Unauthorized"; case 403: return "Forbidden"; case 404: return "Not Found"; case 405: return "Method Not Allowed"; case 413: return "Payload Too Large"; case 500: return "Internal Server Error"; default: return "OK"; } }
void writeResponse(int fd, const Response& r) {
  std::string h = "HTTP/1.1 " + std::to_string(r.status) + " " + statusText(r.status) + "\r\nContent-Type: " + r.type + "\r\nContent-Length: " + std::to_string(r.body.size()) + "\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n";
  for (const auto& kv : r.headers) h += kv.first + ": " + kv.second + "\r\n";
  h += "\r\n"; sendAll(fd, h.data(), h.size()); sendAll(fd, r.body.data(), r.body.size());
}

// SHA-1 (for the WebSocket accept key only)
struct Sha1 {
  std::uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0}; unsigned char buf[64]; std::size_t len = 0; std::uint64_t total = 0;
  static std::uint32_t rol(std::uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
  void block(const unsigned char* p) {
    std::uint32_t w[80]; for (int i = 0; i < 16; ++i) w[i] = (static_cast<std::uint32_t>(p[i * 4]) << 24) | (static_cast<std::uint32_t>(p[i * 4 + 1]) << 16) | (static_cast<std::uint32_t>(p[i * 4 + 2]) << 8) | p[i * 4 + 3];
    for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) { std::uint32_t f, k; if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; } else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; } else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; } else { f = b ^ c ^ d; k = 0xCA62C1D6; }
      const std::uint32_t t = rol(a, 5) + f + e + k + w[i]; e = d; d = c; c = rol(b, 30); b = a; a = t; }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }
  void update(const unsigned char* p, std::size_t n) { total += n; while (n) { const std::size_t take = std::min(n, 64 - len); std::memcpy(buf + len, p, take); len += take; p += take; n -= take; if (len == 64) { block(buf); len = 0; } } }
  void final(unsigned char out[20]) { const std::uint64_t bits = total * 8; unsigned char one = 0x80; update(&one, 1); unsigned char z = 0; while (len != 56) update(&z, 1); unsigned char L[8]; for (int i = 0; i < 8; ++i) L[i] = static_cast<unsigned char>(bits >> (56 - 8 * i)); update(L, 8); for (int i = 0; i < 5; ++i) { out[i * 4] = static_cast<unsigned char>(h[i] >> 24); out[i * 4 + 1] = static_cast<unsigned char>(h[i] >> 16); out[i * 4 + 2] = static_cast<unsigned char>(h[i] >> 8); out[i * 4 + 3] = static_cast<unsigned char>(h[i]); } }
};
std::string base64(const unsigned char* p, std::size_t n) {
  static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; std::string o;
  for (std::size_t i = 0; i < n; i += 3) { const unsigned v = (p[i] << 16) | (i + 1 < n ? p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0); o += T[(v >> 18) & 63]; o += T[(v >> 12) & 63]; o += i + 1 < n ? T[(v >> 6) & 63] : '='; o += i + 2 < n ? T[v & 63] : '='; }
  return o;
}

}  // namespace

std::string Request::header(const std::string& k) const { const auto it = headers.find(lower(k)); return it == headers.end() ? "" : it->second; }
std::string Request::param(const std::string& k) const {
  std::size_t i = 0;
  while (i < query.size()) { const std::size_t amp = query.find('&', i), end = amp == std::string::npos ? query.size() : amp; const std::string kv = query.substr(i, end - i); const std::size_t eq = kv.find('='); if (kv.substr(0, eq) == k) return eq == std::string::npos ? "" : kv.substr(eq + 1); i = end + 1; }
  return "";
}

std::string Server::sha1Base64(const std::string& s) { Sha1 h; h.update(reinterpret_cast<const unsigned char*>(s.data()), s.size()); unsigned char d[20]; h.final(d); return base64(d, 20); }

// ── WebSocket ──
bool WebSocket::send(const std::string& text) {
  if (!open_) return false;
  std::string f; f += static_cast<char>(0x81);
  const std::size_t n = text.size();
  if (n < 126) f += static_cast<char>(n); else if (n < 65536) { f += static_cast<char>(126); f += static_cast<char>(n >> 8); f += static_cast<char>(n & 255); } else { f += static_cast<char>(127); for (int i = 7; i >= 0; --i) f += static_cast<char>((static_cast<std::uint64_t>(n) >> (8 * i)) & 255); }
  f += text;
  std::lock_guard<std::mutex> g(w_); if (!sendAll(fd_, f.data(), f.size())) { open_ = false; return false; } return true;
}
bool WebSocket::read(std::string& text, int& opcode) {
  text.clear(); unsigned char h[2]; if (!recvExact(fd_, h, 2)) { open_ = false; return false; }
  opcode = h[0] & 0x0F; const bool masked = h[1] & 0x80; std::uint64_t n = h[1] & 0x7F;
  if (n == 126) { unsigned char e[2]; if (!recvExact(fd_, e, 2)) { open_ = false; return false; } n = (e[0] << 8) | e[1]; } else if (n == 127) { unsigned char e[8]; if (!recvExact(fd_, e, 8)) { open_ = false; return false; } n = 0; for (int i = 0; i < 8; ++i) n = (n << 8) | e[i]; }
  if (n > 1 << 20) { open_ = false; return false; }
  unsigned char mask[4] = {0, 0, 0, 0}; if (masked && !recvExact(fd_, mask, 4)) { open_ = false; return false; }
  text.resize(static_cast<std::size_t>(n)); if (n && !recvExact(fd_, reinterpret_cast<unsigned char*>(&text[0]), static_cast<std::size_t>(n))) { open_ = false; return false; }
  if (masked) for (std::size_t i = 0; i < text.size(); ++i) text[i] = static_cast<char>(static_cast<unsigned char>(text[i]) ^ mask[i & 3]);
  if (opcode == 9) { std::string pong; pong += static_cast<char>(0x8A); pong += static_cast<char>(text.size()); pong += text; std::lock_guard<std::mutex> g(w_); sendAll(fd_, pong.data(), pong.size()); }
  if (opcode == 8) { close(); return false; }
  return true;
}
// close: the close frame, then the socket is shut both ways, so a reader thread blocked on this connection returns at once even when the
// other side never answers the close (a phone that went to sleep, a dropped Wi-Fi): the handler can then join it and finish
void WebSocket::close() { if (open_.exchange(false)) { const char f[2] = {static_cast<char>(0x88), 0}; std::lock_guard<std::mutex> g(w_); sendAll(fd_, f, 2); ::shutdown(fd_, SHUT_RDWR); } }

// ── Server ──
Server::Server() { initSockets(); }
Server::~Server() { stop(); }

bool Server::listen(const std::string& bind, unsigned short port, std::string& err) {
  lfd_ = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0)); if (lfd_ < 0) { err = "socket() failed"; return false; }
  int one = 1; setsockopt(lfd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof one);
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port);
  if (inet_pton(AF_INET, bind.c_str(), &a.sin_addr) != 1) { err = "bad bind address: " + bind; return false; }
  if (::bind(lfd_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) { err = "cannot bind " + bind + ":" + std::to_string(port) + " (in use?)"; CLOSESOCK(lfd_); lfd_ = -1; return false; }
  if (::listen(lfd_, 16) != 0) { err = "listen() failed"; return false; }
  socklen_t len = sizeof a; getsockname(lfd_, reinterpret_cast<sockaddr*>(&a), &len); port_ = ntohs(a.sin_port);
  return true;
}
bool Server::localPeer(unsigned long ip) {
  const unsigned a = static_cast<unsigned>((ip >> 24) & 255u), b = static_cast<unsigned>((ip >> 16) & 255u);
  return a == 127 || a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) || (a == 169 && b == 254);
}
void Server::start() {
  running_ = true;
  acceptor_ = std::thread([this] {
    while (running_) {
      sockaddr_in c{}; socklen_t len = sizeof c; const int fd = static_cast<int>(::accept(lfd_, reinterpret_cast<sockaddr*>(&c), &len));
      if (fd < 0) { if (!running_) break; continue; }
      if (localOnly_ && !localPeer(ntohl(c.sin_addr.s_addr))) { CLOSESOCK(fd); continue; }   // the local policy: not from this network, not served
      int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
      std::lock_guard<std::mutex> g(threads_); const std::uint32_t ip4 = ntohl(c.sin_addr.s_addr); const std::string peer = std::to_string(ip4 >> 24) + "." + std::to_string((ip4 >> 16) & 255) + "." + std::to_string((ip4 >> 8) & 255) + "." + std::to_string(ip4 & 255);
      threads_v_.emplace_back([this, fd, peer] { serve(fd, peer); });
    }
  });
}
void Server::stop() {
  if (!running_.exchange(false)) { if (lfd_ >= 0) { CLOSESOCK(lfd_); lfd_ = -1; } return; }
  if (lfd_ >= 0) {
#ifdef _WIN32
    CLOSESOCK(lfd_);
#else
    ::shutdown(lfd_, SHUT_RDWR); CLOSESOCK(lfd_);
#endif
    lfd_ = -1; }
  if (acceptor_.joinable()) acceptor_.join();
  std::vector<std::thread> t; { std::lock_guard<std::mutex> g(threads_); t.swap(threads_v_); }
  for (auto& th : t) if (th.joinable()) th.join();
}
void Server::serve(int fd, const std::string& peer) {
  Request r; std::string err; r.peer = peer;
  if (!readRequest(fd, r, err)) { Response e; e.status = err == "body too large" ? 413 : 400; e.body = "{\"ok\":false,\"error\":\"" + err + "\"}"; if (err != "closed") writeResponse(fd, e); CLOSESOCK(fd); return; }
  const auto ws = ws_.find(r.path);
  if (ws != ws_.end() && lower(r.header("upgrade")) == "websocket") {
    const std::string key = r.header("sec-websocket-key");
    if (key.empty()) { Response e; e.status = 400; e.body = "{\"ok\":false,\"error\":\"no websocket key\"}"; writeResponse(fd, e); CLOSESOCK(fd); return; }
    const std::string accept = sha1Base64(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    const std::string h = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + "\r\n\r\n";
    if (!sendAll(fd, h.data(), h.size())) { CLOSESOCK(fd); return; }
    WebSocket sock(fd); ws->second(r, sock); sock.close(); CLOSESOCK(fd); return;
  }
  Response resp; if (handler_) resp = handler_(r); else { resp.status = 404; resp.body = "{\"ok\":false,\"error\":\"not found\"}"; }
  writeResponse(fd, resp); CLOSESOCK(fd);
}

// ── Client (tests) ──
static int connectTo(const std::string& host, unsigned short port) {
  initSockets(); const int fd = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0)); if (fd < 0) return -1;
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port); if (inet_pton(AF_INET, host.c_str(), &a.sin_addr) != 1) { CLOSESOCK(fd); return -1; }
  if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) { CLOSESOCK(fd); return -1; }
  return fd;
}
bool Client::request(const std::string& host, unsigned short port, const std::string& method, const std::string& path, const std::string& body, const std::vector<std::pair<std::string, std::string>>& headers, int& status, std::string& out) {
  const int fd = connectTo(host, port); if (fd < 0) return false;
  std::string h = method + " " + path + " HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
  for (const auto& kv : headers) h += kv.first + ": " + kv.second + "\r\n";
  h += "\r\n" + body; if (!sendAll(fd, h.data(), h.size())) { CLOSESOCK(fd); return false; }
  std::string all; char buf[4096]; for (;;) { const auto k = ::recv(fd, buf, sizeof buf, 0); if (k <= 0) break; all.append(buf, static_cast<std::size_t>(k)); }
  CLOSESOCK(fd);
  const std::size_t sp = all.find(' '); status = sp == std::string::npos ? 0 : std::atoi(all.c_str() + sp + 1);
  const std::size_t hb = all.find("\r\n\r\n"); out = hb == std::string::npos ? "" : all.substr(hb + 4); return status > 0;
}
int Client::connectWs(const std::string& host, unsigned short port, const std::string& path, const std::vector<std::pair<std::string, std::string>>& headers) {
  const int fd = connectTo(host, port); if (fd < 0) return -1;
  std::string h = "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n";
  for (const auto& kv : headers) h += kv.first + ": " + kv.second + "\r\n";
  h += "\r\n"; if (!sendAll(fd, h.data(), h.size())) { CLOSESOCK(fd); return -1; }
  std::string head; unsigned char c; while (head.size() < 4096) { if (!recvExact(fd, &c, 1)) { CLOSESOCK(fd); return -1; } head += static_cast<char>(c); if (head.size() >= 4 && head.compare(head.size() - 4, 4, "\r\n\r\n") == 0) break; }
  if (head.find(" 101 ") == std::string::npos || head.find("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == std::string::npos) { CLOSESOCK(fd); return -1; }   // the RFC 6455 sample key's accept value
  return fd;
}

}  // namespace am::httpd
