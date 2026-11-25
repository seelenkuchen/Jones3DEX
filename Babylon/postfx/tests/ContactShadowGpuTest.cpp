// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

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
    constexpr float kHalfFovX = 0.75f;
    constexpr float kHalfFovY = 0.75f;
    constexpr float kNear = 0.1f;
    constexpr float kFar = 100.0f;
    constexpr float kDepthNear = 0.1f;
    constexpr float kDepthFar = 1.0f;
    constexpr float kFloorY = -1.0f;
    constexpr float kBoxMinX = 0.0f;
    constexpr float kBoxMaxX = 0.3f;
    constexpr float kBoxTopY = -0.3f;

    // The test row just above the box's bottom edge. The floor there lies a
    // little behind the box's front face, inside the default thickness, so the
    // pixel just left of the box base is a contact shadow.
    constexpr std::uint32_t kFloorRow = 50;
    constexpr std::uint32_t kShadowedColumn = 31;   // just left of the box
    constexpr std::uint32_t kFarFloorColumn = 5;
    constexpr std::uint32_t kBoxTopColumn = 34;
    constexpr std::uint32_t kBoxTopRow = 37;

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

    // The box front sits slightly in front of the floor depth at kFloorRow, so
    // a floor pixel just left of the base has the box front close enough in
    // depth to count as a blocker rather than a gap.
    float floorDepthAtRow(std::uint32_t row)
    {
        const float v = (static_cast<float>(row) + 0.5f) / static_cast<float>(kSize);
        return kFloorY / ((1.0f - 2.0f * v) * kHalfFovY);
    }

    float boxFrontDepth()
    {
        return floorDepthAtRow(kFloorRow) - 0.01f;
    }

    // Renders the synthetic scene's depth into depth with a depth-only
    // full-screen pass. The scene is an infinite floor plane at y = -1 and an
    // axis-aligned box standing on it, x in [0, 0.3], y in [-1, -0.3], z in
    // [front, front + 0.3]. The camera is at the origin looking along +z. The
    // top-left 8x8 pixels get a depth in front of the near plane (an overlay)
    // and the sky gets the clear depth.
    void renderDepthScene(Device& device, TextureHandle depth, TextureFormat depthFormat)
    {
        const float front = boxFrontDepth();
        const SceneUniforms uniforms{
            kNear, kFar, kDepthNear, kDepthFar, kHalfFovX, kHalfFovY, kFloorY,
            kBoxMinX, kBoxMaxX, kBoxTopY, front, front + 0.3f, { 0.0f, 0.0f, 0.0f, 0.0f } };

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

    void checkUnchanged(const std::array<int, 4>& pixel)
    {
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            CHECK(std::abs(pixel[channel] - 128) <= 1);
        }
        CHECK(pixel[3] == 255);
    }

    TEST_CASE("Contact shadows darken the floor beside a box (GPU)")
    {
        babylon::render::test::keepGpuDriverLoaded();
        babylon::log::CaptureSink sink;
        const std::unique_ptr<Device> device = createSdlGpuDevice(nullptr);
        if ( device == nullptr )
        {
            babylon::render::test::noGpuDevice("no GPU device: " + joined(sink));
            return;
        }

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
        renderDepthScene(*device, depth, depthFormat);

        Projection projection;
        projection.nearPlane   = kNear;
        projection.farPlane    = kFar;
        projection.tanHalfFovX = kHalfFovX;
        projection.tanHalfFovY = kHalfFovY;
        projection.depthNear   = kDepthNear;
        projection.depthFar    = kDepthFar;

        ContactShadowEffect effect;
        effect.setLightDirection(0.5f, 0.85f, 0.0f);

        std::vector<std::string_view> ran;
        const std::vector<std::byte> pixels =
            runScene(*device, &effect, color, depth, normals, projection, &ran);
        REQUIRE(pixels.size() == std::size_t{ kSize } * kSize * 4u);
        REQUIRE(ran.size() == 2);
        CHECK(ran[0] == "contact shadows");
        CHECK(ran[1] == "copy");

        // Just left of the box base, on the side away from the light: the ray
        // starts toward the box and is blocked by its front face.
        const std::array<int, 4> shadowed = pixelAt(pixels, kSize, kShadowedColumn, kFloorRow);
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            const float value = static_cast<float>(shadowed[channel]) / 255.0f;
            CHECK(value <= 0.5f * (1.0f - 0.5f * effect.settings().strength) + 1.0f / 255.0f);
        }
        CHECK(shadowed[3] == 255);

        // The floor away from the box, the box top and the two kinds of
        // non-scene depth are unchanged.
        checkUnchanged(pixelAt(pixels, kSize, kFarFloorColumn, kFloorRow));
        checkUnchanged(pixelAt(pixels, kSize, kBoxTopColumn, kBoxTopRow));
        checkUnchanged(pixelAt(pixels, kSize, 2, 2));   // overlay depth below depthNear
        checkUnchanged(pixelAt(pixels, kSize, 40, 4));  // cleared depth

        // Strength 0 skips the effect, leaving a byte-identical copy.
        ContactShadowEffect inactive;
        ContactShadowSettings off;
        off.strength = 0.0f;
        inactive.setSettings(off);
        std::vector<std::string_view> inactiveRan;
        const std::vector<std::byte> skipped =
            runScene(*device, &inactive, color, depth, normals, projection, &inactiveRan);
        REQUIRE(inactiveRan.size() == 1);
        CHECK(inactiveRan[0] == "copy");
        std::vector<std::string_view> plainRan;
        const std::vector<std::byte> plain =
            runScene(*device, nullptr, color, depth, normals, projection, &plainRan);
        CHECK(skipped == plain);

        INFO("captured log: " << joined(sink));
        CHECK(sink.records().empty());

        device->destroyTexture(color);
        device->destroyTexture(normals);
        device->destroyTexture(depth);
    }
}
