/* Audio Mixer for macOS: a Core Audio "AudioServerPlugIn" (HAL plug-in, user-space, no kernel extension) that adds a virtual audio device "Audio Mixer".
 *
 *   output stream  (applications play to "Audio Mixer")      ->  the mixer reads it as an input source (LIVE SOURCES: READ)
 *   input  stream  (applications record from "Audio Mixer")  <-  what the mixer sends to the device    (LIVE SOURCES: WRITE)
 *
 * 2 channels, 32-bit float, 48 kHz (macOS converts for applications that use another format). The plug-in runs inside coreaudiod and talks to the running
 * Audio Mixer program through drivers/common/am_link.c on 127.0.0.1 (a coreaudiod sandbox profile that blocks loopback sockets would stop it; see README.md).
 *
 * STATUS: written against Apple's AudioServerPlugIn.h following the structure of Apple's NullAudio sample; NOT compiled or run in this repository's
 * tests (no macOS SDK here). Build and install: see README.md. Expect to debug it on a Mac (log with `log stream --predicate 'process == "coreaudiod"'`).
 */
#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>

#include "../../drivers/common/am_link.h"

#define kPlugIn_BundleID "org.audiomixer.driver"
#define kDevice_UID "AudioMixerDevice_UID"
#define kDevice_Model "AudioMixerDevice_Model"
#define kDevice_Name "Audio Mixer"
#define kManufacturer "Audio Mixer"
enum { kObjectID_PlugIn = kAudioObjectPlugInObject, kObjectID_Device = 2, kObjectID_Stream_Input = 3, kObjectID_Stream_Output = 4 };
#define kChannels 2
#define kSampleRate 48000.0
#define kRingFrames 16384u
#define kZeroTimeStampPeriod 16384u   /* frames between the zero timestamps */

static pthread_mutex_t gStateMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t gIOMutex = PTHREAD_MUTEX_INITIALIZER;
static UInt32 gRefCount = 0;
static AudioServerPlugInHostRef gHost = NULL;
static UInt32 gStartCount = 0;
static UInt64 gAnchorHostTime = 0, gTimeStampCount = 0;
static Float64 gHostTicksPerFrame = 0;
static am_link *gLink = NULL;
static float gRecordRing[kRingFrames * kChannels];     /* what the mixer sent: read by the applications' input stream */
static UInt32 gRecHead = 0, gRecTail = 0, gRecCount = 0;
static dispatch_queue_t gConnectQueue;

static void onRecord(void *user, const int16_t *f, size_t n) {
  (void)user;
  pthread_mutex_lock(&gIOMutex);
  for (size_t i = 0; i < n; i++) {
    if (gRecCount == kRingFrames) { gRecTail = (gRecTail + 1) % kRingFrames; gRecCount--; }
    for (int c = 0; c < kChannels; c++) gRecordRing[gRecHead * kChannels + c] = (float)f[i * kChannels + c] / 32768.0f;
    gRecHead = (gRecHead + 1) % kRingFrames; gRecCount++;
  }
  pthread_mutex_unlock(&gIOMutex);
}

static void connectMixer(void) {   /* off the real-time thread; retried each time the device is started */
  char err[160]; am_link_config c;
  if (gLink && am_link_alive(gLink)) return;
  if (gLink) { am_link_close(gLink); gLink = NULL; }
  memset(&c, 0, sizeof c);
  c.name = kDevice_Name; c.channels = kChannels; c.rate = (int)kSampleRate; c.on_record = onRecord;
  gLink = am_link_open(&c, err, sizeof err);
}

