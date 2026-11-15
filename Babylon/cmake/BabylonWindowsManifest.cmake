# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Babylon's own copy of the UTF-8 application manifest helper, so Babylon's test programs get
# the manifest when Babylon builds on its own. It must not reach outside Babylon/ for it.
#
#   babylon_add_windows_manifest(<target>)
#
# adds the manifest to an executable target on Windows: MSVC embeds a .manifest source with
# mt.exe, MinGW embeds the same file through a generated resource script. Non-Windows hosts and
# other compilers do nothing.

set(BABYLON_WINDOWS_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/windows/utf8.manifest")

if (WIN32 AND MINGW)
    file(TO_CMAKE_PATH "${BABYLON_WINDOWS_MANIFEST}" babylon_windows_manifest_rc_path)
    set(BABYLON_WINDOWS_MANIFEST_RC "${CMAKE_BINARY_DIR}/babylon-utf8-manifest.rc")
    file(WRITE "${BABYLON_WINDOWS_MANIFEST_RC}"
        "1 24 \"${babylon_windows_manifest_rc_path}\"\n")
endif()

function(babylon_add_windows_manifest target)
    if (NOT WIN32)
        return()
    endif()
    if (NOT TARGET ${target})
        message(FATAL_ERROR "babylon_add_windows_manifest(${target}): no such target")
    endif()

    if (MSVC)
        target_sources(${target} PRIVATE "${BABYLON_WINDOWS_MANIFEST}")
    elseif (MINGW)
        target_sources(${target} PRIVATE "${BABYLON_WINDOWS_MANIFEST_RC}")
    else()
        message(WARNING "babylon_add_windows_manifest(${target}): "
            "no manifest support for ${CMAKE_CXX_COMPILER_ID}")
    endif()
endfunction()
