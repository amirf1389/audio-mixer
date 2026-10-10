cmake_minimum_required(VERSION 3.21)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" AM_VERSION)          # written by `node scripts/prepare-qt.js`
string(STRIP "${AM_VERSION}" AM_VERSION)
project(AudioMixerQt VERSION 1.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt 6.5 or newer (QMicrophonePermission, QMediaDevices, QtWebView on Android)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick Network Multimedia WebView)

qt_standard_project_setup()
qt_add_executable(AudioMixerQt
    src/main.cpp
    src/MixerServer.cpp src/MixerServer.h
    src/AudioDevices.cpp src/AudioDevices.h)
qt_add_qml_module(AudioMixerQt URI AudioMixer VERSION 1.0 QML_FILES qml/Main.qml)
target_compile_definitions(AudioMixerQt PRIVATE AM_VERSION="${AM_VERSION}")
target_link_libraries(AudioMixerQt PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Network Qt6::Multimedia Qt6::WebView)

if(ANDROID)
    # the staged page (assets/www) and the manifest travel in the APK; Qt reads the assets back as  assets:/www/...
    set_target_properties(AudioMixerQt PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
        QT_ANDROID_VERSION_NAME "${AM_VERSION}"
        QT_ANDROID_VERSION_CODE 10500)
endif()
