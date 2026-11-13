# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

babylon_add_module(image
    SOURCES src/Image.cpp
    DEPS core
    LIBS stb)
babylon_add_module_tests(image
    DEPS core
    SOURCES tests/ImageTest.cpp)

# The test reads the committed Xelu prompt from the source tree, not from the
# build or an installed game.
target_compile_definitions(babylon_image_tests PRIVATE
    "BABYLON_IMAGE_TEST_PNG=\"${CMAKE_CURRENT_SOURCE_DIR}/../../Data/ui/prompts/xbox/XboxSeriesX_A.png\"")
