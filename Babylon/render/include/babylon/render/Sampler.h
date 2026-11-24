// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/render/CompareOp.h>

#include <cstdint>
#include <optional>

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

        // The maximum anisotropy of minified samples, 1 to 16: 1 turns
        // anisotropic filtering off, higher values trade cost for sharpness on
        // surfaces seen at a slant. Backends clamp it to the device's own
        // limit and treat a non-finite value as 1. It has no effect on
        // magnification.
        float maxAnisotropy = 1.0f;

        // When set, the sampler is a comparison sampler: reading a depth
        // texture compares the texel with the reference the shader passes
        // (HLSL SampleCmp and SampleCmpLevelZero) through compare and returns
        // the filtered 0..1 result instead of the texel, so Linear filters
        // give hardware PCF. Empty samples normally. It is only meaningful on
        // depth textures; other textures read normally.
        std::optional<CompareOp> compare = std::nullopt;

        bool operator==(const SamplerDesc&) const = default;
    };

}
