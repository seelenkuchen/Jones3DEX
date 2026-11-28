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
#include <utility>
#include <vector>

namespace
{
    using namespace babylon::postfx;
    using namespace babylon::render;

    constexpr std::uint32_t kSize = 64;
    constexpr float kHalfFovX = 1.245f;
    constexpr float kHalfFovY = 20.0f;
    constexpr float kNear = 0.01f;
    constexpr float kFar = 100.0f;
    constexpr float kDepthNear = 0.1f;
    constexpr float kDepthFar = 1.0f;
    constexpr float kFloorY = -1.0f;
    constexpr float kBoxMinX = 0.05f;
    constexpr float kBoxMaxX = 0.07f;
    constexpr float kBoxTopY = -0.3f;

    // The box front sits in front of the floor by less than the default
    // thickness over a band of rows, so the floor to its left is a wide contact
    // shadow. The test row runs through the middle of that band.
    constexpr std::uint32_t kFloorRow = 50;
    constexpr std::uint32_t kShadowedColumn = 20;   // left of the box
    constexpr std::uint32_t kFarFloorColumn = 5;
    constexpr std::uint32_t kBoxTopColumn = 54;
    constexpr std::uint32_t kBoxTopRow = 38;

    // An 8x8 block well inside the shadow, for the smoothness check.
    constexpr std::uint32_t kSmoothColumn = 20;
    constexpr std::uint32_t kSmoothRow = 47;
    constexpr std::uint32_t kSmoothSize = 8;

    // The floor normal in view space is +y, so a normalised light's y component
    // is its dot with the floor normal. 0.25 is grazing: at kShadowedColumn the
    // box front still sits within the thickness, so the floor would shadow
    // under the old 0..0.2 facing ramp. 0.82 is the fixed overhead light at
    // about 35 degrees and keeps the floor shadowed.
    constexpr float kGrazingLightY  = 0.25f;
    constexpr float kOverheadLightY = 0.82f;

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
    // (an ordinary receiver) and the right half class 2 (sky in the receiver
    // mask), so the tagged half is adjacent to the shadowed ordinary floor.
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
    float floorDepthAtRow(std::uint32_t row)
    {
        const float v = (static_cast<float>(row) + 0.5f) / static_cast<float>(kSize);
        return kFloorY / ((1.0f - 2.0f * v) * kHalfFovY);
    }

    float boxFrontDepth()
    {
        return floorDepthAtRow(kFloorRow) - 0.02f;
    }

