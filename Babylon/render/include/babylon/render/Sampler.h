// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>

namespace babylon::render
{

    enum class Filter : std::uint8_t
    {
        Nearest,
        Linear,
    };

    enum class AddressMode : std::uint8_t
    {
        Repeat,
        ClampToEdge,
        MirroredRepeat,
    };

    // How a shader reads a bound texture. Every combination is valid on every
    // device, so Device::createSampler() refuses nothing but an exhausted
    // device.
    struct SamplerDesc
    {
        Filter minFilter = Filter::Linear;
        Filter magFilter = Filter::Linear;
        Filter mipFilter = Filter::Nearest;
        AddressMode addressU = AddressMode::Repeat;
        AddressMode addressV = AddressMode::Repeat;

        bool operator==(const SamplerDesc&) const = default;
    };

}