/* ───────────── driver interface (the table Core Audio calls) ───────────── */
static HRESULT QueryInterface(void *d, REFIID iid, LPVOID *out);
static ULONG AddRef(void *d);
static ULONG Release(void *d);
static OSStatus Initialize(AudioServerPlugInDriverRef d, AudioServerPlugInHostRef host);
static OSStatus CreateDevice(AudioServerPlugInDriverRef d, CFDictionaryRef desc, const AudioServerPlugInClientInfo *c, AudioObjectID *out) { (void)d; (void)desc; (void)c; (void)out; return kAudioHardwareUnsupportedOperationError; }
static OSStatus DestroyDevice(AudioServerPlugInDriverRef d, AudioObjectID id) { (void)d; (void)id; return kAudioHardwareUnsupportedOperationError; }
static OSStatus AddDeviceClient(AudioServerPlugInDriverRef d, AudioObjectID id, const AudioServerPlugInClientInfo *c) { (void)d; (void)id; (void)c; return kAudioHardwareNoError; }
static OSStatus RemoveDeviceClient(AudioServerPlugInDriverRef d, AudioObjectID id, const AudioServerPlugInClientInfo *c) { (void)d; (void)id; (void)c; return kAudioHardwareNoError; }
static OSStatus PerformDeviceConfigurationChange(AudioServerPlugInDriverRef d, AudioObjectID id, UInt64 a, void *i) { (void)d; (void)id; (void)a; (void)i; return kAudioHardwareNoError; }
static OSStatus AbortDeviceConfigurationChange(AudioServerPlugInDriverRef d, AudioObjectID id, UInt64 a, void *i) { (void)d; (void)id; (void)a; (void)i; return kAudioHardwareNoError; }
static Boolean HasProperty(AudioServerPlugInDriverRef d, AudioObjectID o, pid_t pid, const AudioObjectPropertyAddress *a);
static OSStatus IsPropertySettable(AudioServerPlugInDriverRef d, AudioObjectID o, pid_t pid, const AudioObjectPropertyAddress *a, Boolean *out);
static OSStatus GetPropertyDataSize(AudioServerPlugInDriverRef d, AudioObjectID o, pid_t pid, const AudioObjectPropertyAddress *a, UInt32 qs, const void *q, UInt32 *size);
static OSStatus GetPropertyData(AudioServerPlugInDriverRef d, AudioObjectID o, pid_t pid, const AudioObjectPropertyAddress *a, UInt32 qs, const void *q, UInt32 size, UInt32 *used, void *data);
static OSStatus SetPropertyData(AudioServerPlugInDriverRef d, AudioObjectID o, pid_t pid, const AudioObjectPropertyAddress *a, UInt32 qs, const void *q, UInt32 size, const void *data) { (void)d; (void)o; (void)pid; (void)a; (void)qs; (void)q; (void)size; (void)data; return kAudioHardwareUnsupportedOperationError; }
static OSStatus StartIO(AudioServerPlugInDriverRef d, AudioObjectID dev, UInt32 client);
static OSStatus StopIO(AudioServerPlugInDriverRef d, AudioObjectID dev, UInt32 client);
static OSStatus GetZeroTimeStamp(AudioServerPlugInDriverRef d, AudioObjectID dev, UInt32 client, Float64 *sample, UInt64 *host, UInt64 *seed);
static OSStatus WillDoIOOperation(AudioServerPlugInDriverRef d, AudioObjectID dev, UInt32 client, UInt32 op, Boolean *willDo, Boolean *inPlace);
static OSStatus BeginIOOperation(AudioServerPlugInDriverRef d, AudioObjectID dev, UInt32 client, UInt32 op, UInt32 size, const AudioServerPlugInIOCycleInfo *info) { (void)d; (void)dev; (void)client; (void)op; (void)size; (void)info; return kAudioHardwareNoError; }
static OSStatus DoIOOperation(AudioServerPlugInDriverRef d, AudioObjectID dev, AudioObjectID stream, UInt32 client, UInt32 op, UInt32 size, const AudioServerPlugInIOCycleInfo *info, void *main, void *secondary);
static OSStatus EndIOOperation(AudioServerPlugInDriverRef d, AudioObjectID dev, UInt32 client, UInt32 op, UInt32 size, const AudioServerPlugInIOCycleInfo *info) { (void)d; (void)dev; (void)client; (void)op; (void)size; (void)info; return kAudioHardwareNoError; }

static AudioServerPlugInDriverInterface gInterface = {
  NULL, QueryInterface, AddRef, Release, Initialize, CreateDevice, DestroyDevice, AddDeviceClient, RemoveDeviceClient,
  PerformDeviceConfigurationChange, AbortDeviceConfigurationChange, HasProperty, IsPropertySettable, GetPropertyDataSize, GetPropertyData, SetPropertyData,
  StartIO, StopIO, GetZeroTimeStamp, WillDoIOOperation, BeginIOOperation, DoIOOperation, EndIOOperation
};
static AudioServerPlugInDriverInterface *gInterfacePtr = &gInterface;
static AudioServerPlugInDriverRef gDriverRef = &gInterfacePtr;

/* the factory named in Info.plist (CFPlugInFactories) */
__attribute__((visibility("default"))) void *AudioMixerDriverFactory(CFAllocatorRef alloc, CFUUIDRef typeUUID) {
  (void)alloc;
  if (CFEqual(typeUUID, kAudioServerPlugInTypeUUID)) return gDriverRef;
  return NULL;
}

