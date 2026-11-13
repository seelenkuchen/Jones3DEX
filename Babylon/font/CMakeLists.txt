# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

babylon_add_module(font
    SOURCES src/Font.cpp
    DEPS core log
    LIBS stb)
babylon_add_module_tests(font
    DEPS core
    SOURCES tests/FontTest.cpp)

# The test reads the committed Jost font from the source tree, not from the
# build or an installed game.
target_compile_definitions(babylon_font_tests PRIVATE
    "BABYLON_FONT_TEST_FONT=\"${CMAKE_CURRENT_SOURCE_DIR}/../../Data/ui/fonts/Jost[wght].ttf\"")
