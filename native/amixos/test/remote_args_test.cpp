// Remote::openArgs: what the console hands the remote service for each state it can be in. The service must open exactly the console's
// devices and never the system's default input (the microphone) by itself: a console with no input open hands over no input ("--in -").
#include <cstdio>
#include <QStringList>
#include "Remote.h"

namespace {
int fails = 0;
void expect(const char* what, const Remote::Takeover& t, const QStringList& want) {
  const QStringList got = Remote::openArgs(t);
  if (got != want) { ++fails; std::fprintf(stderr, "FAIL %s\n  got  %s\n  want %s\n", what, qPrintable(got.join(' ')), qPrintable(want.join(' '))); }
  const int in = got.indexOf("--in");
  if (in < 0 || in + 1 >= got.size()) { ++fails; std::fprintf(stderr, "FAIL %s: no --in (the service would choose an input itself)\n", what); }
  else if (got[in + 1] == "default" && !(t.running && t.inChannels > 0 && t.in.isEmpty())) { ++fails; std::fprintf(stderr, "FAIL %s: the default microphone without the console having it\n", what); }
}
Remote::Takeover tk(bool running, const QString& backend, const QString& in, const QString& out, double rate, int frames, int inCh, int outCh) {
  Remote::Takeover t; t.running = running; t.backend = backend; t.in = in; t.out = out; t.rate = rate; t.frames = frames; t.inChannels = inCh; t.outChannels = outCh; return t; }
}  // namespace

int main() {
  // the engine off: the remembered output, no input
  expect("console off", tk(false, "wasapi", "-", "{spk}", 48000, 256, 0, 2), {"--open", "wasapi", "--out", "{spk}", "--in", "-", "--rate", "48000", "--frames", "256", "--out-channels", "2"});
  expect("console off, a microphone remembered", tk(false, "wasapi", "{mic}", "{spk}", 48000, 256, 2, 2), {"--open", "wasapi", "--out", "{spk}", "--in", "-", "--rate", "48000", "--frames", "256", "--out-channels", "2"});
  expect("nothing known", tk(false, "", "", "", 0, 0, 0, 0), {"--open", "auto", "--in", "-"});
  // the engine on, outputs only (AUTO INPUT off: the usual Windows start)
  expect("outputs only", tk(true, "wasapi", "-", "{spk}", 48000, 480, 0, 2), {"--open", "wasapi", "--out", "{spk}", "--in", "-", "--rate", "48000", "--frames", "480", "--out-channels", "2"});
  expect("an input id but no channels open", tk(true, "wasapi", "{mic}", "{spk}", 48000, 480, 0, 2), {"--open", "wasapi", "--out", "{spk}", "--in", "-", "--rate", "48000", "--frames", "480", "--out-channels", "2"});
  // the engine on with an input: exactly that input
  expect("the default microphone", tk(true, "wasapi", "", "{spk}", 48000, 480, 2, 2), {"--open", "wasapi", "--out", "{spk}", "--in", "default", "--in-channels", "2", "--rate", "48000", "--frames", "480", "--out-channels", "2"});
  expect("a picked microphone", tk(true, "wasapi", "{usb-mic}", "{spk}", 44100, 256, 1, 2), {"--open", "wasapi", "--out", "{spk}", "--in", "{usb-mic}", "--in-channels", "1", "--rate", "44100", "--frames", "256", "--out-channels", "2"});
  expect("COMPUTER SOUND", tk(true, "wasapi", "loopback:system", "{spk}", 48000, 480, 2, 2), {"--open", "wasapi", "--out", "{spk}", "--in", "loopback:system", "--in-channels", "2", "--rate", "48000", "--frames", "480", "--out-channels", "2"});
  expect("an ASIO interface", tk(true, "asio", "{a1}", "{a1}", 96000, 128, 8, 8), {"--open", "asio", "--out", "{a1}", "--in", "{a1}", "--in-channels", "8", "--rate", "96000", "--frames", "128", "--out-channels", "8"});
  expect("the default output", tk(true, "alsa", "-", "", 48000, 256, 0, 2), {"--open", "alsa", "--in", "-", "--rate", "48000", "--frames", "256", "--out-channels", "2"});
  if (fails) { std::fprintf(stderr, "remote args: %d failure(s)\n", fails); return 1; }
  std::printf("remote args: ok\n");
  return 0;
}
