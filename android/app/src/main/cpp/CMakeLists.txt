# The engine's C SDK for Android: the same sources as engine/cpp, no device backend yet (the null backend clocks the mixer; an AAudio
# backend is the next step), plus the JNI glue. Built by Gradle through the NDK.
cmake_minimum_required(VERSION 3.22)
project(amixos_android CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)
add_library(amix SHARED
  ${ROOT}/engine/cpp/src/engine.cpp ${ROOT}/engine/cpp/src/runtime.cpp ${ROOT}/engine/cpp/src/backend_null.cpp ${ROOT}/engine/cpp/src/devices.cpp ${ROOT}/engine/cpp/src/driver_intel.cpp
  ${ROOT}/engine/cpp/app/license/license.cpp ${ROOT}/engine/cpp/app/license/crypto.cpp
  ${ROOT}/engine/cpp/app/sdk/amix_c.cpp
  amix_jni.cpp)
target_include_directories(amix PRIVATE ${ROOT}/engine/cpp/include ${ROOT}/engine/cpp/src ${ROOT}/engine/cpp/app ${ROOT}/sdk/c/include)
file(READ ${ROOT}/native/amixos/version.txt AMIX_SDK_VERSION)
string(STRIP "${AMIX_SDK_VERSION}" AMIX_SDK_VERSION)
target_compile_definitions(amix PRIVATE AMIX_VERSION_STRING="${AMIX_SDK_VERSION}" AMIX_BUILD_DLL)
find_library(log-lib log)
target_link_libraries(amix ${log-lib})
