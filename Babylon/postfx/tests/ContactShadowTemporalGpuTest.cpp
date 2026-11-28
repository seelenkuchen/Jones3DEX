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

#include "../../render/backends/sdl/tests/GpuRequirement.h"
#include "../../render/backends/sdl/tests/KeepGpuDriverLoaded.h"

#include "ContactShadowGpuSupport.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace babylon::postfx;
    using namespace babylon::render;
    using namespace babylon::postfx::testsupport;

    constexpr float kHistoryHalfLife = 0.04f;
    constexpr float kFrameSeconds    = 1.0f / 60.0f;

    // The effect's contact-shadow look, with the history half life the test asks for.
    void configureEffect(ContactShadowEffect& effect, float historyHalfLife)
    {
        ContactShadowSettings settings;
        settings.historyHalfLife = historyHalfLife;
        effect.setSettings(settings);
        effect.setLightDirection(0.5f, 0.85f, 0.0f);
    }

    Projection testProjection()
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

    bool chooseDepthFormat(Device& device, TextureFormat* pFormat)
    {
        const TextureUsage usage = TextureUsage::DepthTarget | TextureUsage::Sampled;
        for ( TextureFormat format : { TextureFormat::D32F, TextureFormat::D24S8 } )
        {
            if ( device.supportsFormat(format, usage) )
            {
                *pFormat = format;
                return true;
            }
        }
        return false;
    }

    // One chain kept open across frames, so the effect keeps its history between
    // them. runScene() closes its chain and would drop it.
    class HistoryRunner
    {
    public:
        HistoryRunner(Device& device, Effect& effect)
            : m_device(device)
            , m_chain(device)
        {
            REQUIRE(m_chain.open());
            REQUIRE(m_chain.add(effect));
            m_output = m_device.createTexture(TextureDesc{ kSize, kSize, 1,
                TextureFormat::Rgba8, TextureUsage::ColorTarget });
            REQUIRE_FALSE(m_output.isNull());
        }

        ~HistoryRunner()
        {
            m_chain.close();
            m_device.destroyTexture(m_output);
        }

        HistoryRunner(const HistoryRunner&) = delete;
        HistoryRunner& operator=(const HistoryRunner&) = delete;

        std::vector<std::byte> run(TextureHandle color, TextureHandle depth,
            TextureHandle normals, const Projection& projection, float deltaSeconds)
        {
            FrameInputs frame;
            frame.color        = color;
            frame.colorFormat  = TextureFormat::Rgba16F;
            frame.depth        = depth;
            frame.normals      = normals;
            frame.width        = kSize;
            frame.height       = kSize;
            frame.projection   = projection;
            frame.deltaSeconds = deltaSeconds;

            OutputTarget target;
            target.texture = m_output;
            target.format  = TextureFormat::Rgba8;
            target.width   = kSize;
            target.height  = kSize;

            CommandList list = m_device.beginFrame();
            REQUIRE(m_chain.record(list, frame, target));
            REQUIRE(m_device.submit(list));
            return m_device.readTexture(m_output, 0);
        }

    private:
        Device& m_device;
        Chain m_chain;
        TextureHandle m_output;
    };

    // The single-frame result of the scene currently in depth: a fresh effect
    // with no history at all.
    std::vector<std::byte> singleFrame(Device& device, TextureHandle color, TextureHandle depth,
        TextureHandle normals, const Projection& projection)
    {
        ContactShadowEffect effect;
        configureEffect(effect, 0.0f);
        std::vector<std::string_view> ran;
        return runScene(device, &effect, color, depth, normals, projection, &ran);
    }

    bool sameBytes(const std::vector<std::byte>& first, const std::vector<std::byte>& second)
    {
        return first == second;
    }

    // The largest per-channel difference of two whole images.
    int maxChannelDifference(const std::vector<std::byte>& first,
        const std::vector<std::byte>& second)
    {
        int maxDifference = 0;
        for ( std::size_t i = 0; i < first.size() && i < second.size(); ++i )
        {
            maxDifference = std::max(maxDifference,
                std::abs(static_cast<int>(first[i]) - static_cast<int>(second[i])));
        }
        return maxDifference;
    }

    struct ErrorStats
    {
        double mean = 0.0;
        int max = 0;
        int countAboveFour = 0;
    };

    // The difference between two images over an interior rectangle, all colour
    // channels. The border is left out because a reprojected tap there is
    // rejected and the values need not match.
    ErrorStats imageError(const std::vector<std::byte>& first,
        const std::vector<std::byte>& second, std::uint32_t minX, std::uint32_t minY,
        std::uint32_t maxX, std::uint32_t maxY)
    {
        double sum = 0.0;
        int maxDifference = 0;
        int countAboveFour = 0;
        int count = 0;
        for ( std::uint32_t y = minY; y < maxY; ++y )
        {
            for ( std::uint32_t x = minX; x < maxX; ++x )
            {
                const std::array<int, 4> a = pixelAt(first, kSize, x, y);
                const std::array<int, 4> b = pixelAt(second, kSize, x, y);
                for ( std::size_t channel = 0; channel < 3; ++channel )
                {
                    const int difference = std::abs(a[channel] - b[channel]);
                    sum += static_cast<double>(difference);
                    maxDifference = std::max(maxDifference, difference);
                    if ( difference > 4 )
                    {
                        ++countAboveFour;
                    }
                    ++count;
                }
            }
        }
        return ErrorStats{ sum / static_cast<double>(count), maxDifference, countAboveFour };
    }
}

