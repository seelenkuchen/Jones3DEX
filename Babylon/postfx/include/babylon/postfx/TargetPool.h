// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/render/Handle.h>
#include <babylon/render/Texture.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace babylon::render
{
    class Device;
}

namespace babylon::postfx
{

    // Scratch color targets for effects, sampled and renderable. acquire() hands out a texture of
    // the given size and format that no one else holds this frame; endFrame() returns every target
    // to the pool. A target not acquired during a whole frame is destroyed at the next endFrame(),
    // so a size change frees the old targets one frame later. Textures persist across frames, so an
    // effect may NOT assume a target's contents (another effect may have drawn into it).
    class TargetPool
    {
    public:
        explicit TargetPool(render::Device& device);
        ~TargetPool();                    // calls releaseAll()
        TargetPool(const TargetPool&) = delete;
        TargetPool& operator=(const TargetPool&) = delete;

        // A null handle, with one warning, for a zero size, a depth format, a format the device
        // can't sample and render, or a failed createTexture().
        [[nodiscard]] render::TextureHandle acquire(std::uint32_t width, std::uint32_t height,
            render::TextureFormat format);
        void endFrame();
        void releaseAll();                // destroys every texture; the pool stays usable
        std::size_t textureCount() const; // textures alive, held or not (for tests)

    private:
        struct Entry
        {
            render::TextureDesc desc;
            render::TextureHandle texture;
            bool held = false;
        };

        render::Device& m_device;
        std::vector<Entry> m_entries;
    };

}
