// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/render/Handle.h>
#include <babylon/shadow/ShadowMath.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace babylon::render
{
    class Device;
}

namespace babylon::shadow
{

    // One triangle list of static casters (three float3 world-space positions
    // per triangle, no indices) kept in its own GPU vertex buffer. Upload it
    // once per level with upload(); DirectionalShadowMap::record() then draws
    // it into the shadow map without uploading it again, transformed by the
    // batch's toView.
    //
    // Borrows the device, which must outlive it and must already be open.
    // Not thread-safe.
    class StaticCasterSet
    {
    public:
        explicit StaticCasterSet(render::Device& device);
        ~StaticCasterSet();                  // calls clear()
        StaticCasterSet(const StaticCasterSet&) = delete;
        StaticCasterSet& operator=(const StaticCasterSet&) = delete;

        // Replaces the set with positions: three float3 world-space positions
        // per triangle, so positions.size() must be a multiple of 9. An empty
        // span clears the set. False and one warning when the size isn't a
        // multiple of 9 or the buffer can't be made or filled; a bad size
        // leaves the previous contents, while a buffer failure leaves the set
        // empty.
        [[nodiscard]] bool upload(std::span<const float> positions);

        // Releases the buffer and empties the set; a safe no-op when empty.
        void clear();

        std::size_t numVertices() const;
        render::BufferHandle buffer() const; // null when empty
        Bounds bounds() const;               // all zero when empty

    private:
        render::Device& m_device;
        render::BufferHandle m_buffer;
        std::uint64_t m_capacityBytes = 0;
        std::size_t m_numVertices = 0;
        Bounds m_bounds{};
    };

}
