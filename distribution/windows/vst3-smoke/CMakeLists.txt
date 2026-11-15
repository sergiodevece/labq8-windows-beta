# LABQ8 Windows beta: a JUCE VST3 test host that loads the REAL built bundle
# (not the product sources). Separate project so the product's CMake stays
# identical to the stable commit. Adapted from HARMOHIT MASTER R2.
cmake_minimum_required(VERSION 3.22)
project(LABQ8Vst3Smoke VERSION 0.0.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JUCE_DIR "" CACHE PATH "Pinned JUCE 9.0.3 checkout")
add_subdirectory("${JUCE_DIR}" juce)

juce_add_console_app(labq8_vst3_smoke PRODUCT_NAME "labq8_vst3_smoke")
target_sources(labq8_vst3_smoke PRIVATE Vst3Smoke.cpp)
target_compile_definitions(labq8_vst3_smoke PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(labq8_vst3_smoke PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
