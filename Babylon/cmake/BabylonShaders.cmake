# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Shaders compiled at build time, with a local cache (ADR-0022).
#
# The compiler is SDL_shadercross: DXC turns HLSL into SPIR-V and DXIL, and
# SPIRV-Cross turns the SPIR-V into MSL. At configure time this file gets a
# working shadercross command line tool:
#
#   - SDL_shadercross is cloned at the pinned commit, with the SPIRV-Cross
#     commit that commit pins as a submodule, into the build tree;
#   - DXC comes as the official prebuilt release for the host (Linux x86_64,
#     Windows x64), checked against its SHA-256; other hosts set
#     BABYLON_DXC_ROOT to a DXC they built (the layout of the release);
#   - the tool is built as its own project (cmake/shadercross/), with SDL3
#     from Babylon's own fetch, and installed with DXC's dxcompiler library
#     into ${BABYLON_CACHE_DIR}/shadercross/<toolchain id>/.
#
# DXC's dxil validator library (libdxil.so, dxil.dll) is never copied or
# loaded on any host: the release package covers it with a Microsoft EULA
# (LICENSE-MS.txt, "solely for use on Windows"), not the NCSA license of the
# rest. DXC 1.9 validates and signs DXIL internally, so dxcompiler alone
# produces the same signed DXIL. The library and that license file are
# deleted right after extraction; only LICENSE-LLVM.txt (NCSA) and
# LICENSE-MIT.txt are kept.
#
# A finished toolchain in the cache is used as it is, so only the first
# configure of a checkout downloads and builds anything. BABYLON_SHADERCROSS
# names an existing tool instead (DXC's dxcompiler next to it); its toolchain
# id is then the hash of the executable.
#
# The cache lives in ${BABYLON_CACHE_DIR}, by default .cache/ in the top
# level source directory: Babylon/.cache for the standalone build, the
# repository root's .cache for the repository build. It is gitignored.
#
# With BABYLON_BUILD_SHADERS OFF nothing is fetched or built, and
# babylon_add_shaders() is a configure error: callers leave out what needs
# shaders.

option(BABYLON_BUILD_SHADERS "Build the shader compiler and compile Babylon's shaders" ON)
set(BABYLON_CACHE_DIR "${CMAKE_SOURCE_DIR}/.cache" CACHE PATH
    "Cache of the shader toolchain and compiled shaders, shared by build trees")
set(BABYLON_SHADERCROSS "" CACHE FILEPATH
    "An existing shadercross executable to use instead of building one")
set(BABYLON_DXC_ROOT "" CACHE PATH
    "A DXC install in the layout of its release packages, for hosts without one")

# Pinned versions. SDL_shadercross has no release tags; the commit is the
# head of main on 2026-09-03. SPIRV-Cross is the submodule commit it pins.
set(_BABYLON_SHADERCROSS_REPOSITORY "https://github.com/libsdl-org/SDL_shadercross.git")
set(_BABYLON_SHADERCROSS_COMMIT "1ff05bec573988a98ef9e0260b4da44f512b8367")
set(_BABYLON_SPIRV_CROSS_COMMIT "1a6169566c73d3da552748fc372fe2bbb856e46e")
set(_BABYLON_DXC_VERSION "1.9.2609")
set(_BABYLON_DXC_BASE_URL
    "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v${_BABYLON_DXC_VERSION}")
set(_BABYLON_SHADERS_DIR "${CMAKE_CURRENT_LIST_DIR}")
# babylon_add_shaders() may be called from any directory of the project that
# added Babylon, so what it needs lives in global properties, not in this
# directory's variables.
set_property(GLOBAL PROPERTY BABYLON_SHADERS_DIR "${_BABYLON_SHADERS_DIR}")

# shadercross is a host tool: it runs during the build on the build machine,
# whatever the target. These describe the host, never the target, so a
# Win32 or cross-compiling configure still gets a tool that runs here and
# loads the host's DXC.
if (CMAKE_HOST_WIN32)
    set(_BABYLON_HOST_EXECUTABLE_SUFFIX ".exe")
else()
    set(_BABYLON_HOST_EXECUTABLE_SUFFIX "")
