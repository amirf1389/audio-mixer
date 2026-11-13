// Reading audio files of any common kind into an arr::Audio (float, stereo, the file's own rate).
//   WAV (PCM / float)                built in, every platform (arr::readWav)
//   FLAC (8 .. 32-bit, 1 .. 8 ch)    built in, every platform: a complete FLAC decoder (fixed and LPC predictors, Rice residuals, stereo decorrelation)
//   AIFF / AIFF-C (PCM)              built in, every platform
//   M4A / AAC, MP3, WMA, ALAC, ...   Windows: the system's own decoders (Media Foundation); OGG / Opus when Windows has them (Web Media Extensions)
//                                    Linux / macOS: ffmpeg, when it is installed (run directly, never through a shell)
// More than two channels: the first two are kept; mono is duplicated to both sides.
#pragma once
#include <string>
#include <vector>
#include "arrange/arrange.hpp"

namespace am::arr {

bool readAudio(const std::string& path, Audio& out, std::string& err);
bool readFlac(const std::string& path, Audio& out, std::string& err);
bool readAiff(const std::string& path, Audio& out, std::string& err);
std::vector<std::string> audioExtensions();   // "wav", "flac", ... : what readAudio is meant for on this platform (for file dialogs and folder scans)
bool isAudioFile(const std::string& path);    // by extension

}  // namespace am::arr
