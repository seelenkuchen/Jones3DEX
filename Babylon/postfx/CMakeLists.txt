# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

babylon_add_module(postfx
    SOURCES src/Resources.cpp src/TargetPool.cpp src/Effect.cpp src/Chain.cpp src/CopyEffect.cpp
        src/VignetteEffect.cpp src/FilmGrainEffect.cpp
    DEPS log
    PUBLIC_DEPS render)

babylon_add_shaders(babylon_postfx
    NAMESPACE babylon::postfx::shaders
    SOURCES shaders/fullscreen.vert.hlsl shaders/copy.frag.hlsl shaders/vignette.frag.hlsl
        shaders/film_grain.frag.hlsl)

babylon_add_module_tests(postfx
    DEPS log render
    SOURCES tests/TargetPoolTest.cpp tests/ChainTest.cpp tests/ChainGpuTest.cpp
        tests/VignetteEffectTest.cpp tests/FilmGrainEffectTest.cpp tests/StockEffectsGpuTest.cpp)

if (TARGET babylon_postfx_tests)
    # The GPU case starts SDL's video subsystem; the offscreen driver needs
    # no display and still offers Vulkan. Without a GPU driver the case is
    # reported as skipped.
    target_link_libraries(babylon_postfx_tests PRIVATE ${BABYLON_SDL3_TARGET})
    set_tests_properties(babylon.postfx PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=$<IF:$<PLATFORM_ID:Darwin>,dummy,offscreen>")
endif()
