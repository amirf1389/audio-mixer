// The VST 2.4 binary interface as the public, widely documented ABI (written here from that documentation, not copied from any SDK):
// the AEffect structure a plug-in returns from its entry point, the opcodes the host sends it, and the opcodes it may send back.
#pragma once
#include <cstdint>

namespace am::vst2::abi {

struct AEffect;
using DispatcherProc = std::intptr_t (*)(AEffect*, std::int32_t opcode, std::int32_t index, std::intptr_t value, void* ptr, float opt);
using ProcessProc = void (*)(AEffect*, float** inputs, float** outputs, std::int32_t frames);
using ProcessDoubleProc = void (*)(AEffect*, double** inputs, double** outputs, std::int32_t frames);
using SetParameterProc = void (*)(AEffect*, std::int32_t index, float value);
using GetParameterProc = float (*)(AEffect*, std::int32_t index);
using HostCallback = std::intptr_t (*)(AEffect*, std::int32_t opcode, std::int32_t index, std::intptr_t value, void* ptr, float opt);
using EntryPoint = AEffect* (*)(HostCallback);

constexpr std::int32_t kMagic = 0x56737450;   // 'VstP'

struct AEffect {
  std::int32_t magic;
  DispatcherProc dispatcher;
  ProcessProc process;              // deprecated accumulating process
  SetParameterProc setParameter;
  GetParameterProc getParameter;
  std::int32_t numPrograms, numParams, numInputs, numOutputs;
  std::int32_t flags;
  std::intptr_t resvd1, resvd2;
  std::int32_t initialDelay;
  std::int32_t realQualities, offQualities;
  float ioRatio;
  void* object;
  void* user;
  std::int32_t uniqueID, version;
  ProcessProc processReplacing;
  ProcessDoubleProc processDoubleReplacing;
  char future[56];
};

enum Flags : std::int32_t { kHasEditor = 1 << 0, kCanReplacing = 1 << 4, kProgramChunks = 1 << 5, kIsSynth = 1 << 8, kNoSoundInStop = 1 << 9, kCanDoubleReplacing = 1 << 12 };

// opcodes the host sends to the plug-in
enum EffectOpcode : std::int32_t {
  effOpen = 0, effClose = 1, effSetProgram = 2, effGetProgram = 3, effSetProgramName = 4, effGetProgramName = 5, effGetParamLabel = 6, effGetParamDisplay = 7, effGetParamName = 8,
  effSetSampleRate = 10, effSetBlockSize = 11, effMainsChanged = 12, effEditGetRect = 13, effEditOpen = 14, effEditClose = 15, effEditIdle = 19,
  effGetChunk = 23, effSetChunk = 24, effProcessEvents = 25, effCanBeAutomated = 26, effString2Parameter = 27, effGetProgramNameIndexed = 29,
  effGetInputProperties = 33, effGetOutputProperties = 34, effGetPlugCategory = 35, effSetSpeakerArrangement = 42, effSetBypass = 44,
  effGetEffectName = 45, effGetVendorString = 47, effGetProductString = 48, effGetVendorVersion = 49, effCanDo = 51, effGetTailSize = 52,
  effGetParameterProperties = 56, effGetVstVersion = 58, effStartProcess = 71, effStopProcess = 72, effSetProcessPrecision = 77
};
// opcodes the plug-in sends to the host
enum HostOpcode : std::int32_t {
  audioMasterAutomate = 0, audioMasterVersion = 1, audioMasterCurrentId = 2, audioMasterIdle = 3, audioMasterGetTime = 7, audioMasterProcessEvents = 8,
  audioMasterIOChanged = 13, audioMasterSizeWindow = 15, audioMasterGetSampleRate = 16, audioMasterGetBlockSize = 17, audioMasterGetInputLatency = 18, audioMasterGetOutputLatency = 19,
  audioMasterGetCurrentProcessLevel = 23, audioMasterGetAutomationState = 24, audioMasterGetVendorString = 32, audioMasterGetProductString = 33, audioMasterGetVendorVersion = 34,
  audioMasterCanDo = 37, audioMasterGetLanguage = 38, audioMasterUpdateDisplay = 42, audioMasterBeginEdit = 43, audioMasterEndEdit = 44
};
constexpr std::int32_t kVstVersion = 2400;
constexpr std::int32_t kStringMax = 256;   // plug-ins are allowed 8..64 characters; a larger buffer forgives the ones that write more

}  // namespace am::vst2::abi
