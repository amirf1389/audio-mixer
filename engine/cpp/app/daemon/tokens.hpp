// THE DAEMON'S TOKENS. The master token (<data>/daemon.token) stays the owner's key: it controls the console and manages the others.
// Beside it, any number of named tokens can be given out, one per phone or operator:
//   role "control"  may change the console (POST /api/control) and read the meters
//   role "view"     may only read the meters over the WebSocket
// A token is shown once, when it is made ("amx_" + 64 hex). The file (<data>/daemon.tokens.json, owner only) keeps the SHA-256 of each,
// never the token itself, with its name, role, the day it was made, the day it ends (0 = never) and whether it was revoked.
#pragma once
#include <mutex>
#include <string>
#include <vector>

namespace am {

class TokenStore {
 public:
  struct Entry { std::string id, name, role, hash; long long created = 0, expires = 0; bool revoked = false; };
  explicit TokenStore(std::string file = "") : file_(std::move(file)) {}
  void setFile(std::string file) { std::lock_guard<std::mutex> g(m_); file_ = std::move(file); }
  bool load(std::string& err);   // a missing file is an empty store
  // makes a token and saves its hash; returns the token (the only time it exists in the clear), "" with err on failure. days 0 = no end.
  std::string issue(const std::string& name, const std::string& role, int days, std::string& id, std::string& err);
  bool revoke(const std::string& id, std::string& err);
  std::vector<Entry> list() const;
  std::string roleOf(const std::string& token, long long now = 0) const;   // "control", "view" or "" (unknown, revoked, ended)
  static std::string hashOf(const std::string& token);
  static bool same(const std::string& a, const std::string& b);   // compares without stopping at the first difference
 private:
  bool save(std::string& err) const;
  mutable std::mutex m_; std::string file_; std::vector<Entry> e_;
};

}  // namespace am
