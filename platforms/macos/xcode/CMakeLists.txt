# Audio Mixer for macOS: generate an Xcode project from this folder.
#   cmake -S platforms/macos/xcode -B build -G Xcode && open build/AudioMixerMac.xcodeproj
# It builds from the existing sources, nothing is copied:
#   audiomixer-native   the native engine on Core Audio (engine/cpp)
#   AudioMixerDriver    the Core Audio AudioServerPlugIn bundle (platforms/macos/coreaudio-driver + drivers/common/am_link.c)
#   AudioDevices        the Swift device lister (platforms/macos/native), with the Xcode generator only
cmake_minimum_required(VERSION 3.20)
project(AudioMixerMac C CXX)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
add_subdirectory(${REPO}/engine/cpp engine)
include(${REPO}/drivers/asio/asio-libs.cmake)   # AudioMixer::AsioSdk (Steinberg ASIO SDK) and AudioMixer::Asio (Asio C++ networking)

# The HAL plug-in: a bundle with the .driver extension and the same Info.plist as the Makefile build
add_library(AudioMixerDriver MODULE
  ${REPO}/platforms/macos/coreaudio-driver/AudioMixerDriver.c
  ${REPO}/drivers/common/am_link.c)
target_include_directories(AudioMixerDriver PRIVATE ${REPO}/drivers/common)
target_link_libraries(AudioMixerDriver PRIVATE "-framework CoreAudio" "-framework CoreFoundation")
target_compile_options(AudioMixerDriver PRIVATE -fvisibility=hidden)
set_target_properties(AudioMixerDriver PROPERTIES
  BUNDLE TRUE
  BUNDLE_EXTENSION driver
  OUTPUT_NAME AudioMixer
  C_STANDARD 99 C_EXTENSIONS ON
  MACOSX_BUNDLE_INFO_PLIST ${REPO}/platforms/macos/coreaudio-driver/Info.plist)

if(CMAKE_GENERATOR STREQUAL "Xcode")
  enable_language(Swift)
  add_executable(AudioDevices ${REPO}/platforms/macos/native/AudioDevices.swift)
  target_link_libraries(AudioDevices PRIVATE "-framework CoreAudio" "-framework Foundation")
endif()
