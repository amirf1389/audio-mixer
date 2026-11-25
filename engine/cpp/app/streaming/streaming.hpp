// STREAMING: "now playing" and the play / pause / next / previous buttons for the music services, through their OFFICIAL APIs only.
// Sign-in is OAuth 2.0 with PKCE (RFC 7636): no client secret is ever in the program; the client id is the user's own (they register
// a developer app and type its id into SETTINGS). The tokens are kept only sealed by the operating system (DPAPI on Windows, the
// Keychain on macOS, the Android Keystore on Android); where there is none the sign-in lasts until the program closes.
// Without a sign-in (or for a service whose API has no playback control) the operating system's MEDIA SESSION is used instead
// (Windows: GlobalSystemMediaTransportControls; Linux: MPRIS): what plays now, and its own transport buttons.
// NOTHING here downloads, records, rips or captures what a service plays; isStreamingApp() lets APP SOUND refuse to record them.
// Pure C++ (no Qt): the HTTP call, the clock and the OS sealing are passed in, so the tests run them against mocks.
#pragma once
#include <cctype>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "daemon/json.hpp"
#include "license/crypto.hpp"

namespace am::streaming {

struct Provider {
  std::string id, name;
  bool login = false;     // OAuth 2.0 + PKCE sign-in without a client secret is possible
  bool control = false;   // the API reads what plays and has play / pause / next / previous
  std::string authUrl, tokenUrl, scopes, apiBase, consoleUrl;
  std::vector<std::string> setup;   // the steps to get a client id (developer app, review)
  std::string note;                 // what the service allows and what it does not
};

inline const char* redirectUri() { return "http://127.0.0.1:43117/callback"; }

inline const std::vector<Provider>& providers() {
  static const std::vector<Provider> p = {
      {"spotify", "Spotify", true, true, "https://accounts.spotify.com/authorize", "https://accounts.spotify.com/api/token",
       "user-read-playback-state user-modify-playback-state user-read-currently-playing", "https://api.spotify.com/v1", "https://developer.spotify.com/dashboard",
       {"Open developer.spotify.com/dashboard and sign in with your Spotify account.", "CREATE APP: any name; tick Web API.",
        "Redirect URI: http://127.0.0.1:43117/callback (exactly).", "Copy the app's Client ID into the field below (never the client secret).",
        "Development mode allows the accounts you add under User Management; more users need Spotify's quota extension review."},
       "Play / pause / next need Spotify Premium (Spotify's rule). The sound stays in Spotify's own app."},
      {"tidal", "TIDAL", true, false, "https://login.tidal.com/authorize", "https://auth.tidal.com/v1/oauth2/token", "user.read", "https://openapi.tidal.com/v2",
       "https://developer.tidal.com/dashboard",
       {"Open developer.tidal.com and sign in.", "Create an app; add the redirect URI http://127.0.0.1:43117/callback.",
        "Copy the Client ID into the field below (never the client secret)."},
       "TIDAL's API has no playback control: what plays and the buttons come from the MEDIA SESSION of the TIDAL app."},
      {"deezer", "Deezer", false, false, "", "", "", "", "https://developers.deezer.com/myapps",
       {"Deezer's sign-in needs the app's secret key for the token exchange, which a desktop program must not hold, so there is no Deezer sign-in here.",
        "Play Deezer in its app or the browser: the MEDIA SESSION shows what plays and has the buttons."},
       "No sign-in (Deezer has no PKCE). MEDIA SESSION only."},
      {"youtube", "YouTube", false, false, "", "", "", "", "https://developers.google.com/youtube/iframe_api_reference",
       {"YouTube plays only in YouTube's official embedded player: the web remote's STREAMING tab has one (paste a video link).",
        "A video playing in a browser or the YouTube app shows up in the MEDIA SESSION."},
       "Official embedded player only. Nothing is downloaded or recorded."},
      {"applemusic", "Apple Music", false, false, "", "", "", "", "https://developer.apple.com/musickit/",
       {"Apple Music's API needs a developer token signed with a MusicKit private key (Apple Developer Program), which a desktop program must not hold.",
        "Play in the Music app or music.apple.com: the MEDIA SESSION shows what plays and has the buttons."},
       "No sign-in here. MEDIA SESSION only."},
  };
  return p;
}
inline const Provider* find(const std::string& id) { for (const Provider& p : providers()) if (p.id == id) return &p; return nullptr; }

// a client id as the services issue them: 8 .. 128 of [A-Za-z0-9._-]
inline bool validClientId(const std::string& s) {
  if (s.size() < 8 || s.size() > 128) return false;
  for (char c : s) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
  return true;
}

// ---- PKCE (RFC 7636, S256) ----
struct Pkce { std::string verifier, challenge; };
inline std::string challengeFor(const std::string& verifier) {
  const auto h = am::crypto::sha256(reinterpret_cast<const std::uint8_t*>(verifier.data()), verifier.size());
  return am::crypto::base64url(h.data(), h.size());
}
// `random` comes from the system's secure random source (at least 32 bytes: a 43+ character verifier)
inline bool makePkce(const std::vector<std::uint8_t>& random, Pkce& out) {
  if (random.size() < 32 || random.size() > 96) return false;
  out.verifier = am::crypto::base64url(random.data(), random.size()); out.challenge = challengeFor(out.verifier);
  return out.verifier.size() >= 43 && out.verifier.size() <= 128;
}

inline std::string urlEncode(const std::string& s) {
  static const char* hx = "0123456789ABCDEF"; std::string o;
  for (unsigned char c : s) { if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') o += static_cast<char>(c); else { o += '%'; o += hx[c >> 4]; o += hx[c & 15]; } }
  return o;
}
inline std::string authorizeUrl(const Provider& p, const std::string& clientId, const std::string& state, const std::string& challenge) {
  return p.authUrl + "?response_type=code&client_id=" + urlEncode(clientId) + "&redirect_uri=" + urlEncode(redirectUri()) + "&scope=" + urlEncode(p.scopes) +
         "&state=" + urlEncode(state) + "&code_challenge_method=S256&code_challenge=" + urlEncode(challenge);
}
// the token request bodies: client_id + the verifier, never a client secret
inline std::string tokenBody(const std::string& clientId, const std::string& code, const std::string& verifier) {
  return "grant_type=authorization_code&code=" + urlEncode(code) + "&redirect_uri=" + urlEncode(redirectUri()) + "&client_id=" + urlEncode(clientId) + "&code_verifier=" + urlEncode(verifier);
}
inline std::string refreshBody(const std::string& clientId, const std::string& refresh) {
  return "grant_type=refresh_token&refresh_token=" + urlEncode(refresh) + "&client_id=" + urlEncode(clientId);
}

// ---- tokens ----
struct Tokens { std::string access, refresh, scope; long long expiresAt = 0; bool valid() const { return !access.empty(); } };
inline bool parseTokens(const std::string& body, long long now, Tokens& t, std::string& err) {
  bool ok = false; const json::Value v = json::Value::parse(body, ok);
  if (!ok || !v.isObject()) { err = "the sign-in answer is not JSON"; return false; }
  if (v.has("error")) { err = "the service refused: " + v["error"].asString().substr(0, 60); return false; }
  const std::string a = v["access_token"].asString(); if (a.empty() || a.size() > 4096) { err = "the service sent no access token"; return false; }
  t.access = a; if (!v["refresh_token"].asString().empty()) t.refresh = v["refresh_token"].asString();   // a refresh may not send a new one: keep the old
  t.scope = v["scope"].asString(t.scope); const int e = v["expires_in"].asInt(3600); t.expiresAt = now + (e > 0 && e < 86400 * 30 ? e : 3600); err.clear(); return true;
}
inline std::string serialize(const Tokens& t) { json::Value v = json::Value::object(); v.set("a", t.access); v.set("r", t.refresh); v.set("s", t.scope); v.set("e", t.expiresAt); return v.dump(); }
inline bool deserialize(const std::string& s, Tokens& t) {
  bool ok = false; const json::Value v = json::Value::parse(s, ok); if (!ok || !v.isObject() || v["a"].asString().empty()) return false;
  t.access = v["a"].asString(); t.refresh = v["r"].asString(); t.scope = v["s"].asString(); t.expiresAt = static_cast<long long>(v["e"].asNumber()); return true;
}

// ---- HTTP (passed in) ----
struct Response { int status = 0; std::string body; };
using Header = std::pair<std::string, std::string>;
using Http = std::function<Response(const std::string& method, const std::string& url, const std::vector<Header>& headers, const std::string& body)>;

// a token that ends within a minute is refreshed first; false (signed out) when the refresh is refused
inline bool ensureFresh(const Provider& p, const std::string& clientId, Tokens& t, long long now, const Http& http, std::string& err) {
  if (!t.valid()) { err = "not signed in"; return false; }
  if (t.expiresAt - now > 60) return true;
  if (t.refresh.empty()) { err = "the sign-in ended: sign in again"; t = Tokens{}; return false; }
  const Response r = http("POST", p.tokenUrl, {{"Content-Type", "application/x-www-form-urlencoded"}}, refreshBody(clientId, t.refresh));
  if (r.status != 200) { err = r.status == 400 || r.status == 401 ? "the sign-in ended: sign in again" : "the service did not answer"; if (r.status == 400 || r.status == 401) t = Tokens{}; return false; }
  Tokens n = t; if (!parseTokens(r.body, now, n, err)) return false; t = n; return true;
}

// ---- sealed storage: `seal` / `open` are the OS facility (DPAPI, Keychain, Keystore); the store only ever holds sealed bytes ----
using Seal = std::function<bool(const std::string& in, std::string& out)>;
struct Vault {
  Seal seal, open; std::map<std::string, std::string> blobs;   // provider id -> sealed bytes (what is written to disk)
  bool save(const std::string& id, const Tokens& t) { if (!seal) return false; std::string b; if (!seal(serialize(t), b) || b.empty()) return false; blobs[id] = b; return true; }
  bool load(const std::string& id, Tokens& t) const {
    const auto it = blobs.find(id); if (it == blobs.end() || !open) return false; std::string plain; if (!open(it->second, plain)) return false; return deserialize(plain, t); }
  void erase(const std::string& id) { blobs.erase(id); }
};

// ---- logging: every line goes through redact(): tokens, codes, verifiers and Bearer values never reach a log ----
inline std::string redact(const std::string& line) {
  std::string s = line;
  for (const char* k : {"access_token", "refresh_token", "code_verifier", "code", "client_secret", "id_token"}) {
    const std::string key = k; std::size_t pos = 0;
    while ((pos = s.find(key, pos)) != std::string::npos) {
      const bool wordStart = pos == 0 || !(std::isalnum(static_cast<unsigned char>(s[pos - 1])) || s[pos - 1] == '_');
      std::size_t v = pos + key.size(); if (!wordStart) { pos = v; continue; }
      while (v < s.size() && (s[v] == '"' || s[v] == ' ' || s[v] == ':' || s[v] == '=')) ++v;
      std::size_t e = v; while (e < s.size() && s[e] != '&' && s[e] != '"' && s[e] != ',' && s[e] != ' ' && s[e] != '}' && s[e] != '\n') ++e;
      if (e > v) s.replace(v, e - v, "***");
      pos = v + 3;
    }
  }
  for (const char* k : {"Bearer ", "bearer "}) { std::size_t pos = 0; while ((pos = s.find(k, pos)) != std::string::npos) { const std::size_t v = pos + 7; std::size_t e = v; while (e < s.size() && s[e] != ' ' && s[e] != '"' && s[e] != '\n') ++e; s.replace(v, e - v, "***"); pos = v + 3; } }
  return s;
}

// ---- now playing ----
struct NowPlaying { std::string service, title, artist, album, source; bool playing = false, any = false; };
inline NowPlaying parseSpotifyPlayer(const std::string& body) {
  NowPlaying n; n.source = "spotify-api"; n.service = "Spotify"; bool ok = false; const json::Value v = json::Value::parse(body, ok); if (!ok || !v.isObject()) return n;
  const json::Value& it = v["item"]; if (!it.isObject()) return n;
  n.any = true; n.playing = v["is_playing"].asBool(); n.title = it["name"].asString().substr(0, 200); n.album = it["album"]["name"].asString().substr(0, 200);
  for (const json::Value& a : it["artists"].items()) { if (!n.artist.empty()) n.artist += ", "; n.artist += a["name"].asString(); } n.artist = n.artist.substr(0, 200);
  return n;
}
inline bool validCommand(const std::string& c) { return c == "play" || c == "pause" || c == "toggle" || c == "next" || c == "previous"; }
// Spotify Web API calls for a command (method, path); toggle needs to know if it plays now
inline std::pair<std::string, std::string> spotifyCall(const std::string& cmd, bool playingNow) {
  if (cmd == "next") return {"POST", "/me/player/next"};
  if (cmd == "previous") return {"POST", "/me/player/previous"};
  if (cmd == "pause" || (cmd == "toggle" && playingNow)) return {"PUT", "/me/player/pause"};
  return {"PUT", "/me/player/play"};
}

// APP SOUND: programs whose sound is a streaming service's. Recording them is refused (live metering / routing stays as it was).
inline bool isStreamingApp(const std::string& name) {
  std::string s; for (char c : name) s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (const char* k : {"spotify", "deezer", "tidal", "apple music", "applemusic", "itunes", "youtube music", "youtubemusic", "amazon music", "amazonmusic", "qobuz", "soundcloud", "pandora", "napster"}) if (s.find(k) != std::string::npos) return true;
  return s == "music" || s == "music.exe" || s == "music.app";
}

}  // namespace am::streaming