// Test 1: historyHalfLife 0 is byte-identical to an effect that never had the
// setting, even after a nonzero half life was set first.
TEST_CASE("Contact shadows without history are byte-identical to the old result (GPU)")
{
    babylon::render::test::keepGpuDriverLoaded();
    babylon::log::CaptureSink sink;
    const std::unique_ptr<Device> device = createSdlGpuDevice(nullptr);
    if ( device == nullptr )
    {
        babylon::render::test::noGpuDevice("no GPU device: " + joined(sink));
        return;
    }

    TextureFormat depthFormat = TextureFormat::D32F;
    if ( !chooseDepthFormat(*device, &depthFormat) )
    {
        babylon::render::test::noGpuDevice("no sampled depth format: " + joined(sink));
        return;
    }

    const TextureHandle color   = makeHalfColor(*device, kSize, kSize);
    const TextureHandle normals = makeNormals(*device, kSize, kSize);
    const TextureHandle depth   = device->createTexture(TextureDesc{ kSize, kSize, 1,
        depthFormat, TextureUsage::DepthTarget | TextureUsage::Sampled });
    REQUIRE_FALSE(depth.isNull());
    renderDepthScene(*device, depth, depthFormat);
    const Projection projection = testProjection();

    // One effect switches the history on and back off; the other never sees the
    // setting at all.
    ContactShadowEffect toggled;
    ContactShadowSettings settings;
    settings.historyHalfLife = 0.5f;
    toggled.setSettings(settings);
    toggled.setLightDirection(0.5f, 0.85f, 0.0f);
    settings.historyHalfLife = 0.0f;
    toggled.setSettings(settings);

    ContactShadowEffect never;
    never.setLightDirection(0.5f, 0.85f, 0.0f);

    std::vector<std::string_view> toggledRan;
    const std::vector<std::byte> toggledPixels =
        runScene(*device, &toggled, color, depth, normals, projection, &toggledRan);
    std::vector<std::string_view> neverRan;
    const std::vector<std::byte> neverPixels =
        runScene(*device, &never, color, depth, normals, projection, &neverRan);

    REQUIRE(toggledPixels.size() == std::size_t{ kSize } * kSize * 4u);
    REQUIRE(neverPixels.size() == toggledPixels.size());
    INFO("log: " << joined(sink));
    REQUIRE(toggledRan.size() == 2);
    REQUIRE(neverRan.size() == 2);
    CHECK(toggledRan[0] == "contact shadows");
    CHECK(neverRan[0] == "contact shadows");
    CHECK(sameBytes(toggledPixels, neverPixels));
    CHECK(sink.records().empty());

    device->destroyTexture(color);
    device->destroyTexture(normals);
    device->destroyTexture(depth);
}

