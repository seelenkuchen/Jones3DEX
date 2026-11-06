// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>

namespace babylon::render
{

    // What a buffer may be used for, combined with |; at least one flag.
    // Uniform data does not live in buffers: it is pushed per draw with
    // CommandList::pushUniforms().
    enum class BufferUsage : std::uint8_t
    {
        None = 0,
        Vertex = 1 << 0,    // bound with CommandList::bindVertexBuffer()
        Index = 1 << 1,     // bound with CommandList::bindIndexBuffer()
        Storage = 1 << 2,   // read by shaders, such as a light list
    };

    constexpr BufferUsage operator|(BufferUsage a, BufferUsage b)
    {
        return static_cast<BufferUsage>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
    }

    // True when usage contains every flag in flags.
    constexpr bool hasUsage(BufferUsage usage, BufferUsage flags)
    {
        return (static_cast<std::uint8_t>(usage) & static_cast<std::uint8_t>(flags))
            == static_cast<std::uint8_t>(flags);
    }

    // One GPU buffer of size bytes. Device::createBuffer() refuses a size of
    // 0 or above DeviceInfo::limits.maxBufferSize and a usage of None.
    struct BufferDesc
    {
        std::uint32_t size = 0;
        BufferUsage usage = BufferUsage::Vertex;

        bool operator==(const BufferDesc&) const = default;
    };

}
