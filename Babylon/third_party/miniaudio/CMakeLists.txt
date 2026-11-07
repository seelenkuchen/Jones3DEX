# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Vendored single-header audio backend library: miniaudio 0.11.25
# (public domain / MIT-0 dual license, the public domain terms are chosen;
# ADR-0010). The implementation is compiled once in miniaudio_impl.c; every
# user includes miniaudio.h with the same MA_NO_* defines, set PUBLIC here so
# the declarations always match the compiled API:
#
#   MA_NO_ENGINE, MA_NO_NODE_GRAPH, MA_NO_RESOURCE_MANAGER   the audio module
#       mixes voices itself on a low-level ma_device and needs none of the
#       high-level graph.
#   MA_NO_DECODING, MA_NO_ENCODING                            the module takes
#       PCM only; decoding lives elsewhere.
#   MA_NO_GENERATION                                          no procedural
#       waveform sources.
#
# The include directory is SYSTEM so the vendored header does not get
# Babylon's warning flags, and miniaudio's own warnings are silenced for the
# implementation file only. It is never added through
# _babylon_target_settings, so it also carries no -Werror or sanitizer flags.

add_library(miniaudio STATIC miniaudio_impl.c)

target_include_directories(miniaudio SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(miniaudio PUBLIC
    MA_NO_ENGINE MA_NO_NODE_GRAPH MA_NO_RESOURCE_MANAGER
    MA_NO_DECODING MA_NO_ENCODING MA_NO_GENERATION)
target_compile_definitions(miniaudio PRIVATE MINIAUDIO_IMPLEMENTATION)

if (MSVC)
    target_compile_options(miniaudio PRIVATE /W0)
elseif (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(miniaudio PRIVATE -Wno-everything)
endif()

if (UNIX AND NOT APPLE)
    # Miniaudio runtime-loads the sound system libraries; it only needs the
    # base C runtime pieces directly.
    target_link_libraries(miniaudio PUBLIC pthread m ${CMAKE_DL_LIBS})
elseif (APPLE)
    target_link_libraries(miniaudio PUBLIC
        "-framework AudioToolbox" "-framework CoreAudio" "-framework Foundation")
endif()

set_target_properties(miniaudio PROPERTIES POSITION_INDEPENDENT_CODE ON FOLDER Babylon)
