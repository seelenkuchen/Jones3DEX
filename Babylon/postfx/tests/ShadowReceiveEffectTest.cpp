// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/postfx/Chain.h>
#include <babylon/postfx/FrameInputs.h>
#include <babylon/postfx/ShadowReceiveEffect.h>

#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/NullDevice.h>
#include <babylon/render/Sampler.h>
#include <babylon/render/Texture.h>

#include <babylon/log/CaptureSink.h>

#include <doctest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace
{
    using namespace babylon::postfx;
    using namespace babylon::render;

    template <typename T>
    std::vector<std::byte> bytesOf(const T& value)
    {
        const std::span<const std::byte> bytes = std::as_bytes(std::span(&value, 1));
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    }

    // The cbuffer of shaders/shadow_receive.frag.hlsl, byte for byte.
    struct alignas(16) ExpectedShadowReceiveUniforms
    {
        float matrix[16];
        float towardLight[3];
        float strength;
        float normalOffset;
        float depthBias;
        float filterRadius;
        float mapSize;
        std::uint32_t receiverClasses;
        float fadeStart;
        float fadeEnd;
        float nearPlane;
        float farPlane;
        float depthNear;
        float depthFar;
        float tanHalfFovX;
        float tanHalfFovY;
        std::uint32_t hasExcludeMask;
        float penumbra;
        float penumbraPerDistance;
        float distanceFadeStart;
        float distanceFadeEnd;
        float minLight;
        float fullLight;
        float padding[4];
    };
    static_assert(sizeof(ExpectedShadowReceiveUniforms) == 176);

    struct PassRecord
    {
        TextureHandle target;
        std::vector<BindTexture> textures;
        std::vector<PushUniforms> uniforms;
        std::vector<Draw> draws;
    };

    // Each BeginPass and the commands recorded in it, in submit order.
    std::vector<PassRecord> passRecords(const std::vector<RecordedCommand>& record)
    {
        std::vector<PassRecord> passes;
        for ( const RecordedCommand& command : record )
        {
            if ( const auto* pBegin = std::get_if<BeginPass>(&command) )
            {
                PassRecord pass;
                if ( !pBegin->desc.colorTargets.empty() )
                {
                    pass.target = pBegin->desc.colorTargets.front().target;
                }
                passes.push_back(pass);
            }
            else if ( const auto* pBind = std::get_if<BindTexture>(&command) )
            {
                if ( !passes.empty() )
                {
                    passes.back().textures.push_back(*pBind);
                }
            }
            else if ( const auto* pPush = std::get_if<PushUniforms>(&command) )
            {
                if ( !passes.empty() )
                {
                    passes.back().uniforms.push_back(*pPush);
                }
            }
            else if ( const auto* pDraw = std::get_if<Draw>(&command) )
            {
                if ( !passes.empty() )
                {
                    passes.back().draws.push_back(*pDraw);
                }
            }
        }
        return passes;
    }

    void checkNames(const Chain& chain, std::initializer_list<std::string_view> expected)
    {
        const std::vector<std::string_view>& run = chain.lastRun();
        REQUIRE(run.size() == expected.size());
        std::size_t index = 0;
        for ( const std::string_view name : expected )
        {
            CHECK(run[index] == name);
            ++index;
        }
    }

    TextureHandle makeColor(NullDevice& device, std::uint32_t width, std::uint32_t height)
    {
        return device.createTexture(TextureDesc{ width, height, 1, TextureFormat::Rgba16F,
            TextureUsage::Sampled | TextureUsage::ColorTarget });
    }

    TextureHandle makeDepth(NullDevice& device, std::uint32_t width, std::uint32_t height)
    {
        return device.createTexture(TextureDesc{ width, height, 1, TextureFormat::D32F,
            TextureUsage::DepthTarget | TextureUsage::Sampled });
    }

    TextureHandle makeNormals(NullDevice& device, std::uint32_t width, std::uint32_t height)
    {
        return device.createTexture(TextureDesc{ width, height, 1, TextureFormat::Rgba8,
            TextureUsage::Sampled });
    }

    TextureHandle makeOutput(NullDevice& device, std::uint32_t width, std::uint32_t height)
    {
        return device.createTexture(TextureDesc{ width, height, 1, TextureFormat::Rgba8,
            TextureUsage::ColorTarget });
    }

    TextureHandle makeShadowMap(NullDevice& device, std::uint32_t size)
    {
        return device.createTexture(TextureDesc{ size, size, 1, TextureFormat::D32F,
            TextureUsage::DepthTarget | TextureUsage::Sampled });
    }

    TextureHandle makeExcludeMask(NullDevice& device, std::uint32_t width, std::uint32_t height)
    {
        return device.createTexture(TextureDesc{ width, height, 1, TextureFormat::Rgba8,
            TextureUsage::Sampled });
    }

    SamplerHandle makeCompareSampler(NullDevice& device)
    {
        SamplerDesc desc;
        desc.minFilter = Filter::Linear;
        desc.magFilter = Filter::Linear;
        desc.mipFilter = Filter::Nearest;
        desc.addressU  = AddressMode::ClampToEdge;
        desc.addressV  = AddressMode::ClampToEdge;
        desc.compare   = CompareOp::LessOrEqual;
        return device.createSampler(desc);
    }

    ShadowMapInput makeMap(TextureHandle texture, SamplerHandle sampler)
    {
        ShadowMapInput map;
        map.texture        = texture;
        map.compareSampler = sampler;
        map.mapSize        = 256;
        for ( int index = 0; index < 16; ++index )
        {
            map.matrix[index] = static_cast<float>(index + 1);
        }
        map.towardLight[0] = 0.0f;
        map.towardLight[1] = 1.0f;
        map.towardLight[2] = 0.0f;
        return map;
    }

    void recordChain(Chain& chain, NullDevice& device, const FrameInputs& frame,
        const OutputTarget& output)
    {
        device.clearRecorded();
        CommandList list = device.beginFrame();
        REQUIRE(chain.record(list, frame, output));
        REQUIRE(device.submit(list));
    }

    FrameInputs makeFrame(TextureHandle color, TextureHandle depth, TextureHandle normals,
        std::uint32_t width, std::uint32_t height)
    {
        FrameInputs frame;
        frame.color       = color;
        frame.colorFormat = TextureFormat::Rgba16F;
        frame.depth       = depth;
        frame.normals     = normals;
        frame.width       = width;
        frame.height      = height;
        frame.projection.nearPlane   = 0.2f;
        frame.projection.farPlane    = 500.0f;
        frame.projection.tanHalfFovX = 1.2f;
        frame.projection.tanHalfFovY = 0.8f;
        frame.projection.depthNear   = 0.05f;
        frame.projection.depthFar    = 0.95f;
        return frame;
    }

    OutputTarget makeOutputTarget(TextureHandle target, std::uint32_t width, std::uint32_t height)
    {
        OutputTarget output;
        output.texture = target;
        output.format  = TextureFormat::Rgba8;
        output.width   = width;
        output.height  = height;
        return output;
    }
}

