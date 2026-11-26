// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/postfx/Chain.h>
#include <babylon/postfx/ContactShadowEffect.h>
#include <babylon/postfx/FrameInputs.h>

#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/NullDevice.h>
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

    // The cbuffer of shaders/contact_shadow_mask.frag.hlsl, byte for byte.
    struct alignas(16) ExpectedContactShadowMaskUniforms
    {
        float lightDirection[3];
        float length;
        float thickness;
        std::uint32_t steps;
        std::uint32_t receiverClasses;
        float fadeStart;
        float fadeEnd;
        float nearPlane;
        float farPlane;
        float depthNear;
        float depthFar;
        float tanHalfFovX;
        float tanHalfFovY;
        float inputWidth;
        float inputHeight;
        float padding[3];
    };
    static_assert(sizeof(ExpectedContactShadowMaskUniforms) == 80);

    // The cbuffer of shaders/contact_shadow_blur.frag.hlsl, byte for byte.
    struct alignas(16) ExpectedContactShadowBlurUniforms
    {
        float nearPlane;
        float farPlane;
        float depthNear;
        float depthFar;
        float inputWidth;
        float inputHeight;
        float padding[2];
    };
    static_assert(sizeof(ExpectedContactShadowBlurUniforms) == 32);

    // The cbuffer of shaders/contact_shadow_composite.frag.hlsl, byte for byte.
    struct alignas(16) ExpectedContactShadowCompositeUniforms
    {
        float strength;
        std::uint32_t receiverClasses;
        float depthNear;
        float depthFar;
    };
    static_assert(sizeof(ExpectedContactShadowCompositeUniforms) == 16);

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

TEST_CASE("Contact shadow settings clamp and control active and needs")
{
    ContactShadowEffect effect;
    CHECK(effect.name() == "contact shadows");
    CHECK(effect.stage() == Stage::Hdr);
    CHECK(effect.active());
    CHECK(effect.settings().strength == doctest::Approx(0.7f));
    CHECK(effect.settings().length == doctest::Approx(0.2f));
    CHECK(effect.settings().thickness == doctest::Approx(0.04f));
    CHECK(effect.settings().steps == 16);
    CHECK(effect.settings().receiverClasses == 0x8u);
    CHECK(effect.settings().fadeStart == doctest::Approx(30.0f));
    CHECK(effect.settings().fadeEnd == doctest::Approx(60.0f));
    CHECK(effect.needs().depth);
    CHECK(effect.needs().normals);

    ContactShadowSettings wide;
    wide.strength        = 2.0f;
    wide.length          = -1.0f;
    wide.thickness       = 0.0f;
    wide.steps           = 100;
    wide.receiverClasses = 0xFu;
    wide.fadeStart       = 50.0f;
    wide.fadeEnd         = 10.0f;
    effect.setSettings(wide);
    CHECK(effect.settings().strength == 1.0f);
    CHECK(effect.settings().length > 0.0f);
    CHECK(effect.settings().length == doctest::Approx(0.0001f));
    CHECK(effect.settings().thickness > 0.0f);
    CHECK(effect.settings().thickness == doctest::Approx(0.0001f));
    CHECK(effect.settings().steps == 32);
    CHECK(effect.settings().receiverClasses == 0xFu);
    CHECK(effect.settings().fadeStart == doctest::Approx(50.0f));
    CHECK(effect.settings().fadeEnd == doctest::Approx(50.0f));
    CHECK(effect.needs().depth);
    CHECK_FALSE(effect.needs().normals);

    ContactShadowSettings narrow;
    narrow.strength        = -1.0f;
    narrow.length          = 0.2f;
    narrow.thickness       = 0.05f;
    narrow.steps           = 1;
    narrow.receiverClasses = 0x1u;
    narrow.fadeStart       = 10.0f;
    narrow.fadeEnd         = 20.0f;
    effect.setSettings(narrow);
    CHECK(effect.settings().strength == 0.0f);
    CHECK_FALSE(effect.active());
    CHECK(effect.settings().steps == 4);
    CHECK(effect.settings().fadeEnd == doctest::Approx(20.0f));
    CHECK(effect.needs().normals);
}

