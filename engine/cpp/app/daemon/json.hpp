// A small JSON value with a recursive-descent parser and a serializer: what the daemon's API needs, nothing more.
// Numbers are doubles, strings UTF-8 (escapes decoded, \uXXXX incl. surrogate pairs), objects keep insertion order.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace am::json {

class Value {
 public:
  enum Type { Null, Bool, Number, String, Array, Object };
  Value() = default;
  Value(std::nullptr_t) {}
  Value(bool b) : type_(Bool), b_(b) {}
  Value(int n) : type_(Number), n_(n) {}
  Value(unsigned n) : type_(Number), n_(n) {}
  Value(long n) : type_(Number), n_(static_cast<double>(n)) {}
  Value(long long n) : type_(Number), n_(static_cast<double>(n)) {}
  Value(unsigned long n) : type_(Number), n_(static_cast<double>(n)) {}
  Value(unsigned long long n) : type_(Number), n_(static_cast<double>(n)) {}
  Value(double n) : type_(Number), n_(n) {}
  Value(const char* s) : type_(String), s_(s) {}
  Value(std::string s) : type_(String), s_(std::move(s)) {}
  static Value array() { Value v; v.type_ = Array; return v; }
  static Value object() { Value v; v.type_ = Object; return v; }

  Type type() const { return type_; }
  bool isNull() const { return type_ == Null; }
  bool isObject() const { return type_ == Object; }
  bool isArray() const { return type_ == Array; }
  bool asBool(bool d = false) const { return type_ == Bool ? b_ : type_ == Number ? n_ != 0 : d; }
  double asNumber(double d = 0) const { return type_ == Number ? n_ : d; }
  int asInt(int d = 0) const { return type_ == Number && std::isfinite(n_) ? static_cast<int>(n_) : d; }
  const std::string& asString() const { static const std::string e; return type_ == String ? s_ : e; }
  std::string asString(const std::string& d) const { return type_ == String ? s_ : d; }
  const std::vector<Value>& items() const { return a_; }
  const std::vector<std::pair<std::string, Value>>& members() const { return o_; }
  bool has(const std::string& k) const { for (const auto& m : o_) if (m.first == k) return true; return false; }
  const Value& operator[](const std::string& k) const { static const Value none; for (const auto& m : o_) if (m.first == k) return m.second; return none; }
  Value& set(const std::string& k, Value v) { if (type_ != Object) { *this = object(); } for (auto& m : o_) if (m.first == k) { m.second = std::move(v); return m.second; } o_.emplace_back(k, std::move(v)); return o_.back().second; }
  Value& push(Value v) { if (type_ != Array) *this = array(); a_.push_back(std::move(v)); return a_.back(); }
  std::size_t size() const { return type_ == Array ? a_.size() : type_ == Object ? o_.size() : 0; }