TEST_CASE("Shadow receive settings clamp and control active and needs")
{
    ShadowReceiveEffect effect;
    CHECK(effect.name() == "shadow receive");
    CHECK(effect.stage() == Stage::Hdr);
    CHECK_FALSE(effect.active());
    CHECK(effect.settings().strength == doctest::Approx(0.55f));
    CHECK(effect.settings().normalOffset == doctest::Approx(0.004f));
    CHECK(effect.settings().depthBias == doctest::Approx(0.0015f));
    CHECK(effect.settings().filterRadius == doctest::Approx(1.5f));
    CHECK(effect.settings().receiverClasses == 0x8u);
    CHECK(effect.settings().fadeStart == doctest::Approx(20.0f));
    CHECK(effect.settings().fadeEnd == doctest::Approx(40.0f));
    CHECK(effect.settings().penumbra == 0.0f);
    CHECK(effect.settings().penumbraPerDistance == 0.0f);
    CHECK(effect.settings().distanceFadeStart == 0.0f);
    CHECK(effect.settings().distanceFadeEnd == 0.0f);
    CHECK(effect.settings().minLight == 0.0f);
    CHECK(effect.settings().fullLight == 0.0f);
    CHECK(effect.needs().depth);
    CHECK(effect.needs().normals);

    ShadowReceiveSettings wide;
    wide.strength             = 2.0f;
    wide.normalOffset         = -1.0f;
    wide.depthBias            = -1.0f;
    wide.filterRadius         = 100.0f;
    wide.receiverClasses      = 0xFu;
    wide.fadeStart            = 50.0f;
    wide.fadeEnd              = 10.0f;
    wide.penumbra             = -0.1f;
    wide.penumbraPerDistance  = -0.2f;
    wide.distanceFadeStart    = -0.3f;
    wide.distanceFadeEnd      = -0.4f;
    wide.minLight             = -0.5f;
    wide.fullLight            = -0.6f;
    effect.setSettings(wide);
    CHECK(effect.settings().strength == 1.0f);
    CHECK(effect.settings().normalOffset == 0.0f);
    CHECK(effect.settings().depthBias == 0.0f);
    CHECK(effect.settings().filterRadius == doctest::Approx(4.0f));
    CHECK(effect.settings().receiverClasses == 0xFu);
    CHECK(effect.settings().fadeStart == doctest::Approx(50.0f));
    CHECK(effect.settings().fadeEnd == doctest::Approx(50.0f));
    CHECK(effect.settings().penumbra == 0.0f);
    CHECK(effect.settings().penumbraPerDistance == 0.0f);
    CHECK(effect.settings().distanceFadeStart == 0.0f);
    CHECK(effect.settings().distanceFadeEnd == 0.0f);
    CHECK(effect.settings().minLight == 0.0f);
    CHECK(effect.settings().fullLight == 0.0f);
    CHECK(effect.needs().depth);
    CHECK_FALSE(effect.needs().normals);

    ShadowReceiveSettings narrow;
    narrow.strength            = -1.0f;
    narrow.normalOffset        = 0.01f;
    narrow.depthBias           = 0.002f;
    narrow.filterRadius        = 0.1f;
    narrow.receiverClasses     = 0x1u;
    narrow.fadeStart           = 10.0f;
    narrow.fadeEnd             = 20.0f;
    narrow.penumbra            = 0.02f;
    narrow.penumbraPerDistance = 0.03f;
    narrow.distanceFadeStart   = 0.5f;
    narrow.distanceFadeEnd     = 1.5f;
    narrow.minLight            = 0.1f;
    narrow.fullLight           = 0.7f;
    effect.setSettings(narrow);
    CHECK(effect.settings().strength == 0.0f);
    CHECK_FALSE(effect.active());
    CHECK(effect.settings().filterRadius == doctest::Approx(0.5f));
    CHECK(effect.settings().fadeEnd == doctest::Approx(20.0f));
    CHECK(effect.settings().penumbra == doctest::Approx(0.02f));
    CHECK(effect.settings().penumbraPerDistance == doctest::Approx(0.03f));
    CHECK(effect.settings().distanceFadeStart == doctest::Approx(0.5f));
    CHECK(effect.settings().distanceFadeEnd == doctest::Approx(1.5f));
    CHECK(effect.settings().minLight == doctest::Approx(0.1f));
    CHECK(effect.settings().fullLight == doctest::Approx(0.7f));
    CHECK(effect.needs().normals);
}

