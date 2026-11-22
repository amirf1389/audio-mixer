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
    src/Graphics.cpp src/Graphics.h
    src/Arrange.cpp src/Arrange.h
    src/Settings.cpp src/Settings.h
    src/Access.cpp src/Access.h
    src/Osc.cpp src/Osc.h
    src/Media.cpp src/Media.h
    src/Samples.cpp src/Samples.h
    src/Links.cpp src/Links.h src/Apps.cpp src/Apps.h src/SysNotify.cpp src/SysNotify.h src/AppWindows.cpp src/AppWindows.h
    src/Update.cpp src/Update.h
    src/Scope.cpp src/Scope.h src/Measure.cpp src/Measure.h src/Remote.cpp src/Remote.h src/Extension.cpp src/Extension.h src/Net.cpp src/Net.h src/Frame.cpp src/Frame.h
    src/Generator.cpp src/Generator.h
    src/I18n.cpp src/I18n.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../engine/cpp/app/qr/qrcodegen.cpp)   # the pairing QR code (Project Nayuki, MIT)
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
        qml/MoreText.qml
        qml/Fader.qml
        qml/MSlider.qml
        qml/DynEqEditor.qml
        qml/MeterBar.qml
        qml/VuMeter.qml
        qml/RoutingPage.qml
        qml/LicensePage.qml
        qml/ArrangePage.qml
        qml/EffectsPage.qml
        qml/WaveformPage.qml
        qml/PhasePage.qml
        qml/BankBar.qml
        qml/AppCapturePanel.qml
        qml/AppPicker.qml
        qml/WindowsSoundPanel.qml
        qml/SoundCardWindows.qml
        qml/Combo.qml
        qml/ScrollRow.qml
        qml/Knob.qml
        qml/PluginEditor.qml qml/PluginDock.qml qml/StripTone.qml
        qml/Field.qml
        qml/BootSplash.qml
        qml/AcousticsPage.qml
        qml/MasteringPage.qml
        qml/SpatialPage.qml
        qml/AoipPanel.qml
        qml/VolumeEnhancer.qml qml/AudioSettings.qml
        qml/MasterAnalyzer.qml
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
        qml/AiMixPage.qml qml/EngineControlPage.qml qml/DelayPage.qml qml/CloudPage.qml qml/CascadePage.qml
        qml/PluginsPage.qml
        qml/NetworkPage.qml
        qml/FramePage.qml
        qml/MediaPage.qml
        qml/SamplesPage.qml
        qml/StreamingPage.qml
        qml/UpdatePage.qml
        qml/SoundCardPage.qml
        qml/DspModeSwitch.qml
        qml/ScorePage.qml
        qml/AmpPage.qml
        qml/Aurora.qml
        qml/Songket.qml
        qml/Icon.qml
        qml/StorePage.qml
        qml/DantePanel.qml
        qml/AssistantPage.qml
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
        qml/DcaStrip.qml
        qml/DcaPanels.qml
        qml/ScenesPage.qml
        qml/PresetsPage.qml
        qml/ConnectionsPage.qml
        qml/LevelsPage.qml
        qml/LoudnessPage.qml
        qml/SettingsPage.qml
        qml/ComingPage.qml
    RESOURCES
        icons/amfx1.png
        skin/wood.png
        skin/key.png
        skin/key-down.png
        skin/badge.png
        skin/serial.png
        skin/plate.png
        skin/brushed.png
        skin/knob.png
        skin/knob-pointer.png
        skin/selector.png
        skin/screw.png
        skin/meter.png
        skin/meter-glass.png
        skin/toggle.png
        skin/lamp-on.png
        skin/lamp-off.png
        i18n/verified-22.json
        i18n/auto-58.json
        i18n/ui/fa.json
        i18n/ui/ar.json
        i18n/ui/ur.json
        i18n/ui/es.json
        i18n/ui/fr.json
        i18n/ui/de.json
        i18n/ui/pt.json
        i18n/ui/ru.json
        i18n/ui/tr.json
        i18n/ui/hi.json
        i18n/ui/zh.json
        i18n/ui/ja.json
        i18n/ui/ko.json
        i18n/ui/ms.json
        i18n/ui/id.json
        i18n/ui/it.json
        i18n/ui/nl.json
        i18n/ui/pl.json
        i18n/ui/vi.json
        i18n/ui/th.json
        i18n/ui/uk.json)
