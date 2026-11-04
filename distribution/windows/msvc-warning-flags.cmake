# LABQ8 Windows beta, build system only (no product file is changed).
#
# The product's CMakeLists.txt gives labq8_dsp the Clang/GCC warning options
# -Wall -Wextra -Wpedantic -Wshadow -Wconversion without a compiler check;
# MSVC rejects them (D8021: invalid numeric argument '/Wextra'). They are
# warnings only: removing them does not change the generated code.
#
# Injected by the workflow with -DCMAKE_PROJECT_INCLUDE=<this file>, so it
# runs right after project(LABQ8); the fix is deferred to the end of the
# top-level CMakeLists.txt, once labq8_dsp exists. Only for MSVC.
if(MSVC)
    function(_labq8_drop_clang_warning_flags)
        get_target_property(options labq8_dsp COMPILE_OPTIONS)
        if(options)
            list(REMOVE_ITEM options -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
            set_target_properties(labq8_dsp PROPERTIES COMPILE_OPTIONS "${options}")
        else()
            set_target_properties(labq8_dsp PROPERTIES COMPILE_OPTIONS "")
        endif()
        get_target_property(after labq8_dsp COMPILE_OPTIONS)
        message(STATUS "LABQ8 Windows beta: labq8_dsp COMPILE_OPTIONS for MSVC: '${after}' (Clang warning options removed)")
    endfunction()
    cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _labq8_drop_clang_warning_flags)
endif()