// Test 2: an occluder that appears fades the shadow in over a short time.
TEST_CASE("Contact shadow history fades an appearing occluder in (GPU)")
{
    babylon::render::test::keepGpuDriverLoaded();
    babylon::log::CaptureSink sink;
    const std::unique_ptr<Device> device = createSdlGpuDevice(nullptr);
    if ( device == nullptr )
    {
        babylon::render::test::noGpuDevice("no GPU device: " + joined(sink));
        return;
    }

    TextureFormat depthFormat = TextureFormat::D32F;
    if ( !chooseDepthFormat(*device, &depthFormat) )
    {
        babylon::render::test::noGpuDevice("no sampled depth format: " + joined(sink));
        return;
    }

    const TextureHandle color   = makeHalfColor(*device, kSize, kSize);
    const TextureHandle normals = makeNormals(*device, kSize, kSize);
    const TextureHandle depth   = device->createTexture(TextureDesc{ kSize, kSize, 1,
        depthFormat, TextureUsage::DepthTarget | TextureUsage::Sampled });
    REQUIRE_FALSE(depth.isNull());
    const Projection projection = testProjection();

    ContactShadowEffect effect;
    configureEffect(effect, kHistoryHalfLife);
    CameraMotion identity;
    identity.valid = true;

    HistoryRunner runner(*device, effect);

    // First frame: the open floor, no occluder.
    renderDepthScene(*device, depth, depthFormat, false);
    effect.setCameraMotion(identity);
    const std::vector<std::byte> before =
        runner.run(color, depth, normals, projection, kFrameSeconds);
    const std::array<int, 4> beforePixel =
        pixelAt(before, kSize, kShadowedColumn, kFloorRow);

    // The occluder appears. One frame is only part of the way to its shadow.
    renderDepthScene(*device, depth, depthFormat, true);
    effect.setCameraMotion(identity);
    const std::vector<std::byte> second =
        runner.run(color, depth, normals, projection, kFrameSeconds);
    const std::array<int, 4> secondPixel =
        pixelAt(second, kSize, kShadowedColumn, kFloorRow);

    // The full single-frame shadow of the same scene.
    const std::vector<std::byte> full = singleFrame(*device, color, depth, normals, projection);
    const std::array<int, 4> fullPixel = pixelAt(full, kSize, kShadowedColumn, kFloorRow);

    INFO("log: " << joined(sink));
    // The open floor is unchanged, and the occluded floor is clearly darker.
    CHECK(std::abs(beforePixel[0] - 128) <= 1);
    REQUIRE(fullPixel[0] < beforePixel[0] - 10);

    // The second frame is strictly between no shadow and the full shadow.
    INFO("appearing occluder: " << beforePixel[0] << " before, " << secondPixel[0]
        << " after one frame, " << fullPixel[0] << " full");
    CHECK(secondPixel[0] < beforePixel[0]);
    CHECK(secondPixel[0] > fullPixel[0]);

    // After a fifth of a second at 1/60 s the shadow is within one level of full.
    std::vector<std::byte> converged = second;
    for ( int frame = 0; frame < 20; ++frame )
    {
        effect.setCameraMotion(identity);
        converged = runner.run(color, depth, normals, projection, kFrameSeconds);
    }
    const std::array<int, 4> convergedPixel =
        pixelAt(converged, kSize, kShadowedColumn, kFloorRow);
    INFO("appearing occluder: " << convergedPixel[0] << " after 21 frames, " << fullPixel[0]
        << " full");
    CHECK(std::abs(convergedPixel[0] - fullPixel[0]) <= 1);
    CHECK(sink.records().empty());

    device->destroyTexture(color);
    device->destroyTexture(normals);
    device->destroyTexture(depth);
}

