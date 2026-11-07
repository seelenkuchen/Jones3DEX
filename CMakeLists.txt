cmake_minimum_required(VERSION 3.15)

cmake_policy(SET CMP0091 NEW)

project(OpenJones3D VERSION 0.4.0)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 20) # C++20
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable solution folders
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW) # option() honors normal variables (variables of same name set prior to calling the option()).

function(jones3d_print_config_entry label value)
    set(jones3d_dot_fill "........................................")
    string(LENGTH "${label}" label_len)
    math(EXPR pad_len "32 - ${label_len}")
    if (pad_len LESS 1)
        set(pad_len 1)
    endif()
    string(SUBSTRING "${jones3d_dot_fill}" 0 ${pad_len} dot_pad)
    message(STATUS "  ${label} ${dot_pad} ${value}")
endfunction()

function(jones3d_print_config_option label value)
    if(${value})
        set(option_state "ON")
    else()
        set(option_state "OFF")
    endif()
    jones3d_print_config_entry("${label}" "${option_state}")
endfunction()

option(JONES3D_BUILD_PROGRAMS "Build programs" ON)
option(JONES3D_BUILD_TESTS "Build tests" ON)
option(JONES3D_QOL_IMPROVEMENTS "Enable quality-of-life improvements" ON)
option(JONES3D_SPEEDRUN_BUILD "Enable vanilla bugs and glitches used in speedruns" OFF)
option(JONES3D_RUNTIME_GUARDS "Enable runtime safety guards for hardened release behavior" OFF)
# Added: Sanitizer flags for the native Null build (Jones3D-headless); they are GCC/Clang
# options with no MSVC equivalent here, so Windows builds reject the option instead of
# silently ignoring it.
option(JONES3D_SANITIZE "Add address and undefined behavior sanitizer flags to native builds" OFF)

# Added: the Babylon column: the Null column with platform services moved onto the Babylon Engine one by one.
# Altered: the Win32/DirectX game is gone (ADR-0011), so Windows builds the Babylon column only: the
# option defaults ON there, and the Null column stays non-Windows.
if (WIN32)
    option(JONES3D_PLATFORM_BABYLON "Build the Babylon platform column instead of the Null column" ON)
    if (NOT JONES3D_PLATFORM_BABYLON)
        message(FATAL_ERROR "The Null column is non-Windows; Windows builds the Babylon column: configure with -DJONES3D_PLATFORM_BABYLON=ON.")
    endif()
    if (CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR "32-bit Windows is not supported: the Babylon column builds as a 64-bit Windows program (MSVC x64, or cmake/mingw-x86_64.cmake).")
    endif()
else()
    option(JONES3D_PLATFORM_BABYLON "Build the Babylon platform column instead of the Null column" OFF)
endif()

# Altered: tests stay ON on the Null platform; the portable unit suites run natively.
set(JONES3D_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)

if (JONES3D_QOL_IMPROVEMENTS)
    set(J3D_QOL_IMPROVEMENTS ON)
else()
    # Legacy build, enable the speedrun build option to restore all vanilla engine quirks and bugs
    set(JONES3D_SPEEDRUN_BUILD ON)
endif()

if (JONES3D_SPEEDRUN_BUILD)
    set(J3D_SPEEDRUN_BUILD on)
    if (JONES3D_QOL_IMPROVEMENTS)
      set(JONES3D_BUILD_SUFFIX "-sr") # Add '-sr' version suffix when not legacy build
    endif()
endif()

if (JONES3D_RUNTIME_GUARDS)
    set(J3D_RUNTIME_GUARDS ON)
endif()

# Altered: testing is enabled even without JONES3D_BUILD_TESTS: the native CRT shim test
# (j3dcore.Crt in Libs/j3dcore/Tests) needs it enabled from the root.
enable_testing()

# Babylon Engine: built before the game's compile options so it keeps its own flags.
option(JONES3D_BUILD_BABYLON "Build the Babylon Engine" ON)

# Added: the Babylon column replaces the Null column's stdPlatform time functions with Babylon ones,
# so it needs the Babylon Engine.
# Altered: the column also builds on Windows (64-bit only); the plain Null column stays non-Windows only.
if (JONES3D_PLATFORM_BABYLON)
    if (NOT JONES3D_BUILD_BABYLON)
        message(FATAL_ERROR "JONES3D_PLATFORM_BABYLON needs the Babylon Engine: configure with -DJONES3D_BUILD_BABYLON=ON.")
    endif()
    set(J3D_PLATFORM_BABYLON ON)
