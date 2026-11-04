# LABQ8 Windows beta, build system only (no product file is changed).
#
# Injected by the workflow with -DCMAKE_PROJECT_INCLUDE=<this file>, so it
# runs right after project(LABQ8); the fixes are deferred to the end of the
# top-level CMakeLists.txt, once the targets exist. Only for MSVC.
#
# 1. labq8_dsp: the product's CMakeLists.txt gives it the Clang/GCC warning
#    options -Wall -Wextra -Wpedantic -Wshadow -Wconversion without a
#    compiler check; MSVC rejects them (D8021 '/Wextra'). They are warnings
#    only: removing them does not change the generated code.
# 2. labq8_tests: tests/AllocationGuard.cpp (test support: counts heap
#    allocations) uses posix_memalign and frees aligned blocks with free(),
#    POSIX only. It is replaced by distribution/windows/AllocationGuardWindows.cpp
#    (same API and counting, Windows-safe release). The tests are unchanged.
set(LABQ8_WINDOWS_OVERRIDES_DIR "${CMAKE_CURRENT_LIST_DIR}")
if(MSVC OR LABQ8_FORCE_WINDOWS_OVERRIDES)
    function(_labq8_windows_overrides)
        if(TARGET labq8_dsp)
            get_target_property(options labq8_dsp COMPILE_OPTIONS)
            if(options)
                list(REMOVE_ITEM options -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
            else()
                set(options "")
            endif()
            set_target_properties(labq8_dsp PROPERTIES COMPILE_OPTIONS "${options}")
            message(STATUS "LABQ8 Windows beta: labq8_dsp COMPILE_OPTIONS '${options}' (Clang warning options removed)")
        endif()
        if(TARGET labq8_tests)
            get_target_property(sources labq8_tests SOURCES)
            list(FIND sources tests/AllocationGuard.cpp index)
            if(index GREATER_EQUAL 0)
                list(REMOVE_AT sources ${index})
                list(INSERT sources ${index} "${LABQ8_WINDOWS_OVERRIDES_DIR}/AllocationGuardWindows.cpp")
                set_target_properties(labq8_tests PROPERTIES SOURCES "${sources}")
                target_include_directories(labq8_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests")
                message(STATUS "LABQ8 Windows beta: labq8_tests uses AllocationGuardWindows.cpp instead of tests/AllocationGuard.cpp")
            endif()
        endif()
    endfunction()
    get_property(_labq8_deferred GLOBAL PROPERTY LABQ8_WINDOWS_OVERRIDES_DEFERRED)
    if(NOT _labq8_deferred)
        set_property(GLOBAL PROPERTY LABQ8_WINDOWS_OVERRIDES_DEFERRED TRUE)
        cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _labq8_windows_overrides)
    endif()
endif()
