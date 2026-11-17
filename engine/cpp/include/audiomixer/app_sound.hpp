// APP SOUND: the sound of ONE program (Spotify, TIDAL, Deezer, Roon, Apple Music, a browser playing YouTube ...) as a console input, through
// Windows' process loopback (Windows 10 2004 / build 19041 and later): ActivateAudioInterfaceAsync on VAD\Process_Loopback with that program's
// process tree INCLUDED, so nothing else on the computer is heard. The input id is "loopback:app:<process id>".
// The list comes from Windows' own audio sessions on the default output (IAudioSessionManager2): the programs that have opened a sound stream.
// It is the sound Windows plays, as the user hears it: nothing is downloaded, decoded or copied from the service, and a stream the service
// protects (DRM, "protected audio path") may come through silent; the console says so and does not work around it.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace am::app {

struct Session {
  std::uint32_t pid = 0;      // the process that owns the audio session
  std::string exe;            // its file name, e.g. "Spotify.exe"
  std::string label;          // what the console shows, e.g. "Spotify"
  std::string kind;           // "streaming" (a music service's app), "browser" (YouTube and web players), "player", "other"
  bool active = false;        // the session is playing right now
};

/** the friendly name and kind of a program from its file name (pure) */
void describe(const std::string& exe, std::string& label, std::string& kind);
/** "loopback:app:<pid>" and back (pure); parse returns false for anything else */
std::string deviceId(std::uint32_t pid);
bool parseDeviceId(const std::string& id, std::uint32_t& pid);

/** the programs with an audio session on the default output, this program excluded, one entry per process (the playing ones first);
 *  empty where Windows has no such list (other systems, Windows before 2004) */
std::vector<Session> sessions();

}  // namespace am::app
