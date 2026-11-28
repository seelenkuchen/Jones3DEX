// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/postfx/Chain.h>
#include <babylon/postfx/ContactShadowEffect.h>
#include <babylon/postfx/FrameInputs.h>
#include <babylon/postfx/Resources.h>

#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/SdlGpuDevice.h>
#include <babylon/render/Texture.h>

#include <babylon/log/CaptureSink.h>

#include <babylon/shaders/babylon_postfx_tests.h>

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace babylon::postfx::testsupport
{
    using namespace babylon::postfx;
    using namespace babylon::render;

    inline constexpr std::uint32_t kSize = 64;
    inline constexpr float kHalfFovX = 1.245f;
    inline constexpr float kHalfFovY = 20.0f;
    inline constexpr float kNear = 0.01f;
    inline constexpr float kFar = 100.0f;
    inline constexpr float kDepthNear = 0.1f;
    inline constexpr float kDepthFar = 1.0f;
    inline constexpr float kFloorY = -1.0f;
    inline constexpr float kBoxMinX = 0.05f;
    inline constexpr float kBoxMaxX = 0.07f;
    inline constexpr float kBoxTopY = -0.3f;

    // The box front sits in front of the floor by less than the default
    // thickness over a band of rows, so the floor to its left is a wide contact
    // shadow. The test row runs through the middle of that band.
    inline constexpr std::uint32_t kFloorRow = 50;
    inline constexpr std::uint32_t kShadowedColumn = 20;   // left of the box
    inline constexpr std::uint32_t kFarFloorColumn = 5;
    inline constexpr std::uint32_t kBoxTopColumn = 54;
    inline constexpr std::uint32_t kBoxTopRow = 38;

    // An 8x8 block well inside the shadow, for the smoothness check.
    inline constexpr std::uint32_t kSmoothColumn = 20;
    inline constexpr std::uint32_t kSmoothRow = 47;
    inline constexpr std::uint32_t kSmoothSize = 8;

    inline std::string joined(const babylon::log::CaptureSink& sink)
    {
        std::string text;
        for ( const auto& record : sink.records() )
        {
            text += text.empty() ? "" : "; ";
            text += record.message;
        }
        return text;
    }

    inline std::array<int, 4> pixelAt(const std::vector<std::byte>& pixels, std::uint32_t width,
        std::uint32_t x, std::uint32_t y)
    {
        const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4u;
        return { static_cast<int>(pixels[index + 0u]), static_cast<int>(pixels[index + 1u]),
            static_cast<int>(pixels[index + 2u]), static_cast<int>(pixels[index + 3u]) };
    }

    // An Rgba16F texture of the given half values; they are exact for 0.5 and 1.
    inline TextureHandle makeHalfColor(Device& device, std::uint32_t width, std::uint32_t height)
    {
        std::vector<std::byte> texels(static_cast<std::size_t>(width) * height * 8u);
        for ( std::size_t index = 0; index < texels.size(); index += 8u )
        {
            // red, green and blue 0.5 (0x3800); alpha 1 (0x3C00)
            texels[index + 0u] = std::byte{ 0x00 };
            texels[index + 1u] = std::byte{ 0x38 };
            texels[index + 2u] = std::byte{ 0x00 };
            texels[index + 3u] = std::byte{ 0x38 };
            texels[index + 4u] = std::byte{ 0x00 };
            texels[index + 5u] = std::byte{ 0x38 };
            texels[index + 6u] = std::byte{ 0x00 };
            texels[index + 7u] = std::byte{ 0x3C };
        }

        const TextureHandle texture = device.createTexture(TextureDesc{ width, height, 1,
            TextureFormat::Rgba16F, TextureUsage::Sampled });
        REQUIRE_FALSE(texture.isNull());
        REQUIRE(device.uploadTexture(texture, 0, texels));
        return texture;
    }

    // An Rgba8 normals texture whose alpha is 1, which is receiver class 3;
    // the default receiverClasses (0x8) includes it.
    inline TextureHandle makeNormals(Device& device, std::uint32_t width, std::uint32_t height)
    {
        std::vector<std::byte> texels(static_cast<std::size_t>(width) * height * 4u);
        for ( std::size_t index = 0; index < texels.size(); index += 4u )
        {
            texels[index + 0u] = std::byte{ 128 };
            texels[index + 1u] = std::byte{ 128 };
            texels[index + 2u] = std::byte{ 255 };
            texels[index + 3u] = std::byte{ 255 };
        }

        const TextureHandle texture = device.createTexture(TextureDesc{ width, height, 1,
            TextureFormat::Rgba8, TextureUsage::Sampled });
        REQUIRE_FALSE(texture.isNull());
        REQUIRE(device.uploadTexture(texture, 0, texels));
        return texture;
    }

    // An Rgba8 normals texture that tags the left half of the screen class 3
    // (an ordinary receiver) and the right half class 2 (sky in the receiver
    // mask), so the tagged half is adjacent to the shadowed ordinary floor.
    inline TextureHandle makeMixedNormals(Device& device, std::uint32_t width, std::uint32_t height)
    {
        std::vector<std::byte> texels(static_cast<std::size_t>(width) * height * 4u);
        for ( std::uint32_t y = 0; y < height; ++y )
        {
            for ( std::uint32_t x = 0; x < width; ++x )
            {
                const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4u;
                const bool receiver     = x < width / 2u;
                texels[index + 0u] = std::byte{ 128 };
                texels[index + 1u] = std::byte{ 128 };
                texels[index + 2u] = std::byte{ 255 };
                texels[index + 3u] = receiver ? std::byte{ 255 } : std::byte{ 170 };
            }
        }

        const TextureHandle texture = device.createTexture(TextureDesc{ width, height, 1,
            TextureFormat::Rgba8, TextureUsage::Sampled });
        REQUIRE_FALSE(texture.isNull());
        REQUIRE(device.uploadTexture(texture, 0, texels));
        return texture;
    }

    // Matches the cbuffer in tests/shaders/scene_depth.frag.hlsl.
    struct alignas(16) SceneUniforms
    {
        float nearPlane;
        float farPlane;
        float depthNear;
        float depthFar;
        float tanHalfFovX;
        float tanHalfFovY;
        float floorY;
        float boxMinX;
        float boxMaxX;
        float boxTopY;
        float boxFrontZ;
        float boxBackZ;
        float padding[4];
    };
    static_assert(sizeof(SceneUniforms) == 64);

    // The box front sits in front of the floor depth at kFloorRow by less than
    // the default thickness, so a floor pixel left of the base has the box
    // front close enough in depth to count as a blocker rather than a gap.
    inline float floorDepthAtRow(std::uint32_t row)
    {
        const float v = (static_cast<float>(row) + 0.5f) / static_cast<float>(kSize);
        return kFloorY / ((1.0f - 2.0f * v) * kHalfFovY);
    }

    inline float boxFrontDepth()
    {
        return floorDepthAtRow(kFloorRow) - 0.02f;
    }

    // Mirrors tests/shaders/scene_depth.frag.hlsl's box intersection: true when
    // the box, not the floor or the sky, is the nearest surface at the pixel.
    inline bool isBoxPixel(std::uint32_t x, std::uint32_t y)
    {
        const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(kSize);
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(kSize);
        const float dx = (u * 2.0f - 1.0f) * kHalfFovX;
        const float dy = (1.0f - v * 2.0f) * kHalfFovY;

        float floorZ = kFar;
        if ( dy < -1.0e-6f && kFloorY / dy > kNear )
        {
            floorZ = kFloorY / dy;
        }

        // The ray starts at the origin, so a slab from low to high along one
        // axis narrows [enter, exit] the same way the test shader does.
        float enter  = 0.0f;
        float exitT  = kFar;
        bool hit     = true;
        const auto intersectSlab = [&enter, &exitT, &hit](
            float direction, float low, float high)
        {
            if ( std::abs(direction) < 1.0e-8f )
            {
                if ( low > 0.0f || high < 0.0f )
                {
                    hit = false;
                }
                return;
            }

            float a = low / direction;
            float b = high / direction;
            if ( a > b )
            {
                std::swap(a, b);
            }
            enter = std::max(enter, a);
            exitT = std::min(exitT, b);
            if ( enter > exitT )
            {
                hit = false;
            }
        };
        intersectSlab(dx, kBoxMinX, kBoxMaxX);
        intersectSlab(dy, kFloorY, kBoxTopY);
        intersectSlab(1.0f, boxFrontDepth(), boxFrontDepth() + 0.3f);
        return hit && enter > kNear && enter < floorZ;
    }

    // An Rgba8 ignore mask with red 255 where pSelected is true and 0 elsewhere.
    template <typename Predicate>
    inline TextureHandle makeMask(Device& device, std::uint32_t width, std::uint32_t height,
        Predicate pSelected)
    {
        std::vector<std::byte> texels(static_cast<std::size_t>(width) * height * 4u);
        for ( std::uint32_t y = 0; y < height; ++y )
        {
            for ( std::uint32_t x = 0; x < width; ++x )
            {
                const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4u;
                texels[index + 0u] = pSelected(x, y) ? std::byte{ 255 } : std::byte{ 0 };
                texels[index + 1u] = std::byte{ 0 };
                texels[index + 2u] = std::byte{ 0 };
                texels[index + 3u] = std::byte{ 255 };
            }
        }

        const TextureHandle texture = device.createTexture(TextureDesc{ width, height, 1,
            TextureFormat::Rgba8, TextureUsage::Sampled });
        REQUIRE_FALSE(texture.isNull());
        REQUIRE(device.uploadTexture(texture, 0, texels));
        return texture;
    }

    inline TextureHandle makeOccluderMask(Device& device, std::uint32_t width, std::uint32_t height)
    {
        return makeMask(device, width, height,
            [](std::uint32_t x, std::uint32_t y) { return isBoxPixel(x, y); });
    }

    inline TextureHandle makeRectMask(Device& device, std::uint32_t width, std::uint32_t height,
        std::uint32_t minX, std::uint32_t maxX, std::uint32_t minY, std::uint32_t maxY)
    {
        return makeMask(device, width, height,
            [minX, maxX, minY, maxY](std::uint32_t x, std::uint32_t y)
            {
                return x >= minX && x < maxX && y >= minY && y < maxY;
            });
    }

    // Renders the synthetic scene's depth into depth with a depth-only
    // full-screen pass. The scene is an infinite floor plane at y = kFloorY
    // and, when bBox is true, an axis-aligned box standing on it, x in
    // [kBoxMinX, kBoxMaxX], y in [kFloorY, kBoxTopY], z in [front, front + 0.3].
    // The camera is at the origin looking along +z. The top-left 8x8 pixels get
    // a depth in front of the near plane (an overlay) and the sky gets the
    // clear depth.
    inline void renderDepthScene(Device& device, TextureHandle depth, TextureFormat depthFormat,
        bool bBox = true)
    {
        const float front = boxFrontDepth();
        // A box far off screen and outside the far plane never intersects a
        // ray, so the scene is the open floor alone.
        const float boxMinX = bBox ? kBoxMinX : 1000.0f;
        const float boxMaxX = bBox ? kBoxMaxX : 1001.0f;
        const float boxTopY = bBox ? kBoxTopY : kFloorY;
        const float boxFront = bBox ? front : 1000.0f;
        const SceneUniforms uniforms{
            kNear, kFar, kDepthNear, kDepthFar, kHalfFovX, kHalfFovY, kFloorY,
            boxMinX, boxMaxX, boxTopY, boxFront, boxFront + 0.3f,
            { 0.0f, 0.0f, 0.0f, 0.0f } };

        PipelineDesc desc;
        desc.vertexShader   = Resources::fullscreenVertexShader();
        desc.fragmentShader = babylon::postfx::test::shaders::sceneDepthFrag();
        desc.primitive      = PrimitiveType::TriangleList;
        desc.depthFormat    = depthFormat;
        desc.depth          = DepthState{ true, true, CompareOp::Always };

        const PipelineHandle pipeline = device.createPipeline(desc);
        REQUIRE_FALSE(pipeline.isNull());

        CommandList list = device.beginFrame();
        PassDesc pass;
        pass.depthTarget = DepthAttachment{ depth, LoadOp::Clear, StoreOp::Store, 1.0f, 0, 0 };
        REQUIRE(list.beginPass(pass));
        list.setPipeline(pipeline);
        list.pushUniforms(ShaderStage::Fragment, 0, std::as_bytes(std::span(&uniforms, 1)));
        list.draw(3);
        REQUIRE(list.endPass());
        REQUIRE(device.submit(list));
        device.destroyPipeline(pipeline);
    }

    // Runs the chain with an optional effect and returns the output pixels and
    // (when asked) the effect names that ran.
    inline std::vector<std::byte> runScene(Device& device, Effect* pEffect, TextureHandle color,
        TextureHandle depth, TextureHandle normals, const Projection& projection,
        std::vector<std::string_view>* pRan)
    {
        const TextureHandle output = device.createTexture(TextureDesc{ kSize, kSize, 1,
            TextureFormat::Rgba8, TextureUsage::ColorTarget });
        REQUIRE_FALSE(output.isNull());

        Chain chain(device);
        REQUIRE(chain.open());
        if ( pEffect != nullptr )
        {
            REQUIRE(chain.add(*pEffect));
        }

        FrameInputs frame;
        frame.color       = color;
        frame.colorFormat = TextureFormat::Rgba16F;
        frame.depth       = depth;
        frame.normals     = normals;
        frame.width       = kSize;
        frame.height      = kSize;
        frame.projection  = projection;

        OutputTarget target;
        target.texture = output;
        target.format  = TextureFormat::Rgba8;
        target.width   = kSize;
        target.height  = kSize;

        CommandList list = device.beginFrame();
        REQUIRE(chain.record(list, frame, target));
        REQUIRE(device.submit(list));

        if ( pRan != nullptr )
        {
            *pRan = chain.lastRun();
        }

        std::vector<std::byte> pixels = device.readTexture(output, 0);
        chain.close();
        device.destroyTexture(output);
        return pixels;
    }

    inline void checkUnchanged(const std::array<int, 4>& pixel)
    {
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            CHECK(std::abs(pixel[channel] - 128) <= 1);
        }
        CHECK(pixel[3] == 255);
    }

}
