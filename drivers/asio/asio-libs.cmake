# The two ASIO libraries in this repository, as CMake targets the platform projects (platforms/*/visualstudio, cmake, xcode) link against.
#   AudioMixer::AsioSdk  Steinberg ASIO SDK headers (drivers/asio/sdk, used under the GPLv3 option, see drivers/asio/sdk/README.txt) + asio_sdk.h
#   AudioMixer::Asio     standalone Asio C++ networking library 1.38.2 (native/asio, header-only, Boost Software License)
# Include it after project(): include(<repo>/drivers/asio/asio-libs.cmake)
get_filename_component(AM_REPO "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
find_package(Threads REQUIRED)

if(NOT TARGET am_asio_sdk)
  add_library(am_asio_sdk INTERFACE)
  target_include_directories(am_asio_sdk INTERFACE ${AM_REPO}/drivers/asio ${AM_REPO}/drivers/asio/sdk)
  add_library(AudioMixer::AsioSdk ALIAS am_asio_sdk)
endif()

if(NOT TARGET am_asio)
  add_library(am_asio INTERFACE)
  target_include_directories(am_asio INTERFACE ${AM_REPO}/native/asio/include)
  target_compile_definitions(am_asio INTERFACE ASIO_STANDALONE)
  target_link_libraries(am_asio INTERFACE Threads::Threads)
  if(WIN32)
    target_link_libraries(am_asio INTERFACE ws2_32 mswsock)
    target_compile_definitions(am_asio INTERFACE _WIN32_WINNT=0x0A00)   # Windows 10 and later
  endif()
  add_library(AudioMixer::Asio ALIAS am_asio)
endif()
