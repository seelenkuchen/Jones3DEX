// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/postfx/Chain.h>
#include <babylon/postfx/FrameInputs.h>
#include <babylon/postfx/Resources.h>
#include <babylon/postfx/ShadowReceiveEffect.h>

#include <babylon/shadow/DirectionalShadowMap.h>

#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/SdlGpuDevice.h>
#include <babylon/render/Texture.h>

#include <babylon/log/CaptureSink.h>

#include <babylon/shaders/babylon_postfx_tests.h>

#include "../../render/backends/sdl/tests/GpuRequirement.h"
#include "../../render/backends/sdl/tests/KeepGpuDriverLoaded.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace
{
    using namespace babylon::postfx;
    using namespace babylon::render;

    constexpr std::uint32_t kSize = 64;
    constexpr float kHalfFovX = 1.0f;
    constexpr float kHalfFovY = 1.0f;
    constexpr float kNear = 0.1f;
    constexpr float kFar = 100.0f;
    constexpr float kDepthNear = 0.0f;
    constexpr float kDepthFar = 1.0f;
    constexpr float kFloorY = -1.0f;

    // The caster is a horizontal quad above the floor. The light looks down
    // along view y, so the floor directly below it is shadowed.
    constexpr float kCasterY = -0.4f;
    constexpr float kCasterMinX = -0.2f;
    constexpr float kCasterMaxX = 0.2f;
    constexpr float kCasterMinZ = 2.36f;
    constexpr float kCasterMaxZ = 2.76f;
    constexpr float kMapMargin = 0.7f;    // large enough to put the floor inside the depth range
    constexpr std::uint32_t kMapSize = 256;
    const float kLight[] = { 0.0f, 1.0f, 0.0f };

    // A small margin fits the map's depth range to the caster only: the floor
    // 0.6 view units below it is past the far end (depth > 1) and must still be
    // compared against the clamped reference.
    constexpr float kFarBelowMargin = 0.3f;

    // Row 44 puts the floor at z ~2.56, inside the caster's z range. Column 32
    // is under the caster; column 38 is the same distance from the camera but
    // clear of it and still inside the wide map.
    constexpr std::uint32_t kFloorRow = 44;
    constexpr std::uint32_t kShadowedColumn = 32;
    constexpr std::uint32_t kLitColumn = 38;
    // Inside the narrow map's x footprint but clear of the caster (x ~0.36).
    constexpr std::uint32_t kFarBelowLitColumn = 36;

    std::string joined(const babylon::log::CaptureSink& sink)
    {
        std::string text;
        for ( const auto& record : sink.records() )
        {
            text += text.empty() ? "" : "; ";
            text += record.message;
        }
        return text;
    }

    std::array<int, 4> pixelAt(const std::vector<std::byte>& pixels, std::uint32_t width,
        std::uint32_t x, std::uint32_t y)
    {
        const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4u;
        return { static_cast<int>(pixels[index + 0u]), static_cast<int>(pixels[index + 1u]),
            static_cast<int>(pixels[index + 2u]), static_cast<int>(pixels[index + 3u]) };
    }

    // An Rgba16F texture of the given half values; they are exact for 0.5 and 1.
    TextureHandle makeHalfColor(Device& device, std::uint32_t width, std::uint32_t height)
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
    TextureHandle makeNormals(Device& device, std::uint32_t width, std::uint32_t height)
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
    TextureHandle makeMixedNormals(Device& device, std::uint32_t width, std::uint32_t height)
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
    TextureHandle makeExcludeMask(Device& device, std::uint32_t width, std::uint32_t height)
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
    void renderFloorDepth(Device& device, TextureHandle depth, TextureFormat depthFormat)
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
    std::vector<float> casterVertices()
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
    std::vector<std::byte> runScene(Device& device, Effect* pEffect, TextureHandle color,
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

    TEST_CASE("Shadow receive darkens the floor under a shadow-map caster (GPU)")
    {
        babylon::render::test::keepGpuDriverLoaded();
        babylon::log::CaptureSink sink;
        const std::unique_ptr<Device> device = createSdlGpuDevice(nullptr);
        if ( device == nullptr )
        {
            babylon::render::test::noGpuDevice("no GPU device: " + joined(sink));
            return;
        }

        // The shadow map is a real one, built with the shadow module in the
        // test only.
        babylon::shadow::DirectionalShadowMap map(*device);
        babylon::shadow::ShadowMapDesc mapDesc;
        mapDesc.size = kMapSize;
        if ( !map.open(mapDesc) )
        {
            babylon::render::test::noGpuDevice("no sampled depth format: " + joined(sink));
            return;
        }
        REQUIRE(map.isOpen());

        const std::vector<float> vertices = casterVertices();
        const babylon::shadow::CasterBatch batch{ vertices.data(), vertices.size() / 3 };
        const std::array<babylon::shadow::CasterBatch, 1> batches{ batch };
        CommandList shadowList = device->beginFrame();
        REQUIRE(map.record(shadowList, kLight,
            std::span<const babylon::shadow::CasterBatch>(batches), kMapMargin));
        REQUIRE(device->submit(shadowList));
        REQUIRE(map.isValid());

        const TextureUsage depthUsage = TextureUsage::DepthTarget | TextureUsage::Sampled;
        TextureFormat depthFormat = TextureFormat::D32F;
        if ( !device->supportsFormat(depthFormat, depthUsage) )
        {
            depthFormat = TextureFormat::D24S8;
        }
        if ( !device->supportsFormat(depthFormat, depthUsage) )
        {
            babylon::render::test::noGpuDevice("no sampled depth format: " + joined(sink));
            return;
        }

        const TextureHandle color          = makeHalfColor(*device, kSize, kSize);
        const TextureHandle normals        = makeNormals(*device, kSize, kSize);
        const TextureHandle mixedNormals   = makeMixedNormals(*device, kSize, kSize);
        const TextureHandle excludeMask    = makeExcludeMask(*device, kSize, kSize);
        const TextureHandle depth          = device->createTexture(TextureDesc{ kSize, kSize, 1,
            depthFormat, depthUsage });
        REQUIRE_FALSE(depth.isNull());
        renderFloorDepth(*device, depth, depthFormat);

        Projection projection;
        projection.nearPlane   = kNear;
        projection.farPlane    = kFar;
        projection.tanHalfFovX = kHalfFovX;
        projection.tanHalfFovY = kHalfFovY;
        projection.depthNear   = kDepthNear;
        projection.depthFar    = kDepthFar;

        ShadowReceiveEffect effect;
        ShadowMapInput mapInput;
        mapInput.texture        = map.texture();
        mapInput.compareSampler = map.compareSampler();
        mapInput.mapSize        = map.size();
        for ( int index = 0; index < 16; ++index )
        {
            mapInput.matrix[index] = map.viewToShadowTexture().m[index];
        }
        mapInput.towardLight[0] = kLight[0];
        mapInput.towardLight[1] = kLight[1];
        mapInput.towardLight[2] = kLight[2];
        effect.setShadowMap(mapInput);

        std::vector<std::string_view> plainRan;
        const std::vector<std::byte> plain =
            runScene(*device, nullptr, color, depth, normals, projection, &plainRan);
        REQUIRE(plain.size() == std::size_t{ kSize } * kSize * 4u);
        REQUIRE(plainRan.size() == 1);
        CHECK(plainRan[0] == "copy");

        std::vector<std::string_view> ran;
        const std::vector<std::byte> pixels =
            runScene(*device, &effect, color, depth, normals, projection, &ran);
        REQUIRE(pixels.size() == std::size_t{ kSize } * kSize * 4u);
        INFO("log: " << joined(sink));
        REQUIRE(ran.size() == 2);
        CHECK(ran[0] == "shadow receive");
        CHECK(ran[1] == "copy");

        // The floor directly under the caster is shadowed.
        const std::array<int, 4> shadowed =
            pixelAt(pixels, kSize, kShadowedColumn, kFloorRow);
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            const float value = static_cast<float>(shadowed[channel]) / 255.0f;
            CHECK(value <= 0.5f * (1.0f - 0.5f * effect.settings().strength)
                + 1.0f / 255.0f);
        }
        CHECK(shadowed[3] == 255);

        // The floor away from the caster is lit and unchanged.
        const std::array<int, 4> lit = pixelAt(pixels, kSize, kLitColumn, kFloorRow);
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            CHECK(std::abs(lit[channel] - 128) <= 1);
        }
        CHECK(lit[3] == 255);

        // Without a mask the shadow covers both halves of the screen. Column
        // 31 is left of the middle and column 33 right, both under the caster.
        const std::array<int, 4> shadowedLeft =
            pixelAt(pixels, kSize, kShadowedColumn - 1u, kFloorRow);
        const std::array<int, 4> shadowedRight =
            pixelAt(pixels, kSize, kShadowedColumn + 1u, kFloorRow);
        for ( const std::array<int, 4>* pShadowed : { &shadowedLeft, &shadowedRight } )
        {
            for ( std::size_t channel = 0; channel < 3; ++channel )
            {
                const float value = static_cast<float>((*pShadowed)[channel]) / 255.0f;
                CHECK(value <= 0.5f * (1.0f - 0.5f * effect.settings().strength)
                    + 1.0f / 255.0f);
            }
            CHECK((*pShadowed)[3] == 255);
        }

        // A mask that is red on the left half keeps those shadowed pixels at
        // their exact input colour, while the right half darkens as before.
        ShadowMapInput maskedMap = mapInput;
        maskedMap.excludeMask = excludeMask;
        effect.setShadowMap(maskedMap);
        std::vector<std::string_view> maskedRan;
        const std::vector<std::byte> masked =
            runScene(*device, &effect, color, depth, normals, projection, &maskedRan);
        REQUIRE(masked.size() == std::size_t{ kSize } * kSize * 4u);
        INFO("log: " << joined(sink));
        REQUIRE(maskedRan.size() == 2);
        for ( std::uint32_t y = 0; y < kSize; ++y )
        {
            for ( std::uint32_t x = 0; x < kSize / 2u; ++x )
            {
                CHECK(pixelAt(masked, kSize, x, y) == pixelAt(plain, kSize, x, y));
            }
            for ( std::uint32_t x = kSize / 2u; x < kSize; ++x )
            {
                CHECK(pixelAt(masked, kSize, x, y) == pixelAt(pixels, kSize, x, y));
            }
        }
        effect.setShadowMap(mapInput);

        // Half the screen is tagged class 2 (not in the receiver mask) in the
        // normals alpha; those pixels stay byte-identical to the plain copy.
        std::vector<std::string_view> mixedRan;
        const std::vector<std::byte> mixed =
            runScene(*device, &effect, color, depth, mixedNormals, projection, &mixedRan);
        REQUIRE(mixed.size() == std::size_t{ kSize } * kSize * 4u);
        INFO("log: " << joined(sink));
        REQUIRE(mixedRan.size() == 2);
        for ( std::uint32_t y = 0; y < kSize; ++y )
        {
            for ( std::uint32_t x = kSize / 2u; x < kSize; ++x )
            {
                CHECK(pixelAt(mixed, kSize, x, y) == pixelAt(plain, kSize, x, y));
            }
        }

        // Strength 0 skips the effect, leaving a byte-identical copy.
        ShadowReceiveEffect inactive;
        ShadowReceiveSettings off;
        off.strength = 0.0f;
        inactive.setSettings(off);
        inactive.setShadowMap(mapInput);
        CHECK_FALSE(inactive.active());
        std::vector<std::string_view> inactiveRan;
        const std::vector<std::byte> skipped =
            runScene(*device, &inactive, color, depth, normals, projection, &inactiveRan);
        INFO("log: " << joined(sink));
        REQUIRE(inactiveRan.size() == 1);
        CHECK(inactiveRan[0] == "copy");
        CHECK(skipped == plain);

        INFO("captured log: " << joined(sink));
        CHECK(sink.records().empty());

        device->destroyTexture(color);
        device->destroyTexture(normals);
        device->destroyTexture(mixedNormals);
        device->destroyTexture(excludeMask);
        device->destroyTexture(depth);
    }

    TEST_CASE("Shadow receive still darkens a floor below the map's depth range (GPU)")
    {
        babylon::render::test::keepGpuDriverLoaded();
        babylon::log::CaptureSink sink;
        const std::unique_ptr<Device> device = createSdlGpuDevice(nullptr);
        if ( device == nullptr )
        {
            babylon::render::test::noGpuDevice("no GPU device: " + joined(sink));
            return;
        }

        // The small margin fits the depth range to the caster only; the floor
        // is farther from the light than the map's far end, so its normalized
        // depth is greater than 1.
        babylon::shadow::DirectionalShadowMap map(*device);
        babylon::shadow::ShadowMapDesc mapDesc;
        mapDesc.size = kMapSize;
        if ( !map.open(mapDesc) )
        {
            babylon::render::test::noGpuDevice("no sampled depth format: " + joined(sink));
            return;
        }
        REQUIRE(map.isOpen());

        const std::vector<float> vertices = casterVertices();
        const babylon::shadow::CasterBatch batch{ vertices.data(), vertices.size() / 3 };
        const std::array<babylon::shadow::CasterBatch, 1> batches{ batch };
        CommandList shadowList = device->beginFrame();
        REQUIRE(map.record(shadowList, kLight,
            std::span<const babylon::shadow::CasterBatch>(batches), kFarBelowMargin));
        REQUIRE(device->submit(shadowList));
        REQUIRE(map.isValid());

        const TextureUsage depthUsage = TextureUsage::DepthTarget | TextureUsage::Sampled;
        TextureFormat depthFormat = TextureFormat::D32F;
        if ( !device->supportsFormat(depthFormat, depthUsage) )
        {
            depthFormat = TextureFormat::D24S8;
        }
        if ( !device->supportsFormat(depthFormat, depthUsage) )
        {
            babylon::render::test::noGpuDevice("no sampled depth format: " + joined(sink));
            return;
        }

        const TextureHandle color   = makeHalfColor(*device, kSize, kSize);
        const TextureHandle normals = makeNormals(*device, kSize, kSize);
        const TextureHandle depth   = device->createTexture(TextureDesc{ kSize, kSize, 1,
            depthFormat, depthUsage });
        REQUIRE_FALSE(depth.isNull());
        renderFloorDepth(*device, depth, depthFormat);

        Projection projection;
        projection.nearPlane   = kNear;
        projection.farPlane    = kFar;
        projection.tanHalfFovX = kHalfFovX;
        projection.tanHalfFovY = kHalfFovY;
        projection.depthNear   = kDepthNear;
        projection.depthFar    = kDepthFar;

        ShadowReceiveEffect effect;
        ShadowMapInput mapInput;
        mapInput.texture        = map.texture();
        mapInput.compareSampler = map.compareSampler();
        mapInput.mapSize        = map.size();
        for ( int index = 0; index < 16; ++index )
        {
            mapInput.matrix[index] = map.viewToShadowTexture().m[index];
        }
        mapInput.towardLight[0] = kLight[0];
        mapInput.towardLight[1] = kLight[1];
        mapInput.towardLight[2] = kLight[2];
        effect.setShadowMap(mapInput);

        std::vector<std::string_view> ran;
        const std::vector<std::byte> pixels =
            runScene(*device, &effect, color, depth, normals, projection, &ran);
        REQUIRE(pixels.size() == std::size_t{ kSize } * kSize * 4u);
        INFO("log: " << joined(sink));
        REQUIRE(ran.size() == 2);
        CHECK(ran[0] == "shadow receive");
        CHECK(ran[1] == "copy");

        // The floor under the caster is beyond the map's far end but must still
        // be shadowed.
        const std::array<int, 4> shadowed =
            pixelAt(pixels, kSize, kShadowedColumn, kFloorRow);
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            const float value = static_cast<float>(shadowed[channel]) / 255.0f;
            CHECK(value <= 0.5f * (1.0f - 0.5f * effect.settings().strength)
                + 1.0f / 255.0f);
        }
        CHECK(shadowed[3] == 255);

        // The cleared part of the map still passes, so the floor clear of the
        // caster stays lit.
        const std::array<int, 4> lit =
            pixelAt(pixels, kSize, kFarBelowLitColumn, kFloorRow);
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            CHECK(std::abs(lit[channel] - 128) <= 1);
        }
        CHECK(lit[3] == 255);

        INFO("captured log: " << joined(sink));
        CHECK(sink.records().empty());

        device->destroyTexture(color);
        device->destroyTexture(normals);
        device->destroyTexture(depth);
    }
}
