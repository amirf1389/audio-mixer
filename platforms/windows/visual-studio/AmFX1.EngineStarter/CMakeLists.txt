# Visual Studio opens this folder directly (File > Open > Folder). It builds the C++ audio engine from the repository (engine/cpp).
cmake_minimum_required(VERSION 3.20)
project(AmFX1EngineStarter CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ENGINE ${CMAKE_CURRENT_SOURCE_DIR}/../../../../engine/cpp)
add_executable(engine_check main.cpp ${ENGINE}/src/engine.cpp)
target_include_directories(engine_check PRIVATE ${ENGINE}/include)
