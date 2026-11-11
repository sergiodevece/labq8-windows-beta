cmake_minimum_required(VERSION 3.22)

# Must be set before project(). 11.0 is the first macOS with Apple Silicon.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(LABQ8 VERSION 0.0.1 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use: cmake -S . -B build")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LABQ8_BUILD_TESTS "Build the DSP / processor test executable" ON)

# JUCE is pinned to 9.0.3 (be29c81492b6151c8ea8d14c840e1311963b3a83) and lives
# inside this repository's tree, never shared with other projects.
set(LABQ8_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE" CACHE PATH "JUCE 9.0.3 source checkout")

if(NOT EXISTS "${LABQ8_JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE not found at ${LABQ8_JUCE_DIR}.\n"
        "Run: git clone --depth 1 --branch 9.0.3 https://github.com/juce-framework/JUCE.git external/JUCE")
endif()

add_subdirectory("${LABQ8_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

#==============================================================================
# EQ Core: pure C++ DSP, no JUCE dependency
add_library(labq8_dsp STATIC
    src/dsp/BiquadDesign.cpp
    src/dsp/BellBand.cpp
    src/dsp/EqBand.cpp
    src/dsp/EqCore.cpp
    src/dsp/DynamicDetector.cpp
    src/dsp/DynamicSection.cpp
    src/dsp/DynamicBellBand.cpp
    src/dsp/StereoModeEngine.cpp)

target_include_directories(labq8_dsp PUBLIC src)
target_compile_features(labq8_dsp PUBLIC cxx_std_20)
target_compile_options(labq8_dsp PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)

set(LABQ8_HOST_ADAPTER_SOURCES
    src/plugin/PluginProcessor.cpp
    src/parameters/ParameterLayout.cpp
    src/parameters/StateMigration.cpp
    src/ui/LabQ8Editor.cpp
    src/ui/LevelMeterView.cpp
    src/ui/SpectrumAnalyzer.cpp
    src/ui/SpectrumView.cpp
    src/ui/DynamicsOverlay.cpp
    src/ui/UiModel.cpp
    src/ui/UiPreferences.cpp)

# UI-1 implementation assets, derived from the approved design by
# labq8_ui_assetgen (docs/UI_IMPLEMENTATION.md). The master PNG itself is
# never embedded or modified.
juce_add_binary_data(labq8_ui_assets
    HEADER_NAME LabQ8Assets.h
    NAMESPACE LabQ8Assets
    SOURCES
        assets/ui/background.png
        assets/ui/dynamic_switch_on.png
        assets/ui/dynamic_switch_off.png
        assets/ui/meter_lit.png
        assets/ui/viewtab_light.png
        assets/ui/axis_blank.png)

#==============================================================================
# Plugin (AU + VST3). Never copied into system or user plug-in folders.
juce_add_plugin(LABQ8
    COMPANY_NAME "VitaPlug"
    COMPANY_WEBSITE ""
    BUNDLE_ID com.vitaplug.labq8
    PLUGIN_MANUFACTURER_CODE Vtpl
    PLUGIN_CODE Lbq8
    FORMATS AU VST3
    PRODUCT_NAME "LABQ8"
    DESCRIPTION "VitaPlug LABQ8 dynamic parametric EQ"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx EQ
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

target_sources(LABQ8 PRIVATE ${LABQ8_HOST_ADAPTER_SOURCES})

target_compile_definitions(LABQ8
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(LABQ8
    PRIVATE
        labq8_dsp
        labq8_ui_assets
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

#==============================================================================
# Tests: run without any DAW. Compiles the host adapter sources again so the
# processor can be exercised headless (state, layouts, automation).
if(LABQ8_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(labq8_tests
        PRODUCT_NAME "labq8_tests"
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE)

    target_sources(labq8_tests PRIVATE
        tests/TestMain.cpp
        tests/DspTests.cpp
        tests/ProcessorTests.cpp
        tests/EqCoreTests.cpp
        tests/ProcessorEqTests.cpp
        tests/StateMigrationTests.cpp
        tests/RealtimeTests.cpp
        tests/FilterDesignTests.cpp
        tests/EqBandTests.cpp
        tests/ProcessorE1bTests.cpp
        tests/EffectiveQTests.cpp
        tests/DynamicTests.cpp
        tests/EqCoreDynamicTests.cpp
        tests/ProcessorDynamicTests.cpp
        tests/EditorTests.cpp
        tests/MeterTests.cpp
        tests/SpectrumTests.cpp
        tests/DynamicsTests.cpp
        tests/E4aParameterTests.cpp
        tests/MidSideTests.cpp
        tests/AllocationGuard.cpp
        ${LABQ8_HOST_ADAPTER_SOURCES})

    target_include_directories(labq8_tests PRIVATE tests)

    target_compile_definitions(labq8_tests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        LABQ8_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

    target_link_libraries(labq8_tests
        PRIVATE
            labq8_dsp
            labq8_ui_assets
            juce::juce_audio_processors
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME labq8_tests COMMAND labq8_tests)

    # CPU benchmark (JUCE-free). Not part of ctest: run it explicitly, in Release.
    add_executable(labq8_bench tests/bench/EqBenchmark.cpp)
    target_link_libraries(labq8_bench PRIVATE labq8_dsp)

    # E4b: STEREO <-> MID/SIDE mode-change study and engine CPU. Not part of
    # ctest: run it explicitly, in Release (--transitions, --cpu).
    add_executable(labq8_ms_bench tests/bench/MidSideBench.cpp)
    target_link_libraries(labq8_ms_bench PRIVATE labq8_dsp)

    # UI-1 asset generator: derives assets/ui/*.png from the approved design
    # (docs/design/, never modified). Run it explicitly; see docs/UI_IMPLEMENTATION.md.
    juce_add_console_app(labq8_ui_assetgen PRODUCT_NAME "labq8_ui_assetgen")
    target_sources(labq8_ui_assetgen PRIVATE tools/ui/GenerateUiAssets.cpp)
    target_include_directories(labq8_ui_assetgen PRIVATE src)
    target_compile_definitions(labq8_ui_assetgen PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(labq8_ui_assetgen PRIVATE juce::juce_gui_basics juce::juce_recommended_config_flags)

    # UI-1 snapshot: renders the real editor offscreen and compares it with the
    # approved design (docs/UI_IMPLEMENTATION.md).
    juce_add_console_app(labq8_ui_snapshot PRODUCT_NAME "labq8_ui_snapshot")
    target_sources(labq8_ui_snapshot PRIVATE tools/ui/UiSnapshot.cpp ${LABQ8_HOST_ADAPTER_SOURCES})
    target_include_directories(labq8_ui_snapshot PRIVATE src)
    target_compile_definitions(labq8_ui_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(labq8_ui_snapshot PRIVATE labq8_dsp labq8_ui_assets juce::juce_audio_processors juce::juce_dsp juce::juce_recommended_config_flags)

    # Shelf and cut response sweep (E1b.1). Not part of ctest: run it explicitly.
    add_executable(labq8_sweep tests/bench/ResonanceSweep.cpp)
    target_include_directories(labq8_sweep PRIVATE tests)
    target_link_libraries(labq8_sweep PRIVATE labq8_dsp)
endif()