string(TIMESTAMP AMIX_BUILD_STAMP "%Y-%m-%d %H:%M" UTC)   # when this build was configured: shown next to the version, so two builds of one version can be told apart
target_compile_definitions(amixos PRIVATE AMIX_VERSION="${AMIX_VERSION}" AMIX_BUILD_STAMP="${AMIX_BUILD_STAMP}")
target_compile_definitions(amixos PRIVATE AMIX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
# TEST BUILDS ONLY: lets AMIX_TEST_CHANNELS show the console with more licensed channels (the 300-channel surface) without a vendor key.
# OFF for every package; the release scripts never set it.
option(AMIXOS_TEST_LICENSE "test builds: AMIX_TEST_CHANNELS overrides the licensed channel count" OFF)
if(AMIXOS_TEST_LICENSE)
  target_compile_definitions(amixos PRIVATE AMIXOS_TEST_LICENSE)
endif()
target_link_libraries(amixos PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Network audiomixer_runtime audiomixer_engine amixos_net amixos_license amixos_arrange amixos_vst3 amixos_vst2 amixos_record amixos_fx amixos_samples amixos_measure)
# libsoxr (LGPL 2.1) stays its own library next to the program, loaded at run time (app/hires): copied beside amixos on every build
if(TARGET amix_soxr)
  add_dependencies(amixos amix_soxr)
  add_custom_command(TARGET amixos POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:amix_soxr> $<TARGET_FILE_DIR:amixos>)
endif()
add_dependencies(amixos amfb_plugin_shell amfb_plugin_builder)   # the separate PLUG-IN BUILDER and the shell it exports: built next to the console

if(WIN32)
    set_target_properties(amixos PROPERTIES WIN32_EXECUTABLE TRUE)
    target_sources(amixos PRIVATE win/amixos.rc)   # version information + manifest (company, product, asInvoker, DPI aware)
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

# AmF X1 PLUG-IN BUILDER: the builder as its own program (shares the builder page, Theme, the language tables and the plug-in shell)
file(GLOB AMIX_I18N_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} i18n/verified-22.json i18n/auto-58.json i18n/ui/*.json)
# AmF X1 DIAGNOSTICS: the system check as its own program (devices, test tone, input meter, ASIO drivers, report)
qt_add_executable(amfb_diagnostics src/diag_main.cpp src/Diag.cpp src/Diag.h)
amfb_win_version(amfb_diagnostics "AmF X1 Diagnostics.exe" "AmF X1 Diagnostics: checks speakers, headphones, microphones and drivers" APP)
set_target_properties(amfb_diagnostics PROPERTIES OUTPUT_NAME "AmF X1 Diagnostics" WIN32_EXECUTABLE TRUE)
target_include_directories(amfb_diagnostics PRIVATE src)
target_compile_definitions(amfb_diagnostics PRIVATE AMIX_VERSION="${AMIX_VERSION}")
qt_add_qml_module(amfb_diagnostics
    URI AmixOS
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/diag_qml/AmixOS
    NO_CACHEGEN
    NO_PLUGIN
    QML_FILES qml/DiagApp.qml qml/Theme.qml qml/Icon.qml qml/Aurora.qml
    RESOURCES icons/amfx1.png)
target_link_libraries(amfb_diagnostics PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 audiomixer_runtime audiomixer_engine)
add_dependencies(amixos amfb_diagnostics)

qt_add_executable(amfb_plugin_builder src/builder_main.cpp src/Builder.cpp src/Builder.h src/I18n.cpp src/I18n.h)
amfb_win_version(amfb_plugin_builder "AmF X1 Plugin Builder.exe" "AmF X1 Plugin Builder: design VST3 plug-ins from AMFB blocks" APP)
set_target_properties(amfb_plugin_builder PROPERTIES OUTPUT_NAME "AmF X1 Plugin Builder" WIN32_EXECUTABLE TRUE)
target_include_directories(amfb_plugin_builder PRIVATE src)
qt_add_qml_module(amfb_plugin_builder
    URI AmixOS
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/builder_qml/AmixOS
    NO_CACHEGEN
    NO_PLUGIN
    QML_FILES qml/BuilderApp.qml qml/BuilderPage.qml qml/Theme.qml
    RESOURCES ${AMIX_I18N_FILES})
target_link_libraries(amfb_plugin_builder PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 amixos_plugin_design amixos_plugin_preview amixos_paths)
add_dependencies(amfb_plugin_builder amfb_plugin_shell)
