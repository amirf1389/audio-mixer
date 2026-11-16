# The engine's C SDK for Android: the same sources as engine/cpp, the native AAudio low-latency path in amix_jni.cpp (its output callback
# drives the mixer), plus the JNI glue. Built by Gradle through the NDK.
cmake_minimum_required(VERSION 3.22)
project(amixos_android C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)   # android/amix-sdk/src/main/cpp -> the repository
file(GLOB HIRES_FLAC_SRC ${ROOT}/engine/cpp/app/hires/flac/src/*.c)
list(REMOVE_ITEM HIRES_FLAC_SRC ${ROOT}/engine/cpp/app/hires/flac/src/win_utf8_io.c)
add_library(amix SHARED
  ${ROOT}/engine/cpp/src/engine.cpp ${ROOT}/engine/cpp/src/runtime.cpp ${ROOT}/engine/cpp/src/backend_null.cpp ${ROOT}/engine/cpp/src/devices.cpp ${ROOT}/engine/cpp/src/driver_intel.cpp
  ${ROOT}/engine/cpp/app/license/license.cpp ${ROOT}/engine/cpp/app/license/crypto.cpp ${ROOT}/engine/cpp/app/paths/paths.cpp
  ${ROOT}/engine/cpp/app/net/rtp.cpp ${ROOT}/engine/cpp/app/net/sock.cpp ${ROOT}/engine/cpp/app/net/aoip.cpp ${ROOT}/engine/cpp/app/net/midi.cpp   # network audio (AES67) and MIDI: the C SDK links them
  ${ROOT}/engine/cpp/app/sdk/amix_c.cpp
  # HIGH-RESOLUTION AUDIO (engine/cpp/app/hires): r8brain resampling, dr_wav / dr_flac reading, libFLAC writing. libsoxr (LGPL) is not built in here.
  ${ROOT}/engine/cpp/app/sdk/amix_hires_c.cpp ${ROOT}/engine/cpp/app/hires/hires.cpp ${ROOT}/engine/cpp/app/hires/r8brain/r8bbase.cpp ${HIRES_FLAC_SRC}
  amix_jni.cpp)
target_include_directories(amix PRIVATE ${ROOT}/engine/cpp/app/hires/flac/include ${ROOT}/engine/cpp/app/hires/flac ${ROOT}/engine/cpp/app/hires/flac/src)
target_compile_definitions(amix PRIVATE FLAC__NO_DLL HAVE_CONFIG_H)
set_source_files_properties(${HIRES_FLAC_SRC} PROPERTIES COMPILE_FLAGS -w)
target_include_directories(amix PRIVATE ${ROOT}/engine/cpp/include ${ROOT}/engine/cpp/src ${ROOT}/engine/cpp/app ${ROOT}/sdk/c/include)
file(READ ${ROOT}/native/amixos/version.txt AMIX_SDK_VERSION)
string(STRIP "${AMIX_SDK_VERSION}" AMIX_SDK_VERSION)
target_compile_definitions(amix PRIVATE AMIX_VERSION_STRING="${AMIX_SDK_VERSION}" AMIX_BUILD_DLL)
find_library(log-lib log)
target_link_libraries(amix ${log-lib} aaudio)   # AAudio: the native low-latency audio path (Android 8+)