// Test 3: an invalid camera motion or resetHistory() takes the current mask at
// once, with no shadow from the previous frames.
TEST_CASE("Contact shadow history drops on an invalid motion or a reset (GPU)")
{
    babylon::render::test::keepGpuDriverLoaded();
    babylon::log::CaptureSink sink;
    const std::unique_ptr<Device> device = createSdlGpuDevice(nullptr);
    if ( device == nullptr )
    {
        babylon::render::test::noGpuDevice("no GPU device: " + joined(sink));
        return;
    }

    TextureFormat depthFormat = TextureFormat::D32F;
    if ( !chooseDepthFormat(*device, &depthFormat) )
    {
        babylon::render::test::noGpuDevice("no sampled depth format: " + joined(sink));
        return;
    }

    const TextureHandle color   = makeHalfColor(*device, kSize, kSize);
    const TextureHandle normals = makeNormals(*device, kSize, kSize);
    const TextureHandle depth   = device->createTexture(TextureDesc{ kSize, kSize, 1,
        depthFormat, TextureUsage::DepthTarget | TextureUsage::Sampled });
    REQUIRE_FALSE(depth.isNull());
    const Projection projection = testProjection();

    ContactShadowEffect effect;
    configureEffect(effect, kHistoryHalfLife);
    HistoryRunner runner(*device, effect);
    CameraMotion identity;
    identity.valid = true;

    // A history of no shadow, then an occluder with an invalid motion.
    renderDepthScene(*device, depth, depthFormat, false);
    effect.setCameraMotion(identity);
    (void)runner.run(color, depth, normals, projection, kFrameSeconds);
    renderDepthScene(*device, depth, depthFormat, true);
    CameraMotion invalid;
    effect.setCameraMotion(invalid);
    const std::vector<std::byte> invalidFrame =
        runner.run(color, depth, normals, projection, kFrameSeconds);
    const std::vector<std::byte> full = singleFrame(*device, color, depth, normals, projection);
    INFO("log: " << joined(sink));
    // The temporal target is Rgba16F, so the copy through it can round by one level.
    INFO("invalid motion: max channel difference " << maxChannelDifference(invalidFrame, full));
    CHECK(maxChannelDifference(invalidFrame, full) <= 1);

    // The open floor again, with the occluder's shadow still in the history: a
    // blended frame keeps part of it, a reset frame takes the current mask.
    renderDepthScene(*device, depth, depthFormat, false);
    effect.setCameraMotion(identity);
    const std::vector<std::byte> blended =
        runner.run(color, depth, normals, projection, kFrameSeconds);
    effect.resetHistory();
    effect.setCameraMotion(identity);
    const std::vector<std::byte> resetFrame =
        runner.run(color, depth, normals, projection, kFrameSeconds);
    const std::vector<std::byte> openFull = singleFrame(*device, color, depth, normals,
        projection);
    const std::array<int, 4> blendedPixel = pixelAt(blended, kSize, kShadowedColumn, kFloorRow);
    const std::array<int, 4> resetPixel = pixelAt(resetFrame, kSize, kShadowedColumn, kFloorRow);
    const std::array<int, 4> openPixel = pixelAt(openFull, kSize, kShadowedColumn, kFloorRow);
    INFO("reset: " << blendedPixel[0] << " blended, " << resetPixel[0] << " reset, "
        << openPixel[0] << " open");
    CHECK(blendedPixel[0] < openPixel[0] - 1);
    CHECK(maxChannelDifference(resetFrame, openFull) <= 1);
    CHECK(sink.records().empty());

    device->destroyTexture(color);
    device->destroyTexture(normals);
    device->destroyTexture(depth);
}

// Test 4: the history follows the camera. With the right motion the moved
// shadow matches the new view; an identity motion leaves a ghost.
TEST_CASE("Contact shadow history reprojects through the camera move (GPU)")
{
    babylon::render::test::keepGpuDriverLoaded();
    babylon::log::CaptureSink sink;
    const std::unique_ptr<Device> device = createSdlGpuDevice(nullptr);
    if ( device == nullptr )
    {
        babylon::render::test::noGpuDevice("no GPU device: " + joined(sink));
        return;
    }

    TextureFormat depthFormat = TextureFormat::D32F;
    if ( !chooseDepthFormat(*device, &depthFormat) )
    {
        babylon::render::test::noGpuDevice("no sampled depth format: " + joined(sink));
        return;
    }

    const TextureHandle color   = makeHalfColor(*device, kSize, kSize);
    const TextureHandle normals = makeNormals(*device, kSize, kSize);
    const TextureHandle depth   = device->createTexture(TextureDesc{ kSize, kSize, 1,
        depthFormat, TextureUsage::DepthTarget | TextureUsage::Sampled });
    REQUIRE_FALSE(depth.isNull());
    const Projection projection = testProjection();

    // The camera slides right by dx, so the static box appears shifted left by
    // dx in view space. The world point that a current pixel sees was at
    // x + dx in the previous view.
    constexpr float kMoveX = 0.012f;
    CameraMotion correctMotion;
    correctMotion.currentToPrevious[3] = kMoveX;
    correctMotion.valid = true;
    CameraMotion identityMotion;
    identityMotion.valid = true;

    ContactShadowEffect correctEffect;
    configureEffect(correctEffect, kHistoryHalfLife);
    ContactShadowEffect ghostEffect;
    configureEffect(ghostEffect, kHistoryHalfLife);
    HistoryRunner correctRunner(*device, correctEffect);
    HistoryRunner ghostRunner(*device, ghostEffect);

    // Both start on the old view. There is no history yet, so this frame only
    // records the mask.
    renderDepthScene(*device, depth, depthFormat, true, 0.0f);
    CameraMotion noMotion;
    correctEffect.setCameraMotion(noMotion);
    ghostEffect.setCameraMotion(noMotion);
    (void)correctRunner.run(color, depth, normals, projection, kFrameSeconds);
    (void)ghostRunner.run(color, depth, normals, projection, kFrameSeconds);

    // The new view. The correct motion reprojects, the identity one does not.
    renderDepthScene(*device, depth, depthFormat, true, -kMoveX);
    correctEffect.setCameraMotion(correctMotion);
    ghostEffect.setCameraMotion(identityMotion);
    const std::vector<std::byte> correct =
        correctRunner.run(color, depth, normals, projection, kFrameSeconds);
    const std::vector<std::byte> ghost =
        ghostRunner.run(color, depth, normals, projection, kFrameSeconds);

    // The single-frame shadow of the new view.
    const std::vector<std::byte> full = singleFrame(*device, color, depth, normals, projection);

    const ErrorStats correctError = imageError(correct, full, 8, 8, kSize - 8, kSize - 8);
    const ErrorStats ghostError = imageError(ghost, full, 8, 8, kSize - 8, kSize - 8);
    std::printf("contact shadow history: reprojected %.3f mean / %d max / %d over 4, identity "
        "%.3f mean / %d max / %d over 4\n", correctError.mean, correctError.max,
        correctError.countAboveFour, ghostError.mean, ghostError.max, ghostError.countAboveFour);
    INFO("log: " << joined(sink));

    // The reprojected history matches the new view; the identity history keeps a
    // ghost of the old shadow position, which proves the reprojection matters.
    // The pixels over 4 are the shadow's soft edge, where a sub-pixel shift of
    // the fixed screen jitter tile changes the antialiased value; the ghost
    // instead lifts a whole band well away from the new shadow.
    CHECK(correctError.mean < 1.0);
    CHECK(ghostError.mean > correctError.mean + 0.5);
    CHECK(ghostError.countAboveFour > correctError.countAboveFour + 100);
    CHECK(sink.records().empty());

    device->destroyTexture(color);
    device->destroyTexture(normals);
    device->destroyTexture(depth);
}

