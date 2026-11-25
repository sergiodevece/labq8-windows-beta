cmake_minimum_required(VERSION 3.22)

# Must be set before project(). 11.0 is the first macOS with Apple Silicon.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(IAQ8 VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use: cmake -S iaq8 -B build/iaq8-debug")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(IAQ8_BUILD_TESTS "Build the IAQ8 test executable" ON)

# IAQ8 is its own project. It reuses LABQ8's DSP sources and test harness by
# path and never modifies anything outside iaq8/.
set(IAQ8_LABQ8_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "LABQ8 repository root")
set(IAQ8_JUCE_DIR "${IAQ8_LABQ8_DIR}/external/JUCE" CACHE PATH "JUCE 9.0.3 source checkout")

if(NOT EXISTS "${IAQ8_JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE not found at ${IAQ8_JUCE_DIR}.\n"
        "Run: git clone --depth 1 --branch 9.0.3 https://github.com/juce-framework/JUCE.git external/JUCE")
endif()

add_subdirectory("${IAQ8_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

#==============================================================================
# LABQ8 DSP (pure C++), compiled from ../src/dsp as is.
set(IAQ8_DSP_DIR "${IAQ8_LABQ8_DIR}/src/dsp")
add_library(labq8_dsp_for_iaq8 STATIC
    ${IAQ8_DSP_DIR}/BiquadDesign.cpp
    ${IAQ8_DSP_DIR}/BellBand.cpp
    ${IAQ8_DSP_DIR}/EqBand.cpp
    ${IAQ8_DSP_DIR}/EqCore.cpp
    ${IAQ8_DSP_DIR}/DynamicDetector.cpp
    ${IAQ8_DSP_DIR}/DynamicSection.cpp
    ${IAQ8_DSP_DIR}/DynamicBellBand.cpp
    ${IAQ8_DSP_DIR}/StereoModeEngine.cpp)

target_include_directories(labq8_dsp_for_iaq8 PUBLIC "${IAQ8_LABQ8_DIR}/src")
target_compile_features(labq8_dsp_for_iaq8 PUBLIC cxx_std_20)
target_compile_options(labq8_dsp_for_iaq8 PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)

#==============================================================================
# IAQ8 core: everything under src/ except plugin/ (host adapter). Later tasks
# just drop their sources in; the glob picks them up on reconfigure.
file(GLOB_RECURSE IAQ8_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER IAQ8_CORE_SOURCES EXCLUDE REGEX "/src/plugin/")

add_library(iaq8_core STATIC ${IAQ8_CORE_SOURCES})
target_include_directories(iaq8_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(iaq8_core PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(iaq8_core
    PUBLIC
        labq8_dsp_for_iaq8
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

#==============================================================================
# Plugin (AU + VST3). Never copied into system or user plug-in folders.
juce_add_plugin(IAQ8
    COMPANY_NAME "VitaPlug"
    COMPANY_WEBSITE ""
    BUNDLE_ID com.vitaplug.iaq8
    PLUGIN_MANUFACTURER_CODE Vtpl
    PLUGIN_CODE Iaq8
    FORMATS AU VST3
    PRODUCT_NAME "IAQ8"
    VERSION 0.1.0
    DESCRIPTION "VitaPlug IAQ8 AI-assisted EQ"
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

set(IAQ8_PLUGIN_SOURCES
    src/plugin/Iaq8Processor.cpp
    src/plugin/DebugEditor.cpp)

target_sources(IAQ8 PRIVATE ${IAQ8_PLUGIN_SOURCES})

target_compile_definitions(IAQ8
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(IAQ8
    PRIVATE
        iaq8_core
        juce::juce_audio_processors
        juce::juce_gui_basics
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

#==============================================================================
# iaq8_analyze <file.wav> <source>: analysis JSON on stdout (reads whole files).
juce_add_console_app(iaq8_analyze
    PRODUCT_NAME "iaq8_analyze"
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

target_sources(iaq8_analyze PRIVATE tools/AnalyzeMain.cpp)
target_compile_definitions(iaq8_analyze PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(iaq8_analyze
    PRIVATE
        iaq8_core
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

#==============================================================================
# Tests: run without any DAW. Reuses LABQ8's harness by path (no copies).
if(IAQ8_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(iaq8_tests
        PRODUCT_NAME "iaq8_tests"
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE)

    file(GLOB IAQ8_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")

    target_sources(iaq8_tests PRIVATE
        ${IAQ8_LABQ8_DIR}/tests/TestMain.cpp
        ${IAQ8_LABQ8_DIR}/tests/AllocationGuard.cpp
        # LABQ8's layout, compiled only to compare against IAQ8's (parity test).
        ${IAQ8_LABQ8_DIR}/src/parameters/ParameterLayout.cpp
        ${IAQ8_TEST_SOURCES}
        ${IAQ8_PLUGIN_SOURCES})

    target_include_directories(iaq8_tests PRIVATE "${IAQ8_LABQ8_DIR}/tests")

    target_compile_definitions(iaq8_tests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        IAQ8_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

    target_link_libraries(iaq8_tests
        PRIVATE
            iaq8_core
            juce::juce_audio_processors
            juce::juce_gui_basics
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME iaq8_tests COMMAND iaq8_tests)
endif()
