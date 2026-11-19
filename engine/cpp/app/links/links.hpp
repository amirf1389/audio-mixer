// STREAMING LINKS: what a pasted link is, without going to the network. A link to YouTube, Spotify, TIDAL, Deezer or TikTok is recognised
// by its host and path only: the service, the kind (track, album, playlist, video, live ...), the item's id, a clean https address to open
// in the service's own app or the browser, and the service's own app address where it has one (spotify:track:ID, tidal://track/ID ...).
// Nothing is fetched, played, downloaded or decoded here: the sound of a link is only ever heard as the service's own app plays it.
// Anything that is not one of these services, not https, or carries characters an id never has is refused (ok = false).
#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace am::links {

struct Link { bool ok = false; std::string service, serviceName, kind, id, web, app, error; };

namespace detail {
inline std::string lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); return s; }
inline bool idOk(const std::string& s, std::size_t maxLen = 64) { return !s.empty() && s.size() <= maxLen && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-' || c == '.'; }); }
inline std::vector<std::string> split(const std::string& s, char sep) { std::vector<std::string> v; std::string cur; for (char c : s) { if (c == sep) { if (!cur.empty()) v.push_back(cur); cur.clear(); } else cur += c; } if (!cur.empty()) v.push_back(cur); return v; }
inline std::string query(const std::string& q, const std::string& key) { for (const std::string& kv : split(q, '&')) { const auto e = kv.find('='); if (e != std::string::npos && kv.substr(0, e) == key) return kv.substr(e + 1); } return {}; }
inline bool isLang(const std::string& s) { return (s.size() == 2 && std::isalpha(static_cast<unsigned char>(s[0])) && std::isalpha(static_cast<unsigned char>(s[1]))) || (s.size() == 5 && s[2] == '-'); }
}  // namespace detail

