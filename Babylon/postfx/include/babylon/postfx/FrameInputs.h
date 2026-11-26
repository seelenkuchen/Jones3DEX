// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/render/Handle.h>
#include <babylon/render/Texture.h>

#include <cstdint>

namespace babylon::postfx
{

    // The camera's projection, for effects that turn depth into distance. Depth texels are the
    // standard 0 (near) to 1 (far) perspective depth of a projection with these planes.
    struct Projection
    {
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        float tanHalfFovX = 1.0f;   // tan of half the horizontal field of view
        float tanHalfFovY = 0.75f;  // tan of half the vertical field of view

        // The depth texel at the near and at the far plane; depth is affine in 1/z between them
        // (true of every perspective depth buffer). Standard: 0 and 1. A texel nearer than
        // depthNear (or farther than depthFar) is no scene surface (for example a first-person
        // overlay drawn in front) and effects that reconstruct positions leave it unchanged.
        float depthNear = 0.0f;
        float depthFar = 1.0f;
    };

    // What one frame gives the chain. color is the rendered scene, sampled and never written by
    // the chain. depth and normals are optional (null handles); an effect that needs one is
    // skipped when it is missing. Time comes from the game's clock, never the wall clock, so a
    // replayed frame looks the same.
    struct FrameInputs
    {
        render::TextureHandle color;
        render::TextureFormat colorFormat = render::TextureFormat::Rgba16F;
        render::TextureHandle depth;
        render::TextureHandle normals;
        std::uint32_t width = 0;    // size of color, depth and normals in pixels
        std::uint32_t height = 0;
        Projection projection;
        double timeSeconds = 0.0;   // game time of this frame
        float deltaSeconds = 0.0f;  // game time since the previous frame, 0 on the first
        std::uint64_t frameIndex = 0;
    };

    // Where the chain's result goes. A null texture names the device's swapchain; format is then
    // ignored and the device's swapchain format is used. width and height are the output size,
    // which may differ from the scene size (render scale): the Resolve effect scales.
    struct OutputTarget
    {
        render::TextureHandle texture;
        render::TextureFormat format = render::TextureFormat::Bgra8;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

}