static HRESULT QueryInterface(void *d, REFIID iid, LPVOID *out) {
  CFUUIDRef want = CFUUIDCreateFromUUIDBytes(NULL, iid);
  HRESULT r = E_NOINTERFACE;
  (void)d;
  if (CFEqual(want, IUnknownUUID) || CFEqual(want, kAudioServerPlugInDriverInterfaceUUID)) {
    pthread_mutex_lock(&gStateMutex); gRefCount++; pthread_mutex_unlock(&gStateMutex);
    *out = gDriverRef; r = S_OK;
  } else *out = NULL;
  CFRelease(want);
  return r;
}
static ULONG AddRef(void *d) { ULONG n; (void)d; pthread_mutex_lock(&gStateMutex); n = ++gRefCount; pthread_mutex_unlock(&gStateMutex); return n; }
static ULONG Release(void *d) { ULONG n; (void)d; pthread_mutex_lock(&gStateMutex); if (gRefCount > 0) gRefCount--; n = gRefCount; pthread_mutex_unlock(&gStateMutex); return n; }

static OSStatus Initialize(AudioServerPlugInDriverRef d, AudioServerPlugInHostRef host) {
  struct mach_timebase_info tb;
  (void)d;
  gHost = host;
  gConnectQueue = dispatch_queue_create("org.audiomixer.driver.connect", DISPATCH_QUEUE_SERIAL);
  mach_timebase_info(&tb);
  gHostTicksPerFrame = ((Float64)tb.denom / (Float64)tb.numer) * 1e9 / kSampleRate;
  return kAudioHardwareNoError;
}

/* ───────────── properties ───────────── */
static AudioStreamBasicDescription format(void) {
  AudioStreamBasicDescription f; memset(&f, 0, sizeof f);
  f.mSampleRate = kSampleRate; f.mFormatID = kAudioFormatLinearPCM;
  f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
  f.mBytesPerPacket = 4 * kChannels; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4 * kChannels; f.mChannelsPerFrame = kChannels; f.mBitsPerChannel = 32;
  return f;
}

static Boolean HasProperty(AudioServerPlugInDriverRef d, AudioObjectID o, pid_t pid, const AudioObjectPropertyAddress *a) {
  UInt32 size = 0; (void)d; (void)pid;
  return GetPropertyDataSize(d, o, pid, a, 0, NULL, &size) == kAudioHardwareNoError;
}

static OSStatus IsPropertySettable(AudioServerPlugInDriverRef d, AudioObjectID o, pid_t pid, const AudioObjectPropertyAddress *a, Boolean *out) {
  UInt32 size = 0; OSStatus r = GetPropertyDataSize(d, o, pid, a, 0, NULL, &size);
  *out = false;
  return r;                          /* nothing is settable: one fixed format, one fixed rate */
}

#define NEED(sz) do { if (size < (sz)) return kAudioHardwareBadPropertySizeError; *used = (sz); } while (0)

