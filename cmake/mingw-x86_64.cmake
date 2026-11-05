# MinGW-w64 x86_64 cross-compilation toolchain (Linux host -> Windows 64-bit), the 64-bit twin of
# cmake/mingw-i686.cmake for the Babylon column (-DJONES3D_PLATFORM_BABYLON=ON).
# Use with: cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-x86_64.cmake
#
# The runtime is switched from the ancient msvcrt to UCRT: the engine and its tests use the
# Secure CRT functions (e.g. vsscanf_s from <stdio.h> with buffer-size arguments), which the
# classic msvcrt headers/library do not provide. GCC 13 has no -mcrtdll, so the linker spec of
# the driver is rewritten here (-lmsvcrt -> -lucrt) and activated through a specs file.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
# Executables built for Windows (e.g. the std_hlslc shader compiler) run under Wine.
set(CMAKE_CROSSCOMPILING_EMULATOR wine)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# Search libraries and system headers only in the MinGW sysroot.
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

execute_process(
    COMMAND x86_64-w64-mingw32-gcc -dumpspecs
    OUTPUT_VARIABLE jones3d_mingw_dumpspecs
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE jones3d_mingw_dumpspecs_result
)
if (NOT jones3d_mingw_dumpspecs_result EQUAL 0 OR NOT jones3d_mingw_dumpspecs)
    message(FATAL_ERROR "Could not obtain the linker specs from x86_64-w64-mingw32-gcc -dumpspecs.")
endif()
string(REPLACE "-lmsvcrt" "-lucrt" jones3d_mingw_ucrt_specs "${jones3d_mingw_dumpspecs}")
file(WRITE "${CMAKE_BINARY_DIR}/mingw-ucrt.specs" "${jones3d_mingw_ucrt_specs}\n")

set(CMAKE_C_FLAGS_INIT "-D_UCRT -specs=${CMAKE_BINARY_DIR}/mingw-ucrt.specs")
# Note: keep -D_UCRT off the C++ flags. The C++ standard libraries in this toolchain build were
# compiled for the msvcrt flavor, where mbstate_t is "int"; with -D_UCRT the Unicode conversion
# templates of <codecvt> mangle to the _Mbstatet struct flavor and no longer link. The driver
# specs file alone keeps the UCRT import table for C++ without the header ABI change.
set(CMAKE_CXX_FLAGS_INIT "-specs=${CMAKE_BINARY_DIR}/mingw-ucrt.specs")
# Added: link the GCC and C++ runtimes statically, so the test executables (the C++ Babylon suites and
# the column's C++ entry points) run under Wine without libgcc_s_seh-1.dll and libstdc++-6.dll next to
# them. Babylon is configured before the root's -static-libgcc link option, so it is set here for all.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++")