    // Mirrors tests/shaders/scene_depth.frag.hlsl's box intersection: true when
    // the box, not the floor or the sky, is the nearest surface at the pixel.
    bool isBoxPixel(std::uint32_t x, std::uint32_t y)
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
    TextureHandle makeMask(Device& device, std::uint32_t width, std::uint32_t height,
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

    TextureHandle makeOccluderMask(Device& device, std::uint32_t width, std::uint32_t height)
    {
        return makeMask(device, width, height,
            [](std::uint32_t x, std::uint32_t y) { return isBoxPixel(x, y); });
    }

    TextureHandle makeRectMask(Device& device, std::uint32_t width, std::uint32_t height,
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
    void renderDepthScene(Device& device, TextureHandle depth, TextureFormat depthFormat,
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

    // The mean red channel of a rectangle, 0..255.
    double meanBrightness(const std::vector<std::byte>& pixels, std::uint32_t minX,
        std::uint32_t minY, std::uint32_t maxX, std::uint32_t maxY)
    {
        double sum = 0.0;
        for ( std::uint32_t y = minY; y < maxY; ++y )
        {
            for ( std::uint32_t x = minX; x < maxX; ++x )
            {
                sum += static_cast<double>(pixelAt(pixels, kSize, x, y)[0]);
            }
        }
        return sum / static_cast<double>((maxX - minX) * (maxY - minY));
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
        INFO("log: " << joined(sink));
        REQUIRE(ran.size() == 2);
        CHECK(ran[0] == "contact shadows");
        CHECK(ran[1] == "copy");

        // The mask is smoothed by the blur, so the shadow over an 8x8 block in
        // its middle has no dither pattern left: a low standard deviation.
        double sum = 0.0;
        double sumSquares = 0.0;
        for ( std::uint32_t y = kSmoothRow; y < kSmoothRow + kSmoothSize; ++y )
        {
            for ( std::uint32_t x = kSmoothColumn; x < kSmoothColumn + kSmoothSize; ++x )
            {
                const double value = static_cast<double>(pixelAt(pixels, kSize, x, y)[0]) / 255.0;
                sum += value;
                sumSquares += value * value;
            }
        }
        const double count = static_cast<double>(kSmoothSize * kSmoothSize);
        const double mean = sum / count;
        const double variance = sumSquares / count - mean * mean;
        CHECK(mean < 0.5);
        CHECK(std::sqrt(std::max(0.0, variance)) < 0.02);

        // Just left of the box base, on the side away from the light: the ray
        // starts toward the box and is blocked by its front face.
        const std::array<int, 4> shadowed = pixelAt(pixels, kSize, kShadowedColumn, kFloorRow);
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            const float value = static_cast<float>(shadowed[channel]) / 255.0f;
            CHECK(value <= 0.5f * (1.0f - 0.5f * effect.settings().strength) + 1.0f / 255.0f);
        }
        CHECK(shadowed[3] == 255);

        // The floor away from the box and the box top are unchanged; the two
        // kinds of non-scene depth stay byte-exact.
        checkUnchanged(pixelAt(pixels, kSize, kFarFloorColumn, kFloorRow));
        checkUnchanged(pixelAt(pixels, kSize, kBoxTopColumn, kBoxTopRow));
        CHECK(pixelAt(pixels, kSize, 2, 2) == std::array<int, 4>{ 128, 128, 128, 255 });
        CHECK(pixelAt(pixels, kSize, 40, 4) == std::array<int, 4>{ 128, 128, 128, 255 });

        // The no-effect copy, for the exact-input comparisons below.
        std::vector<std::string_view> plainRan;
        const std::vector<std::byte> plain =
            runScene(*device, nullptr, color, depth, normals, projection, &plainRan);
        REQUIRE(plain.size() == std::size_t{ kSize } * kSize * 4u);
        INFO("log: " << joined(sink));
        REQUIRE(plainRan.size() == 1);
        CHECK(plainRan[0] == "copy");

        // Half the screen is tagged class 2 (sky) in the normals alpha, next to
        // the shadowed ordinary floor. The tagged half takes no shadow at all,
        // byte for byte, even where the blur carries a shadow across the edge.
        const TextureHandle mixedNormals = makeMixedNormals(*device, kSize, kSize);
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
                CHECK(pixelAt(mixed, kSize, x, y) == std::array<int, 4>{ 128, 128, 128, 255 });
            }
        }

        // Just left of the tag edge the ordinary floor still takes its shadow:
        // the tagged taps weigh nothing and cannot lighten it.
        const std::array<int, 4> edgeShadow = pixelAt(mixed, kSize, kSize / 2u - 1u, kFloorRow);
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            const float value = static_cast<float>(edgeShadow[channel]) / 255.0f;
            CHECK(value <= 0.5f * (1.0f - 0.5f * effect.settings().strength) + 1.0f / 255.0f);
        }

        // A mask on the occluder's pixels: the rays that the box blocked now
        // cross it as gaps, so the floor it shadowed keeps its input colour,
        // and the box itself keeps its input colour too, byte for byte.
        const TextureHandle occluderMask = makeOccluderMask(*device, kSize, kSize);
        effect.setIgnoreMask(occluderMask);
        std::vector<std::string_view> occluderRan;
        const std::vector<std::byte> occluded =
            runScene(*device, &effect, color, depth, normals, projection, &occluderRan);
        REQUIRE(occluded.size() == std::size_t{ kSize } * kSize * 4u);
        INFO("log: " << joined(sink));
        REQUIRE(occluderRan.size() == 2);
        CHECK(occluderRan[0] == "contact shadows");
        CHECK(occluderRan[1] == "copy");
        CHECK(pixelAt(occluded, kSize, kShadowedColumn, kFloorRow)
            == pixelAt(plain, kSize, kShadowedColumn, kFloorRow));
        CHECK(pixelAt(occluded, kSize, kBoxTopColumn, kBoxTopRow)
            == pixelAt(plain, kSize, kBoxTopColumn, kBoxTopRow));