TEST_CASE("The shadow receive effect writes the default soft-edge uniforms as zero")
{
    NullDevice device;
    ShadowReceiveEffect effect;
    Chain chain(device);
    REQUIRE(chain.open());

    effect.setShadowMap(makeMap(makeShadowMap(device, 256),
        makeCompareSampler(device)));
    REQUIRE(chain.add(effect));

    const TextureHandle color   = makeColor(device, 16, 16);
    const TextureHandle depth   = makeDepth(device, 16, 16);
    const TextureHandle normals = makeNormals(device, 16, 16);
    const TextureHandle target  = makeOutput(device, 16, 16);
    const FrameInputs frame     = makeFrame(color, depth, normals, 16, 16);
    const OutputTarget output   = makeOutputTarget(target, 16, 16);

    recordChain(chain, device, frame, output);

    const std::vector<PassRecord> passes = passRecords(device.recorded());
    REQUIRE(passes.size() == 2);
    REQUIRE(passes[0].uniforms.size() == 1);
    const ExpectedShadowReceiveUniforms expected{
        { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f,
            9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f },
        { 0.0f, 1.0f, 0.0f },
        0.55f, 0.004f, 0.0015f, 1.5f, 256.0f, 0x8u, 20.0f, 40.0f,
        0.2f, 500.0f, 0.05f, 0.95f, 1.2f, 0.8f, 0u,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f, 0.0f } };
    CHECK(passes[0].uniforms.front().data == bytesOf(expected));

    checkNames(chain, { "shadow receive", "copy" });
    chain.close();
}