endif()
string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" _BABYLON_HOST_CPU)
if (_BABYLON_HOST_CPU MATCHES "^(x86_64|amd64)$")
    set(_BABYLON_HOST_CPU "x86_64")
    set(_BABYLON_HOST_VS_PLATFORM "x64")
elseif (_BABYLON_HOST_CPU MATCHES "^(arm64|aarch64)$")
    set(_BABYLON_HOST_CPU "arm64")
    set(_BABYLON_HOST_VS_PLATFORM "ARM64")
else()
    set(_BABYLON_HOST_VS_PLATFORM "Win32")
endif()

# Sets out_url and out_sha256 to the prebuilt DXC package for the host, or
# both empty when there is none.
function(_babylon_dxc_package out_url out_sha256)
    set(url "")
    set(sha "")
    if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND _BABYLON_HOST_CPU STREQUAL "x86_64")
        set(url "${_BABYLON_DXC_BASE_URL}/linux_dxc_2026_09_28.x86_x64.tar.gz")
        set(sha "96faadc7f5c282d2ffda49804beb4c3ee38127bc252b723234e3c5cdf7aa39a1")
    elseif (CMAKE_HOST_WIN32 AND _BABYLON_HOST_CPU STREQUAL "x86_64")
        set(url "${_BABYLON_DXC_BASE_URL}/dxc_2026_09_29.zip")
        set(sha "ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1")
    endif()
    set(${out_url} "${url}" PARENT_SCOPE)
    set(${out_sha256} "${sha}" PARENT_SCOPE)
endfunction()

# Runs a command at configure time; a failure is a configure error that
# shows the command's output.
function(_babylon_run what)
    execute_process(COMMAND ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output)
    if (NOT result EQUAL 0)
        message(FATAL_ERROR "Shader toolchain: ${what} failed (${result}):\n${output}")
    endif()
endfunction()

# The DXC library the tool loads, dxcompiler, below a DXC root in the
# release layout; never dxil (see the top of this file).
function(_babylon_dxc_libraries root out)
    if (CMAKE_HOST_WIN32)
        file(GLOB libraries "${root}/bin/x64/dxcompiler.dll")
    else()
        file(GLOB libraries "${root}/lib/libdxcompiler.*")
    endif()
    set(${out} "${libraries}" PARENT_SCOPE)
endfunction()

# Deletes the dxil validator library and its Microsoft license from a DXC
# root, every architecture's copy.
function(_babylon_remove_dxil root)
    file(GLOB_RECURSE dxil "${root}/*dxil.dll" "${root}/*libdxil.*" "${root}/LICENSE-MS.txt")
    if (dxil)
        file(REMOVE ${dxil})
    endif()
endfunction()

