# Compiler / linker hardening for the C and C++ targets of this repository.
# Included, after project(), by engine/cpp/CMakeLists.txt and native/amixos/CMakeLists.txt (once per build: include_guard below; the
# console's include covers the engine it adds as a subdirectory). None of these flags changes the behaviour of correct code;
# _FORTIFY_SOURCE turns some overflows into aborts. Android: the NDK sets stack protector, FORTIFY, PIE and RELRO itself.
# scripts/security/audit.js recognises the include and then reports the flags as present.
#
#   GCC / Clang (Linux, macOS, MinGW): -fstack-protector-strong, -D_FORTIFY_SOURCE=2 (optimised builds only), PIE, full RELRO, noexecstack
#   MSVC: /GS (default), /guard:cf, /DYNAMICBASE, /NXCOMPAT, /HIGHENTROPYVA, /CETCOMPAT where the linker knows it
#   MinGW (GNU ld / lld): --dynamicbase, --nxcompat, --high-entropy-va
# AM_HARDENING=OFF switches all of it off (for a quick comparison build).
include_guard(GLOBAL)
option(AM_HARDENING "compiler and linker hardening flags" ON)
if(NOT AM_HARDENING OR ANDROID)
  return()
endif()

include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(CheckPIESupported OPTIONAL RESULT_VARIABLE _am_has_pie_check)

if(_am_has_pie_check)
  check_pie_supported(OUTPUT_VARIABLE _am_pie_msg LANGUAGES C CXX)
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
  add_compile_options(/GS /guard:cf)
  add_link_options(/guard:cf /DYNAMICBASE /NXCOMPAT)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_link_options(/HIGHENTROPYVA)
  endif()
else()
  check_cxx_compiler_flag(-fstack-protector-strong AM_HAS_SSP_STRONG)
  if(AM_HAS_SSP_STRONG)
    add_compile_options(-fstack-protector-strong)
  endif()
  # FORTIFY needs optimisation; do not redefine it if the toolchain already sets it
  add_compile_options("$<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE>" "$<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=2>")
  check_cxx_compiler_flag(-fcf-protection=full AM_HAS_CF_PROTECTION)
  if(AM_HAS_CF_PROTECTION AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    add_compile_options(-fcf-protection=full)
  endif()
  if(WIN32)   # MinGW
    add_link_options(-Wl,--dynamicbase -Wl,--nxcompat)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      add_link_options(-Wl,--high-entropy-va)
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      check_cxx_compiler_flag(-mguard=cf AM_HAS_MGUARD)
      if(AM_HAS_MGUARD)
        add_compile_options(-mguard=cf)
        add_link_options(-mguard=cf)
      endif()
    endif()
  elseif(APPLE)
    # PIE and non-executable stacks are the platform default
  else()
    add_link_options(-Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack)
  endif()
endif()