endif()

if (JONES3D_BUILD_BABYLON)
    set(BABYLON_BUILD_TESTS ${JONES3D_BUILD_TESTS} CACHE BOOL "" FORCE)
    set(BABYLON_SANITIZE ${JONES3D_SANITIZE} CACHE BOOL "" FORCE)
    add_subdirectory(Babylon)
endif()

# Added: The Null platform has no graphics backend yet (the Null/Babylon columns only).
# Altered: the DirectX 6 and 9 selection is gone with the Win32/DirectX game (ADR-0011); nothing sets
# J3D_DIRECTX6, J3D_DIRECTX9 or J3D_COLORKEYSUPPORTED any more, and the code no longer tests them.
set(JONES3D_GAPI_NAME "Null")

# Keep configuration-specific headers in the build tree so differently configured builds can coexist.
set(JONES3D_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${JONES3D_GENERATED_INCLUDE_DIR}/j3dcore")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Libs/j3dcore/j3d.h.in"
    "${JONES3D_GENERATED_INCLUDE_DIR}/j3dcore/j3d.h"
)
include_directories(BEFORE "${JONES3D_GENERATED_INCLUDE_DIR}")

# Use the static CRT for optimized configurations and the debug DLL CRT for Debug.
set(CMAKE_MSVC_RUNTIME_LIBRARY
    "$<$<CONFIG:Debug>:MultiThreadedDebugDLL>$<$<CONFIG:Release>:MultiThreaded>$<$<CONFIG:RelWithDebInfo>:MultiThreaded>$<$<CONFIG:MinSizeRel>:MultiThreaded>"
)

if (MSVC)
    # Clion IDE can't read the preprocessor macro below and needs special treatment
    if($ENV{CLION_IDE})
        message(STATUS "Generating in Clion environment")
        add_compile_definitions(__FILE_NAME__=__FILE__)
    else()
        add_compile_definitions("__FILE_NAME__=\"%(Filename)%(Extension)\"")
    endif()

    add_compile_options(
        /utf-8
        /Zc:__STDC__
        /Zc:__cplusplus
        /diagnostics:caret
        /sdl
        #"$<$<CONFIG:DEBUG>:/Gm>" # minimal rebuild TODO: uncomment
        /W4 # warning level 4
        /Zc:preprocessor # This option enables a token-based preprocessor that conforms to C99 and C++11 and later standards.
        "$<$<NOT:$<CXX_COMPILER_ID:Clang>>:/MP>" # TODO: remove when /Gm is uncommented
        #$<$<AND:$<NOT:$<CXX_COMPILER_ID:Clang>>,$<NOT:$<CONFIG:DEBUG>>>:/MP> TODO: uncomment when /Gm is uncommented
        "$<$<CONFIG:DEBUG>:/ZI>"
    )
    add_link_options("/Brepro")
elseif(MINGW)
    # Altered: the MinGW-w64 x86_64 build of the Babylon column (cmake/mingw-x86_64.cmake), used for
    # the Windows unit tests on Linux; the i686 build went with the DirectX game. -mlong-double-64
    # matches MSVC x64's 8-byte long double.
    # -msse2/-mfpmath=sse to match the MSVC SSE2 code generation, -fno-strict-aliasing to
    # keep bit relayout behavior of the original code paths the same.
    # -mlong-double-64 makes long double 8 bytes like on MSVC (i686 GCC defaults to 12) and keeps
    # the 64-bit form that the UCRT scanf/printf conversion specifiers expect.
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:C>:-std=gnu11>" # keep CMAKE_C_EXTENSIONS from forcing -std=c11, tests rely on GNU behavior
        -fms-extensions
        -msse2
        -mfpmath=sse
        -fno-strict-aliasing
        -mlong-double-64
        -Wall
        -Wno-unused
        -Wno-missing-braces
        -Wno-switch
        -Wno-parentheses
    )
    add_link_options(-static-libgcc)
