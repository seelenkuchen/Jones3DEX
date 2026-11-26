# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

babylon_add_module(shadow
    SOURCES src/ShadowMath.cpp src/DirectionalShadowMap.cpp
    DEPS log
    PUBLIC_DEPS render)

babylon_add_shaders(babylon_shadow
    NAMESPACE babylon::shadow::shaders
    SOURCES shaders/shadow_caster.vert.hlsl shaders/shadow_caster.frag.hlsl)

babylon_add_module_tests(shadow
    DEPS log render
    SOURCES tests/ShadowMathTest.cpp tests/DirectionalShadowMapTest.cpp)

if (TARGET babylon_shadow_tests)
    # The GPU case starts SDL's video subsystem; the offscreen driver needs
    # no display and still offers Vulkan. Without a GPU driver the case is
    # reported as skipped.
    target_link_libraries(babylon_shadow_tests PRIVATE ${BABYLON_SDL3_TARGET})
    set_tests_properties(babylon.shadow PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=$<IF:$<PLATFORM_ID:Darwin>,dummy,offscreen>")

    # The GPU case checks the map through a tiny receive pass with its own
    # full-screen shaders.
    if (BABYLON_BUILD_SHADERS)
        babylon_add_shaders(babylon_shadow_tests
            NAMESPACE babylon::shadow::test::shaders
            SOURCES tests/shaders/shadow_receive.vert.hlsl
                tests/shaders/shadow_receive.frag.hlsl)
        target_sources(babylon_shadow_tests PRIVATE tests/DirectionalShadowMapGpuTest.cpp)
    endif()
endif()
