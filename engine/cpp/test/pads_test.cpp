// 1000 SOUND EFFECTS + 1000 CUSTOM PADS: every effect renders, is deterministic, distinct and levelled; the pads file is shared between two
// programs (the console and the remote service) and checked like untrusted input; the player plays any pad and keeps its memory bounded.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "arrange/arrange.hpp"
#include "samples/pad_player.hpp"
#include "samples/padbank.hpp"
#include "samples/sfx.hpp"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

int main() {
  namespace fs = std::filesystem;
  // SOUND EFFECTS
  CHECK(am::sfx::families().size() == 20 && am::sfx::kCount == 1000);
  std::set<std::string> names; int silent = 0, hot = 0; double longest = 0;
  for (int id = 1; id <= am::sfx::kCount; ++id) {
    const am::sfx::Info in = am::sfx::info(id); names.insert(in.name); longest = std::max(longest, in.seconds);
    std::vector<float> L, R; CHECK(am::sfx::render(id, 24000, L, R)); CHECK(L.size() == R.size() && L.size() == static_cast<std::size_t>(in.seconds * 24000));
    float pk = 0; bool finite = true; for (std::size_t i = 0; i < L.size(); ++i) { finite = finite && std::isfinite(L[i]) && std::isfinite(R[i]); pk = std::max(pk, std::max(std::fabs(L[i]), std::fabs(R[i]))); }
    CHECK(finite); if (pk < 0.5f) ++silent; if (pk > 0.75f) ++hot;
  }
  CHECK(names.size() == 1000); CHECK(silent == 0); CHECK(hot == 0); CHECK(longest <= 6.0);
  CHECK(am::sfx::info(0).id == 0 && am::sfx::info(1001).id == 0 && am::sfx::info(1).name == "Whoosh 01" && am::sfx::info(1000).name == "Bell 50" && am::sfx::info(260).family == "Laser");
  { std::vector<float> a, b, c, d; am::sfx::render(257, 48000, a, b); am::sfx::render(257, 48000, c, d); CHECK(a == c && b == d);   // deterministic
    std::vector<float> e, f; am::sfx::render(258, 48000, e, f); CHECK(e != a); }   // a neighbouring variation sounds different
  std::printf("pads_test: 1000 effects in 20 families, all levelled (-3 dBFS), names unique, longest %.2f s\n", longest);

  // THE PADS FILE, shared by two programs
  const fs::path dir = fs::temp_directory_path() / "amix_pads_test"; std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir);
  const std::string file = (dir / "pads.json").string();
  am::pads::Bank console(file), service(file);
  CHECK(console.at(1).source == "sample:1" && console.at(1).name == "Kick Deep" && console.at(101).source == "sfx:1" && console.at(1000).source == "sfx:900" && console.at(0).source.empty());
  std::string err;
  CHECK(console.set(5, {"sfx:777", "", 3, 4}, true, err) && console.at(5).name == am::sfx::info(777).name);
  CHECK(!console.set(6, {"sfx:1001", "", 0, 0}, true, err) && !console.set(6, {"sample:0", "", 0, 0}, true, err) && !console.set(6, {"bogus", "", 0, 0}, true, err) && !console.set(1001, {"sfx:1", "", 0, 0}, true, err));
  CHECK(console.set(7, {"", "", 0, 0}, true, err) && console.at(7).source.empty());   // an empty pad
  CHECK(console.set(8, {"sample:3", "My\nKick\x01 Name", 99, 42}, true, err) && console.at(8).name == "MyKick Name" && console.at(8).gainDb == 12 && console.at(8).color == 7);
  // a file: only from the computer, only an audio file that exists
  const std::string wav = (dir / "my hit.wav").string(); { std::vector<float> L(4800, 0.25f), R(4800, -0.25f); CHECK(am::arr::writeWav24(wav, L, R, 48000, err)); }
  CHECK(!console.set(9, {"file:" + wav, "", 0, 0}, false, err) && err == "files are chosen on the computer");
  CHECK(!console.set(9, {"file:" + (dir / "missing.wav").string(), "", 0, 0}, true, err));
  CHECK(!console.set(9, {"file:" + file, "", 0, 0}, true, err));   // not an audio file
  CHECK(console.set(9, {"file:" + wav, "", -6, 2}, true, err) && console.at(9).name == "my hit");
  const int v0 = service.version();
  CHECK(console.save(err)); CHECK(service.reloadIfChanged() && service.at(5).source == "sfx:777" && service.at(9).source == "file:" + wav && service.at(7).source.empty() && service.version() > v0);
  CHECK(!service.reloadIfChanged());   // nothing new
  CHECK(service.set(5, {"sfx:12", "Door", 0, 1}, false, err) && service.save(err)); CHECK(console.reloadIfChanged() && console.at(5).name == "Door");
  { const am::json::Value j = service.toJson(1, 10); CHECK(j["pads"].size() == 10 && j["count"].asInt() == 1000 && j["pads"].at(4)["custom"].asBool() && !j["pads"].at(0)["custom"].asBool()); }
  CHECK(console.resetPad(5) && console.at(5).source == "sample:5");
  // a damaged or hostile file: refused, the pads stay as they were
  { std::ofstream(file, std::ios::trunc) << "{\"format\":\"amf-x1-pads\",\"pads\":[{\"n\":3,\"source\":\"sfx:99999\"},{\"n\":4,\"source\":\"sfx:4\",\"gainDb\":1e309,\"name\":\"" << std::string(500, 'x') << "\"},{\"n\":-1,\"source\":\"sfx:2\"}]}"; }
  am::pads::Bank damaged(file); CHECK(damaged.at(3).source == "sample:3" && damaged.at(4).source == "sfx:4" && damaged.at(4).name.size() == 48 && damaged.at(4).gainDb == 0);
  { std::ofstream(file, std::ios::trunc) << "not json"; } am::pads::Bank junk(file); CHECK(junk.at(1).source == "sample:1");
  std::printf("pads_test: the pads file: shared both ways, files only from the computer, damaged files read safely\n");

  // THE PLAYER
  am::pads::Player p; CHECK(!p.ready()); CHECK(!p.play("sfx:1", 101, 0, err) && err == "no sound card open");
  p.prepare(48000); CHECK(p.ready() && p.kept() == 100);
  std::vector<float> L(256), R(256); float maxPk = 0;
  CHECK(p.play("sfx:260", 360, 0, err)); p.render(L.data(), R.data(), 256, 48000); CHECK(p.active.load() == 1 && p.peak.load() > 0.05f);
  CHECK(p.play("file:" + wav, 9, -6, err)); CHECK(!p.play("", 7, 0, err) && err == "empty pad"); CHECK(!p.play("file:" + wav, 0, 0, err));
  p.render(L.data(), R.data(), 256, 48000); CHECK(p.active.load() == 2);
  for (int i = 0; i < 40; ++i) { CHECK(p.play("sample:" + std::to_string(1 + i % 20), 1, 0, err)); if (i % 8 == 7) { p.render(L.data(), R.data(), 256, 48000); maxPk = std::max(maxPk, p.peak.load()); } }
  CHECK(p.active.load() == am::pads::Player::kVoices);   // polyphony capped, the oldest stolen
  p.stopAll(); p.render(L.data(), R.data(), 256, 48000); CHECK(p.active.load() == 0);
  // memory stays bounded: play every effect once
  for (int id = 1; id <= 1000; ++id) { CHECK(p.play("sfx:" + std::to_string(id), 100 + id, 0, err)); p.render(L.data(), R.data(), 64, 48000); if (id % 16 == 0) { p.stopAll(); p.render(L.data(), R.data(), 64, 48000); } }
  CHECK(p.bytesKept() <= am::pads::Player::kBudgetBytes); std::printf("pads_test: player: 16 voices, 1000 effects played, %d sounds kept in %.0f MB (budget %.0f MB)\n", p.kept(), p.bytesKept() / 1048576.0, am::pads::Player::kBudgetBytes / 1048576.0);
  // a new device rate renders again
  p.prepare(44100); CHECK(p.kept() == 100 && p.rate() == 44100);
  for (int i = 0; i < 4; ++i) p.render(L.data(), R.data(), 64, 44100);
  fs::remove_all(dir, ec);
  if (g_fail) { std::fprintf(stderr, "pads_test: %d failure(s)\n", g_fail); return 1; }
  std::printf("pads_test: OK\n"); return 0;
}
