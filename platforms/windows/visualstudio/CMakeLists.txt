# Audio Mixer for Windows: a CMake project that Visual Studio opens directly (File > Open > Folder, or open CMakePresets.json).
# It builds from the existing sources, nothing is copied:
#   audiomixer-native   the native engine (engine/cpp)
#   AudioMixerASIO64/32 the ASIO driver DLL (drivers/asio + drivers/common/am_link.c)
#   AudioDevices        the WASAPI endpoint lister (platforms/windows/native/src)
# The release installers are still made by `npm run build:installers` (MinGW + wixl); this project is for editing and debugging.
cmake_minimum_required(VERSION 3.16)
project(AudioMixerWindows C CXX)

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
add_subdirectory(${REPO}/engine/cpp engine)

# ASIO driver: a COM in-process server. MinGW needs the stdcall fixups the build script uses; MSVC takes the exports from the .def file.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(ASIO_DLL_NAME AudioMixerASIO64)
else()
  set(ASIO_DLL_NAME AudioMixerASIO32)
endif()
add_library(AudioMixerASIO SHARED
  ${REPO}/drivers/asio/audiomixer_asio.cpp
  ${REPO}/drivers/common/am_link.c
  ${REPO}/drivers/asio/audiomixer_asio.def)
target_include_directories(AudioMixerASIO PRIVATE ${REPO}/drivers/asio ${REPO}/drivers/common)
target_link_libraries(AudioMixerASIO PRIVATE ws2_32 ole32 oleaut32 uuid advapi32 winmm avrt)
set_target_properties(AudioMixerASIO PROPERTIES PREFIX "" OUTPUT_NAME ${ASIO_DLL_NAME} C_STANDARD 99 C_EXTENSIONS ON CXX_STANDARD 14)
if(MINGW)
  target_link_options(AudioMixerASIO PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--kill-at -Wl,--enable-stdcall-fixup)
endif()

# AudioDevices.exe: lists the WASAPI endpoints as JSON (wmain entry point)
add_executable(AudioDevices ${REPO}/platforms/windows/native/src/AudioDevices.cpp)
target_compile_definitions(AudioDevices PRIVATE UNICODE _UNICODE)
target_link_libraries(AudioDevices PRIVATE ole32 oleaut32 uuid)
if(MINGW)
  target_link_options(AudioDevices PRIVATE -municode -static)
endif()