        // A mask on floor pixels well away from the shadow: those pixels keep
        // their input colour and every other pixel, including the whole shadow,
        // matches the no-mask run byte for byte.
        const TextureHandle floorMask = makeRectMask(*device, kSize, kSize, 2, 10, 44, 52);
        effect.setIgnoreMask(floorMask);
        std::vector<std::string_view> floorMaskRan;
        const std::vector<std::byte> floorMasked =
            runScene(*device, &effect, color, depth, normals, projection, &floorMaskRan);
        REQUIRE(floorMasked.size() == std::size_t{ kSize } * kSize * 4u);
        INFO("log: " << joined(sink));
        REQUIRE(floorMaskRan.size() == 2);
        for ( std::uint32_t y = 0; y < kSize; ++y )
        {
            for ( std::uint32_t x = 0; x < kSize; ++x )
            {
                const bool masked = x >= 2 && x < 10 && y >= 44 && y < 52;
                CHECK(pixelAt(floorMasked, kSize, x, y)
                    == pixelAt(masked ? plain : pixels, kSize, x, y));
            }
        }

        // Removing the mask restores the byte-identical no-mask result.
        effect.setIgnoreMask({});
        std::vector<std::string_view> restoredRan;
        const std::vector<std::byte> restored =
            runScene(*device, &effect, color, depth, normals, projection, &restoredRan);
        REQUIRE(restored.size() == std::size_t{ kSize } * kSize * 4u);
        INFO("log: " << joined(sink));
        REQUIRE(restoredRan.size() == 2);
        CHECK(restored == pixels);