TEST_CASE("Shadow receive is active only with strength and a valid map")
{
    NullDevice device;
    ShadowReceiveEffect effect;

    const TextureHandle texture = makeShadowMap(device, 256);
    const SamplerHandle sampler = makeCompareSampler(device);
    REQUIRE_FALSE(texture.isNull());
    REQUIRE_FALSE(sampler.isNull());

    const ShadowMapInput map = makeMap(texture, sampler);
    effect.setShadowMap(map);
    CHECK(effect.active());

    ShadowReceiveSettings off;
    off.strength = 0.0f;
    effect.setSettings(off);
    CHECK_FALSE(effect.active());

    effect.setSettings(ShadowReceiveSettings{});
    effect.clearShadowMap();
    CHECK_FALSE(effect.active());

    ShadowMapInput noTexture = map;
    noTexture.texture = {};
    effect.setShadowMap(noTexture);
    CHECK_FALSE(effect.active());

    ShadowMapInput noSampler = map;
    noSampler.compareSampler = {};
    effect.setShadowMap(noSampler);
    CHECK_FALSE(effect.active());

    ShadowMapInput noSize = map;
    noSize.mapSize = 0;
    effect.setShadowMap(noSize);
    CHECK_FALSE(effect.active());
}

TEST_CASE("The chain skips shadow receive until the frame has depth and normals")
{
    NullDevice device;
    ShadowReceiveEffect effect;
    effect.setShadowMap(makeMap(makeShadowMap(device, 256),
        makeCompareSampler(device)));
    Chain chain(device);
    REQUIRE(chain.open());
    REQUIRE(chain.add(effect));

    const TextureHandle color  = makeColor(device, 16, 16);
    const TextureHandle target = makeOutput(device, 16, 16);
    const OutputTarget output  = makeOutputTarget(target, 16, 16);

    FrameInputs frame = makeFrame(color, {}, {}, 16, 16);
    recordChain(chain, device, frame, output);
    CHECK(passRecords(device.recorded()).size() == 1);
    checkNames(chain, { "copy" });

    frame.depth = makeDepth(device, 16, 16);
    REQUIRE_FALSE(frame.depth.isNull());
    recordChain(chain, device, frame, output);
    CHECK(passRecords(device.recorded()).size() == 1);
    checkNames(chain, { "copy" });

    frame.normals = makeNormals(device, 16, 16);
    REQUIRE_FALSE(frame.normals.isNull());
    recordChain(chain, device, frame, output);
    CHECK(passRecords(device.recorded()).size() == 2);
    checkNames(chain, { "shadow receive", "copy" });
}

