// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <SDL3/SDL_loadso.h>

namespace babylon::render::test
{

    // Mesa's lavapipe allocates a little memory under pthread_once while
    // listing its devices and never frees it, so LeakSanitizer reports it
    // each time the Vulkan loader unloads the driver, which SDL GPU makes
    // happen on every device creation and destruction. Loading the driver
    // library once more and never unloading it keeps that memory reachable.
    // Tests only; a no-op where lavapipe isn't installed. Call before
    // creating SDL GPU devices.
    inline void keepGpuDriverLoaded()
    {
        static SDL_SharedObject* const lavapipe = SDL_LoadObject("libvulkan_lvp.so");
        (void)lavapipe;
    }

}
