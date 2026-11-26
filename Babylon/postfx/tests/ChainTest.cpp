// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/postfx/Chain.h>
#include <babylon/postfx/Effect.h>
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
#include <string_view>
#include <variant>
#include <vector>

namespace
{
    using namespace babylon::postfx;
    using namespace babylon::render;

    // An effect that records one full-screen pass on its output and binds its input for the
    // checks. It can be made inactive or made to fail, and remembers releaseResources().
    class TestEffect final : public Effect
    {
    public:
        TestEffect(std::string_view name, Stage stage)
            : m_name(name)
            , m_stage(stage)
        {
        }

        std::string_view name() const override
        {
            return m_name;
        }

        Stage stage() const override
        {
            return m_stage;
        }

        Needs needs() const override
        {
            return m_needs;
        }

        bool active() const override
        {
            return m_active;
        }

        void releaseResources() override
        {
            m_released = true;
        }

        void setActive(bool active)
        {
            m_active = active;
        }

        void setFail(bool fail)
        {
            m_fail = fail;
        }

        void setNeeds(Needs needs)
        {
            m_needs = needs;
        }

        bool released() const
        {
            return m_released;
        }

    private:
        bool doRecord(PassContext& ctx) override
        {
            if ( m_fail )
            {
                return false;
            }

            PassDesc pass;
            ColorAttachment attachment;
            attachment.target = ctx.output;
            attachment.load   = LoadOp::DontCare;
            attachment.store  = StoreOp::Store;
            pass.colorTargets.push_back(attachment);
            if ( !ctx.list.beginPass(pass) )
            {
                return false;
            }
            if ( !ctx.input.isNull() )
            {
                ctx.list.bindTexture(ShaderStage::Fragment, 0, ctx.input,
                    ctx.resources.nearestClamp());
            }
            return ctx.list.endPass();
        }

        std::string_view m_name;
        Stage m_stage;
        bool m_active = true;
        bool m_fail = false;
        bool m_released = false;
        Needs m_needs;
    };

    struct PassInfo
    {
        TextureHandle target;
        std::vector<TextureHandle> textures;
    };