# Downloads, builds and installs the toolchain into install_dir.
function(_babylon_build_shadercross install_dir)
    set(work "${CMAKE_BINARY_DIR}/_babylon_shadercross")
    set(source "${work}/SDL_shadercross")
    set(dxc_root "${source}/external/DirectXShaderCompiler-binaries")
    find_package(Git REQUIRED)

    message(STATUS "Shader toolchain: fetching SDL_shadercross ${_BABYLON_SHADERCROSS_COMMIT}")
    file(REMOVE_RECURSE "${work}")
    file(MAKE_DIRECTORY "${source}")
    _babylon_run("git init" ${GIT_EXECUTABLE} -C "${source}" init -q)
    _babylon_run("fetching SDL_shadercross" ${GIT_EXECUTABLE} -C "${source}" fetch -q --depth 1
        "${_BABYLON_SHADERCROSS_REPOSITORY}" ${_BABYLON_SHADERCROSS_COMMIT})
    _babylon_run("checking out SDL_shadercross" ${GIT_EXECUTABLE} -C "${source}"
        -c advice.detachedHead=false checkout -q FETCH_HEAD)
    _babylon_run("fetching SPIRV-Cross" ${GIT_EXECUTABLE} -C "${source}" submodule update -q
        --init --depth 1 external/SPIRV-Cross)
    execute_process(COMMAND ${GIT_EXECUTABLE} -C "${source}/external/SPIRV-Cross" rev-parse HEAD
        OUTPUT_VARIABLE spirv_cross_head OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (NOT spirv_cross_head STREQUAL _BABYLON_SPIRV_CROSS_COMMIT)
        message(FATAL_ERROR "Shader toolchain: SPIRV-Cross is at ${spirv_cross_head}, "
            "expected ${_BABYLON_SPIRV_CROSS_COMMIT}")
    endif()

    if (BABYLON_DXC_ROOT)
        file(COPY "${BABYLON_DXC_ROOT}/" DESTINATION "${dxc_root}")
    else()
        _babylon_dxc_package(url sha)
        if (NOT url)
            message(FATAL_ERROR "Shader toolchain: DXC has no prebuilt release for "
                "${CMAKE_HOST_SYSTEM_NAME} ${CMAKE_HOST_SYSTEM_PROCESSOR}; build DXC and set "
                "BABYLON_DXC_ROOT, set BABYLON_SHADERCROSS, or configure with "
                "BABYLON_BUILD_SHADERS=OFF")
        endif()
        get_filename_component(archive_name "${url}" NAME)
        set(archive "${work}/${archive_name}")
        message(STATUS "Shader toolchain: downloading DXC ${_BABYLON_DXC_VERSION}")
        file(DOWNLOAD "${url}" "${archive}" EXPECTED_HASH SHA256=${sha} STATUS status)
        list(GET status 0 status_code)
        if (NOT status_code EQUAL 0)
            list(GET status 1 status_text)
            message(FATAL_ERROR "Shader toolchain: downloading ${url} failed: ${status_text}")
        endif()
        file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${dxc_root}")
    endif()
    _babylon_remove_dxil("${dxc_root}")
    _babylon_dxc_libraries("${dxc_root}" libraries)
    if (NOT libraries)
        message(FATAL_ERROR "Shader toolchain: no dxcompiler library in ${dxc_root}")
    endif()

    # The same generator as this build, but for the host (see the top of
    # this file) and a release build of its own, without Babylon's warnings
    # and sanitizers. Visual Studio gets the host's platform, never this
    # build's -A (a Win32 shadercross can't load the x64 dxcompiler.dll);
    # other generators get this build's compilers unless it cross-compiles,
    # when the host's default compilers are used and the toolchain file
    # stays out.
    set(configure_args -S "${_BABYLON_SHADERS_DIR}/shadercross" -B "${work}/build"
        -G "${CMAKE_GENERATOR}"
        "-DSHADERCROSS_SOURCE_DIR=${source}"
        "-DCMAKE_BUILD_TYPE=Release")
    if (CMAKE_GENERATOR MATCHES "^Visual Studio")
        list(APPEND configure_args -A "${_BABYLON_HOST_VS_PLATFORM}")
        if (CMAKE_GENERATOR_TOOLSET)
            list(APPEND configure_args -T "${CMAKE_GENERATOR_TOOLSET}")
        endif()
    else()
        if (CMAKE_MAKE_PROGRAM)
            list(APPEND configure_args "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
        endif()
        if (NOT CMAKE_CROSSCOMPILING)
            list(APPEND configure_args
                "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
                "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
        endif()
    endif()
    if (sdl3_SOURCE_DIR)
        list(APPEND configure_args "-DSHADERCROSS_SDL_SOURCE_DIR=${sdl3_SOURCE_DIR}")
    elseif (SDL3_DIR)
        list(APPEND configure_args "-DSDL3_DIR=${SDL3_DIR}")
    endif()
    # shadercross's FindDirectXShaderCompiler requires a dxil library it
    # never links; point it at dxcompiler so nothing looks for dxil.
    list(GET libraries 0 dxcompiler)
    if (CMAKE_HOST_WIN32)
        list(APPEND configure_args "-DDirectXShaderCompiler_dxil_BINARY=${dxcompiler}")
    else()
        list(APPEND configure_args "-DDirectXShaderCompiler_dxil_LIBRARY=${dxcompiler}")
    endif()
    message(STATUS "Shader toolchain: building shadercross")
    # The environment's compilers and flags may be the target's (a cross
    # configure exports CC and CXX to its children); the host tool takes
    # neither them nor a toolchain file from the environment.
    _babylon_run("configuring shadercross" ${CMAKE_COMMAND} -E env
        --unset=CMAKE_TOOLCHAIN_FILE --unset=CC --unset=CXX --unset=CFLAGS
        --unset=CXXFLAGS --unset=LDFLAGS
        ${CMAKE_COMMAND} ${configure_args})
    _babylon_run("building shadercross" ${CMAKE_COMMAND} --build "${work}/build"
        --config Release --target shadercross)

    # Single-config generators build into shadercross/, multi-config ones
    # into shadercross/Release/.
    file(GLOB tool "${work}/build/shadercross/shadercross${_BABYLON_HOST_EXECUTABLE_SUFFIX}"
        "${work}/build/shadercross/*/shadercross${_BABYLON_HOST_EXECUTABLE_SUFFIX}")
    list(LENGTH tool tool_count)
    if (tool_count GREATER 0)
        list(GET tool 0 tool)
    endif()
    if (NOT EXISTS "${tool}")
        message(FATAL_ERROR "Shader toolchain: the build left no shadercross")
    endif()

    # Installed under a temporary name and renamed, so an interrupted
    # configure never leaves a half toolchain that looks complete.
    set(staging "${install_dir}.partial")
    file(REMOVE_RECURSE "${staging}")
    file(COPY ${tool} ${libraries} DESTINATION "${staging}")
    file(MAKE_DIRECTORY "${staging}/licenses")
    foreach (license IN ITEMS "${source}/LICENSE.txt"
            "${source}/external/SPIRV-Cross/LICENSE"
            "${dxc_root}/LICENSE-LLVM.txt" "${dxc_root}/LICENSE-MIT.txt")
        if (EXISTS "${license}")
            get_filename_component(license_name "${license}" NAME)
            get_filename_component(license_dir "${license}" DIRECTORY)
            get_filename_component(license_owner "${license_dir}" NAME)
            file(COPY_FILE "${license}" "${staging}/licenses/${license_owner}-${license_name}")
        endif()
    endforeach()
    file(RENAME "${staging}" "${install_dir}")
    file(REMOVE_RECURSE "${work}")
endfunction()

# Sets BABYLON_SHADERCROSS_TOOL (the executable) and BABYLON_SHADER_TOOLCHAIN_ID
# (what the shader cache is keyed by), building the toolchain on a miss.
function(_babylon_setup_shader_toolchain)
    if (BABYLON_SHADERCROSS)
        if (NOT EXISTS "${BABYLON_SHADERCROSS}")
            message(FATAL_ERROR "BABYLON_SHADERCROSS: no such file: ${BABYLON_SHADERCROSS}")
        endif()
        file(SHA256 "${BABYLON_SHADERCROSS}" tool_hash)
        set(BABYLON_SHADERCROSS_TOOL "${BABYLON_SHADERCROSS}" PARENT_SCOPE)
        set(BABYLON_SHADER_TOOLCHAIN_ID "custom-${tool_hash}" PARENT_SCOPE)
        return()
    endif()

    string(SUBSTRING "${_BABYLON_SHADERCROSS_COMMIT}" 0 12 shadercross_short)
    string(SUBSTRING "${_BABYLON_SPIRV_CROSS_COMMIT}" 0 12 spirv_cross_short)
    if (BABYLON_DXC_ROOT)
        set(dxc_id "dxc-local")
    else()
        set(dxc_id "dxc-${_BABYLON_DXC_VERSION}")
    endif()
    # The host's system and normalized CPU (windows-amd64 is windows-x86_64
    # now, so tools an earlier Win32 configure built for x86 aren't reused).
    # nodxil: built without DXC's dxil library; older entries carry it.
    string(TOLOWER "${CMAKE_HOST_SYSTEM_NAME}-${_BABYLON_HOST_CPU}" host)
    set(id "${host}-shadercross-${shadercross_short}-spirvcross-${spirv_cross_short}-${dxc_id}-nodxil")
    set(install_dir "${BABYLON_CACHE_DIR}/shadercross/${id}")
    set(tool "${install_dir}/shadercross${_BABYLON_HOST_EXECUTABLE_SUFFIX}")

    if (NOT EXISTS "${tool}")
        # One configure at a time builds a toolchain; the others wait and
        # then find it.
        file(MAKE_DIRECTORY "${BABYLON_CACHE_DIR}/shadercross")
        file(LOCK "${BABYLON_CACHE_DIR}/shadercross/${id}.lock" GUARD FUNCTION TIMEOUT 3600)
        if (NOT EXISTS "${tool}")
            string(TIMESTAMP started "%s")
            _babylon_build_shadercross("${install_dir}")
            string(TIMESTAMP finished "%s")
            math(EXPR seconds "${finished} - ${started}")
            message(STATUS "Shader toolchain: built ${id} in ${seconds} s")
        endif()
    endif()
    message(STATUS "Shader toolchain: ${tool}")
    set(BABYLON_SHADERCROSS_TOOL "${tool}" PARENT_SCOPE)
    set(BABYLON_SHADER_TOOLCHAIN_ID "${id}" PARENT_SCOPE)
endfunction()

if (BABYLON_BUILD_SHADERS)
    _babylon_setup_shader_toolchain()
    set_property(GLOBAL PROPERTY BABYLON_SHADERCROSS_TOOL "${BABYLON_SHADERCROSS_TOOL}")
    set_property(GLOBAL PROPERTY BABYLON_SHADER_TOOLCHAIN_ID "${BABYLON_SHADER_TOOLCHAIN_ID}")
endif()

#   babylon_add_shaders(<target>
#       SOURCES <name>.vert.hlsl | <name>.frag.hlsl ...
#       [NAMESPACE <c++ namespace>]          # default babylon::shaders
#       [PUBLIC_HEADER])                     # see below
#
# Callable from any directory of a project that added Babylon (the toolchain
# is read from global properties set when this file is included).
# Compiles each HLSL file, one shader stage per file, into SPIR-V, DXIL and
# MSL at build time and adds their code to <target>. The stage comes from the
# file name: <name>.vert.hlsl is a vertex shader, <name>.frag.hlsl a fragment
# shader, and the entry point is main. The generated header
#
#       #include <babylon/shaders/<target>.h>
#
# is on <target>'s include path, PRIVATE by default; with PUBLIC_HEADER it is
# PUBLIC, so targets linking a library of shaders include it too. It
# declares, in NAMESPACE, one function per file, named from the file in
# camel case (color.vert.hlsl -> colorVert(), my_tint.frag.hlsl ->
# myTintFrag()), that returns a const babylon::render::ShaderDesc& with the
# three blobs and the texture, uniform and storage buffer counts shadercross
# reflects from the code. The target must link babylon_render.
#
# Resources follow SDL GPU's HLSL conventions: vertex textures and samplers
# in register space0, vertex uniform blocks (cbuffer) in space1, fragment
# textures and samplers in space2, fragment uniform blocks in space3, slots
# from 0 up. Vertex inputs carry the semantic TEXCOORD<n> and
# [[vk::location(n)]], n being VertexAttribute::location. Read-only storage
# buffers (StructuredBuffer) follow the stage's textures in its texture
# registers (ShaderDesc). Storage textures are refused: ShaderDesc has no
# count for them yet.
#
# Shaders must not #include other files: the cache key covers only the file
# itself. Each result is cached in ${BABYLON_CACHE_DIR}/shaders/<key>/, the
# key being the SHA-256 of the source, the toolchain id and the options; a
# hit skips the compiler. A file is recompiled only when it changes.
function(babylon_add_shaders target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "PUBLIC_HEADER" "NAMESPACE" "SOURCES")
    if (arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "babylon_add_shaders(${target}): unknown arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if (NOT BABYLON_BUILD_SHADERS)
        message(FATAL_ERROR "babylon_add_shaders(${target}) needs BABYLON_BUILD_SHADERS=ON")
    endif()
    if (NOT TARGET ${target})
        message(FATAL_ERROR "babylon_add_shaders(${target}): no such target")
    endif()
    if (NOT arg_SOURCES)
        message(FATAL_ERROR "babylon_add_shaders(${target}): SOURCES must not be empty")
    endif()
    if (NOT arg_NAMESPACE)
        set(arg_NAMESPACE "babylon::shaders")
    endif()
    get_property(shadercross_tool GLOBAL PROPERTY BABYLON_SHADERCROSS_TOOL)
    get_property(toolchain_id GLOBAL PROPERTY BABYLON_SHADER_TOOLCHAIN_ID)
    get_property(shaders_dir GLOBAL PROPERTY BABYLON_SHADERS_DIR)
    if (NOT arg_NAMESPACE MATCHES "^[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)*$")
        message(FATAL_ERROR "babylon_add_shaders(${target}): not a namespace: ${arg_NAMESPACE}")
    endif()

    set(generated "${CMAKE_CURRENT_BINARY_DIR}/babylon_shaders/${target}")
    set(header "babylon/shaders/${target}.h")
    set(declarations "")
    set(outputs "")
    set(functions "")
    foreach (source IN LISTS arg_SOURCES)
        get_filename_component(source "${source}" ABSOLUTE)
        get_filename_component(file_name "${source}" NAME)
        if (file_name MATCHES "^([A-Za-z][A-Za-z0-9_]*)\\.(vert|frag)\\.hlsl$")
            set(base "${CMAKE_MATCH_1}")
            set(stage_suffix "${CMAKE_MATCH_2}")
        else()
            message(FATAL_ERROR "babylon_add_shaders(${target}): ${file_name} is not named "
                "<name>.vert.hlsl or <name>.frag.hlsl")
        endif()
        if (stage_suffix STREQUAL "vert")
            set(stage vertex)
        else()
            set(stage fragment)
        endif()

        # base_name + stage in camel case: my_tint + frag -> myTintFrag.
        string(REPLACE "_" ";" words "${base};${stage_suffix}")
        set(function "")
        foreach (word IN LISTS words)
            if (word STREQUAL "")
                continue()
            endif()
            if (function STREQUAL "")
                string(SUBSTRING "${word}" 0 1 first)
                string(TOLOWER "${first}" first)
            else()
                string(SUBSTRING "${word}" 0 1 first)
                string(TOUPPER "${first}" first)
            endif()
            string(SUBSTRING "${word}" 1 -1 rest)
            string(APPEND function "${first}${rest}")
        endforeach()
        if (function IN_LIST functions)
            message(FATAL_ERROR "babylon_add_shaders(${target}): two shaders are named ${function}")
        endif()
        list(APPEND functions "${function}")

        set(output "${generated}/src/${base}.${stage_suffix}.cpp")
        add_custom_command(OUTPUT "${output}"
            COMMAND ${CMAKE_COMMAND}
                "-DSHADERCROSS=${shadercross_tool}"
                "-DTOOLCHAIN_ID=${toolchain_id}"
                "-DCACHE_DIR=${BABYLON_CACHE_DIR}/shaders"
                "-DSOURCE=${source}"
                "-DSTAGE=${stage}"
                "-DOUTPUT=${output}"
                "-DHEADER=${header}"
                "-DNAMESPACE=${arg_NAMESPACE}"
                "-DFUNCTION=${function}"
                -P "${shaders_dir}/BabylonCompileShader.cmake"
            DEPENDS "${source}" "${shaders_dir}/BabylonCompileShader.cmake"
                "${shadercross_tool}"
            COMMENT "Compiling shader ${file_name}"
            VERBATIM)
        list(APPEND outputs "${output}")
        string(APPEND declarations "    // ${file_name}\n"
            "    const babylon::render::ShaderDesc& ${function}();\n")
    endforeach()

    # One quoted string: the declarations hold semicolons.
    set(content "// Generated by babylon_add_shaders(${target}); do not edit.

#pragma once

#include <babylon/render/Pipeline.h>

namespace ${arg_NAMESPACE}
{

${declarations}
}
")
    # Rewritten only when it changes, so a reconfigure rebuilds nothing.
    set(header_path "${generated}/include/${header}")
    if (EXISTS "${header_path}")
        file(READ "${header_path}" existing)
    else()
        set(existing "")
    endif()
    if (NOT existing STREQUAL content)
        file(WRITE "${header_path}" "${content}")
    endif()

    target_sources(${target} PRIVATE ${outputs} "${header_path}")
    if (arg_PUBLIC_HEADER)
        target_include_directories(${target} PUBLIC "${generated}/include")
    else()
        target_include_directories(${target} PRIVATE "${generated}/include")
    endif()
endfunction()
