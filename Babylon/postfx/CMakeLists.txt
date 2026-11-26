# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

babylon_add_module(postfx
    SOURCES src/Resources.cpp src/TargetPool.cpp src/Effect.cpp src/Chain.cpp src/CopyEffect.cpp
        src/VignetteEffect.cpp src/FilmGrainEffect.cpp src/DepthMath.cpp src/ContactShadowEffect.cpp
        src/ShadowReceiveEffect.cpp
    DEPS log
    PUBLIC_DEPS render)

babylon_add_shaders(babylon_postfx
    NAMESPACE babylon::postfx::shaders
    SOURCES shaders/fullscreen.vert.hlsl shaders/copy.frag.hlsl shaders/vignette.frag.hlsl
        shaders/film_grain.frag.hlsl shaders/contact_shadow_mask.frag.hlsl
        shaders/contact_shadow_blur.frag.hlsl shaders/contact_shadow_composite.frag.hlsl
        shaders/shadow_receive.frag.hlsl)

# The GPU case builds its shadow map with the shadow module; that is a test
# dependency only, so postfx itself still depends on render and log alone.
babylon_add_module_tests(postfx
    DEPS log render shadow
    SOURCES tests/TargetPoolTest.cpp tests/ChainTest.cpp tests/ChainGpuTest.cpp
        tests/VignetteEffectTest.cpp tests/FilmGrainEffectTest.cpp tests/StockEffectsGpuTest.cpp
        tests/DepthMathTest.cpp tests/ContactShadowEffectTest.cpp
        tests/ShadowReceiveEffectTest.cpp)

if (TARGET babylon_postfx_tests)
    # The GPU case starts SDL's video subsystem; the offscreen driver needs
    # no display and still offers Vulkan. Without a GPU driver the case is
    # reported as skipped.
    target_link_libraries(babylon_postfx_tests PRIVATE ${BABYLON_SDL3_TARGET})
    set_tests_properties(babylon.postfx PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=$<IF:$<PLATFORM_ID:Darwin>,dummy,offscreen>")

    # The GPU cases build synthetic depth scenes with a tiny depth-only
    # shader; the shadow-receive case also builds a real map with the shadow
    # module (a test dependency only).
    if (BABYLON_BUILD_SHADERS)
        babylon_add_shaders(babylon_postfx_tests
            NAMESPACE babylon::postfx::test::shaders
            SOURCES tests/shaders/scene_depth.frag.hlsl
                tests/shaders/shadow_floor_depth.frag.hlsl)
        target_sources(babylon_postfx_tests PRIVATE tests/ContactShadowGpuTest.cpp
            tests/ShadowReceiveGpuTest.cpp)
    endif()
endif()
