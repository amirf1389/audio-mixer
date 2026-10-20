/* The Steinberg ASIO SDK 2.3 headers (sdk/asiosys.h, sdk/asio.h, sdk/iasiodrv.h), used under the GNU GPL v3. See sdk/README.txt. */
#ifndef AM_ASIO_SDK_H
#define AM_ASIO_SDK_H
#include <windows.h>
#include <objbase.h>

/* 32-bit hosts built with Visual C++ call the IASIO methods with `thiscall` (the object in ECX, the callee removes the arguments); GCC/MinGW would
 * otherwise pass the object on the stack and the host crashes at the first call. 64-bit Windows has one calling convention. */
#if defined(__GNUC__) && !defined(__x86_64__) && !defined(_WIN64) && (defined(__i386__) || defined(_M_IX86))
#define ASIO_CALL __attribute__((thiscall))
#else
#define ASIO_CALL
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#include "sdk/asiosys.h"
#include "sdk/asio.h"
#include "sdk/iasiodrv.h"
#pragma GCC diagnostic pop

/* the layout the hosts were built against */
static_assert(sizeof(ASIOTimeStamp) == 8 && sizeof(ASIOSamples) == 8, "ASIOTimeStamp / ASIOSamples are 8 bytes");
static_assert(sizeof(ASIOClockSource) == 48, "ASIOClockSource is 48 bytes");
static_assert(sizeof(ASIOChannelInfo) == 52, "ASIOChannelInfo is 52 bytes");
static_assert(sizeof(ASIOBufferInfo) == 8 + 2 * sizeof(void *), "ASIOBufferInfo");
static_assert(sizeof(ASIOCallbacks) == 4 * sizeof(void *), "ASIOCallbacks");
#endif