TEST_CASE("The shadow receive effect records one pass with five textures")
{
    NullDevice device;
    ShadowReceiveEffect effect;
    Chain chain(device);
    REQUIRE(chain.open());

    ShadowReceiveSettings settings;
    settings.strength            = 0.75f;
    settings.normalOffset        = 0.01f;
    settings.depthBias           = 0.002f;
    settings.filterRadius        = 2.0f;
    settings.receiverClasses     = 0x8u;
    settings.fadeStart           = 12.0f;
    settings.fadeEnd             = 24.0f;
    settings.penumbra            = 0.02f;
    settings.penumbraPerDistance = 0.03f;
    settings.distanceFadeStart   = 0.5f;
    settings.distanceFadeEnd     = 1.5f;
    settings.minLight            = 0.1f;
    settings.fullLight           = 0.7f;
    effect.setSettings(settings);

    const TextureHandle shadowMap = makeShadowMap(device, 256);
    const SamplerHandle compareSampler = makeCompareSampler(device);
    REQUIRE_FALSE(shadowMap.isNull());
    REQUIRE_FALSE(compareSampler.isNull());
    const ShadowMapInput map = makeMap(shadowMap, compareSampler);
    effect.setShadowMap(map);
    REQUIRE(chain.add(effect));

    const TextureHandle color   = makeColor(device, 16, 16);
    const TextureHandle depth   = makeDepth(device, 16, 16);
    const TextureHandle normals = makeNormals(device, 16, 16);
    const TextureHandle target  = makeOutput(device, 16, 16);
    const FrameInputs frame     = makeFrame(color, depth, normals, 16, 16);
    const OutputTarget output   = makeOutputTarget(target, 16, 16);

    recordChain(chain, device, frame, output);

    // The receive pass into a scratch target, then the built-in copy.
    const std::vector<PassRecord> passes = passRecords(device.recorded());
    REQUIRE(passes.size() == 2);
    CHECK(passes[0].target != target);
    CHECK(passes[1].target == target);

    // Colour, depth, normals, the shadow map and the exclude-mask fallback
    // (depth again) in slots 0..4. The colour, depth, normals and mask slots
    // use the nearest clamped sampler; slot 3 uses the map's comparison
    // sampler.
    REQUIRE(passes[0].textures.size() == 5);
    CHECK(passes[0].textures[0].texture == color);
    CHECK(passes[0].textures[1].texture == depth);
    CHECK(passes[0].textures[2].texture == normals);
    CHECK(passes[0].textures[3].texture == shadowMap);
    CHECK(passes[0].textures[4].texture == depth);
    for ( std::size_t slot = 0; slot < 3; ++slot )
    {
        const BindTexture& bind = passes[0].textures[slot];
        CHECK(bind.stage == ShaderStage::Fragment);
        CHECK(bind.slot == slot);
        const std::optional<SamplerDesc> sampler = device.samplerDesc(bind.sampler);
        REQUIRE(sampler.has_value());
        CHECK(sampler->minFilter == Filter::Nearest);
        CHECK(sampler->magFilter == Filter::Nearest);
        CHECK(sampler->addressU == AddressMode::ClampToEdge);
        CHECK(sampler->addressV == AddressMode::ClampToEdge);
    }
    const BindTexture& shadowBind = passes[0].textures[3];
    CHECK(shadowBind.stage == ShaderStage::Fragment);
    CHECK(shadowBind.slot == 3);
    CHECK(shadowBind.sampler == compareSampler);
    const std::optional<SamplerDesc> shadowSampler = device.samplerDesc(compareSampler);
    REQUIRE(shadowSampler.has_value());
    CHECK(shadowSampler->compare == CompareOp::LessOrEqual);
    const BindTexture& maskBind = passes[0].textures[4];
    CHECK(maskBind.stage == ShaderStage::Fragment);
    CHECK(maskBind.slot == 4);
    const std::optional<SamplerDesc> maskSampler = device.samplerDesc(maskBind.sampler);
    REQUIRE(maskSampler.has_value());
    CHECK(maskSampler->minFilter == Filter::Nearest);
    CHECK(maskSampler->magFilter == Filter::Nearest);
    CHECK(maskSampler->addressU == AddressMode::ClampToEdge);
    CHECK(maskSampler->addressV == AddressMode::ClampToEdge);

    REQUIRE(passes[0].uniforms.size() == 1);
    const PushUniforms& push = passes[0].uniforms.front();
    CHECK(push.stage == ShaderStage::Fragment);
    CHECK(push.slot == 0);
    const ExpectedShadowReceiveUniforms expected{
        { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f,
            9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f },
        { 0.0f, 1.0f, 0.0f },
        0.75f, 0.01f, 0.002f, 2.0f, 256.0f, 0x8u, 12.0f, 24.0f,
        0.2f, 500.0f, 0.05f, 0.95f, 1.2f, 0.8f, 0u,
        0.02f, 0.03f, 0.5f, 1.5f, 0.1f, 0.7f, { 0.0f, 0.0f, 0.0f, 0.0f } };
    CHECK(push.data == bytesOf(expected));

    REQUIRE(passes[0].draws.size() == 1);
    CHECK(passes[0].draws.front().vertexCount == 3);
    CHECK(passes[0].draws.front().firstVertex == 0);

    checkNames(chain, { "shadow receive", "copy" });

    effect.releaseResources();
    effect.releaseResources();
    chain.close();
}