  std::string dump() const { std::string out; dumpTo(out); return out; }
  void dumpTo(std::string& out) const {
    switch (type_) {
      case Null: out += "null"; break;
      case Bool: out += b_ ? "true" : "false"; break;
      case Number: { if (!std::isfinite(n_)) { out += "null"; break; } char buf[40]; if (n_ == std::floor(n_) && std::fabs(n_) < 1e15) std::snprintf(buf, sizeof buf, "%.0f", n_); else std::snprintf(buf, sizeof buf, "%.10g", n_); out += buf; break; }
      case String: escape(s_, out); break;
      case Array: out += '['; for (std::size_t i = 0; i < a_.size(); ++i) { if (i) out += ','; a_[i].dumpTo(out); } out += ']'; break;
      case Object: out += '{'; for (std::size_t i = 0; i < o_.size(); ++i) { if (i) out += ','; escape(o_[i].first, out); out += ':'; o_[i].second.dumpTo(out); } out += '}'; break;
    }
  }
  static void escape(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
      switch (c) {
        case '"': out += "\\\""; break; case '\\': out += "\\\\"; break; case '\n': out += "\\n"; break; case '\r': out += "\\r"; break; case '\t': out += "\\t"; break;
        default: if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; } else out += static_cast<char>(c);
      }
    }
    out += '"';
  }

  // parse; ok = false on any syntax error (the value is then Null). Depth-limited, size-limited by the caller.
  static Value parse(const std::string& text, bool& ok) { Parser p{text, 0}; ok = true; Value v = p.value(ok, 0); p.ws(); if (p.i != text.size()) ok = false; return ok ? v : Value(); }

 private:
  struct Parser {
    const std::string& s; std::size_t i;
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i; }
    bool lit(const char* w) { std::size_t n = std::char_traits<char>::length(w); if (s.compare(i, n, w) == 0) { i += n; return true; } return false; }
    Value value(bool& ok, int depth) {
      ws(); if (!ok || depth > 64 || i >= s.size()) { ok = false; return {}; }
      const char c = s[i];
      if (c == '{') { ++i; Value o = object(); ws(); if (i < s.size() && s[i] == '}') { ++i; return o; }
        for (;;) { ws(); if (i >= s.size() || s[i] != '"') { ok = false; return {}; } std::string k = str(ok); ws(); if (!ok || i >= s.size() || s[i] != ':') { ok = false; return {}; } ++i; Value v = value(ok, depth + 1); if (!ok) return {}; o.set(k, std::move(v)); ws(); if (i < s.size() && s[i] == ',') { ++i; continue; } if (i < s.size() && s[i] == '}') { ++i; return o; } ok = false; return {}; } }
      if (c == '[') { ++i; Value a = array(); ws(); if (i < s.size() && s[i] == ']') { ++i; return a; }
        for (;;) { Value v = value(ok, depth + 1); if (!ok) return {}; a.push(std::move(v)); ws(); if (i < s.size() && s[i] == ',') { ++i; continue; } if (i < s.size() && s[i] == ']') { ++i; return a; } ok = false; return {}; } }
      if (c == '"') return Value(str(ok));
      if (lit("true")) return Value(true);
      if (lit("false")) return Value(false);
      if (lit("null")) return Value(nullptr);
      if (c == '-' || (c >= '0' && c <= '9')) { const char* b = s.c_str() + i; char* e = nullptr; const double d = std::strtod(b, &e); if (e == b) { ok = false; return {}; } i += static_cast<std::size_t>(e - b); return Value(d); }
      ok = false; return {};
    }
    std::string str(bool& ok) {
      std::string out; ++i;   // opening quote
      while (i < s.size()) {
        const char c = s[i++];
        if (c == '"') return out;
        if (c != '\\') { out += c; continue; }
        if (i >= s.size()) break;
        const char e = s[i++];
        switch (e) {
          case '"': out += '"'; break; case '\\': out += '\\'; break; case '/': out += '/'; break; case 'b': out += '\b'; break; case 'f': out += '\f'; break; case 'n': out += '\n'; break; case 'r': out += '\r'; break; case 't': out += '\t'; break;
          case 'u': { unsigned cp = hex4(ok); if (!ok) return out; if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') { i += 2; const unsigned lo = hex4(ok); if (!ok) return out; cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); } utf8(cp, out); break; }
          default: ok = false; return out;
        }
      }
      ok = false; return out;
    }
    unsigned hex4(bool& ok) { if (i + 4 > s.size()) { ok = false; return 0; } unsigned v = 0; for (int k = 0; k < 4; ++k) { const char c = s[i++]; v <<= 4; if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0'); else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10); else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10); else { ok = false; return 0; } } return v; }
    static void utf8(unsigned cp, std::string& out) {
      if (cp < 0x80) out += static_cast<char>(cp);
      else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
      else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
      else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    }
  };
  Type type_ = Null; bool b_ = false; double n_ = 0; std::string s_; std::vector<Value> a_; std::vector<std::pair<std::string, Value>> o_;
};

}  // namespace am::json
