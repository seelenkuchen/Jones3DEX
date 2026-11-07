// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>

namespace babylon::render
{

    // A typed reference to one resource a Device owns: a slot index and the
    // generation of that slot when the resource was created. Destroying the
    // resource bumps the slot's generation, so every copy of the old handle
    // becomes stale and the device detects it instead of touching a reused
    // slot. Generation 0 is never handed out: a default-constructed handle is
    // the null handle and names nothing. Handles are plain values; they own
    // nothing and say nothing about the backend object behind them.
    template <class Tag>
    struct Handle
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;

        // True only for the null handle; false for every handle a device
        // handed out, live or stale.
        constexpr bool isNull() const { return generation == 0; }

        bool operator==(const Handle&) const = default;
    };

    struct TextureTag;
    struct BufferTag;
    struct SamplerTag;
    struct PipelineTag;

    using TextureHandle = Handle<TextureTag>;
    using BufferHandle = Handle<BufferTag>;
    using SamplerHandle = Handle<SamplerTag>;
    using PipelineHandle = Handle<PipelineTag>;

}
