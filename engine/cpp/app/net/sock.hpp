// UDP sockets for the network audio and MIDI code: the same few calls on Winsock and BSD sockets. IPv4 only.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace am::net {

#ifdef _WIN32
using Sock = std::uintptr_t;
constexpr Sock kNoSock = ~static_cast<Sock>(0);
#else
using Sock = int;
constexpr Sock kNoSock = -1;
#endif

struct Peer { std::uint32_t ip = 0; std::uint16_t port = 0; };   // host byte order

Sock udpOpen(std::string& err);                                   // a UDP socket, address reuse on
bool udpBind(Sock s, const std::string& ip, int port, std::string& err);   // ip "" = every interface
bool udpJoin(Sock s, const std::string& group, const std::string& iface, std::string& err);   // multicast membership; iface "" = the system's choice
void udpMulticastOut(Sock s, const std::string& iface, int ttl, bool loop);
void udpTimeout(Sock s, int ms);                                  // receive timeout
bool udpSendTo(Sock s, const std::string& ip, int port, const void* data, std::size_t n);
bool udpSendTo(Sock s, const Peer& to, const void* data, std::size_t n);
int udpRecv(Sock s, void* buf, std::size_t n, Peer* from = nullptr);   // bytes, 0 on timeout, -1 on error
int udpLocalPort(Sock s);
void udpClose(Sock& s);
std::string ipToString(std::uint32_t hostOrder);
// the IPv4 addresses of this machine that are up and not loopback ("192.168.1.20"), for the "which network card" choice
std::vector<std::string> localAddresses();
// the address the system would send from to reach `dest` (no packet is sent); "" when there is no route
std::string routeSource(const std::string& dest);

}  // namespace am::net
