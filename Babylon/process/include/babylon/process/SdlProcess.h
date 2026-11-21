// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/process/Process.h>

#include <memory>

namespace babylon::process
{

    // A Process on SDL3's process API (SDL_CreateProcessWithProperties,
    // SDL_GetProcessOutput, SDL_KillProcess and SDL_WaitProcess). Needs no
    // SDL_Init and no window. The returned process is owned by the caller.
    std::unique_ptr<Process> createSdlProcess();

}