inline Link parse(std::string url) {
  using namespace detail; Link l;
  while (!url.empty() && std::isspace(static_cast<unsigned char>(url.back()))) url.pop_back();
  while (!url.empty() && std::isspace(static_cast<unsigned char>(url.front()))) url.erase(url.begin());
  if (url.empty() || url.size() > 512) { l.error = "not a link"; return l; }
  for (unsigned char c : url) if (c < 0x21 || c > 0x7e || c == '"' || c == '<' || c == '>' || c == '\\' || c == '`') { l.error = "not a link"; return l; }
  // the service's own app address (spotify:track:ID)
  if (url.rfind("spotify:", 0) == 0) { const auto p = split(url, ':'); static const char* kinds[] = {"track", "album", "playlist", "artist", "episode", "show"};
    if (p.size() == 3 && std::find(std::begin(kinds), std::end(kinds), p[1]) != std::end(kinds) && idOk(p[2], 32)) { l.ok = true; l.service = "spotify"; l.serviceName = "Spotify"; l.kind = p[1]; l.id = p[2]; l.web = "https://open.spotify.com/" + l.kind + "/" + l.id; l.app = "spotify:" + l.kind + ":" + l.id; return l; }
    l.error = "not a Spotify address"; return l; }
  if (lower(url.substr(0, 8)) != "https://") { l.error = "only https links"; return l; }
  std::string rest = url.substr(8); const auto frag = rest.find('#'); if (frag != std::string::npos) rest.resize(frag);
  std::string q; const auto qm = rest.find('?'); if (qm != std::string::npos) { q = rest.substr(qm + 1); rest.resize(qm); }
  const auto slash = rest.find('/'); std::string host = lower(slash == std::string::npos ? rest : rest.substr(0, slash)); const std::string path = slash == std::string::npos ? "" : rest.substr(slash);
  if (host.find('@') != std::string::npos || host.find(':') != std::string::npos) { l.error = "not a link to a music service"; return l; }   // no user@host, no ports
  if (host.rfind("www.", 0) == 0) host = host.substr(4);
  if (host.rfind("m.", 0) == 0) host = host.substr(2);
  std::vector<std::string> p = split(path, '/');
  auto done = [&](const char* service, const char* name, const std::string& kind, const std::string& id, const std::string& web, const std::string& app) { l.ok = true; l.service = service; l.serviceName = name; l.kind = kind; l.id = id; l.web = web; l.app = app; return l; };
  if (host == "youtube.com" || host == "music.youtube.com" || host == "youtu.be") {
    const bool music = host == "music.youtube.com"; const std::string base = music ? "https://music.youtube.com" : "https://www.youtube.com";
    if (host == "youtu.be") { if (p.size() == 1 && idOk(p[0], 16)) return done("youtube", "YouTube", "video", p[0], "https://www.youtube.com/watch?v=" + p[0], ""); }
    else if (p.size() == 1 && p[0] == "watch") { const std::string v = query(q, "v"); if (idOk(v, 16)) return done("youtube", music ? "YouTube Music" : "YouTube", "video", v, base + "/watch?v=" + v, ""); }
    else if (p.size() == 1 && p[0] == "playlist") { const std::string v = query(q, "list"); if (idOk(v, 64)) return done("youtube", music ? "YouTube Music" : "YouTube", "playlist", v, base + "/playlist?list=" + v, ""); }
    else if (p.size() == 2 && (p[0] == "shorts" || p[0] == "live") && idOk(p[1], 16)) return done("youtube", "YouTube", p[0] == "live" ? "live" : "short", p[1], "https://www.youtube.com/" + p[0] + "/" + p[1], "");
    l.error = "a YouTube link to a video, a short, a live or a playlist"; return l; }
  if (host == "open.spotify.com") { if (!p.empty() && p[0].rfind("intl-", 0) == 0) p.erase(p.begin());
    static const char* kinds[] = {"track", "album", "playlist", "artist", "episode", "show"};
    if (p.size() == 2 && std::find(std::begin(kinds), std::end(kinds), p[0]) != std::end(kinds) && idOk(p[1], 32)) return done("spotify", "Spotify", p[0], p[1], "https://open.spotify.com/" + p[0] + "/" + p[1], "spotify:" + p[0] + ":" + p[1]);
    l.error = "a Spotify link to a track, an album, a playlist, an artist or a podcast"; return l; }
  if (host == "tidal.com" || host == "listen.tidal.com") { if (!p.empty() && p[0] == "browse") p.erase(p.begin());
    static const char* kinds[] = {"track", "album", "playlist", "artist", "video", "mix"};
    if (p.size() >= 2 && std::find(std::begin(kinds), std::end(kinds), p[0]) != std::end(kinds) && idOk(p[1], 48)) return done("tidal", "TIDAL", p[0], p[1], "https://tidal.com/browse/" + p[0] + "/" + p[1], "tidal://" + p[0] + "/" + p[1]);
    l.error = "a TIDAL link to a track, an album, a playlist, an artist or a video"; return l; }
  if (host == "deezer.com") { if (!p.empty() && isLang(p[0])) p.erase(p.begin());
    static const char* kinds[] = {"track", "album", "playlist", "artist", "episode", "show"};
    if (p.size() == 2 && std::find(std::begin(kinds), std::end(kinds), p[0]) != std::end(kinds) && idOk(p[1], 24) && std::all_of(p[1].begin(), p[1].end(), [](unsigned char c) { return std::isdigit(c); })) return done("deezer", "Deezer", p[0], p[1], "https://www.deezer.com/" + p[0] + "/" + p[1], "deezer://www.deezer.com/" + p[0] + "/" + p[1]);
    l.error = "a Deezer link to a track, an album, a playlist, an artist or a podcast"; return l; }
  if (host == "tiktok.com") {
    if (p.size() == 3 && p[0].size() > 1 && p[0][0] == '@' && idOk(p[0].substr(1), 32) && p[1] == "video" && idOk(p[2], 24)) return done("tiktok", "TikTok", "video", p[2], "https://www.tiktok.com/" + p[0] + "/video/" + p[2], "");
    if (p.size() == 2 && p[0].size() > 1 && p[0][0] == '@' && idOk(p[0].substr(1), 32) && p[1] == "live") return done("tiktok", "TikTok", "live", p[0].substr(1), "https://www.tiktok.com/" + p[0] + "/live", "");
    if (p.size() == 1 && p[0].size() > 1 && p[0][0] == '@' && idOk(p[0].substr(1), 32)) return done("tiktok", "TikTok", "profile", p[0].substr(1), "https://www.tiktok.com/" + p[0], "");
    l.error = "a TikTok link to a video, a live or a profile"; return l; }
  if (host == "vm.tiktok.com" || host == "vt.tiktok.com") { if (p.size() == 1 && idOk(p[0], 24)) return done("tiktok", "TikTok", "short link", p[0], "https://" + host + "/" + p[0], ""); l.error = "a TikTok short link"; return l; }
  l.error = "not a link to YouTube, Spotify, TIDAL, Deezer or TikTok"; return l;
}

}  // namespace am::links
