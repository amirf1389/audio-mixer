#include "tokens.hpp"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <random>
#include <sstream>

#include "json.hpp"
#include "license/crypto.hpp"
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace am {

std::string TokenStore::hashOf(const std::string& t) { const auto h = crypto::sha256(reinterpret_cast<const std::uint8_t*>(t.data()), t.size()); return crypto::hex(h.data(), h.size(), false); }
bool TokenStore::same(const std::string& a, const std::string& b) { unsigned char d = static_cast<unsigned char>(a.size() != b.size()); const std::size_t n = a.size() < b.size() ? a.size() : b.size(); for (std::size_t i = 0; i < n; ++i) d |= static_cast<unsigned char>(a[i] ^ b[i]); return d == 0; }

bool TokenStore::load(std::string& err) {
  std::lock_guard<std::mutex> g(m_); e_.clear(); if (file_.empty()) return true;
  std::ifstream in(file_, std::ios::binary); if (!in) return true;
  std::stringstream ss; ss << in.rdbuf(); bool ok = false; const json::Value v = json::Value::parse(ss.str(), ok);
  if (!ok || !v.isObject()) { err = file_ + " is not a token file"; return false; }
  for (const json::Value& t : v["tokens"].items()) {
    Entry e; e.id = t["id"].asString(); e.name = t["name"].asString(); e.role = t["role"].asString(); e.hash = t["hash"].asString();
    e.created = static_cast<long long>(t["created"].asNumber()); e.expires = static_cast<long long>(t["expires"].asNumber()); e.revoked = t["revoked"].asBool();
    if (e.id.empty() || e.hash.size() != 64 || (e.role != "control" && e.role != "view")) continue;
    e_.push_back(e);
  }
  return true;
}

bool TokenStore::save(std::string& err) const {
  if (file_.empty()) { err = "no token file"; return false; }
  json::Value root = json::Value::object(); json::Value arr = json::Value::array();
  for (const Entry& e : e_) { json::Value t = json::Value::object(); t.set("id", e.id); t.set("name", e.name); t.set("role", e.role); t.set("hash", e.hash); t.set("created", e.created); t.set("expires", e.expires); t.set("revoked", e.revoked); arr.push(t); }
  root.set("tokens", arr); const std::string text = root.dump() + "\n", tmp = file_ + ".tmp";
#ifdef _WIN32
  FILE* f = std::fopen(tmp.c_str(), "wb"); if (!f) { err = "cannot write " + tmp; return false; }
  std::fwrite(text.data(), 1, text.size(), f); std::fclose(f); std::remove(file_.c_str());
#else
  const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600); if (fd < 0) { err = "cannot write " + tmp; return false; }
  const auto w = ::write(fd, text.data(), text.size()); ::close(fd); ::chmod(tmp.c_str(), 0600); if (w != static_cast<long>(text.size())) { err = "cannot write " + tmp; return false; }
#endif
  if (std::rename(tmp.c_str(), file_.c_str()) != 0) { err = "cannot write " + file_; return false; }
  return true;
}

std::string TokenStore::issue(const std::string& name, const std::string& role, int days, std::string& id, std::string& err) {
  if (role != "control" && role != "view") { err = "role must be control or view"; return ""; }
  std::string n; for (char c : name) if (static_cast<unsigned char>(c) >= 32 && n.size() < 40) n += c;
  if (n.empty()) { err = "the token needs a name"; return ""; }
  if (days < 0 || days > 3650) { err = "days must be 0 (no end) to 3650"; return ""; }
  std::random_device rd; std::string t = "amx_"; static const char* hx = "0123456789abcdef";
  for (int i = 0; i < 8; ++i) { const std::uint32_t v = rd(); for (int k = 0; k < 8; ++k) t += hx[(v >> (4 * k)) & 15]; }
  std::lock_guard<std::mutex> g(m_);
  if (e_.size() >= 200) { err = "200 tokens are the limit: revoke some first"; return ""; }
  Entry e; e.name = n; e.role = role; e.hash = hashOf(t); e.created = static_cast<long long>(std::time(nullptr)); e.expires = days > 0 ? e.created + 86400LL * days : 0;
  e.id = e.hash.substr(0, 8); for (const Entry& o : e_) if (o.id == e.id) { err = "try again"; return ""; }
  e_.push_back(e);
  if (!save(err)) { e_.pop_back(); return ""; }
  id = e.id; return t;
}

bool TokenStore::revoke(const std::string& id, std::string& err) {
  std::lock_guard<std::mutex> g(m_);
  for (Entry& e : e_) if (e.id == id) { if (e.revoked) return true; e.revoked = true; if (!save(err)) { e.revoked = false; return false; } return true; }
  err = "no token with the id " + id; return false;
}

std::vector<TokenStore::Entry> TokenStore::list() const { std::lock_guard<std::mutex> g(m_); return e_; }

std::string TokenStore::roleOf(const std::string& token, long long now) const {
  if (token.size() != 68 || token.compare(0, 4, "amx_") != 0) return "";
  if (now == 0) now = static_cast<long long>(std::time(nullptr));
  const std::string h = hashOf(token); std::string role; std::lock_guard<std::mutex> g(m_);
  for (const Entry& e : e_) if (same(e.hash, h) && !e.revoked && (e.expires == 0 || now < e.expires)) role = e.role;
  return role;
}

}  // namespace am