static OSStatus GetPropertyDataSize(AudioServerPlugInDriverRef d, AudioObjectID o, pid_t pid, const AudioObjectPropertyAddress *a, UInt32 qs, const void *q, UInt32 *size) {
  (void)d; (void)pid; (void)qs; (void)q;
  switch (o) {
    case kObjectID_PlugIn:
      switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass: case kAudioObjectPropertyClass: case kAudioObjectPropertyOwner: *size = sizeof(UInt32); return 0;
        case kAudioObjectPropertyManufacturer: case kAudioPlugInPropertyResourceBundle: *size = sizeof(CFStringRef); return 0;
        case kAudioObjectPropertyOwnedObjects: case kAudioPlugInPropertyDeviceList: *size = sizeof(AudioObjectID); return 0;
        case kAudioPlugInPropertyTranslateUIDToDevice: *size = sizeof(AudioObjectID); return 0;
      }
      break;
    case kObjectID_Device:
      switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass: case kAudioObjectPropertyClass: case kAudioObjectPropertyOwner:
        case kAudioDevicePropertyTransportType: case kAudioDevicePropertyClockDomain: case kAudioDevicePropertyDeviceIsAlive: case kAudioDevicePropertyDeviceIsRunning:
        case kAudioDevicePropertyDeviceCanBeDefaultDevice: case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice: case kAudioDevicePropertyLatency:
        case kAudioDevicePropertySafetyOffset: case kAudioDevicePropertyIsHidden: case kAudioDevicePropertyZeroTimeStampPeriod: *size = sizeof(UInt32); return 0;
        case kAudioObjectPropertyName: case kAudioObjectPropertyManufacturer: case kAudioDevicePropertyDeviceUID: case kAudioDevicePropertyModelUID: *size = sizeof(CFStringRef); return 0;
        case kAudioObjectPropertyOwnedObjects: *size = (a->mScope == kAudioObjectPropertyScopeGlobal ? 2 : 1) * sizeof(AudioObjectID); return 0;
        case kAudioDevicePropertyStreams: *size = (a->mScope == kAudioObjectPropertyScopeGlobal ? 2 : 1) * sizeof(AudioObjectID); return 0;
        case kAudioDevicePropertyRelatedDevices: *size = sizeof(AudioObjectID); return 0;
        case kAudioDevicePropertyNominalSampleRate: *size = sizeof(Float64); return 0;
        case kAudioDevicePropertyAvailableNominalSampleRates: *size = sizeof(AudioValueRange); return 0;
      }
      break;
    case kObjectID_Stream_Input: case kObjectID_Stream_Output:
      switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass: case kAudioObjectPropertyClass: case kAudioObjectPropertyOwner: case kAudioStreamPropertyIsActive: case kAudioStreamPropertyDirection:
        case kAudioStreamPropertyTerminalType: case kAudioStreamPropertyStartingChannel: case kAudioStreamPropertyLatency: *size = sizeof(UInt32); return 0;
        case kAudioStreamPropertyVirtualFormat: case kAudioStreamPropertyPhysicalFormat: *size = sizeof(AudioStreamBasicDescription); return 0;
        case kAudioStreamPropertyAvailableVirtualFormats: case kAudioStreamPropertyAvailablePhysicalFormats: *size = sizeof(AudioStreamRangedDescription); return 0;
      }
      break;
  }
  return kAudioHardwareUnknownPropertyError;
}

