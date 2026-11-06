// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <doctest.h>

#include <cstdlib>
#include <cstring>
#include <string>

namespace babylon::render::test
{

    // True when BABYLON_REQUIRE_GPU is set to exactly "1". CI sets it so a
    // test that finds no GPU device fails instead of silently skipping.
    inline bool gpuRequired()
    {
        const char* const value = std::getenv("BABYLON_REQUIRE_GPU");
        return value != nullptr && std::strcmp(value, "1") == 0;
    }

    // A test found no GPU device, with reason saying what was missing. When
    // BABYLON_REQUIRE_GPU=1 that is a failure with the same reason; otherwise
    // the case is reported as skipped and continues.
    inline void noGpuDevice(const std::string& reason)
    {
        if ( gpuRequired() )
        {
            FAIL(reason);
        }
        MESSAGE("skipped, " << reason);
    }

}
