# Audio Mixer for Linux: a CMake project for VS Code (CMake Tools), CLion, Qt Creator or the command line.
# It builds from the existing sources, nothing is copied:
#   audiomixer-native                 the native engine (engine/cpp), with ALSA when its headers are installed
#   libasound_module_pcm_audiomixer   the ALSA plugin (platforms/linux/alsa-plugin + drivers/common/am_link.c)
#   cmake -S platforms/linux/cmake -B build && cmake --build build
# The kernel module (platforms/linux/kernel-module) is built with DKMS against the running kernel, not here.
cmake_minimum_required(VERSION 3.16)
project(AudioMixerLinux C CXX)

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
add_subdirectory(${REPO}/engine/cpp engine)

find_package(ALSA REQUIRED)        # libasound2-dev (Debian / Ubuntu), alsa-lib-devel (Fedora); scripts/setup-build-tools.sh installs it
find_package(Threads REQUIRED)
add_library(asound_module_pcm_audiomixer MODULE
  ${REPO}/platforms/linux/alsa-plugin/pcm_audiomixer.c
  ${REPO}/drivers/common/am_link.c)
target_include_directories(asound_module_pcm_audiomixer PRIVATE ${REPO}/drivers/common)
target_compile_definitions(asound_module_pcm_audiomixer PRIVATE PIC)
target_link_libraries(asound_module_pcm_audiomixer PRIVATE ALSA::ALSA Threads::Threads)
set_target_properties(asound_module_pcm_audiomixer PROPERTIES C_STANDARD 99 C_EXTENSIONS ON)

# sudo cmake --install build: the plugin goes where ALSA looks for plugins (see platforms/linux/alsa-plugin/asound.conf.example for the device)
execute_process(COMMAND pkg-config --variable=libdir alsa OUTPUT_VARIABLE ALSA_LIBDIR OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT ALSA_LIBDIR)
  set(ALSA_LIBDIR /usr/lib)
endif()
install(TARGETS asound_module_pcm_audiomixer LIBRARY DESTINATION ${ALSA_LIBDIR}/alsa-lib)