TEST_CASE("With every receiver class the pass binds depth in the normals slot")
{
    NullDevice device;
    ShadowReceiveEffect effect;
    Chain chain(device);
    REQUIRE(chain.open());

    ShadowReceiveSettings settings;
    settings.receiverClasses = 0xFu;
    effect.setSettings(settings);
    effect.setShadowMap(makeMap(makeShadowMap(device, 256),
        makeCompareSampler(device)));
    REQUIRE(chain.add(effect));

    const TextureHandle color  = makeColor(device, 16, 16);
    const TextureHandle depth  = makeDepth(device, 16, 16);
    const TextureHandle target = makeOutput(device, 16, 16);
    const FrameInputs frame    = makeFrame(color, depth, {}, 16, 16);
    const OutputTarget output  = makeOutputTarget(target, 16, 16);

    recordChain(chain, device, frame, output);

    const std::vector<PassRecord> passes = passRecords(device.recorded());
    REQUIRE(passes.size() == 2);
    REQUIRE(passes[0].textures.size() == 5);
    CHECK(passes[0].textures[1].texture == depth);
    CHECK(passes[0].textures[2].texture == depth);
    CHECK(passes[0].textures[4].texture == depth);
    checkNames(chain, { "shadow receive", "copy" });
}

TEST_CASE("The shadow receive effect binds an exclude mask and flags it in the uniforms")
{
    NullDevice device;
    ShadowReceiveEffect effect;
    Chain chain(device);
    REQUIRE(chain.open());

    ShadowReceiveSettings settings;
    settings.strength        = 0.75f;
    settings.normalOffset    = 0.01f;
    settings.depthBias       = 0.002f;
    settings.filterRadius    = 2.0f;
    settings.receiverClasses = 0x8u;
    settings.fadeStart       = 12.0f;
    settings.fadeEnd         = 24.0f;
    effect.setSettings(settings);

    const TextureHandle excludeMask = makeExcludeMask(device, 16, 16);
    REQUIRE_FALSE(excludeMask.isNull());
    ShadowMapInput map = makeMap(makeShadowMap(device, 256),
        makeCompareSampler(device));
    map.excludeMask = excludeMask;
    effect.setShadowMap(map);
    REQUIRE(chain.add(effect));

    const TextureHandle color   = makeColor(device, 16, 16);
    const TextureHandle depth   = makeDepth(device, 16, 16);
    const TextureHandle normals = makeNormals(device, 16, 16);
    const TextureHandle target  = makeOutput(device, 16, 16);
    const FrameInputs frame     = makeFrame(color, depth, normals, 16, 16);
    const OutputTarget output   = makeOutputTarget(target, 16, 16);

    recordChain(chain, device, frame, output);

    const std::vector<PassRecord> passes = passRecords(device.recorded());
    REQUIRE(passes.size() == 2);
    REQUIRE(passes[0].textures.size() == 5);
    CHECK(passes[0].textures[4].texture == excludeMask);
    CHECK(passes[0].textures[4].stage == ShaderStage::Fragment);
    CHECK(passes[0].textures[4].slot == 4);
    const std::optional<SamplerDesc> maskSampler =
        device.samplerDesc(passes[0].textures[4].sampler);
    REQUIRE(maskSampler.has_value());
    CHECK(maskSampler->minFilter == Filter::Nearest);
    CHECK(maskSampler->magFilter == Filter::Nearest);
    CHECK(maskSampler->addressU == AddressMode::ClampToEdge);
    CHECK(maskSampler->addressV == AddressMode::ClampToEdge);

    REQUIRE(passes[0].uniforms.size() == 1);
    const PushUniforms& push = passes[0].uniforms.front();
    CHECK(push.stage == ShaderStage::Fragment);
    CHECK(push.slot == 0);
    const ExpectedShadowReceiveUniforms expected{
        { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f,
            9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f },
        { 0.0f, 1.0f, 0.0f },
        0.75f, 0.01f, 0.002f, 2.0f, 256.0f, 0x8u, 12.0f, 24.0f,
        0.2f, 500.0f, 0.05f, 0.95f, 1.2f, 0.8f, 1u,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f, 0.0f } };
    CHECK(push.data == bytesOf(expected));

    checkNames(chain, { "shadow receive", "copy" });
    chain.close();
}