TEST_CASE("A zero light direction is refused with one warning")
{
    babylon::log::CaptureSink sink;
    ContactShadowEffect effect;
    effect.setLightDirection(3.0f, 4.0f, 0.0f);
    CHECK(sink.records().empty());

    effect.setLightDirection(0.0f, 0.0f, 0.0f);
    REQUIRE(sink.records().size() == 1);
    CHECK(sink.records()[0].level == babylon::log::Level::Warning);
}

TEST_CASE("Releasing contact shadow resources before they exist is safe")
{
    ContactShadowEffect effect;
    effect.releaseResources();
    effect.releaseResources();
}

TEST_CASE("The chain skips contact shadows until the frame has depth and normals")
{
    NullDevice device;
    ContactShadowEffect effect;
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
    CHECK(passRecords(device.recorded()).size() == 4);
    checkNames(chain, { "contact shadows", "copy" });
}

TEST_CASE("The contact shadow effect records mask, blur and composite passes")
{
    NullDevice device;
    ContactShadowEffect effect;
    Chain chain(device);
    REQUIRE(chain.open());

    ContactShadowSettings settings;
    settings.strength        = 0.75f;
    settings.length          = 0.2f;
    settings.thickness       = 0.04f;
    settings.steps           = 12;
    settings.receiverClasses = 0x8u;
    settings.fadeStart       = 12.0f;
    settings.fadeEnd         = 24.0f;
    effect.setSettings(settings);
    effect.setLightDirection(3.0f, 4.0f, 0.0f);
    REQUIRE(chain.add(effect));

    const TextureHandle color   = makeColor(device, 16, 16);
    const TextureHandle depth   = makeDepth(device, 16, 16);
    const TextureHandle normals = makeNormals(device, 16, 16);
    const TextureHandle target  = makeOutput(device, 16, 16);
    const FrameInputs frame     = makeFrame(color, depth, normals, 16, 16);
    const OutputTarget output   = makeOutputTarget(target, 16, 16);

    recordChain(chain, device, frame, output);

    // Mask into scratch A, blur A into scratch B, composite B into the effect's
    // output, then the built-in copy into the caller's output.
    const std::vector<PassRecord> passes = passRecords(device.recorded());
    REQUIRE(passes.size() == 4);
    const TextureHandle maskTarget = passes[0].target;
    const TextureHandle blurTarget = passes[1].target;
    CHECK_FALSE(maskTarget.isNull());
    CHECK_FALSE(blurTarget.isNull());
    CHECK(maskTarget != blurTarget);
    CHECK(maskTarget != passes[2].target);
    CHECK(blurTarget != passes[2].target);
    CHECK(passes[2].target != target);
    CHECK(passes[3].target == target);

    // Every pass binds its textures to fragment slots 0..n-1 with the nearest
    // clamped sampler.
    for ( std::size_t passIndex = 0; passIndex < 3; ++passIndex )
    {
        const PassRecord& pass = passes[passIndex];
        for ( std::size_t slot = 0; slot < pass.textures.size(); ++slot )
        {
            const BindTexture& bind = pass.textures[slot];
            CHECK(bind.stage == ShaderStage::Fragment);
            CHECK(bind.slot == slot);
            const std::optional<SamplerDesc> sampler = device.samplerDesc(bind.sampler);
            REQUIRE(sampler.has_value());
            CHECK(sampler->minFilter == Filter::Nearest);
            CHECK(sampler->magFilter == Filter::Nearest);
            CHECK(sampler->addressU == AddressMode::ClampToEdge);
            CHECK(sampler->addressV == AddressMode::ClampToEdge);
        }
    }

    // Mask: depth and normals in, the shadow amount out.
    REQUIRE(passes[0].textures.size() == 2);
    CHECK(passes[0].textures[0].texture == depth);
    CHECK(passes[0].textures[1].texture == normals);
    REQUIRE(passes[0].uniforms.size() == 1);
    const PushUniforms& maskPush = passes[0].uniforms.front();
    CHECK(maskPush.stage == ShaderStage::Fragment);
    CHECK(maskPush.slot == 0);
    const ExpectedContactShadowMaskUniforms expectedMask{
        { 0.6f, 0.8f, 0.0f },
        0.2f, 0.04f, 12u, 0x8u, 12.0f, 24.0f,
        0.2f, 500.0f, 0.05f, 0.95f, 1.2f, 0.8f, 16.0f, 16.0f,
        { 0.0f, 0.0f, 0.0f } };
    CHECK(maskPush.data == bytesOf(expectedMask));

    // Blur: the mask and the depth that weights its taps in.
    REQUIRE(passes[1].textures.size() == 2);
    CHECK(passes[1].textures[0].texture == maskTarget);
    CHECK(passes[1].textures[1].texture == depth);
    REQUIRE(passes[1].uniforms.size() == 1);
    const PushUniforms& blurPush = passes[1].uniforms.front();
    CHECK(blurPush.stage == ShaderStage::Fragment);
    CHECK(blurPush.slot == 0);
    const ExpectedContactShadowBlurUniforms expectedBlur{
        0.2f, 500.0f, 0.05f, 0.95f, 16.0f, 16.0f, { 0.0f, 0.0f } };
    CHECK(blurPush.data == bytesOf(expectedBlur));

    // Composite: depth and normals in the mask pass's slots, then the input colour
    // and the blurred mask.
    REQUIRE(passes[2].textures.size() == 4);
    CHECK(passes[2].textures[0].texture == depth);
    CHECK(passes[2].textures[1].texture == normals);
    CHECK(passes[2].textures[2].texture == color);
    CHECK(passes[2].textures[3].texture == blurTarget);
    REQUIRE(passes[2].uniforms.size() == 1);
    const PushUniforms& compositePush = passes[2].uniforms.front();
    CHECK(compositePush.stage == ShaderStage::Fragment);
    CHECK(compositePush.slot == 0);
    const ExpectedContactShadowCompositeUniforms expectedComposite{
        0.75f, 0x8u, 0.05f, 0.95f };
    CHECK(compositePush.data == bytesOf(expectedComposite));

    for ( std::size_t index = 0; index < 3; ++index )
    {
        REQUIRE(passes[index].draws.size() == 1);
        CHECK(passes[index].draws.front().vertexCount == 3);
        CHECK(passes[index].draws.front().firstVertex == 0);
    }

    checkNames(chain, { "contact shadows", "copy" });

    effect.releaseResources();
    effect.releaseResources();
    chain.close();
}

