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
    src/dsp/BellBand.cpp)

target_include_directories(labq8_dsp PUBLIC src)
target_compile_features(labq8_dsp PUBLIC cxx_std_20)
target_compile_options(labq8_dsp PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)

set(LABQ8_HOST_ADAPTER_SOURCES
    src/plugin/PluginProcessor.cpp
    src/parameters/ParameterLayout.cpp)

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
        juce::juce_audio_processors
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
        ${LABQ8_HOST_ADAPTER_SOURCES})

    target_include_directories(labq8_tests PRIVATE tests)

    target_compile_definitions(labq8_tests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

    target_link_libraries(labq8_tests
        PRIVATE
            labq8_dsp
            juce::juce_audio_processors
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME labq8_tests COMMAND labq8_tests)
endif()
