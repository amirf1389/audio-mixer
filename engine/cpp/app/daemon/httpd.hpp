// A small HTTP/1.1 + WebSocket server for the daemon's local API: one thread per connection, Content-Length bodies up to 64 KB, the
// WebSocket handshake (SHA-1 + base64), text frames both ways, ping / pong / close. POSIX sockets, Winsock on Windows. No TLS: the server
// binds 127.0.0.1 unless told otherwise, and on a LAN it should sit behind the nginx TLS front (deploy/nginx).
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace am::httpd {

struct Request { std::string method, path, query, body; std::map<std::string, std::string> headers; std::string header(const std::string& k) const; std::string param(const std::string& k) const; };
struct Response { int status = 200; std::string type = "application/json; charset=utf-8", body; std::vector<std::pair<std::string, std::string>> headers; };

class WebSocket {   // one upgraded connection; send() from any thread, the reader loop runs on the connection's own thread
 public:
  explicit WebSocket(int fd) : fd_(fd) {}
  bool send(const std::string& text);
  bool read(std::string& text, int& opcode);   // false = closed; opcode 1 text, 8 close, 9 ping (answered inside), 10 pong
  void close();
  bool open() const { return open_; }
 private:
  int fd_; std::mutex w_; std::atomic<bool> open_{true};
};

using Handler = std::function<Response(const Request&)>;
using WsHandler = std::function<void(const Request&, WebSocket&)>;   // runs on the connection thread until it returns; the socket is closed after

class Server {
 public:
  Server();
  ~Server();
  bool listen(const std::string& bind, unsigned short port, std::string& err);   // port 0 = any free port
  unsigned short port() const { return port_; }
  void route(Handler h) { handler_ = std::move(h); }
  void websocket(const std::string& path, WsHandler h) { ws_[path] = std::move(h); }
  void start();   // the accept loop on its own thread
  void stop();
  static std::string sha1Base64(const std::string& s);   // for the WebSocket accept key (and tests)
 private:
  void serve(int fd);
  int lfd_ = -1; unsigned short port_ = 0; std::thread acceptor_; std::atomic<bool> running_{false};
  Handler handler_; std::map<std::string, WsHandler> ws_;
  std::mutex threads_; std::vector<std::thread> threads_v_;
};

// a small client for the tests: GET / POST with headers, returns status and body; connectWs performs the upgrade
struct Client {
  static bool request(const std::string& host, unsigned short port, const std::string& method, const std::string& path, const std::string& body, const std::vector<std::pair<std::string, std::string>>& headers, int& status, std::string& out);
  static int connectWs(const std::string& host, unsigned short port, const std::string& path, const std::vector<std::pair<std::string, std::string>>& headers);   // returns fd or -1
};

}  // namespace am::httpd