// Test 5: a pixel whose surface changed depth a lot rejects the history and
// takes the current mask at once.
TEST_CASE("Contact shadow history rejects a disoccluded pixel (GPU)")
{
    babylon::render::test::keepGpuDriverLoaded();
    babylon::log::CaptureSink sink;
    const std::unique_ptr<Device> device = createSdlGpuDevice(nullptr);
    if ( device == nullptr )
    {
        babylon::render::test::noGpuDevice("no GPU device: " + joined(sink));
        return;
    }

    TextureFormat depthFormat = TextureFormat::D32F;
    if ( !chooseDepthFormat(*device, &depthFormat) )
    {
        babylon::render::test::noGpuDevice("no sampled depth format: " + joined(sink));
        return;
    }

    const TextureHandle color   = makeHalfColor(*device, kSize, kSize);
    const TextureHandle normals = makeNormals(*device, kSize, kSize);
    const TextureHandle depth   = device->createTexture(TextureDesc{ kSize, kSize, 1,
        depthFormat, TextureUsage::DepthTarget | TextureUsage::Sampled });
    REQUIRE_FALSE(depth.isNull());
    const Projection projection = testProjection();

    ContactShadowEffect effect;
    configureEffect(effect, kHistoryHalfLife);
    HistoryRunner runner(*device, effect);
    CameraMotion identity;
    identity.valid = true;

    // The floor pixel beside the box is shadowed, and that shadow enters the
    // history.
    renderDepthScene(*device, depth, depthFormat, true, 0.0f);
    effect.setCameraMotion(identity);
    const std::vector<std::byte> first =
        runner.run(color, depth, normals, projection, kFrameSeconds);
    const std::array<int, 4> firstPixel = pixelAt(first, kSize, kShadowedColumn, kFloorRow);
    REQUIRE(firstPixel[0] < 118);

    // The box slides over that pixel, so its surface is now much nearer. The
    // frame must take the new surface's mask, not blend the old shadow onto it.
    renderDepthScene(*device, depth, depthFormat, true, -0.09f);
    effect.setCameraMotion(identity);
    const std::vector<std::byte> second =
        runner.run(color, depth, normals, projection, kFrameSeconds);
    const std::vector<std::byte> full = singleFrame(*device, color, depth, normals, projection);
    const std::array<int, 4> secondPixel = pixelAt(second, kSize, kShadowedColumn, kFloorRow);
    const std::array<int, 4> fullPixel = pixelAt(full, kSize, kShadowedColumn, kFloorRow);
    INFO("log: " << joined(sink));
    INFO("disocclusion: " << firstPixel[0] << " shadowed, " << secondPixel[0] << " after, "
        << fullPixel[0] << " single frame");
    CHECK(std::abs(secondPixel[0] - fullPixel[0]) <= 1);
    CHECK(sink.records().empty());

    device->destroyTexture(color);
    device->destroyTexture(normals);
    device->destroyTexture(depth);
}
