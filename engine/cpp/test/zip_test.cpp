// The ZIP writer of the console's EXPORT: what it writes reads back; unsafe names and oversized input are refused.
#include <cstdio>
#include <filesystem>
#include <string>

#include "zip/zipstore.hpp"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

int main(int argc, char** argv) {
  namespace fs = std::filesystem; using namespace am::zip;
  CHECK(crc32("123456789") == 0xCBF43926u && crc32("") == 0);   // the standard check value
  const fs::path dir = fs::temp_directory_path() / "amix_zip_test"; std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir);
  const std::string path = (argc > 1 ? std::string(argv[1]) : (dir / "export.zip").string()); std::string err;
  std::string big(300000, 'x'); for (std::size_t i = 0; i < big.size(); ++i) big[i] = static_cast<char>(i * 31 + 7);
  const std::vector<Entry> files = {{"README.txt", "AmF X1 export\n"}, {"console/autosave.json", "{\"strips\":[1,2,3]}"}, {"console/pads.json", "{}"}, {"empty.txt", ""}, {"data/blob (1).bin", big}};
  CHECK(writeZip(path, files, err)); std::vector<Listed> got; CHECK(listZip(path, got, err)); CHECK(got.size() == files.size());
  for (std::size_t i = 0; i < got.size() && i < files.size(); ++i) CHECK(got[i].name == files[i].name && got[i].size == files[i].data.size() && got[i].crcOk);
  for (const char* bad : {"", "/etc/passwd", "../up.txt", "a/../b.txt", "C:/x.txt", "back\\slash.txt", ".hidden", "dir/", "a//b", "tab\tname", "nul\x01", "quote\".txt"}) { CHECK(!safeName(bad)); CHECK(!writeZip(path + ".bad", {{bad, "x"}}, err)); }
  CHECK(!writeZip(path + ".bad", {}, err)); CHECK(!writeZip(path + ".bad", {{"a.txt", "1"}, {"a.txt", "2"}}, err));   // nothing, and the same name twice
  CHECK(!fs::exists(path + ".bad"));
  { std::vector<Listed> l; std::ofstream(dir / "junk.zip") << "not a zip at all, just some text that is long enough"; CHECK(!listZip((dir / "junk.zip").string(), l, err)); }
  if (argc <= 1) fs::remove_all(dir, ec);
  if (g_fail) { std::fprintf(stderr, "zip_test: %d failure(s)\n", g_fail); return 1; }
  std::printf("zip_test: five files written and read back with matching CRC-32; unsafe names, an empty list and a repeated name refused\nzip_test: OK\n"); return 0;
}
