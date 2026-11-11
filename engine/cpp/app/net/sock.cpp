#include "net/sock.hpp"

#include <cstdio>
#include <cerrno>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
namespace { struct WsaInit { WsaInit() { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); } } g_wsa; }
#define AM_CLOSE(s) closesocket(static_cast<SOCKET>(s))
using socklen_type = int;
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#define AM_CLOSE(s) ::close(s)
using socklen_type = socklen_t;
#endif

namespace am::net {

static bool toAddr(const std::string& ip, in_addr& a) { if (ip.empty()) { a.s_addr = htonl(INADDR_ANY); return true; } return inet_pton(AF_INET, ip.c_str(), &a) == 1; }
static void opt(Sock s, int level, int name, const void* v, int n) { setsockopt(s, level, name, static_cast<const char*>(v), n); }

Sock udpOpen(std::string& err) {
  const auto s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
  if (s == INVALID_SOCKET) { err = "no UDP socket"; return kNoSock; }
#else
  if (s < 0) { err = "no UDP socket"; return kNoSock; }
#endif
  const int one = 1; opt(static_cast<Sock>(s), SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#ifdef SO_REUSEPORT
  opt(static_cast<Sock>(s), SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
#endif
  const int big = 1 << 20; opt(static_cast<Sock>(s), SOL_SOCKET, SO_RCVBUF, &big, sizeof big);
  return static_cast<Sock>(s);
}

bool udpBind(Sock s, const std::string& ip, int port, std::string& err) {
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(static_cast<std::uint16_t>(port));
  if (!toAddr(ip, a.sin_addr)) { err = "not an IPv4 address: " + ip; return false; }
  if (::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) { err = "port " + std::to_string(port) + " cannot be opened (in use, or not allowed)"; return false; }
  return true;
}

bool udpJoin(Sock s, const std::string& group, const std::string& iface, std::string& err) {
  ip_mreq m{};
  if (!toAddr(group, m.imr_multiaddr) || !toAddr(iface, m.imr_interface)) { err = "not an IPv4 address"; return false; }
  if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&m), sizeof m) != 0) { err = "the multicast group " + group + " cannot be joined on this network"; return false; }
  return true;
}

void udpMulticastOut(Sock s, const std::string& iface, int ttl, bool loop) {
  const unsigned char t = static_cast<unsigned char>(ttl < 1 ? 1 : (ttl > 255 ? 255 : ttl)), l = loop ? 1 : 0;
  opt(s, IPPROTO_IP, IP_MULTICAST_TTL, &t, sizeof t); opt(s, IPPROTO_IP, IP_MULTICAST_LOOP, &l, sizeof l);
  in_addr a{}; if (!iface.empty() && toAddr(iface, a)) opt(s, IPPROTO_IP, IP_MULTICAST_IF, &a, sizeof a);
}

void udpTimeout(Sock s, int ms) {
#ifdef _WIN32
  const DWORD t = static_cast<DWORD>(ms); opt(s, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof t);
#else
  timeval t{ms / 1000, (ms % 1000) * 1000}; opt(s, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof t);
#endif
}

bool udpSendTo(Sock s, const Peer& to, const void* data, std::size_t n) {
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(to.port); a.sin_addr.s_addr = htonl(to.ip);
  return ::sendto(s, static_cast<const char*>(data), static_cast<int>(n), 0, reinterpret_cast<sockaddr*>(&a), sizeof a) == static_cast<int>(n);
}
bool udpSendTo(Sock s, const std::string& ip, int port, const void* data, std::size_t n) {
  in_addr a{}; if (ip.empty() || !toAddr(ip, a)) return false;
  return udpSendTo(s, Peer{ntohl(a.s_addr), static_cast<std::uint16_t>(port)}, data, n);
}

int udpRecv(Sock s, void* buf, std::size_t n, Peer* from) {
  sockaddr_in a{}; socklen_type len = sizeof a;
  const int r = static_cast<int>(::recvfrom(s, static_cast<char*>(buf), static_cast<int>(n), 0, reinterpret_cast<sockaddr*>(&a), &len));
  if (r < 0) {
#ifdef _WIN32
    const int e = WSAGetLastError(); return (e == WSAETIMEDOUT || e == WSAEWOULDBLOCK || e == WSAECONNRESET) ? 0 : -1;
#else
    return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
#endif
  }
  if (from) { from->ip = ntohl(a.sin_addr.s_addr); from->port = ntohs(a.sin_port); }
  return r;
}

int udpLocalPort(Sock s) { sockaddr_in a{}; socklen_type len = sizeof a; return getsockname(s, reinterpret_cast<sockaddr*>(&a), &len) == 0 ? ntohs(a.sin_port) : 0; }
void udpClose(Sock& s) { if (s != kNoSock) { AM_CLOSE(s); s = kNoSock; } }
std::string ipToString(std::uint32_t h) { char b[20]; std::snprintf(b, sizeof b, "%u.%u.%u.%u", (h >> 24) & 255, (h >> 16) & 255, (h >> 8) & 255, h & 255); return b; }

std::string routeSource(const std::string& dest) {
  std::string err; Sock s = udpOpen(err); if (s == kNoSock) return {};
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(9); std::string out;
  if (toAddr(dest, a.sin_addr) && !dest.empty() && ::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0) {
    sockaddr_in me{}; socklen_type len = sizeof me;
    if (getsockname(s, reinterpret_cast<sockaddr*>(&me), &len) == 0 && me.sin_addr.s_addr != 0) out = ipToString(ntohl(me.sin_addr.s_addr));
  }
  udpClose(s); return out;
}

std::vector<std::string> localAddresses() {
  std::vector<std::string> out;
#ifdef _WIN32
  ULONG size = 16 * 1024; std::vector<unsigned char> buf(size);
  ULONG r = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
  if (r == ERROR_BUFFER_OVERFLOW) { buf.resize(size); r = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size); }
  if (r != NO_ERROR) return out;
  for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a; a = a->Next) {
    if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
    for (auto* u = a->FirstUnicastAddress; u; u = u->Next) if (u->Address.lpSockaddr && u->Address.lpSockaddr->sa_family == AF_INET)
      out.push_back(ipToString(ntohl(reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr.s_addr)));
  }
#else
  ifaddrs* list = nullptr; if (getifaddrs(&list) != 0) return out;
  for (ifaddrs* i = list; i; i = i->ifa_next) {
    if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK)) continue;
    out.push_back(ipToString(ntohl(reinterpret_cast<sockaddr_in*>(i->ifa_addr)->sin_addr.s_addr)));
  }
  freeifaddrs(list);
#endif
  return out;
}

}  // namespace am::net