static OSStatus GetPropertyData(AudioServerPlugInDriverRef d, AudioObjectID o, pid_t pid, const AudioObjectPropertyAddress *a, UInt32 qs, const void *q, UInt32 size, UInt32 *used, void *data) {
  (void)d; (void)pid; (void)qs;
  switch (o) {
    case kObjectID_PlugIn:
      switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass: NEED(4); *(AudioClassID *)data = kAudioObjectClassID; return 0;
        case kAudioObjectPropertyClass: NEED(4); *(AudioClassID *)data = kAudioPlugInClassID; return 0;
        case kAudioObjectPropertyOwner: NEED(4); *(AudioObjectID *)data = kAudioObjectUnknown; return 0;
        case kAudioObjectPropertyManufacturer: NEED(sizeof(CFStringRef)); *(CFStringRef *)data = CFSTR(kManufacturer); return 0;
        case kAudioObjectPropertyOwnedObjects: case kAudioPlugInPropertyDeviceList: NEED(sizeof(AudioObjectID)); *(AudioObjectID *)data = kObjectID_Device; return 0;
        case kAudioPlugInPropertyTranslateUIDToDevice: {
          NEED(sizeof(AudioObjectID));
          *(AudioObjectID *)data = (q && CFStringCompare(*(CFStringRef *)q, CFSTR(kDevice_UID), 0) == kCFCompareEqualTo) ? kObjectID_Device : kAudioObjectUnknown;
          return 0;
        }
        case kAudioPlugInPropertyResourceBundle: NEED(sizeof(CFStringRef)); *(CFStringRef *)data = CFSTR(""); return 0;
      }
      break;
    case kObjectID_Device:
      switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass: NEED(4); *(AudioClassID *)data = kAudioObjectClassID; return 0;
        case kAudioObjectPropertyClass: NEED(4); *(AudioClassID *)data = kAudioDeviceClassID; return 0;
        case kAudioObjectPropertyOwner: NEED(4); *(AudioObjectID *)data = kObjectID_PlugIn; return 0;
        case kAudioObjectPropertyName: NEED(sizeof(CFStringRef)); *(CFStringRef *)data = CFSTR(kDevice_Name); return 0;
        case kAudioObjectPropertyManufacturer: NEED(sizeof(CFStringRef)); *(CFStringRef *)data = CFSTR(kManufacturer); return 0;
        case kAudioDevicePropertyDeviceUID: NEED(sizeof(CFStringRef)); *(CFStringRef *)data = CFSTR(kDevice_UID); return 0;
        case kAudioDevicePropertyModelUID: NEED(sizeof(CFStringRef)); *(CFStringRef *)data = CFSTR(kDevice_Model); return 0;
        case kAudioDevicePropertyTransportType: NEED(4); *(UInt32 *)data = kAudioDeviceTransportTypeVirtual; return 0;
        case kAudioDevicePropertyRelatedDevices: NEED(sizeof(AudioObjectID)); *(AudioObjectID *)data = kObjectID_Device; return 0;
        case kAudioDevicePropertyClockDomain: case kAudioDevicePropertyLatency: case kAudioDevicePropertySafetyOffset: case kAudioDevicePropertyIsHidden: NEED(4); *(UInt32 *)data = 0; return 0;
        case kAudioDevicePropertyDeviceIsAlive: NEED(4); *(UInt32 *)data = 1; return 0;
        case kAudioDevicePropertyDeviceIsRunning: NEED(4); pthread_mutex_lock(&gStateMutex); *(UInt32 *)data = gStartCount > 0; pthread_mutex_unlock(&gStateMutex); return 0;
        case kAudioDevicePropertyDeviceCanBeDefaultDevice: case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice: NEED(4); *(UInt32 *)data = 1; return 0;
        case kAudioDevicePropertyZeroTimeStampPeriod: NEED(4); *(UInt32 *)data = kZeroTimeStampPeriod; return 0;
        case kAudioObjectPropertyOwnedObjects: case kAudioDevicePropertyStreams: {
          UInt32 n = 0; AudioObjectID *ids = (AudioObjectID *)data;
          if (a->mScope == kAudioObjectPropertyScopeGlobal || a->mScope == kAudioObjectPropertyScopeInput) { if (size >= (n + 1) * sizeof(AudioObjectID)) ids[n] = kObjectID_Stream_Input; n++; }
          if (a->mScope == kAudioObjectPropertyScopeGlobal || a->mScope == kAudioObjectPropertyScopeOutput) { if (size >= (n + 1) * sizeof(AudioObjectID)) ids[n] = kObjectID_Stream_Output; n++; }
          *used = n * sizeof(AudioObjectID) > size ? size : n * (UInt32)sizeof(AudioObjectID);
          return 0;
        }
        case kAudioDevicePropertyNominalSampleRate: NEED(sizeof(Float64)); *(Float64 *)data = kSampleRate; return 0;
        case kAudioDevicePropertyAvailableNominalSampleRates: { NEED(sizeof(AudioValueRange)); ((AudioValueRange *)data)->mMinimum = kSampleRate; ((AudioValueRange *)data)->mMaximum = kSampleRate; return 0; }
      }
      break;
    case kObjectID_Stream_Input: case kObjectID_Stream_Output:
      switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass: NEED(4); *(AudioClassID *)data = kAudioObjectClassID; return 0;
        case kAudioObjectPropertyClass: NEED(4); *(AudioClassID *)data = kAudioStreamClassID; return 0;
        case kAudioObjectPropertyOwner: NEED(4); *(AudioObjectID *)data = kObjectID_Device; return 0;
        case kAudioStreamPropertyIsActive: NEED(4); *(UInt32 *)data = 1; return 0;
        case kAudioStreamPropertyDirection: NEED(4); *(UInt32 *)data = o == kObjectID_Stream_Input ? 1 : 0; return 0;     /* 1 = input (record), 0 = output (play) */
        case kAudioStreamPropertyTerminalType: NEED(4); *(UInt32 *)data = o == kObjectID_Stream_Input ? kAudioStreamTerminalTypeLine : kAudioStreamTerminalTypeSpeaker; return 0;
        case kAudioStreamPropertyStartingChannel: NEED(4); *(UInt32 *)data = 1; return 0;
        case kAudioStreamPropertyLatency: NEED(4); *(UInt32 *)data = 0; return 0;
        case kAudioStreamPropertyVirtualFormat: case kAudioStreamPropertyPhysicalFormat: NEED(sizeof(AudioStreamBasicDescription)); *(AudioStreamBasicDescription *)data = format(); return 0;
        case kAudioStreamPropertyAvailableVirtualFormats: case kAudioStreamPropertyAvailablePhysicalFormats: {
          AudioStreamRangedDescription *r = (AudioStreamRangedDescription *)data;
          NEED(sizeof(AudioStreamRangedDescription)); r->mFormat = format(); r->mSampleRateRange.mMinimum = kSampleRate; r->mSampleRateRange.mMaximum = kSampleRate; return 0;
        }
      }
      break;
  }
  return kAudioHardwareUnknownPropertyError;
}