    // Each BeginPass and the textures bound in it, in submit order.
    std::vector<PassInfo> passInfos(const std::vector<RecordedCommand>& record)
    {
        std::vector<PassInfo> passes;
        for ( const RecordedCommand& command : record )
        {
            if ( const auto* pBegin = std::get_if<BeginPass>(&command) )
            {
                PassInfo info;
                if ( !pBegin->desc.colorTargets.empty() )
                {
                    info.target = pBegin->desc.colorTargets.front().target;
                }
                passes.push_back(info);
            }
            else if ( const auto* pBind = std::get_if<BindTexture>(&command) )
            {
                if ( !passes.empty() )
                {
                    passes.back().textures.push_back(pBind->texture);
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

    TextureHandle makeOutput(NullDevice& device, std::uint32_t width, std::uint32_t height)
    {
        return device.createTexture(TextureDesc{ width, height, 1, TextureFormat::Rgba8,
            TextureUsage::ColorTarget });
    }

    TextureHandle makeDepth(NullDevice& device, std::uint32_t width, std::uint32_t height)
    {
        return device.createTexture(TextureDesc{ width, height, 1, TextureFormat::D32F,
            TextureUsage::DepthTarget });
    }

    void recordChain(Chain& chain, NullDevice& device, const FrameInputs& frame,
        const OutputTarget& output)
    {
        device.clearRecorded();
        CommandList list = device.beginFrame();
        REQUIRE(chain.record(list, frame, output));
        REQUIRE(device.submit(list));
    }

    TEST_CASE("A chain with no effects copies the scene into the output")
    {
        NullDevice device;
        Chain chain(device);
        REQUIRE(chain.open());

        const TextureHandle color  = makeColor(device, 16, 16);
        const TextureHandle target = makeOutput(device, 16, 16);
        REQUIRE_FALSE(color.isNull());
        REQUIRE_FALSE(target.isNull());

        FrameInputs frame;
        frame.color       = color;
        frame.colorFormat = TextureFormat::Rgba16F;
        frame.width       = 16;
        frame.height      = 16;
        OutputTarget output;
        output.texture = target;
        output.format  = TextureFormat::Rgba8;
        output.width   = 16;
        output.height  = 16;

        recordChain(chain, device, frame, output);

        const std::vector<PassInfo> passes = passInfos(device.recorded());
        REQUIRE(passes.size() == 1);
        CHECK(passes[0].target == target);
        REQUIRE(passes[0].textures.size() == 1);
        CHECK(passes[0].textures.front() == color);
        checkNames(chain, { "copy" });
    }

    TEST_CASE("Hdr effects, the resolve and Ldr effects run in order")
    {
        NullDevice device;
        TestEffect hdrA("hdr-a", Stage::Hdr);
        TestEffect hdrB("hdr-b", Stage::Hdr);
        TestEffect ldrC("ldr-c", Stage::Ldr);
        Chain chain(device);
        REQUIRE(chain.open());

        CHECK(chain.add(hdrA));
        CHECK(chain.add(hdrB));
        CHECK(chain.add(ldrC));

        const TextureHandle color  = makeColor(device, 16, 16);
        const TextureHandle target = makeOutput(device, 16, 16);
        FrameInputs frame;
        frame.color       = color;
        frame.colorFormat = TextureFormat::Rgba16F;
        frame.width       = 16;
        frame.height      = 16;
        OutputTarget output;
        output.texture = target;
        output.format  = TextureFormat::Rgba8;
        output.width   = 16;
        output.height  = 16;

        recordChain(chain, device, frame, output);

        const std::vector<PassInfo> passes = passInfos(device.recorded());
        REQUIRE(passes.size() == 4);
        REQUIRE(passes[0].textures.size() == 1);
        REQUIRE(passes[1].textures.size() == 1);
        REQUIRE(passes[2].textures.size() == 1);
        REQUIRE(passes[3].textures.size() == 1);

        // Each pass reads what the previous pass wrote; the last writes the OutputTarget.
        CHECK(passes[0].textures.front() == color);
        CHECK(passes[1].textures.front() == passes[0].target);
        CHECK(passes[2].textures.front() == passes[1].target);
        CHECK(passes[3].textures.front() == passes[2].target);
        CHECK(passes[3].target == target);

        // The scene color is never an attachment.
        for ( const PassInfo& pass : passes )
        {
            CHECK(pass.target != color);
        }

        checkNames(chain, { "hdr-a", "hdr-b", "copy", "ldr-c" });
    }

    TEST_CASE("An inactive effect is skipped")
    {
        NullDevice device;
        TestEffect active("active", Stage::Hdr);
        TestEffect inactive("inactive", Stage::Hdr);
        Chain chain(device);
        REQUIRE(chain.open());

        inactive.setActive(false);
        CHECK(chain.add(active));
        CHECK(chain.add(inactive));

        const TextureHandle color  = makeColor(device, 16, 16);
        const TextureHandle target = makeOutput(device, 16, 16);
        FrameInputs frame;
        frame.color       = color;
        frame.colorFormat = TextureFormat::Rgba16F;
        frame.width       = 16;
        frame.height      = 16;
        OutputTarget output;
        output.texture = target;
        output.format  = TextureFormat::Rgba8;
        output.width   = 16;
        output.height  = 16;

        recordChain(chain, device, frame, output);

        CHECK(passInfos(device.recorded()).size() == 2);
        checkNames(chain, { "active", "copy" });
    }

    TEST_CASE("An effect that needs depth is skipped until the frame has it")
    {
        NullDevice device;
        TestEffect depthEffect("depth", Stage::Hdr);
        Chain chain(device);
        REQUIRE(chain.open());

        depthEffect.setNeeds(Needs{ true, false });
        CHECK(chain.add(depthEffect));

        const TextureHandle color  = makeColor(device, 16, 16);
        const TextureHandle target = makeOutput(device, 16, 16);
        FrameInputs frame;
        frame.color       = color;
        frame.colorFormat = TextureFormat::Rgba16F;
        frame.width       = 16;
        frame.height      = 16;
        OutputTarget output;
        output.texture = target;
        output.format  = TextureFormat::Rgba8;
        output.width   = 16;
        output.height  = 16;

        recordChain(chain, device, frame, output);
        CHECK(passInfos(device.recorded()).size() == 1);
        checkNames(chain, { "copy" });

        frame.depth = makeDepth(device, 16, 16);
        REQUIRE_FALSE(frame.depth.isNull());
        recordChain(chain, device, frame, output);
        CHECK(passInfos(device.recorded()).size() == 2);
        checkNames(chain, { "depth", "copy" });
    }

    TEST_CASE("A failing effect passes its input to the next effect and warns once")
    {
        babylon::log::CaptureSink sink;
        NullDevice device;
        TestEffect first("first", Stage::Hdr);
        TestEffect failing("failing", Stage::Hdr);
        TestEffect last("last", Stage::Hdr);
        Chain chain(device);
        REQUIRE(chain.open());

        failing.setFail(true);
        CHECK(chain.add(first));
        CHECK(chain.add(failing));
        CHECK(chain.add(last));

        const TextureHandle color  = makeColor(device, 16, 16);
        const TextureHandle target = makeOutput(device, 16, 16);
        FrameInputs frame;
        frame.color       = color;
        frame.colorFormat = TextureFormat::Rgba16F;
        frame.width       = 16;
        frame.height      = 16;
        OutputTarget output;
        output.texture = target;
        output.format  = TextureFormat::Rgba8;
        output.width   = 16;
        output.height  = 16;

        recordChain(chain, device, frame, output);

        const std::vector<PassInfo> passes = passInfos(device.recorded());
        REQUIRE(passes.size() == 3);
        REQUIRE(passes[0].textures.size() == 1);
        REQUIRE(passes[1].textures.size() == 1);
        REQUIRE(passes[2].textures.size() == 1);
        CHECK(passes[1].textures.front() == passes[0].target);
        CHECK(passes[2].textures.front() == passes[1].target);
        checkNames(chain, { "first", "last", "copy" });

        REQUIRE(sink.records().size() == 1);
        CHECK(sink.records()[0].level == babylon::log::Level::Warning);

        // The same failure on the next frame does not warn again.
        sink.clear();
        recordChain(chain, device, frame, output);
        CHECK(sink.records().empty());
        checkNames(chain, { "first", "last", "copy" });
    }

    TEST_CASE("The chain refuses invalid frames and lists")
    {
        NullDevice device;
        Chain chain(device);
        REQUIRE(chain.open());

        const TextureHandle color  = makeColor(device, 16, 16);
        const TextureHandle target = makeOutput(device, 16, 16);
        FrameInputs frame;
        frame.color       = color;
        frame.colorFormat = TextureFormat::Rgba16F;
        frame.width       = 16;
        frame.height      = 16;
        OutputTarget output;
        output.texture = target;
        output.format  = TextureFormat::Rgba8;
        output.width   = 16;
        output.height  = 16;

        {
            babylon::log::CaptureSink sink;
            CommandList list = device.beginFrame();
            FrameInputs bad  = frame;
            bad.color        = {};
            CHECK_FALSE(chain.record(list, bad, output));
            CHECK(sink.records().size() == 1);
        }
        {
            babylon::log::CaptureSink sink;
            CommandList list = device.beginFrame();
            FrameInputs bad  = frame;
            bad.width        = 0;
            CHECK_FALSE(chain.record(list, bad, output));
            CHECK(sink.records().size() == 1);
        }
        {
            babylon::log::CaptureSink sink;
            CommandList list = device.beginFrame();
            OutputTarget bad = output;
            bad.width        = 0;
            CHECK_FALSE(chain.record(list, frame, bad));
            CHECK(sink.records().size() == 1);
        }
        {
            babylon::log::CaptureSink sink;
            CommandList list = device.beginFrame();
            PassDesc pass;
            ColorAttachment attachment;
            attachment.target = target;
            pass.colorTargets.push_back(attachment);
            REQUIRE(list.beginPass(pass));
            CHECK_FALSE(chain.record(list, frame, output));
            CHECK(sink.records().size() == 1);
            REQUIRE(list.endPass());
        }

        chain.close();
        {
            babylon::log::CaptureSink sink;
            CommandList list = device.beginFrame();
            CHECK_FALSE(chain.record(list, frame, output));
            CHECK(sink.records().size() == 1);
        }
    }

    TEST_CASE("Adding an effect twice fails with one warning")
    {
        babylon::log::CaptureSink sink;
        NullDevice device;
        TestEffect effect("effect", Stage::Hdr);
        Chain chain(device);
        REQUIRE(chain.open());

        CHECK(chain.add(effect));
        CHECK(sink.records().empty());
        CHECK_FALSE(chain.add(effect));
        CHECK(sink.records().size() == 1);
    }

    TEST_CASE("Removing an effect releases its resources")
    {
        NullDevice device;
        TestEffect effect("effect", Stage::Hdr);
        Chain chain(device);
        REQUIRE(chain.open());

        CHECK(chain.add(effect));
        CHECK_FALSE(effect.released());

        CHECK(chain.remove(effect));
        CHECK(effect.released());
        CHECK_FALSE(chain.remove(effect));
    }

    TEST_CASE("Closing the chain releases every effect")
    {
        NullDevice device;
        TestEffect hdr("hdr", Stage::Hdr);
        TestEffect ldr("ldr", Stage::Ldr);
        TestEffect resolve("resolve", Stage::Resolve);
        Chain chain(device);
        REQUIRE(chain.open());

        CHECK(chain.add(hdr));
        CHECK(chain.add(ldr));
        CHECK(chain.add(resolve));

        chain.close();
        CHECK(hdr.released());
        CHECK(ldr.released());
        CHECK(resolve.released());

        // Closing again is a safe no-op.
        chain.close();
        CHECK(hdr.released());
    }

    TEST_CASE("An added Resolve effect replaces the built-in copy")
    {
        NullDevice device;
        TestEffect resolve("resolve", Stage::Resolve);
        Chain chain(device);
        REQUIRE(chain.open());

        CHECK(chain.add(resolve));

        const TextureHandle color  = makeColor(device, 16, 16);
        const TextureHandle target = makeOutput(device, 16, 16);
        FrameInputs frame;
        frame.color       = color;
        frame.colorFormat = TextureFormat::Rgba16F;
        frame.width       = 16;
        frame.height      = 16;
        OutputTarget output;
        output.texture = target;
        output.format  = TextureFormat::Rgba8;
        output.width   = 16;
        output.height  = 16;

        recordChain(chain, device, frame, output);

        const std::vector<PassInfo> passes = passInfos(device.recorded());
        REQUIRE(passes.size() == 1);
        CHECK(passes[0].target == target);
        REQUIRE(passes[0].textures.size() == 1);
        CHECK(passes[0].textures.front() == color);
        checkNames(chain, { "resolve" });
    }
}
