// AmF X1 extension ABI. An extension is a DLL (.so on Linux) next to the console that runs one of the
// product's programs inside the console's own process, so it opens the sound card with the same
// rights the console has. The same source also builds the stand-alone program (its main() calls
// amfb_ext_start and waits), so a console without the DLL still starts the program as a process.
//
//   amfb_ext_name()                     the program's name, static string
//   amfb_ext_start(argc, argv)          starts the program on its own thread; 0 = started, else the
//                                       program's exit code (it ran to the end at once, e.g. --list)
//   amfb_ext_running()                  1 while the program's thread runs
//   amfb_ext_status()                   the last line the program printed (static buffer, 200 chars)
//   amfb_ext_output()                   everything the program printed since the last call (static buffer)
//   amfb_ext_stop()                     asks the program to stop and waits for its thread
#pragma once
#ifdef _WIN32
#  define AMFB_EXT_EXPORT __declspec(dllexport)
#else
#  define AMFB_EXT_EXPORT __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
AMFB_EXT_EXPORT const char* amfb_ext_name(void);
AMFB_EXT_EXPORT int amfb_ext_start(int argc, const char* const* argv);
AMFB_EXT_EXPORT int amfb_ext_running(void);
AMFB_EXT_EXPORT const char* amfb_ext_status(void);
AMFB_EXT_EXPORT const char* amfb_ext_output(void);
AMFB_EXT_EXPORT void amfb_ext_stop(void);
#ifdef __cplusplus
}
#endif
