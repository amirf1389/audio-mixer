// STREAMING LINKS: each service's links are recognised from the address alone; everything else is refused.
#include <cstdio>
#include <string>

#include "links/links.hpp"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

int main() {
  using am::links::parse;
  auto is = [](const std::string& url, const char* service, const char* kind, const char* id) { const auto l = parse(url); if (!(l.ok && l.service == service && l.kind == kind && l.id == id)) std::fprintf(stderr, "  %s -> ok %d %s %s %s (%s)\n", url.c_str(), l.ok, l.service.c_str(), l.kind.c_str(), l.id.c_str(), l.error.c_str()); return l.ok && l.service == service && l.kind == kind && l.id == id; };
  // YouTube
  CHECK(is("https://www.youtube.com/watch?v=dQw4w9WgXcQ", "youtube", "video", "dQw4w9WgXcQ"));
  CHECK(is("https://youtu.be/dQw4w9WgXcQ?si=abc", "youtube", "video", "dQw4w9WgXcQ"));
  CHECK(is("https://m.youtube.com/watch?feature=share&v=dQw4w9WgXcQ&t=10s", "youtube", "video", "dQw4w9WgXcQ"));
  CHECK(is("https://www.youtube.com/shorts/abcDEF12345", "youtube", "short", "abcDEF12345"));
  CHECK(is("https://music.youtube.com/playlist?list=PLabc_123-XYZ", "youtube", "playlist", "PLabc_123-XYZ"));
  CHECK(parse("https://youtu.be/dQw4w9WgXcQ").web == "https://www.youtube.com/watch?v=dQw4w9WgXcQ");
  // Spotify
  CHECK(is("https://open.spotify.com/track/4uLU6hMCjMI75M1A2tKUQC?si=xyz", "spotify", "track", "4uLU6hMCjMI75M1A2tKUQC"));
  CHECK(is("https://open.spotify.com/intl-de/album/1DFixLWuPkv3KT3TnV35m3", "spotify", "album", "1DFixLWuPkv3KT3TnV35m3"));
  CHECK(is("spotify:playlist:37i9dQZF1DXcBWIGoYBM5M", "spotify", "playlist", "37i9dQZF1DXcBWIGoYBM5M"));
  CHECK(parse("https://open.spotify.com/track/4uLU6hMCjMI75M1A2tKUQC").app == "spotify:track:4uLU6hMCjMI75M1A2tKUQC");
  // TIDAL
  CHECK(is("https://tidal.com/browse/track/12345678", "tidal", "track", "12345678"));
  CHECK(is("https://listen.tidal.com/album/87654321/track/1", "tidal", "album", "87654321"));
  CHECK(is("https://tidal.com/playlist/36ea71a8-445e-41a4-82ab-6628c581535d", "tidal", "playlist", "36ea71a8-445e-41a4-82ab-6628c581535d"));
  // Deezer
  CHECK(is("https://www.deezer.com/track/3135556", "deezer", "track", "3135556"));
  CHECK(is("https://www.deezer.com/en/album/302127", "deezer", "album", "302127"));
  CHECK(!parse("https://www.deezer.com/track/abc").ok);
  // TikTok
  CHECK(is("https://www.tiktok.com/@some.user/video/7301234567890123456", "tiktok", "video", "7301234567890123456"));
  CHECK(is("https://www.tiktok.com/@some.user/live", "tiktok", "live", "some.user"));
  CHECK(is("https://vm.tiktok.com/ZMabc123/", "tiktok", "short link", "ZMabc123"));
  // refused: other sites, look-alike hosts, other schemes, user@host, ports, script and odd characters
  for (const char* bad : {"", "hello", "http://www.youtube.com/watch?v=dQw4w9WgXcQ", "https://example.com/track/1", "https://youtube.com.evil.example/watch?v=dQw4w9WgXcQ", "https://evil.example/https://open.spotify.com/track/4uLU6hMCjMI75M1A2tKUQC",
                          "https://open.spotify.com@evil.example/track/4uLU6hMCjMI75M1A2tKUQC", "https://open.spotify.com:8443/track/4uLU6hMCjMI75M1A2tKUQC", "javascript:alert(1)", "file:///C:/Windows/system32", "https://www.youtube.com/watch?v=<script>",
                          "https://open.spotify.com/track/../../etc", "https://www.youtube.com/watch?v=dQw4w9 WgXcQ", "spotify:track:../x", "https://open.spotify.com/user/someone", "https://tidal.com/login"})
    if (parse(bad).ok) { std::fprintf(stderr, "FAIL accepted: %s\n", bad); ++g_fail; }
  CHECK(!parse(std::string(600, 'a')).ok);
  { const auto l = parse("  https://open.spotify.com/track/4uLU6hMCjMI75M1A2tKUQC \n"); CHECK(l.ok && l.web == "https://open.spotify.com/track/4uLU6hMCjMI75M1A2tKUQC"); }   // spaces around a pasted link
  if (g_fail) { std::fprintf(stderr, "links_test: %d failure(s)\n", g_fail); return 1; }
  std::printf("links_test: YouTube, Spotify, TIDAL, Deezer and TikTok links recognised; look-alike hosts, other schemes and odd characters refused\nlinks_test: OK\n"); return 0;
}
