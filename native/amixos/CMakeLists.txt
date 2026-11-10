# A mix OS: the native console. Qt 6 Quick (QML) user interface on the C++20 engine in ../../engine/cpp (am::Runtime: ASIO / WASAPI on Windows,
# Core Audio on macOS, ALSA on Linux, a timed "null" device everywhere; am::dsp per-channel DSP).
#   cmake -S native/amixos -B build/amixos && cmake --build build/amixos && ctest --test-dir build/amixos
#   build/amixos/amixos                      the console
#   build/amixos/amixos --smoke              loads the interface offscreen, runs the engine on the null device for a second, exits 0 (the CI check)
# Qt 6.4 or newer: Core Gui Quick QuickControls2 (the Android / iOS kits need Qt's mobile tool chains, see README.md).
cmake_minimum_required(VERSION 3.21)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" AMIX_VERSION)
string(STRIP "${AMIX_VERSION}" AMIX_VERSION)
project(AmixOS VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network)
qt_standard_project_setup()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../engine/cpp ${CMAKE_BINARY_DIR}/engine)

qt_add_executable(amixos
    src/main.cpp
    src/Engine.cpp src/Engine.h
    src/Arrange.cpp src/Arrange.h
    src/Settings.cpp src/Settings.h
    src/Access.cpp src/Access.h
    src/Osc.cpp src/Osc.h
    src/Media.cpp src/Media.h
    src/Samples.cpp src/Samples.h
    src/Update.cpp src/Update.h
    src/Scope.cpp src/Scope.h
    src/Generator.cpp src/Generator.h
    src/I18n.cpp src/I18n.h)
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)   # before qt_add_qml_module, or Theme is a plain type and every Theme.* colour is undefined
qt_add_qml_module(amixos
    URI AmixOS
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    NO_CACHEGEN              # Qt 6.4's ahead-of-time QML compiler loops forever resolving the Theme singleton (lookupType); interpreted bindings are fine here
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/NavRail.qml
        qml/Panel.qml
        qml/Fader.qml
        qml/MeterBar.qml
        qml/VuMeter.qml
        qml/RoutingPage.qml
        qml/LicensePage.qml
        qml/ArrangePage.qml
        qml/EffectsPage.qml
        qml/WaveformPage.qml
        qml/PhasePage.qml
        qml/MicEqPage.qml
        qml/LiveSourcesPage.qml
        qml/ToolsPage.qml
        qml/CalibratePage.qml
        qml/DiagnosticsPage.qml
        qml/HelpPage.qml
        qml/AccountPage.qml
        qml/AccessPage.qml
        qml/AdminPage.qml
        qml/PinPad.qml
        qml/AiMixPage.qml
        qml/PluginsPage.qml
        qml/NetworkPage.qml
        qml/MediaPage.qml
        qml/SamplesPage.qml
        qml/SoundCardPage.qml
        qml/SetupPage.qml
        qml/HomePage.qml
        qml/MixerPage.qml
        qml/TonePage.qml
        qml/RtaEqPage.qml
        qml/BluetoothPage.qml
        qml/CompressorPage.qml
        qml/GatePage.qml
        qml/DynCurve.qml
        qml/SendToPage.qml
        qml/GroupFadersPage.qml
        qml/ScenesPage.qml
        qml/PresetsPage.qml
        qml/ConnectionsPage.qml
        qml/LevelsPage.qml
        qml/LoudnessPage.qml
        qml/SettingsPage.qml
        qml/ComingPage.qml
    RESOURCES
        i18n/verified-22.json
        i18n/auto-58.json)
target_compile_definitions(amixos PRIVATE AMIX_VERSION="${AMIX_VERSION}")
target_compile_definitions(amixos PRIVATE AMIX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(amixos PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Network audiomixer_runtime audiomixer_engine amixos_license amixos_arrange amixos_vst3 amixos_vst2 amixos_record amixos_fx amixos_samples)

if(WIN32)
    set_target_properties(amixos PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
if(APPLE AND NOT IOS)
    set_target_properties(amixos PROPERTIES MACOSX_BUNDLE TRUE)
endif()
if(ANDROID)
    set_target_properties(amixos PROPERTIES QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android" QT_ANDROID_VERSION_NAME "${AMIX_VERSION}")
endif()

enable_testing()
add_test(NAME amixos_smoke COMMAND amixos --smoke)
set_tests_properties(amixos_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 60)