elseif(NOT WIN32 AND (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"))
    # Added: Native Clang/GCC non-Windows build of the engine libraries (Null platform,
    # Windows engine .c files are intentionally not validated). -fms-extensions like the
    # MinGW branch, -fno-strict-aliasing to keep the original bit relayout of the code paths.
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:C>:-std=gnu11>"
        -fms-extensions
        -fno-strict-aliasing
        -Wno-switch
        -Wno-parentheses
        -Wno-unknown-pragmas
    )
    # Added: glibc's backtrace_symbols in stdPlatformNull needs the engine symbols exported to the
    # dynamic symbol table to name the frames of the headless runner. The option is not Linux-only:
    # on Apple hosts the Clang driver turns -rdynamic into ld64's -export_dynamic (and ignores it on
    # linkers too old for that option), so the same flag line works on both native platforms.
    add_link_options(-rdynamic)
    # Added: glibc keeps the math functions (sinf, atan2f, sqrtf) in libm; the Windows CRT has them
    # in the default libraries. Native test executables call engine code that uses them directly.
    # Altered: macOS has no libm to name: its math functions are in libSystem, which the driver
    # always links, so the Apple link line stays without it. Linux and Windows are unchanged.
    if (NOT APPLE)
        link_libraries(m)
    endif()
    if (CMAKE_C_COMPILER_ID MATCHES "Clang")
        # Added: fail the native build on any cast that truncates a pointer (or widens an int
        # into one) so 64-bit builds stay pointer-safe. Clang only: these warning groups have
        # no GCC equivalent. Windows compile flags are unchanged.
        add_compile_options(
            -Werror=pointer-to-int-cast
            -Werror=int-to-pointer-cast
            -Werror=void-pointer-to-int-cast
            -Werror=void-pointer-to-enum-cast
            -Werror=pointer-to-enum-cast
            -Werror=int-conversion
        )
    endif()
else()
    message(FATAL_ERROR "Unsupported compiler: ${CMAKE_CXX_COMPILER_ID}")
endif()

# Added: Sanitizer flags for the native Null build; must be set before the subdirectories
# so every native target (engine libs, Jones3DGame, Jones3D-headless) is instrumented.
if (JONES3D_SANITIZE)
    if (WIN32)
        message(FATAL_ERROR "JONES3D_SANITIZE is only supported on native (non-Windows) builds")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Add Libs subdirectory
add_subdirectory(Libs)

# Add main project Jones3D
add_subdirectory(Jones3D)

if (JONES3D_BUILD_PROGRAMS)
    add_subdirectory(Programs)
endif()

if (CMAKE_CONFIGURATION_TYPES)
    string(REPLACE ";" ", " JONES3D_CONFIGURATION_SUMMARY "${CMAKE_CONFIGURATION_TYPES}")
else()
    if (CMAKE_BUILD_TYPE)
        set(JONES3D_CONFIGURATION_SUMMARY "${CMAKE_BUILD_TYPE}")
    else()
        set(JONES3D_CONFIGURATION_SUMMARY "<default>")
    endif()
endif()

message(STATUS "")
message(STATUS "OpenJones3D configuration summary")
jones3d_print_config_entry("Version" "v${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}${JONES3D_BUILD_SUFFIX}")
jones3d_print_config_entry("Generator" "${CMAKE_GENERATOR}")
if (CMAKE_GENERATOR_PLATFORM)
    jones3d_print_config_entry("Platform" "${CMAKE_GENERATOR_PLATFORM}")
endif()
if (CMAKE_CONFIGURATION_TYPES)
    jones3d_print_config_entry("Configurations" "${JONES3D_CONFIGURATION_SUMMARY}")
else()
    jones3d_print_config_entry("Build type" "${JONES3D_CONFIGURATION_SUMMARY}")
endif()
jones3d_print_config_entry("Graphics API" "${JONES3D_GAPI_NAME}")
jones3d_print_config_option("QOL improvements" JONES3D_QOL_IMPROVEMENTS)
jones3d_print_config_option("Runtime guards" JONES3D_RUNTIME_GUARDS)
jones3d_print_config_option("Speedrun build" JONES3D_SPEEDRUN_BUILD)
jones3d_print_config_option("Build programs" JONES3D_BUILD_PROGRAMS)
jones3d_print_config_option("Build tests" JONES3D_BUILD_TESTS)
