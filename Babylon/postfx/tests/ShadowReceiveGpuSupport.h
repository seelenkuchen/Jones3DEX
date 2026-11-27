// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/postfx/Chain.h>
#include <babylon/postfx/FrameInputs.h>
#include <babylon/postfx/Resources.h>
#include <babylon/postfx/ShadowReceiveEffect.h>

#include <babylon/shadow/DirectionalShadowMap.h>

#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Texture.h>

#include <babylon/log/CaptureSink.h>

#include <babylon/shaders/babylon_postfx_tests.h>

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace babylon::postfx::testsupport
{
    using namespace babylon::postfx;
    using namespace babylon::render;

    inline constexpr std::uint32_t kSize = 64;
    inline constexpr float kHalfFovX = 1.0f;
    inline constexpr float kHalfFovY = 1.0f;
    inline constexpr float kNear = 0.1f;
    inline constexpr float kFar = 100.0f;
    inline constexpr float kDepthNear = 0.0f;
    inline constexpr float kDepthFar = 1.0f;
    inline constexpr float kFloorY = -1.0f;

    // The caster is a horizontal quad above the floor. The light looks down
    // along view y, so the floor directly below it is shadowed.
    inline constexpr float kCasterY = -0.4f;
    inline constexpr float kCasterMinX = -0.2f;
    inline constexpr float kCasterMaxX = 0.2f;
    inline constexpr float kCasterMinZ = 2.36f;
    inline constexpr float kCasterMaxZ = 2.76f;
    inline constexpr float kMapMargin = 0.7f;    // large enough to put the floor inside the depth range
    inline constexpr std::uint32_t kMapSize = 256;
    inline constexpr float kLight[3] = { 0.0f, 1.0f, 0.0f };

    // A small margin fits the map's depth range to the caster only: the floor
    // 0.6 view units below it is past the far end (depth > 1) and must still be
    // compared against the clamped reference.
    inline constexpr float kFarBelowMargin = 0.3f;

    // Row 44 puts the floor at z ~2.56, inside the caster's z range. Column 32
    // is under the caster; column 38 is the same distance from the camera but
    // clear of it and still inside the wide map.
    inline constexpr std::uint32_t kFloorRow = 44;
    inline constexpr std::uint32_t kShadowedColumn = 32;
    inline constexpr std::uint32_t kLitColumn = 38;
    // Inside the narrow map's x footprint but clear of the caster (x ~0.36).
    inline constexpr std::uint32_t kFarBelowLitColumn = 36;

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

    // An Rgba16F colour texture whose left half has leftRgb and right half
    // rightRgb, with the same half value in red, green and blue and alpha 1.
    // 0x3400 is 0.25 and 0x3800 is 0.5.
    inline TextureHandle makeSplitHalfColor(Device& device, std::uint32_t width,
        std::uint32_t height, std::uint16_t leftRgb, std::uint16_t rightRgb)
    {
        std::vector<std::byte> texels(static_cast<std::size_t>(width) * height * 8u);
        for ( std::uint32_t y = 0; y < height; ++y )
        {
            for ( std::uint32_t x = 0; x < width; ++x )
            {
                const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 8u;
                const std::uint16_t rgb = x < width / 2u ? leftRgb : rightRgb;
                for ( std::size_t channel = 0; channel < 3; ++channel )
                {
                    texels[index + channel * 2u + 0u]
                        = static_cast<std::byte>(rgb & 0xFFu);
                    texels[index + channel * 2u + 1u]
                        = static_cast<std::byte>(rgb >> 8u);
                }
                texels[index + 6u] = std::byte{ 0x00 };
                texels[index + 7u] = std::byte{ 0x3C };
            }
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
    // (an ordinary receiver) and the right half class 2 (not in the receiver
    // mask), so non-receivers can be checked byte for byte.
    inline TextureHandle makeMixedNormals(Device& device, std::uint32_t width,
        std::uint32_t height)
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

    // An Rgba8 mask that is red on the left half of the screen and black on
    // the right, so the left half is excluded from receiving shadows.
    inline TextureHandle makeExcludeMask(Device& device, std::uint32_t width,
        std::uint32_t height)
    {
        std::vector<std::byte> texels(static_cast<std::size_t>(width) * height * 4u);
        for ( std::uint32_t y = 0; y < height; ++y )
        {
            for ( std::uint32_t x = 0; x < width; ++x )
            {
                const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4u;
                const bool masked       = x < width / 2u;
                texels[index + 0u] = masked ? std::byte{ 255 } : std::byte{ 0 };
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

    // Matches the cbuffer in tests/shaders/shadow_floor_depth.frag.hlsl.
    struct alignas(16) FloorUniforms
    {
        float nearPlane;
        float farPlane;
        float depthNear;
        float depthFar;
        float tanHalfFovX;
        float tanHalfFovY;
        float floorY;
        float padding;
    };
    static_assert(sizeof(FloorUniforms) == 32);

    // Renders the view-space floor plane into depth with a depth-only
    // full-screen pass. The camera is at the origin looking along +z; rows
    // above the horizon get the clear depth.
    inline void renderFloorDepth(Device& device, TextureHandle depth, TextureFormat depthFormat)
    {
        const FloorUniforms uniforms{
            kNear, kFar, kDepthNear, kDepthFar, kHalfFovX, kHalfFovY, kFloorY, 0.0f };

        PipelineDesc desc;
        desc.vertexShader   = Resources::fullscreenVertexShader();
        desc.fragmentShader = babylon::postfx::test::shaders::shadowFloorDepthFrag();
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

    // The horizontal caster quad as two triangles.
    inline std::vector<float> casterVertices()
    {
        const std::array<std::array<float, 3>, 4> corners = { {
            { kCasterMinX, kCasterY, kCasterMinZ },
            { kCasterMaxX, kCasterY, kCasterMinZ },
            { kCasterMaxX, kCasterY, kCasterMaxZ },
            { kCasterMinX, kCasterY, kCasterMaxZ },
        } };
        constexpr std::array<std::array<int, 3>, 2> triangles = { {
            { 0, 1, 2 },
            { 0, 2, 3 },
        } };

        std::vector<float> vertices;
        vertices.reserve(triangles.size() * 3 * 3);
        for ( const std::array<int, 3>& triangle : triangles )
        {
            for ( const int corner : triangle )
            {
                vertices.push_back(corners[static_cast<std::size_t>(corner)][0]);
                vertices.push_back(corners[static_cast<std::size_t>(corner)][1]);
                vertices.push_back(corners[static_cast<std::size_t>(corner)][2]);
            }
        }
        return vertices;
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

    inline Projection casterFloorProjection()
    {
        Projection projection;
        projection.nearPlane   = kNear;
        projection.farPlane    = kFar;
        projection.tanHalfFovX = kHalfFovX;
        projection.tanHalfFovY = kHalfFovY;
        projection.depthNear   = kDepthNear;
        projection.depthFar    = kDepthFar;
        return projection;
    }

    // Opens a map fitted around the caster with the given margin, renders the
    // floor depth, and fills the shadow-map input. The map borrows the device;
    // the caller keeps it alive while the effect samples it. False when the
    // device cannot make the map or a sampled depth texture.
    inline bool buildCasterFloorScene(Device& device,
        babylon::shadow::DirectionalShadowMap& map, float margin, ShadowMapInput& mapInput,
        TextureHandle& depth)
    {
        babylon::shadow::ShadowMapDesc mapDesc;
        mapDesc.size = kMapSize;
        if ( !map.open(mapDesc) )
        {
            return false;
        }

        const std::vector<float> vertices = casterVertices();
        const babylon::shadow::CasterBatch batch{ vertices.data(), vertices.size() / 3 };
        const std::array<babylon::shadow::CasterBatch, 1> batches{ batch };
        CommandList shadowList = device.beginFrame();
        if ( !map.record(shadowList, kLight,
                std::span<const babylon::shadow::CasterBatch>(batches), margin) )
        {
            return false;
        }
        if ( !device.submit(shadowList) || !map.isValid() )
        {
            return false;
        }

        const TextureUsage depthUsage = TextureUsage::DepthTarget | TextureUsage::Sampled;
        TextureFormat depthFormat = TextureFormat::D32F;
        if ( !device.supportsFormat(depthFormat, depthUsage) )
        {
            depthFormat = TextureFormat::D24S8;
        }
        if ( !device.supportsFormat(depthFormat, depthUsage) )
        {
            return false;
        }

        depth = device.createTexture(TextureDesc{ kSize, kSize, 1, depthFormat, depthUsage });
        if ( depth.isNull() )
        {
            return false;
        }
        renderFloorDepth(device, depth, depthFormat);

        mapInput                 = {};
        mapInput.texture         = map.texture();
        mapInput.compareSampler  = map.compareSampler();
        mapInput.mapSize         = map.size();
        for ( int index = 0; index < 16; ++index )
        {
            mapInput.matrix[index] = map.viewToShadowTexture().m[index];
        }
        mapInput.towardLight[0] = kLight[0];
        mapInput.towardLight[1] = kLight[1];
        mapInput.towardLight[2] = kLight[2];
        return true;
    }

    // Builds a caster/floor scene for one run. Returns false when the device
    // cannot make the map or depth texture; pPixels otherwise holds the LDR
    // output and pRan the effects that ran.
    inline bool runCasterFloorScene(Device& device, float margin,
        const ShadowReceiveSettings& settings, TextureHandle color, TextureHandle normals,
        std::vector<std::byte>* pPixels, std::vector<std::string_view>* pRan)
    {
        babylon::shadow::DirectionalShadowMap map(device);
        ShadowMapInput mapInput;
        TextureHandle depth;
        if ( !buildCasterFloorScene(device, map, margin, mapInput, depth) )
        {
            return false;
        }

        ShadowReceiveEffect effect;
        effect.setSettings(settings);
        effect.setShadowMap(mapInput);
        *pPixels = runScene(device, &effect, color, depth, normals, casterFloorProjection(), pRan);
        device.destroyTexture(depth);
        return true;
    }

}
