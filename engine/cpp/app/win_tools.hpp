// Windows-only setup options of audiomixer-native (win_tools.cpp). runWinTool returns true when an option was handled (rc = exit code).
#pragma once
namespace am { bool runWinTool(int argc, char** argv, int& rc); }
