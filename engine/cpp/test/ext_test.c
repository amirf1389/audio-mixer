/* Loads an extension library the way the console does and runs it: start, status, stop. */
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#define LOAD(p) LoadLibraryA(p)
#define SYM(h, n) GetProcAddress(h, n)
#else
#include <dlfcn.h>
#include <unistd.h>
#define LOAD(p) dlopen(p, RTLD_NOW | RTLD_LOCAL)
#define SYM(h, n) dlsym(h, n)
#endif
typedef const char* (*name_fn)(void); typedef int (*start_fn)(int, const char* const*); typedef int (*running_fn)(void);
typedef const char* (*status_fn)(void); typedef void (*stop_fn)(void);
static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { ++fails; printf("FAIL %s\n", m); } } while (0)
static int run(const char* path, const char* expectName, const char* const* argv, int argc, const char* expectStatus) {
  void* h = LOAD(path); if (!h) { printf("FAIL cannot load %s\n", path); return ++fails; }
  name_fn name = (name_fn)SYM(h, "amfb_ext_name"); start_fn start = (start_fn)SYM(h, "amfb_ext_start"); running_fn running = (running_fn)SYM(h, "amfb_ext_running");
  status_fn status = (status_fn)SYM(h, "amfb_ext_status"); stop_fn stop = (stop_fn)SYM(h, "amfb_ext_stop");
  CHECK(name && start && running && status && stop, "all six calls exported");
  if (!(name && start && running && status && stop)) return fails;
  CHECK(strcmp(name(), expectName) == 0, expectName);
  CHECK(start(argc, argv) == 0, "start returns 0");
  int i; for (i = 0; i < 50 && !strstr(status(), expectStatus); ++i) usleep(100000);
  printf("  %s: %s\n", name(), status());
  CHECK(running() == 1, "running after start");
  CHECK(strstr(status(), expectStatus) != NULL, expectStatus);
  stop();
  CHECK(running() == 0, "stopped");
  CHECK(start(2, (const char* const[]){"x", "--help"}) == 0 && running() == 0, "--help ends at once");
  CHECK(start(2, (const char* const[]){"x", "--no-such-option"}) == 2, "a bad option returns 2");
  return fails;
}
int main(int argc, char** argv) {
  if (argc < 3) { printf("usage: ext_test NETWORK.so HYBRID.so\n"); return 2; }
  setenv("AMFB_SHM", "/amfb-ext-test", 1);
  const char* const net[] = {"net", "--port", "0", "--token", "t1234567890abcdef", "--tokens-file", "/tmp/amfb-ext-test.tokens.json"};
  run(argv[1], "AmF X1 Network", net, 7, "listening");
  const char* const hyb[] = {"hyb", "--backend", "null", "--tone", "2"};
  run(argv[2], "AmF X1 Hybrid", hyb, 5, "amfb-hybrid: null");
  printf(fails ? "ext_test: %d FAILED\n" : "ext_test: all passed\n", fails);
  return fails ? 1 : 0;
}
