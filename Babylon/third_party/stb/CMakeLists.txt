# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Vendored single-header libraries from stb (MIT or public domain, the MIT
# terms are chosen; see LICENSE and ADR-0031). The implementation is compiled
# once in stb_impl.c. Callers include the declarations from this directory
# only; STB_IMAGE_IMPLEMENTATION and STB_TRUETYPE_IMPLEMENTATION stay private
# to the implementation file.
#
#   STBI_ONLY_PNG          the image module decodes PNG only.
#   STBI_NO_STDIO          the modules take bytes, never file names.
#
# The include directory is SYSTEM so the vendored headers do not get Babylon's
# warning flags, and stb's own warnings are silenced for the implementation
# file only. It is never added through _babylon_target_settings, so it also
# carries no -Werror or sanitizer flags.

add_library(stb STATIC stb_impl.c)

target_include_directories(stb SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(stb PRIVATE
    STB_TRUETYPE_IMPLEMENTATION
    STB_IMAGE_IMPLEMENTATION
    STBI_ONLY_PNG
    STBI_NO_STDIO)

if (MSVC)
    target_compile_options(stb PRIVATE /W0)
elseif (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(stb PRIVATE -Wno-everything)
endif()

set_target_properties(stb PROPERTIES POSITION_INDEPENDENT_CODE ON FOLDER Babylon)