/* ───────────── I/O ───────────── */
static OSStatus StartIO(AudioServerPlugInDriverRef d, AudioObjectID dev, UInt32 client) {
  (void)d; (void)client;
  if (dev != kObjectID_Device) return kAudioHardwareBadObjectError;
  pthread_mutex_lock(&gStateMutex);
  if (gStartCount == 0) {
    gTimeStampCount = 0; gAnchorHostTime = mach_absolute_time();
    dispatch_async(gConnectQueue, ^{ connectMixer(); });     /* connecting may take a moment: never on the real-time thread */
  }
  gStartCount++;
  pthread_mutex_unlock(&gStateMutex);
  return kAudioHardwareNoError;
}

static OSStatus StopIO(AudioServerPlugInDriverRef d, AudioObjectID dev, UInt32 client) {
  (void)d; (void)client;
  if (dev != kObjectID_Device) return kAudioHardwareBadObjectError;
  pthread_mutex_lock(&gStateMutex);
  if (gStartCount > 0 && --gStartCount == 0) dispatch_async(gConnectQueue, ^{ if (gLink) { am_link_close(gLink); gLink = NULL; } });
  pthread_mutex_unlock(&gStateMutex);
  return kAudioHardwareNoError;
}

static OSStatus GetZeroTimeStamp(AudioServerPlugInDriverRef d, AudioObjectID dev, UInt32 client, Float64 *sample, UInt64 *host, UInt64 *seed) {
  UInt64 now, ticksPerPeriod, next;
  (void)d; (void)dev; (void)client;
  pthread_mutex_lock(&gStateMutex);
  now = mach_absolute_time();
  ticksPerPeriod = (UInt64)(gHostTicksPerFrame * kZeroTimeStampPeriod);
  next = gAnchorHostTime + (gTimeStampCount + 1) * ticksPerPeriod;
  if (now >= next) gTimeStampCount++;
  *sample = (Float64)(gTimeStampCount * kZeroTimeStampPeriod);
  *host = gAnchorHostTime + gTimeStampCount * ticksPerPeriod;
  *seed = 1;
  pthread_mutex_unlock(&gStateMutex);
  return kAudioHardwareNoError;
}

static OSStatus WillDoIOOperation(AudioServerPlugInDriverRef d, AudioObjectID dev, UInt32 client, UInt32 op, Boolean *willDo, Boolean *inPlace) {
  (void)d; (void)dev; (void)client;
  *willDo = (op == kAudioServerPlugInIOOperationReadInput || op == kAudioServerPlugInIOOperationWriteMix);
  *inPlace = true;
  return kAudioHardwareNoError;
}

static OSStatus DoIOOperation(AudioServerPlugInDriverRef d, AudioObjectID dev, AudioObjectID stream, UInt32 client, UInt32 op, UInt32 size, const AudioServerPlugInIOCycleInfo *info, void *main, void *secondary) {
  (void)d; (void)dev; (void)stream; (void)client; (void)info; (void)secondary;
  if (op == kAudioServerPlugInIOOperationReadInput) {          /* an application records: the mixer's audio, silence while the ring is empty */
    float *out = (float *)main;
    pthread_mutex_lock(&gIOMutex);
    for (UInt32 i = 0; i < size; i++) {
      for (int c = 0; c < kChannels; c++) out[i * kChannels + c] = gRecCount ? gRecordRing[gRecTail * kChannels + c] : 0.0f;
      if (gRecCount) { gRecTail = (gRecTail + 1) % kRingFrames; gRecCount--; }
    }
    pthread_mutex_unlock(&gIOMutex);
  } else if (op == kAudioServerPlugInIOOperationWriteMix) {    /* applications play: the final mix goes to the mixer as 16-bit PCM */
    const float *in = (const float *)main;
    am_link *l = gLink;
    if (l && am_link_alive(l) && size <= 4096) {
      int16_t pcm[4096 * kChannels];
      for (UInt32 i = 0; i < size * kChannels; i++) { float v = in[i]; v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v); pcm[i] = (int16_t)(v * 32767.0f); }
      am_link_play(l, pcm, size);
    }
  }
  return kAudioHardwareNoError;
}
