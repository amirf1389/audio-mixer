/* libFLAC 1.5.0 configuration for AmF X1 (portable C, no assembly, no Ogg). Written by hand in place of the generated config.h. */
#pragma once
#define PACKAGE_VERSION "1.5.0"
#define FLAC__NO_ASM 1
#define FLAC__HAS_OGG 0
#define ENABLE_64_BIT_WORDS 1
#define HAVE_LROUND 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_FSEEKO 1
#define FLAC__ALIGN_MALLOC_DATA 1
#if defined(__x86_64__) || defined(_M_X64)
#define FLAC__CPU_X86_64 1
#endif
#define CPU_IS_BIG_ENDIAN 0
#define CPU_IS_LITTLE_ENDIAN 1
#define WORDS_BIGENDIAN 0
#ifndef _WIN32
#define HAVE_SYS_PARAM_H 1
#endif
