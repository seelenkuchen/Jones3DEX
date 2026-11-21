# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

babylon_add_module(uirender
    SOURCES src/RenderPainter.cpp
    DEPS log
    PUBLIC_DEPS ui render font image)

babylon_add_shaders(babylon_uirender
    NAMESPACE babylon::uirender::shaders
    SOURCES shaders/ui_text.vert.hlsl shaders/ui_text.frag.hlsl)

babylon_add_module_tests(uirender
    DEPS log ui render font image
    SOURCES tests/RenderPainterTest.cpp)

if (TARGET babylon_uirender_tests)
    # The GPU case starts SDL's video subsystem; the offscreen driver needs
    # no display and still offers Vulkan. Without a GPU driver the case is
    # reported as skipped.
    target_link_libraries(babylon_uirender_tests PRIVATE ${BABYLON_SDL3_TARGET})
    set_tests_properties(babylon.uirender PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=$<IF:$<PLATFORM_ID:Darwin>,dummy,offscreen>")

    # The test reads the committed Jost font from the source tree, not from
    # the build or an installed game.
    target_compile_definitions(babylon_uirender_tests PRIVATE
        "BABYLON_UIRENDER_TEST_FONT=\"${CMAKE_CURRENT_SOURCE_DIR}/../../Data/ui/fonts/Jost[wght].ttf\"")
endif()