        // Strength 0 skips the effect, leaving a byte-identical copy.
        ContactShadowEffect inactive;
        ContactShadowSettings off;
        off.strength = 0.0f;
        inactive.setSettings(off);
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
        device->destroyTexture(occluderMask);
        device->destroyTexture(floorMask);
        device->destroyTexture(depth);
    }

    TEST_CASE("Contact shadows leave surfaces that face away from the light unchanged (GPU)")
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

        // The light below the floor: the floor, the box top and the box's
        // visible left face all face away from it, and its front face is edge-on.
        // Nothing lit can be shadowed, so the output is the plain copy.
        ContactShadowEffect effect;
        effect.setLightDirection(0.5f, -0.85f, 0.0f);

        std::vector<std::string_view> ran;
        const std::vector<std::byte> pixels =
            runScene(*device, &effect, color, depth, normals, projection, &ran);
        std::vector<std::string_view> plainRan;
        const std::vector<std::byte> plain =
            runScene(*device, nullptr, color, depth, normals, projection, &plainRan);
        INFO("log: " << joined(sink));
        REQUIRE(pixels.size() == plain.size());
        REQUIRE(ran.size() == 2);
        CHECK(ran[0] == "contact shadows");
        CHECK(ran[1] == "copy");

        std::size_t numDarkened = 0;
        for ( std::uint32_t y = 0; y < kSize; ++y )
        {
            for ( std::uint32_t x = 0; x < kSize; ++x )
            {
                if ( pixelAt(pixels, kSize, x, y) != pixelAt(plain, kSize, x, y) )
                {
                    ++numDarkened;
                }
            }
        }
        INFO("darkened pixels: " << numDarkened << " of " << kSize * kSize);
        CHECK(numDarkened == 0);
        checkUnchanged(pixelAt(pixels, kSize, kFarFloorColumn, kFloorRow));
        checkUnchanged(pixelAt(pixels, kSize, kShadowedColumn, kFloorRow));
        CHECK(sink.records().empty());

        device->destroyTexture(color);
        device->destroyTexture(normals);
        device->destroyTexture(depth);
    }

    TEST_CASE("Contact shadows skip surfaces nearly edge-on to the light (GPU)")
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

        // The plain copy, for exact-input comparisons.
        std::vector<std::string_view> plainRan;
        const std::vector<std::byte> plain =
            runScene(*device, nullptr, color, depth, normals, projection, &plainRan);
        REQUIRE(plain.size() == std::size_t{ kSize } * kSize * 4u);
        INFO("log: " << joined(sink));
        REQUIRE(plainRan.size() == 1);
        CHECK(plainRan[0] == "copy");

        const std::array<int, 4> plainPixel =
            pixelAt(plain, kSize, kShadowedColumn, kFloorRow);

        // The large x component keeps the ray pointing across the box from the
        // floor to its left, so the box blocks it whenever the ramp lets the
        // pixel through.
        ContactShadowEffect effect;

        // Almost edge-on to the floor: the new wide band skips this pixel, so
        // it matches the plain copy. The old 0..0.2 ramp would shadow it.
        const float grazingX = std::sqrt(1.0f - kGrazingLightY * kGrazingLightY);
        effect.setLightDirection(grazingX, kGrazingLightY, 0.0f);

        std::vector<std::string_view> grazingRan;
        const std::vector<std::byte> grazing =
            runScene(*device, &effect, color, depth, normals, projection, &grazingRan);
        REQUIRE(grazing.size() == plain.size());
        INFO("log: " << joined(sink));
        REQUIRE(grazingRan.size() == 2);
        CHECK(grazingRan[0] == "contact shadows");
        CHECK(grazingRan[1] == "copy");

        const std::array<int, 4> grazingPixel =
            pixelAt(grazing, kSize, kShadowedColumn, kFloorRow);
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            CHECK(std::abs(grazingPixel[channel] - plainPixel[channel]) <= 1);
        }
        CHECK(grazingPixel[3] == 255);

        // The overhead light at about 35 degrees, pointing the same way across
        // the box: the same pixel is shadowed well past the 10/255 threshold.
        const float overheadX = std::sqrt(1.0f - kOverheadLightY * kOverheadLightY);
        effect.setLightDirection(overheadX, kOverheadLightY, 0.0f);

        std::vector<std::string_view> overheadRan;
        const std::vector<std::byte> overhead =
            runScene(*device, &effect, color, depth, normals, projection, &overheadRan);
        REQUIRE(overhead.size() == plain.size());
        INFO("log: " << joined(sink));
        REQUIRE(overheadRan.size() == 2);
        CHECK(overheadRan[0] == "contact shadows");
        CHECK(overheadRan[1] == "copy");

        const std::array<int, 4> overheadPixel =
            pixelAt(overhead, kSize, kShadowedColumn, kFloorRow);
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            CHECK(overheadPixel[channel] <= plainPixel[channel] - 10);
        }
        CHECK(overheadPixel[3] == 255);

        CHECK(sink.records().empty());

        device->destroyTexture(color);
        device->destroyTexture(normals);
        device->destroyTexture(depth);
    }

    TEST_CASE("Contact shadows leave a steep open floor facing the light unchanged (GPU)")
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
        renderDepthScene(*device, depth, depthFormat, false);

        Projection projection;
        projection.nearPlane   = kNear;
        projection.farPlane    = kFar;
        projection.tanHalfFovX = kHalfFovX;
        projection.tanHalfFovY = kHalfFovY;
        projection.depthNear   = kDepthNear;
        projection.depthFar    = kDepthFar;

        // A flat open floor with no occluder. The light is almost straight up
        // in view space, so the floor faces it (N.L about 0.99), and a small
        // forward component keeps the ray in the floor. Near the bottom of the
        // screen the floor is steep in screen depth: at the first ray step the
        // sample still lands in the receiver's own pixel, so the stored depth
        // is the receiver's own surface, quantised. The old depth window took
        // that as a blocker; the plane test rejects it as the receiver itself.
        ContactShadowEffect effect;
        effect.setLightDirection(0.0f, 0.99f, 0.141f);

        std::vector<std::string_view> plainRan;
        const std::vector<std::byte> plain =
            runScene(*device, nullptr, color, depth, normals, projection, &plainRan);
        REQUIRE(plain.size() == std::size_t{ kSize } * kSize * 4u);
        INFO("log: " << joined(sink));
        REQUIRE(plainRan.size() == 1);
        CHECK(plainRan[0] == "copy");

        std::vector<std::string_view> onRan;
        const std::vector<std::byte> on =
            runScene(*device, &effect, color, depth, normals, projection, &onRan);
        REQUIRE(on.size() == plain.size());
        INFO("log: " << joined(sink));
        REQUIRE(onRan.size() == 2);
        CHECK(onRan[0] == "contact shadows");
        CHECK(onRan[1] == "copy");

        // The open floor is not its own occluder, so it stays nearly unchanged.
        const double plainMean = meanBrightness(plain, 14, 49, 26, 58);
        const double onMean    = meanBrightness(on, 14, 49, 26, 58);
        INFO("floor mean " << onMean << " on, " << plainMean << " plain");
        CHECK(std::abs(onMean - plainMean) <= 1.0);

        CHECK(sink.records().empty());

        device->destroyTexture(color);
        device->destroyTexture(normals);
        device->destroyTexture(depth);
    }
}