TEST_CASE("With every receiver class the mask pass binds depth in both texture slots")
{
    NullDevice device;
    ContactShadowEffect effect;
    Chain chain(device);
    REQUIRE(chain.open());

    ContactShadowSettings settings;
    settings.receiverClasses = 0xFu;
    effect.setSettings(settings);
    REQUIRE(chain.add(effect));

    const TextureHandle color  = makeColor(device, 16, 16);
    const TextureHandle depth  = makeDepth(device, 16, 16);
    const TextureHandle target = makeOutput(device, 16, 16);
    const FrameInputs frame    = makeFrame(color, depth, {}, 16, 16);
    const OutputTarget output  = makeOutputTarget(target, 16, 16);

    recordChain(chain, device, frame, output);

    const std::vector<PassRecord> passes = passRecords(device.recorded());
    REQUIRE(passes.size() == 4);
    REQUIRE(passes[0].textures.size() == 2);
    CHECK(passes[0].textures[0].texture == depth);
    CHECK(passes[0].textures[1].texture == depth);
    REQUIRE(passes[2].textures.size() == 4);
    CHECK(passes[2].textures[0].texture == depth);
    CHECK(passes[2].textures[1].texture == depth);
    CHECK(passes[2].textures[2].texture == color);
    checkNames(chain, { "contact shadows", "copy" });
}
