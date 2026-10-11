/* The ASIO 2 driver interface, as far as Audio Mixer's driver needs it: the types, the callback table and the IASIO COM interface (method ORDER is the
 * binary contract with ASIO hosts: DAWs). Written from the published interface description; no Steinberg SDK file is included or copied.
 * "ASIO" is a trademark of Steinberg Media Technologies GmbH. Calling conventions: like the SDK, the IASIO methods use the compiler's default
 * (thiscall for C++ member functions on 32-bit Windows), the IUnknown methods are stdcall, the callbacks are plain C functions. */
#ifndef AM_ASIO_IFACE_H
#define AM_ASIO_IFACE_H
#include <windows.h>

typedef long ASIOBool;
typedef long ASIOError;
typedef double ASIOSampleRate;
typedef long ASIOSampleType;
enum { ASIOFalse = 0, ASIOTrue = 1 };

enum {
  ASE_OK = 0, ASE_SUCCESS = 0x3f4847a0, ASE_NotPresent = -1000, ASE_HWMalfunction, ASE_InvalidParameter, ASE_InvalidMode, ASE_SPNotAdvancing, ASE_NoClock, ASE_NoMemory
};
enum { ASIOSTInt16LSB = 16, ASIOSTInt24LSB = 17, ASIOSTInt32LSB = 18 };
enum { kAsioSelectorSupported = 1, kAsioEngineVersion, kAsioResetRequest, kAsioBufferSizeChange, kAsioResyncRequest, kAsioLatenciesChanged, kAsioSupportsTimeInfo };

struct ASIOTimeStamp { unsigned long hi, lo; };   /* nanoseconds, 64 bit as two 32-bit halves (unsigned long is 32 bit on Windows) */
struct ASIOSamples { unsigned long hi, lo; };
struct ASIOClockSource { long index; long associatedChannel; long associatedGroup; ASIOBool isCurrentSource; char name[32]; };
struct ASIOChannelInfo { long channel; ASIOBool isInput; ASIOBool isActive; long channelGroup; ASIOSampleType type; char name[32]; };
struct ASIOBufferInfo { ASIOBool isInput; long channelNum; void *buffers[2]; };
struct ASIOTime;   /* only used through bufferSwitchTimeInfo, which this driver never calls */
struct ASIOCallbacks {
  void (*bufferSwitch)(long doubleBufferIndex, ASIOBool directProcess);
  void (*sampleRateDidChange)(ASIOSampleRate sRate);
  long (*asioMessage)(long selector, long value, void *message, double *opt);
  ASIOTime *(*bufferSwitchTimeInfo)(ASIOTime *params, long doubleBufferIndex, ASIOBool directProcess);
};

struct IASIO : public IUnknown {
  virtual ASIOBool init(void *sysHandle) = 0;
  virtual void getDriverName(char *name) = 0;                 /* up to 32 bytes with the terminating zero */
  virtual long getDriverVersion() = 0;
  virtual void getErrorMessage(char *string) = 0;             /* up to 124 bytes */
  virtual ASIOError start() = 0;
  virtual ASIOError stop() = 0;
  virtual ASIOError getChannels(long *numInputChannels, long *numOutputChannels) = 0;
  virtual ASIOError getLatencies(long *inputLatency, long *outputLatency) = 0;
  virtual ASIOError getBufferSize(long *minSize, long *maxSize, long *preferredSize, long *granularity) = 0;
  virtual ASIOError canSampleRate(ASIOSampleRate sampleRate) = 0;
  virtual ASIOError getSampleRate(ASIOSampleRate *sampleRate) = 0;
  virtual ASIOError setSampleRate(ASIOSampleRate sampleRate) = 0;
  virtual ASIOError getClockSources(ASIOClockSource *clocks, long *numSources) = 0;
  virtual ASIOError setClockSource(long reference) = 0;
  virtual ASIOError getSamplePosition(ASIOSamples *sPos, ASIOTimeStamp *tStamp) = 0;
  virtual ASIOError getChannelInfo(ASIOChannelInfo *info) = 0;
  virtual ASIOError createBuffers(ASIOBufferInfo *bufferInfos, long numChannels, long bufferSize, ASIOCallbacks *callbacks) = 0;
  virtual ASIOError disposeBuffers() = 0;
  virtual ASIOError controlPanel() = 0;
  virtual ASIOError future(long selector, void *opt) = 0;
  virtual ASIOError outputReady() = 0;
};

/* the layout the hosts were built against */
static_assert(sizeof(ASIOTimeStamp) == 8 && sizeof(ASIOSamples) == 8, "ASIOTimeStamp / ASIOSamples are 8 bytes");
static_assert(sizeof(ASIOClockSource) == 48, "ASIOClockSource is 48 bytes");
static_assert(sizeof(ASIOChannelInfo) == 52, "ASIOChannelInfo is 52 bytes");
static_assert(sizeof(ASIOBufferInfo) == 8 + 2 * sizeof(void *), "ASIOBufferInfo");
static_assert(sizeof(ASIOCallbacks) == 4 * sizeof(void *), "ASIOCallbacks");
#endif
