// STREAMING: PKCE (RFC 7636 vector), the sign-in URL and token bodies carry no client secret, token refresh against a mocked HTTP
// server, the sealed token store round trip (and it never holds plain tokens), redacted logs, Spotify's player answer, APP SOUND refusal list.
#include <cstdio>
#include <string>
#include <vector>

#include "streaming/streaming.hpp"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)
static bool has(const std::string& s, const std::string& k) { return s.find(k) != std::string::npos; }

int main() {
  namespace st = am::streaming;
  // PKCE: the RFC 7636 appendix B vector, and a verifier from 32 random bytes is 43 characters
  CHECK(st::challengeFor("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk") == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
  st::Pkce pk; std::vector<std::uint8_t> rnd(32); for (int i = 0; i < 32; ++i) rnd[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i * 7 + 3);
  CHECK(st::makePkce(rnd, pk) && pk.verifier.size() == 43 && pk.challenge == st::challengeFor(pk.verifier) && pk.verifier.find_first_of("+/=") == std::string::npos);
  CHECK(!st::makePkce(std::vector<std::uint8_t>(16), pk));   // too little randomness is refused
  // providers: PKCE sign-in only where the service has it; the others say why and fall back to the media session
  const st::Provider* sp = st::find("spotify"); CHECK(sp && sp->login && sp->control); CHECK(st::find("tidal") && st::find("tidal")->login && !st::find("tidal")->control);
  for (const char* id : {"deezer", "youtube", "applemusic"}) { const st::Provider* p = st::find(id); CHECK(p && !p->login && !p->setup.empty()); }
  CHECK(st::validClientId("0123456789abcdefABCDEF") && !st::validClientId("short") && !st::validClientId("abc def ghi jkl") && !st::validClientId("abcdefgh&x=1"));
  // the sign-in URL: S256 challenge, state, loopback redirect, never a secret
  const std::string au = st::authorizeUrl(*sp, "clientid123", "st4te", pk.challenge);
  CHECK(has(au, "https://accounts.spotify.com/authorize?") && has(au, "code_challenge_method=S256") && has(au, "code_challenge=" + pk.challenge) && has(au, "state=st4te"));
  CHECK(has(au, "redirect_uri=http%3A%2F%2F127.0.0.1%3A43117%2Fcallback") && !has(au, "secret") && !has(au, pk.verifier));
  const std::string tb = st::tokenBody("clientid123", "c0de", pk.verifier);
  CHECK(has(tb, "grant_type=authorization_code") && has(tb, "code_verifier=" + pk.verifier) && has(tb, "client_id=clientid123") && !has(tb, "secret"));
  // tokens from the answer; an error answer is refused
  st::Tokens t; std::string err;
  CHECK(st::parseTokens(R"({"access_token":"AT1","token_type":"Bearer","expires_in":3600,"refresh_token":"RT1","scope":"x"})", 1000, t, err) && t.access == "AT1" && t.refresh == "RT1" && t.expiresAt == 4600);
  st::Tokens bad; CHECK(!st::parseTokens(R"({"error":"invalid_grant"})", 0, bad, err) && !bad.valid()); CHECK(!st::parseTokens("<html>", 0, bad, err));
  // refresh against a mocked server: not before it is due; due -> one POST with the refresh token and client id, no secret; the old refresh token is kept when none comes back
  int calls = 0; std::string lastBody, lastUrl;
  st::Http mock = [&](const std::string& m, const std::string& url, const std::vector<st::Header>& h, const std::string& body) {
    ++calls; lastBody = body; lastUrl = url; CHECK(m == "POST"); CHECK(!h.empty() && h[0].second == "application/x-www-form-urlencoded");
    return st::Response{200, R"({"access_token":"AT2","expires_in":3600})"}; };
  CHECK(st::ensureFresh(*sp, "clientid123", t, 2000, mock, err) && calls == 0 && t.access == "AT1");
  CHECK(st::ensureFresh(*sp, "clientid123", t, 4580, mock, err) && calls == 1 && t.access == "AT2" && t.refresh == "RT1" && t.expiresAt == 8180);
  CHECK(lastUrl == "https://accounts.spotify.com/api/token" && has(lastBody, "grant_type=refresh_token") && has(lastBody, "refresh_token=RT1") && has(lastBody, "client_id=clientid123") && !has(lastBody, "secret"));
  st::Http refuse = [&](const std::string&, const std::string&, const std::vector<st::Header>&, const std::string&) { return st::Response{400, R"({"error":"invalid_grant"})"}; };
  st::Tokens t2 = t; t2.expiresAt = 0; CHECK(!st::ensureFresh(*sp, "clientid123", t2, 9000, refuse, err) && !t2.valid());   // a refused refresh signs out
  st::Http down = [&](const std::string&, const std::string&, const std::vector<st::Header>&, const std::string&) { return st::Response{503, ""}; };
  st::Tokens t3 = t; t3.expiresAt = 0; CHECK(!st::ensureFresh(*sp, "clientid123", t3, 9000, down, err) && t3.valid());      // the service down: keep the sign-in
  // sealed storage: a mock OS sealing (XOR + tag); the store holds sealed bytes only; tampered or foreign bytes do not open
  auto xr = [](const std::string& in) { std::string o = in; for (char& c : o) c = static_cast<char>(c ^ 0x5a); return o; };
  st::Vault v; v.seal = [&](const std::string& in, std::string& out) { out = "SEALED" + xr(in); return true; };
  v.open = [&](const std::string& in, std::string& out) { if (in.rfind("SEALED", 0) != 0) return false; out = xr(in.substr(6)); return true; };
  CHECK(v.save("spotify", t)); CHECK(!has(v.blobs["spotify"], "AT2") && !has(v.blobs["spotify"], "RT1"));
  st::Tokens back; CHECK(v.load("spotify", back) && back.access == "AT2" && back.refresh == "RT1" && back.expiresAt == t.expiresAt);
  v.blobs["spotify"][0] = 'X'; st::Tokens no; CHECK(!v.load("spotify", no) && !no.valid());
  st::Vault none; CHECK(!none.save("spotify", t));   // no OS sealing: nothing is written (the sign-in lasts until the program closes)
  // logs: no token, code, verifier or Bearer value survives redact()
  const std::string line = st::redact("POST token code=c0de&code_verifier=" + pk.verifier + " -> {\"access_token\":\"AT2\",\"refresh_token\":\"RT1\"} Authorization: Bearer AT2");
  CHECK(!has(line, "AT2") && !has(line, "RT1") && !has(line, "c0de") && !has(line, pk.verifier) && has(line, "POST token"));
  CHECK(st::redact("barcode=12") == "barcode=12");   // only whole keys
  // Spotify's player answer and the commands
  const st::NowPlaying np = st::parseSpotifyPlayer(R"({"is_playing":true,"item":{"name":"Song","album":{"name":"LP"},"artists":[{"name":"A"},{"name":"B"}]}})");
  CHECK(np.any && np.playing && np.title == "Song" && np.artist == "A, B" && np.album == "LP"); CHECK(!st::parseSpotifyPlayer("").any);
  CHECK(st::spotifyCall("toggle", true).second == "/me/player/pause" && st::spotifyCall("toggle", false).second == "/me/player/play" && st::spotifyCall("next", false).first == "POST");
  CHECK(st::validCommand("next") && !st::validCommand("record") && !st::validCommand("download"));
  // APP SOUND: streaming apps are recognised (their sound is not recorded)
  for (const char* a : {"Spotify.exe", "Deezer", "TIDAL", "Music", "Apple Music", "Amazon Music"}) CHECK(st::isStreamingApp(a));
  for (const char* a : {"obs64.exe", "chrome.exe", "Discord", "MusicBee Pro Studio Editor"}) CHECK(!st::isStreamingApp(a));
  if (g_fail) { std::fprintf(stderr, "streaming_test: %d failure(s)\n", g_fail); return 1; }
  std::printf("streaming_test: all checks passed\n"); return 0;
}
