# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Compiles one HLSL shader for babylon_add_shaders() (BabylonShaders.cmake)
# and writes the C++ source that returns its ShaderDesc. Run at build time:
#
#   cmake -DSHADERCROSS=<tool> -DTOOLCHAIN_ID=<id> -DCACHE_DIR=<dir>
#         -DSOURCE=<x.vert.hlsl> -DSTAGE=vertex|fragment -DOUTPUT=<x.cpp>
#         -DHEADER=<babylon/shaders/t.h> -DNAMESPACE=<ns> -DFUNCTION=<name>
#         -P BabylonCompileShader.cmake
#
# The cache entry CACHE_DIR/<key>/ holds code.spv, code.dxil, code.msl and
# reflect.json; the key is the SHA-256 of the toolchain id, the options and
# the source bytes. A complete entry skips the compiler.

cmake_minimum_required(VERSION 3.24)

foreach (var IN ITEMS SHADERCROSS TOOLCHAIN_ID CACHE_DIR SOURCE STAGE OUTPUT HEADER NAMESPACE FUNCTION)
    if (NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "BabylonCompileShader.cmake: ${var} is not set")
    endif()
endforeach()

# Bump the format when the entry layout or the options change meaning.
set(options "format=1;stage=${STAGE};entry=main;outputs=spv,dxil,msl,json")
set(outputs spv dxil msl json)

file(READ "${SOURCE}" source_hex HEX)
string(SHA256 key "${TOOLCHAIN_ID}\n${options}\n${source_hex}")
set(entry "${CACHE_DIR}/${key}")

set(complete TRUE)
foreach (format IN LISTS outputs)
    if (format STREQUAL "json")
        set(name "reflect.json")
    else()
        set(name "code.${format}")
    endif()
    if (NOT EXISTS "${entry}/${name}")
        set(complete FALSE)
    endif()
endforeach()

if (NOT complete)
    # Compiled into a private directory and renamed into place, so readers
    # never see half an entry; when two builds race, the first rename wins.
    string(RANDOM LENGTH 12 suffix)
    set(staging "${entry}.tmp-${suffix}")
    file(MAKE_DIRECTORY "${staging}")
    get_filename_component(source_name "${SOURCE}" NAME)
    foreach (format IN LISTS outputs)
        if (format STREQUAL "json")
            set(name "reflect.json")
        else()
            set(name "code.${format}")
        endif()
        # shadercross infers the destination from the extension, so each
        # output gets one first; the source format and stage are explicit.
        execute_process(
            COMMAND "${SHADERCROSS}" "${SOURCE}" --source HLSL --stage ${STAGE}
                --entrypoint main --output "${staging}/out.${format}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE output
            ERROR_VARIABLE output)
        if (NOT result EQUAL 0 OR NOT EXISTS "${staging}/out.${format}")
            file(REMOVE_RECURSE "${staging}")
            message(FATAL_ERROR "shadercross failed on ${source_name} (${format}):\n${output}")
        endif()
        file(RENAME "${staging}/out.${format}" "${staging}/${name}")
    endforeach()
    file(RENAME "${staging}" "${entry}" RESULT rename_result)
    if (NOT rename_result STREQUAL "0")
        # Another build put the same entry in place first.
        file(REMOVE_RECURSE "${staging}")
    endif()
    message(STATUS "Compiled ${source_name} into the shader cache (${key})")
endif()

# Resource counts from shadercross's reflection.
file(READ "${entry}/reflect.json" reflection)
foreach (field IN ITEMS samplers uniform_buffers storage_textures storage_buffers)
    string(JSON value ERROR_VARIABLE json_error GET "${reflection}" ${field})
    if (json_error)
        message(FATAL_ERROR "${SOURCE}: no ${field} in shadercross's reflection: ${json_error}")
    endif()
    set(count_${field} "${value}")
endforeach()
if (NOT count_storage_textures EQUAL 0 OR NOT count_storage_buffers EQUAL 0)
    message(FATAL_ERROR "${SOURCE}: storage textures and buffers are not supported yet "
        "(ShaderDesc has no counts for them)")
endif()

# SPIRV-Cross renames the entry point in MSL (main becomes main0); take the
# name from the function the MSL declares.
file(READ "${entry}/code.msl" msl)
if (msl MATCHES "(vertex|fragment)[ \t\n]+[A-Za-z_][A-Za-z0-9_]*[ \t\n]+([A-Za-z_][A-Za-z0-9_]*)[ \t\n]*\\(")
    set(msl_entry "${CMAKE_MATCH_2}")
else()
    message(FATAL_ERROR "${SOURCE}: found no ${STAGE} function in the MSL")
endif()

# One C++ array per blob: 16 bytes per line.
function(byte_array path out)
    file(READ "${path}" hex HEX)
    string(LENGTH "${hex}" length)
    if (length EQUAL 0)
        message(FATAL_ERROR "${path} is empty")
    endif()
    set(lines "")
    set(position 0)
    while (position LESS length)
        string(SUBSTRING "${hex}" ${position} 32 chunk)
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " chunk "${chunk}")
        string(STRIP "${chunk}" chunk)
        string(APPEND lines "        ${chunk}\n")
        math(EXPR position "${position} + 32")
    endwhile()
    set(${out} "${lines}" PARENT_SCOPE)
endfunction()

byte_array("${entry}/code.spv" spv_bytes)
byte_array("${entry}/code.dxil" dxil_bytes)
byte_array("${entry}/code.msl" msl_bytes)
get_filename_component(source_name "${SOURCE}" NAME)

set(content "// Generated by babylon_add_shaders() from ${source_name}; do not edit.
// Toolchain ${TOOLCHAIN_ID}, cache key ${key}.

#include <${HEADER}>

#include <cstddef>
#include <iterator>
#include <vector>

namespace
{

    const unsigned char kSpirV[] = {
${spv_bytes}    };

    const unsigned char kDxil[] = {
${dxil_bytes}    };

    const unsigned char kMsl[] = {
${msl_bytes}    };

    std::vector<std::byte> bytesOf(const unsigned char* first, std::size_t size)
    {
        const auto* const begin = reinterpret_cast<const std::byte*>(first);
        return std::vector<std::byte>(begin, begin + size);
    }

}

namespace ${NAMESPACE}
{

    const babylon::render::ShaderDesc& ${FUNCTION}()
    {
        using babylon::render::ShaderBlob;
        using babylon::render::ShaderFormat;
        static const babylon::render::ShaderDesc desc{
            {
                ShaderBlob{ ShaderFormat::SpirV, bytesOf(kSpirV, std::size(kSpirV)), \"main\" },
                ShaderBlob{ ShaderFormat::Dxil, bytesOf(kDxil, std::size(kDxil)), \"main\" },
                ShaderBlob{ ShaderFormat::Msl, bytesOf(kMsl, std::size(kMsl)), \"${msl_entry}\" },
            },
            ${count_samplers},   // textureCount
            ${count_uniform_buffers},   // uniformCount
        };
        return desc;
    }

}
")
file(WRITE "${OUTPUT}" "${content}")