TEST_CASE("The shadow receive mask mode selects include, and needs a mask to include")
{
    NullDevice device;
    ShadowReceiveEffect effect;
    Chain chain(device);
    REQUIRE(chain.open());

    ShadowReceiveSettings settings;
    settings.strength        = 0.75f;
    settings.normalOffset    = 0.01f;
    settings.depthBias       = 0.002f;
    settings.filterRadius    = 2.0f;
    settings.receiverClasses = 0x8u;
    settings.fadeStart       = 12.0f;
    settings.fadeEnd         = 24.0f;
    effect.setSettings(settings);

    const TextureHandle mask = makeExcludeMask(device, 16, 16);
    REQUIRE_FALSE(mask.isNull());
    ShadowMapInput map = makeMap(makeShadowMap(device, 256), makeCompareSampler(device));
    map.excludeMask = mask;
    map.maskMode    = MaskMode::Include;
    effect.setShadowMap(map);
    REQUIRE(chain.add(effect));

    const TextureHandle color   = makeColor(device, 16, 16);
    const TextureHandle depth   = makeDepth(device, 16, 16);
    const TextureHandle normals = makeNormals(device, 16, 16);
    const TextureHandle target  = makeOutput(device, 16, 16);
    const FrameInputs frame     = makeFrame(color, depth, normals, 16, 16);
    const OutputTarget output   = makeOutputTarget(target, 16, 16);

    recordChain(chain, device, frame, output);

    const std::vector<PassRecord> passes = passRecords(device.recorded());
    REQUIRE(passes.size() == 2);
    REQUIRE(passes[0].textures.size() == 5);
    CHECK(passes[0].textures[4].texture == mask);
    REQUIRE(passes[0].uniforms.size() == 1);
    const ExpectedShadowReceiveUniforms expected{
        { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f,
            9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f },
        { 0.0f, 1.0f, 0.0f },
        0.75f, 0.01f, 0.002f, 2.0f, 256.0f, 0x8u, 12.0f, 24.0f,
        0.2f, 500.0f, 0.05f, 0.95f, 1.2f, 0.8f, 2u,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f, 0.0f } };
    CHECK(passes[0].uniforms.front().data == bytesOf(expected));
    REQUIRE(passes[0].draws.size() == 1);
    checkNames(chain, { "shadow receive", "copy" });

    // Include without a mask selects no pixel: nothing can receive, so the
    // effect is inactive and the chain only copies the frame through.
    map.excludeMask = {};
    effect.setShadowMap(map);
    CHECK_FALSE(effect.active());
    recordChain(chain, device, frame, output);
    CHECK(passRecords(device.recorded()).size() == 1);
    checkNames(chain, { "copy" });

    chain.close();
}
