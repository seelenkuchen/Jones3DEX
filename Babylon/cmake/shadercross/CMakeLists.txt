# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Builds the SDL_shadercross command line tool for cmake/BabylonShaders.cmake.
# Configured and built once at configure time as its own project, so none of
# these targets reach Babylon's target graph, warnings or sanitizers. The
# caller passes every source directory; nothing is downloaded here.
#
#   SHADERCROSS_SOURCE_DIR   SDL_shadercross checkout, with external/SPIRV-Cross
#                            and the prebuilt DXC in
#                            external/DirectXShaderCompiler-binaries
#   SHADERCROSS_SDL_SOURCE_DIR  SDL3 source to build statically; empty to use
#                            find_package(SDL3) instead
#
# The tool links SDL3 and SPIRV-Cross statically and DXC's dxcompiler
# dynamically; the caller installs dxcompiler (and dxil) next to it.

cmake_minimum_required(VERSION 3.24)
project(BabylonShadercross LANGUAGES C CXX)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (SHADERCROSS_SDL_SOURCE_DIR)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(${SHADERCROSS_SDL_SOURCE_DIR} sdl EXCLUDE_FROM_ALL)
else()
    find_package(SDL3 REQUIRED CONFIG)
endif()

# SPIRV-Cross as static libraries, from the commit shadercross pins.
set(SPIRV_CROSS_STATIC ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
# shadercross export()s its static library, which needs SPIRV-Cross in an
# export set; its install rules are never run.
set(SPIRV_CROSS_SKIP_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${SHADERCROSS_SOURCE_DIR}/external/SPIRV-Cross spirv-cross EXCLUDE_FROM_ALL)
# shadercross looks for this name before it calls find_package().
add_library(spirv_cross_c ALIAS spirv-cross-c)

set(SDLSHADERCROSS_VENDORED OFF CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_DXC ON CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_SPIRVCROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_STATIC ON CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_CLI ON CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_CLI_STATIC ON CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_INSTALL OFF CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${SHADERCROSS_SOURCE_DIR} shadercross)

# The tool finds dxcompiler next to itself on every platform; the caller
# copies the executable and the DXC libraries into one directory.
if (APPLE)
    set_target_properties(shadercross PROPERTIES
        BUILD_WITH_INSTALL_RPATH ON
        INSTALL_RPATH "@loader_path")
elseif (NOT WIN32)
    set_target_properties(shadercross PROPERTIES
        BUILD_WITH_INSTALL_RPATH ON
        INSTALL_RPATH "$ORIGIN")
endif()
