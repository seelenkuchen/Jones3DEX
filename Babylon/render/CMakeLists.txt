# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

babylon_add_module(render
    SOURCES src/CommandList.cpp src/Device.cpp src/NullDevice.cpp
        backends/sdl/SdlGpuDevice.cpp
    DEPS log platform
    LIBS ${BABYLON_SDL3_TARGET})
babylon_add_module_tests(render
    DEPS log platform
    SOURCES tests/RenderContractTest.cpp tests/NullDeviceTest.cpp
        backends/sdl/tests/SdlGpuDeviceTest.cpp)
if (TARGET babylon_render_tests)
    # SDL GPU starts SDL's video subsystem; the offscreen driver needs no
    # display and still offers Vulkan surfaces (VK_EXT_headless_surface), so
    # the windowed path runs too. Without a GPU driver the SDL cases are
    # reported as skipped. macOS has no offscreen driver (see platform).
    set_tests_properties(babylon.render PROPERTIES
        ENVIRONMENT "SDL_VIDEO_DRIVER=$<IF:$<PLATFORM_ID:Darwin>,dummy,offscreen>")
    # KeepGpuDriverLoaded.h calls SDL directly.
    target_link_libraries(babylon_render_tests PRIVATE ${BABYLON_SDL3_TARGET})
endif()
