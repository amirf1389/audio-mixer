// planted unsafe C++ for the audit's unit tests (never compiled)
#include <cstdio>
#include <cstdlib>
#include <cstring>
void a(char* out, const char* in, size_t n, const char* cmd) {
  char line[64];
  gets(line);
  strcpy(out, in);
  sprintf(out, "%s", in);
  std::system(cmd);
  memcpy(out, in, n);
  // strcpy(out, in);   a comment is not a call
  const char* s = "strcat(x, y)";
  snprintf(out, 8, "%s", in);
  memcpy(out, in, sizeof line);
}
