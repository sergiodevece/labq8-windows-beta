# E0 <-> E1a audio compatibility harness. Not part of the main build: run
# tests/compat/run_compat.sh, which exports the E0 sources from git first.
cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
project(LABQ8_COMPAT VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LABQ8_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(LABQ8_JUCE_DIR "${LABQ8_ROOT}/external/JUCE" CACHE PATH "JUCE 9.0.3")
set(E0_SRC_DIR "" CACHE PATH "Exported E0 'src' directory (git archive dfcf25b src)")

if(NOT EXISTS "${E0_SRC_DIR}/plugin/PluginProcessor.cpp")
    message(FATAL_ERROR "Set E0_SRC_DIR to the exported E0 src directory")
endif()

add_subdirectory("${LABQ8_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

function(labq8_compat_target name side src_dir)
    file(GLOB sources "${src_dir}/dsp/*.cpp" "${src_dir}/parameters/*.cpp" "${src_dir}/plugin/*.cpp")
    juce_add_console_app(${name} PRODUCT_NAME "${name}")
    target_sources(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/CompatRender.cpp" ${sources})
    target_include_directories(${name} PRIVATE "${src_dir}")
    target_compile_definitions(${name} PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 LABQ8_COMPAT_SIDE="${side}")
    target_link_libraries(${name} PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
endfunction()

labq8_compat_target(compat_e0 e0 "${E0_SRC_DIR}")
labq8_compat_target(compat_e1a e1a "${LABQ8_ROOT}/src")
