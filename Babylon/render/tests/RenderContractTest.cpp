// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/render/Buffer.h>
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Handle.h>
#include <babylon/render/NullDevice.h>
#include <babylon/render/SdlGpuDevice.h>
#include <babylon/render/Texture.h>

#include <babylon/log/CaptureSink.h>
#include <babylon/log/Log.h>
#include <babylon/log/Sink.h>

#include "../backends/sdl/tests/KeepGpuDriverLoaded.h"

#include <doctest.h>

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using babylon::log::CaptureSink;
    using babylon::log::Level;
    using namespace babylon::render;

    // The backends the Device contract runs against. Each factory makes a
    // fresh device; one that returns nullptr has no device here (no GPU
    // driver) and is reported as skipped, never as passed.
    struct DeviceFactory
    {
        const char* name;
        std::function<std::unique_ptr<Device>()> create;
    };

    const DeviceFactory kDeviceFactories[] = {
        { "null", [] { return std::unique_ptr<Device>(createNullDevice()); } },
        { "null without B4G4R4A4", []
            {
                return std::unique_ptr<Device>(
                    createNullDevice(NullDeviceDesc{ { TextureFormat::B4G4R4A4 } }));
            } },
        { "sdl-gpu headless", []
            {
                test::keepGpuDriverLoaded();
                return createSdlGpuDevice(nullptr);
            } },
    };

    // Runs check once per backend, each on its own device, with the
    // backend's name in every failure message.
    void forEachDevice(const std::function<void(Device&)>& check)
    {
        for ( const DeviceFactory& factory : kDeviceFactories )
        {
            INFO("device: " << std::string(factory.name));
            const std::unique_ptr<Device> device = factory.create();
            if ( device == nullptr )
            {
                MESSAGE("skipped, no device: " << std::string(factory.name));
                continue;
            }
            check(*device);
        }
    }

    std::string joined(const CaptureSink& sink)
    {
        std::string text;
        for ( const auto& record : sink.records() )
        {
            text += text.empty() ? "" : "; ";
            text += record.message;
        }
        return text;
    }

    // Exactly one warning since the last check, then forgets it.
    void oneWarning(CaptureSink& sink)
    {
        INFO("captured log: " << joined(sink));
        REQUIRE(sink.records().size() == 1);
        CHECK(sink.records()[0].level == Level::Warning);
        sink.clear();
    }

    // Nothing logged since the last check.
    void noWarning(CaptureSink& sink)
    {
        INFO("captured log: " << joined(sink));
        CHECK(sink.records().empty());
        sink.clear();
    }

    std::vector<std::byte> pattern(std::size_t size, std::uint32_t seed)
    {
        std::vector<std::byte> bytes(size);
        for ( std::size_t i = 0; i < size; ++i )
        {
            bytes[i] = static_cast<std::byte>((seed * 31u + i * 7u + (i >> 5)) & 0xFFu);
        }
        return bytes;
    }

    std::vector<std::byte> bytesOf(std::initializer_list<unsigned> values)
    {
        std::vector<std::byte> bytes;
        for ( const unsigned value : values )
        {
            bytes.push_back(static_cast<std::byte>(value));
        }
        return bytes;
    }

    // n copies of one texel.
    std::vector<std::byte> repeated(const std::vector<std::byte>& texel, std::size_t n)
    {
        std::vector<std::byte> bytes;
        for ( std::size_t i = 0; i < n; ++i )
        {
            bytes.insert(bytes.end(), texel.begin(), texel.end());
        }
        return bytes;
    }

    std::size_t mipBytes(const TextureDesc& desc, std::uint32_t mip)
    {
        return std::size_t{ mipDimension(desc.width, mip) } * mipDimension(desc.height, mip)
            * bytesPerTexel(desc.format);
    }

    // A color attachment with the defaults of ColorAttachment: clear to
    // opaque black, store.
    ColorAttachment attachment(TextureHandle target)
    {
        ColorAttachment result;
        result.target = target;
        return result;
    }

    PassDesc clearPass(TextureHandle target, std::uint32_t mip, ClearColor color)
    {
        PassDesc pass;
        pass.colorTargets.push_back(ColorAttachment{ target, mip, LoadOp::Clear, StoreOp::Store,
            color });
        return pass;
    }

    bool isColorFormat(TextureFormat format)
    {
        return !isDepthFormat(format);
    }
}

TEST_CASE("A render device reports what it is and what it accepts")
{
    forEachDevice([](Device& device)
    {
        const DeviceInfo& info = device.info();
        CHECK_FALSE(info.backend.empty());
        CHECK(info.shaderFormats != ShaderFormat::None);
        // The floors every backend must reach, so callers can rely on them.
        CHECK(info.limits.maxTextureDimension >= 4096);
        CHECK(info.limits.maxBufferSize >= 16u * 1024u * 1024u);
        CHECK(isColorFormat(info.swapchainFormat));
        CHECK(device.supportsFormat(info.swapchainFormat, TextureUsage::ColorTarget));

        // RGBA8 and BGRA8 are on every device, depth formats never as color
        // targets and color formats never as depth targets.
        for ( const TextureFormat format : { TextureFormat::Rgba8, TextureFormat::Bgra8 } )
        {
            CHECK(device.supportsFormat(format, TextureUsage::Sampled | TextureUsage::ColorTarget));
            CHECK_FALSE(device.supportsFormat(format, TextureUsage::DepthTarget));
        }
        for ( const TextureFormat format : { TextureFormat::D24S8, TextureFormat::D32F } )
        {
            CHECK_FALSE(device.supportsFormat(format, TextureUsage::ColorTarget));
        }
        CHECK((device.supportsFormat(TextureFormat::D24S8, TextureUsage::DepthTarget)
            || device.supportsFormat(TextureFormat::D32F, TextureUsage::DepthTarget)));
    });
}

TEST_CASE("Textures are created and destroyed, and stale handles are refused")
{
    forEachDevice([](Device& device)
    {
        CaptureSink sink;
        const TextureDesc desc{ 16, 8, 1, TextureFormat::Rgba8, TextureUsage::Sampled };

        const TextureHandle first = device.createTexture(desc);
        const TextureHandle second = device.createTexture(desc);
        CHECK_FALSE(first.isNull());
        CHECK_FALSE(second.isNull());
        CHECK(first != second);
        noWarning(sink);

        device.destroyTexture(first);
        noWarning(sink);

        // Every use of the destroyed handle is a logged no-op.
        device.destroyTexture(first);
        oneWarning(sink);
        const std::vector<std::byte> data = pattern(16 * 8 * 4, 1);
        CHECK_FALSE(device.uploadTexture(first, 0, data));
        oneWarning(sink);
        CHECK(device.readTexture(first, 0).empty());
        oneWarning(sink);

        // A new texture never revives the old handle, even in a reused slot.
        const TextureHandle third = device.createTexture(desc);
        CHECK_FALSE(third.isNull());
        CHECK(third != first);
        CHECK_FALSE(device.uploadTexture(first, 0, data));
        oneWarning(sink);
        CHECK(device.uploadTexture(third, 0, data));
        CHECK(device.readTexture(third, 0) == data);
        noWarning(sink);

        // Destroying the null handle is silent; using it is refused.
        device.destroyTexture(TextureHandle{});
        noWarning(sink);
        CHECK(device.readTexture(TextureHandle{}, 0).empty());
        oneWarning(sink);

        // The survivor still works.
        CHECK(device.uploadTexture(second, 0, data));
        noWarning(sink);
        device.destroyTexture(second);
        device.destroyTexture(third);
        noWarning(sink);
    });
}

TEST_CASE("Invalid texture descs are refused with one warning each")
{
    forEachDevice([](Device& device)
    {
        CaptureSink sink;
        const std::uint32_t maxDimension = device.info().limits.maxTextureDimension;
        const TextureUsage sampled = TextureUsage::Sampled;
        const TextureFormat rgba = TextureFormat::Rgba8;

        const TextureDesc invalid[] = {
            { 0, 8, 1, rgba, sampled },
            { 8, 0, 1, rgba, sampled },
            { maxDimension + 1, 8, 1, rgba, sampled },
            { 8, maxDimension + 1, 1, rgba, sampled },
            { 8, 8, 0, rgba, sampled },
            { 8, 8, 5, rgba, sampled },            // 8x8 has 4 levels
            { 16, 2, 6, rgba, sampled },           // 16x2 has 5 levels
            { 8, 8, 1, rgba, TextureUsage::None },
            { 8, 8, 1, rgba, TextureUsage::ColorTarget | TextureUsage::DepthTarget },
            { 8, 8, 1, TextureFormat::D32F, TextureUsage::ColorTarget },
            { 8, 8, 1, TextureFormat::D24S8, TextureUsage::ColorTarget },
            { 8, 8, 1, rgba, TextureUsage::DepthTarget },
        };
        for ( const TextureDesc& desc : invalid )
        {
            INFO("desc " << desc.width << "x" << desc.height << " mips " << desc.mipLevels
                << " format " << static_cast<int>(desc.format)
                << " usage " << static_cast<int>(desc.usage));
            CHECK(device.createTexture(desc).isNull());
            oneWarning(sink);
        }

        // Every format the device lacks for a usage is refused for it.
        for ( const TextureFormat format : kAllTextureFormats )
        {
            const TextureUsage usage = isDepthFormat(format) ? TextureUsage::DepthTarget : sampled;
            if ( !device.supportsFormat(format, usage) )
            {
                INFO("unsupported format " << static_cast<int>(format));
                CHECK(device.createTexture(TextureDesc{ 8, 8, 1, format, usage }).isNull());
                oneWarning(sink);
            }
        }

        // The bounds themselves are accepted.
        const TextureHandle widest = device.createTexture(TextureDesc{ maxDimension, 1, 1, rgba,
            sampled });
        const TextureHandle fullChain = device.createTexture(TextureDesc{ 16, 2, 5, rgba,
            sampled });
        CHECK_FALSE(widest.isNull());
        CHECK_FALSE(fullChain.isNull());
        noWarning(sink);
        device.destroyTexture(widest);
        device.destroyTexture(fullChain);
    });
}

TEST_CASE("Uploaded texels read back byte-exact in every mip of every format")
{
    forEachDevice([](Device& device)
    {
        CaptureSink sink;
        std::uint32_t seed = 0;
        for ( const TextureFormat format : kAllTextureFormats )
        {
            if ( isDepthFormat(format) || !device.supportsFormat(format, TextureUsage::Sampled) )
            {
                continue;
            }
            INFO("format " << static_cast<int>(format));
            // An odd size, so the mips round down: 13x7, 6x3, 3x1, 1x1.
            const TextureDesc desc{ 13, 7, 4, format, TextureUsage::Sampled };
            const TextureHandle texture = device.createTexture(desc);
            REQUIRE_FALSE(texture.isNull());

            std::vector<std::vector<std::byte>> uploaded;
            for ( std::uint32_t mip = 0; mip < desc.mipLevels; ++mip )
            {
                uploaded.push_back(pattern(mipBytes(desc, mip), ++seed));
                CHECK(device.uploadTexture(texture, mip, uploaded.back()));
            }
            for ( std::uint32_t mip = 0; mip < desc.mipLevels; ++mip )
            {
                INFO("mip " << mip);
                CHECK(device.readTexture(texture, mip) == uploaded[mip]);
            }

            // A region replaces exactly its rows and columns.
            const std::uint32_t texel = bytesPerTexel(format);
            const TextureRegion region{ 0, 3, 2, 4, 3 };
            const std::vector<std::byte> patch = pattern(4 * 3 * texel, ++seed);
            CHECK(device.uploadTexture(texture, region, patch));
            std::vector<std::byte> expected = uploaded[0];
            for ( std::uint32_t row = 0; row < region.height; ++row )
            {
                for ( std::uint32_t i = 0; i < region.width * texel; ++i )
                {
                    expected[((region.y + row) * desc.width + region.x) * texel + i] =
                        patch[row * region.width * texel + i];
                }
            }
            CHECK(device.readTexture(texture, 0) == expected);
            CHECK(device.readTexture(texture, 1) == uploaded[1]);
            noWarning(sink);

            // Wrong sizes, regions outside the mip and mips out of range are
            // refused and change nothing.
            CHECK_FALSE(device.uploadTexture(texture, 0, pattern(mipBytes(desc, 0) - 1, 1)));
            oneWarning(sink);
            CHECK_FALSE(device.uploadTexture(texture, 0, pattern(mipBytes(desc, 0) + 1, 1)));
            oneWarning(sink);
            CHECK_FALSE(device.uploadTexture(texture, TextureRegion{ 1, 4, 0, 3, 1 },
                pattern(3 * texel, 1)));
            oneWarning(sink);
            CHECK_FALSE(device.uploadTexture(texture, TextureRegion{ 0, 0, 0, 0, 1 }, {}));
            oneWarning(sink);
            CHECK_FALSE(device.uploadTexture(texture, 4, pattern(texel, 1)));
            oneWarning(sink);
            CHECK(device.readTexture(texture, 4).empty());
            oneWarning(sink);
            CHECK(device.readTexture(texture, 0) == expected);
            CHECK(device.readTexture(texture, 1) == uploaded[1]);
            noWarning(sink);
            device.destroyTexture(texture);
        }

        // Depth textures are never uploaded or read back.
        const TextureFormat depth = device.supportsFormat(TextureFormat::D32F,
            TextureUsage::DepthTarget) ? TextureFormat::D32F : TextureFormat::D24S8;
        const TextureHandle depthTexture = device.createTexture(TextureDesc{ 4, 4, 1, depth,
            TextureUsage::DepthTarget });
        REQUIRE_FALSE(depthTexture.isNull());
        CHECK_FALSE(device.uploadTexture(depthTexture, 0, pattern(4 * 4 * 4, 1)));
        oneWarning(sink);
        CHECK(device.readTexture(depthTexture, 0).empty());
        oneWarning(sink);
        device.destroyTexture(depthTexture);
    });
}

TEST_CASE("A cleared color target reads back as the clear color in its format")
{
    // The expected texels are written out by hand from the layouts in
    // Texture.h, not computed by the code under test.
    struct Expected
    {
        TextureFormat format;
        std::vector<std::byte> red;    // clear (1, 0, 0, 1)
        std::vector<std::byte> cyan;   // clear (0, 1, 1, 0)
    };
    const Expected expectations[] = {
        { TextureFormat::Rgba8, bytesOf({ 0xFF, 0x00, 0x00, 0xFF }), bytesOf({ 0x00, 0xFF, 0xFF, 0x00 }) },
        { TextureFormat::Bgra8, bytesOf({ 0x00, 0x00, 0xFF, 0xFF }), bytesOf({ 0xFF, 0xFF, 0x00, 0x00 }) },
        { TextureFormat::B5G6R5, bytesOf({ 0x00, 0xF8 }), bytesOf({ 0xFF, 0x07 }) },
        { TextureFormat::B5G5R5A1, bytesOf({ 0x00, 0xFC }), bytesOf({ 0xFF, 0x03 }) },
        { TextureFormat::B4G4R4A4, bytesOf({ 0x00, 0xFF }), bytesOf({ 0xFF, 0x00 }) },
    };

    forEachDevice([&expectations](Device& device)
    {
        CaptureSink sink;
        for ( const Expected& expected : expectations )
        {
            const TextureUsage usage = TextureUsage::Sampled | TextureUsage::ColorTarget;
            if ( !device.supportsFormat(expected.format, usage) )
            {
                continue;
            }
            INFO("format " << static_cast<int>(expected.format));
            const TextureDesc desc{ 4, 4, 2, expected.format, usage };
            const TextureHandle target = device.createTexture(desc);
            REQUIRE_FALSE(target.isNull());
            const std::vector<std::byte> mip1 = pattern(mipBytes(desc, 1), 9);
            REQUIRE(device.uploadTexture(target, 1, mip1));

            CommandList frame = device.beginFrame();
            REQUIRE(frame.beginPass(clearPass(target, 0, ClearColor{ 1.0f, 0.0f, 0.0f, 1.0f })));
            REQUIRE(frame.endPass());
            REQUIRE(device.submit(frame));
            CHECK(device.readTexture(target, 0) == repeated(expected.red, 16));
            // A pass clears only the mip it targets.
            CHECK(device.readTexture(target, 1) == mip1);

            CommandList next = device.beginFrame();
            REQUIRE(next.beginPass(clearPass(target, 1, ClearColor{ 0.0f, 1.0f, 1.0f, 0.0f })));
            REQUIRE(next.endPass());
            REQUIRE(device.submit(next));
            CHECK(device.readTexture(target, 0) == repeated(expected.red, 16));
            CHECK(device.readTexture(target, 1) == repeated(expected.cyan, 4));
            noWarning(sink);
            device.destroyTexture(target);
        }
    });
}

TEST_CASE("copyTexture copies exactly the region, and bad copies are dropped")
{
    forEachDevice([](Device& device)
    {
        CaptureSink sink;
        const TextureDesc desc{ 8, 8, 2, TextureFormat::Rgba8, TextureUsage::Sampled };
        const TextureHandle source = device.createTexture(desc);
        const TextureHandle destination = device.createTexture(desc);
        const TextureHandle other = device.createTexture(TextureDesc{ 8, 8, 1,
            TextureFormat::Bgra8, TextureUsage::Sampled });
        REQUIRE_FALSE(source.isNull());
        REQUIRE_FALSE(destination.isNull());
        REQUIRE_FALSE(other.isNull());
        const std::vector<std::byte> sourceBytes = pattern(mipBytes(desc, 0), 3);
        const std::vector<std::byte> destinationBytes = pattern(mipBytes(desc, 0), 4);
        REQUIRE(device.uploadTexture(source, 0, sourceBytes));
        REQUIRE(device.uploadTexture(destination, 0, destinationBytes));
        REQUIRE(device.uploadTexture(destination, 1, pattern(mipBytes(desc, 1), 5)));

        // 3x2 texels from (1,1) of the source to (4,5) of the destination,
        // and 4x4 of the source's mip 0 into its own mip 1.
        CommandList frame = device.beginFrame();
        frame.copyTexture(source, TextureRegion{ 0, 1, 1, 3, 2 }, destination, 0, 4, 5);
        frame.copyTexture(source, TextureRegion{ 0, 4, 4, 4, 4 }, source, 1, 0, 0);
        REQUIRE(device.submit(frame));
        noWarning(sink);

        std::vector<std::byte> expected = destinationBytes;
        for ( std::uint32_t row = 0; row < 2; ++row )
        {
            for ( std::uint32_t i = 0; i < 3 * 4; ++i )
            {
                expected[((5 + row) * 8 + 4) * 4 + i] = sourceBytes[((1 + row) * 8 + 1) * 4 + i];
            }
        }
        CHECK(device.readTexture(destination, 0) == expected);
        std::vector<std::byte> expectedMip1;
        for ( std::uint32_t row = 0; row < 4; ++row )
        {
            const auto first = sourceBytes.begin() + ((4 + row) * 8 + 4) * 4;
            expectedMip1.insert(expectedMip1.end(), first, first + 4 * 4);
        }
        CHECK(device.readTexture(source, 1) == expectedMip1);

        // Out of bounds at either end, different formats, the same mip and a
        // stale texture: each dropped with one warning, nothing copied, the
        // submit itself accepted.
        const TextureHandle stale = device.createTexture(desc);
        device.destroyTexture(stale);
        const auto dropped = [&](auto record)
        {
            CommandList list = device.beginFrame();
            record(list);
            CHECK(device.submit(list));
            oneWarning(sink);
            CHECK(device.readTexture(destination, 0) == expected);
        };
        dropped([&](CommandList& list) {
            list.copyTexture(source, TextureRegion{ 0, 6, 0, 3, 1 }, destination, 0, 0, 0); });
        dropped([&](CommandList& list) {
            list.copyTexture(source, TextureRegion{ 0, 0, 0, 3, 1 }, destination, 0, 6, 0); });
        dropped([&](CommandList& list) {
            list.copyTexture(source, TextureRegion{ 0, 0, 0, 2, 2 }, destination, 1, 3, 3); });
        dropped([&](CommandList& list) {
            list.copyTexture(other, TextureRegion{ 0, 0, 0, 2, 2 }, destination, 0, 0, 0); });
        dropped([&](CommandList& list) {
            list.copyTexture(destination, TextureRegion{ 0, 0, 0, 2, 2 }, destination, 0, 4, 4); });
        dropped([&](CommandList& list) {
            list.copyTexture(stale, TextureRegion{ 0, 0, 0, 2, 2 }, destination, 0, 0, 0); });

        device.destroyTexture(source);
        device.destroyTexture(destination);
        device.destroyTexture(other);
        noWarning(sink);
    });
}

TEST_CASE("Buffers are created, uploaded and read back within their limits")
{
    forEachDevice([](Device& device)
    {
        CaptureSink sink;
        const BufferHandle buffer = device.createBuffer(BufferDesc{ 64,
            BufferUsage::Vertex | BufferUsage::Index });
        REQUIRE_FALSE(buffer.isNull());
        const std::vector<std::byte> whole = pattern(64, 6);
        CHECK(device.uploadBuffer(buffer, 0, whole));
        const std::vector<std::byte> part = pattern(16, 7);
        CHECK(device.uploadBuffer(buffer, 48, part));
        std::vector<std::byte> expected = whole;
        std::copy(part.begin(), part.end(), expected.begin() + 48);
        CHECK(device.readBuffer(buffer, 0, 64) == expected);
        CHECK(device.readBuffer(buffer, 40, 16) == std::vector<std::byte>(
            expected.begin() + 40, expected.begin() + 56));
        noWarning(sink);

        // Size limits and ranges past the end.
        const std::uint32_t maxSize = device.info().limits.maxBufferSize;
        CHECK(device.createBuffer(BufferDesc{ 0, BufferUsage::Vertex }).isNull());
        oneWarning(sink);
        CHECK(device.createBuffer(BufferDesc{ maxSize + 1, BufferUsage::Vertex }).isNull());
        oneWarning(sink);
        CHECK(device.createBuffer(BufferDesc{ 64, BufferUsage::None }).isNull());
        oneWarning(sink);
        CHECK_FALSE(device.uploadBuffer(buffer, 49, part));
        oneWarning(sink);
        CHECK_FALSE(device.uploadBuffer(buffer, 0, {}));
        oneWarning(sink);
        CHECK(device.readBuffer(buffer, 60, 5).empty());
        oneWarning(sink);
        CHECK(device.readBuffer(buffer, 0, 0).empty());
        oneWarning(sink);
        CHECK(device.readBuffer(buffer, 0, 64) == expected);
        noWarning(sink);

        // Stale buffer handles are logged no-ops.
        device.destroyBuffer(buffer);
        noWarning(sink);
        CHECK_FALSE(device.uploadBuffer(buffer, 0, part));
        oneWarning(sink);
        CHECK(device.readBuffer(buffer, 0, 4).empty());
        oneWarning(sink);
        device.destroyBuffer(buffer);
        oneWarning(sink);
        device.destroyBuffer(BufferHandle{});
        noWarning(sink);

        // Samplers have the same handle lifetime.
        const SamplerHandle sampler = device.createSampler(SamplerDesc{});
        CHECK_FALSE(sampler.isNull());
        device.destroySampler(sampler);
        noWarning(sink);
        device.destroySampler(sampler);
        oneWarning(sink);
    });
}

TEST_CASE("Command list misuse is refused, logged and records nothing")
{
    forEachDevice([](Device& device)
    {
        CaptureSink sink;
        const TextureHandle target = device.createTexture(TextureDesc{ 4, 4, 1,
            TextureFormat::Rgba8, TextureUsage::ColorTarget | TextureUsage::Sampled });
        REQUIRE_FALSE(target.isNull());
        const std::vector<std::byte> payload = pattern(16, 1);

        CommandList list = device.beginFrame();

        // Render commands outside a pass, and endPass without one.
        list.draw(3);
        oneWarning(sink);
        list.drawIndexed(3);
        oneWarning(sink);
        list.setPipeline(PipelineHandle{});
        oneWarning(sink);
        list.setViewport(Viewport{ 0, 0, 4, 4 });
        oneWarning(sink);
        list.setScissor(ScissorRect{ 0, 0, 4, 4 });
        oneWarning(sink);
        list.bindVertexBuffer(0, BufferHandle{});
        oneWarning(sink);
        list.bindIndexBuffer(BufferHandle{}, IndexType::Uint16);
        oneWarning(sink);
        list.bindTexture(ShaderStage::Fragment, 0, target, SamplerHandle{});
        oneWarning(sink);
        list.pushUniforms(ShaderStage::Vertex, 0, payload);
        oneWarning(sink);
        CHECK_FALSE(list.endPass());
        oneWarning(sink);

        // Passes without attachments, with too many or with the swapchain
        // beside a texture.
        CHECK_FALSE(list.beginPass(PassDesc{}));
        oneWarning(sink);
        PassDesc tooMany;
        for ( std::uint32_t i = 0; i <= kMaxColorTargets; ++i )
        {
            tooMany.colorTargets.push_back(attachment(target));
        }
        CHECK_FALSE(list.beginPass(tooMany));
        oneWarning(sink);
        PassDesc mixed;
        mixed.colorTargets = { attachment(target), attachment(TextureHandle{}) };
        CHECK_FALSE(list.beginPass(mixed));
        oneWarning(sink);
        CHECK(list.commands().empty());

        // Inside a pass: no nesting, no copies, draws need a pipeline (and an
        // index buffer), slots and sizes in range, a sane viewport.
        REQUIRE(list.beginPass(clearPass(target, 0, ClearColor{})));
        CHECK(list.inPass());
        CHECK_FALSE(list.beginPass(clearPass(target, 0, ClearColor{})));
        oneWarning(sink);
        list.copyTexture(target, TextureRegion{ 0, 0, 0, 1, 1 }, target, 0, 1, 1);
        oneWarning(sink);
        list.draw(3);
        oneWarning(sink);
        list.drawIndexed(3);
        oneWarning(sink);
        list.bindVertexBuffer(kMaxVertexBuffers, BufferHandle{});
        oneWarning(sink);
        list.bindTexture(ShaderStage::Fragment, kMaxTextureSlots, target, SamplerHandle{});
        oneWarning(sink);
        list.pushUniforms(ShaderStage::Vertex, kMaxUniformSlots, payload);
        oneWarning(sink);
        list.pushUniforms(ShaderStage::Vertex, 0, {});
        oneWarning(sink);
        list.pushUniforms(ShaderStage::Vertex, 0,
            std::vector<std::byte>(kMaxUniformBytes + 1));
        oneWarning(sink);
        list.setViewport(Viewport{ 0, 0, 0, 4 });
        oneWarning(sink);
        list.setViewport(Viewport{ 0, 0, 4, 4, 0.5f, 0.25f });
        oneWarning(sink);
        list.setViewport(Viewport{ 0, 0, 4, 4, 0.0f, 1.5f });
        oneWarning(sink);
        // Only the beginPass was recorded.
        CHECK(list.commands().size() == 1);

        // A pass still open refuses the submit and leaves the list as it was.
        CHECK_FALSE(device.submit(list));
        oneWarning(sink);
        CHECK(list.commands().size() == 1);
        CHECK_FALSE(list.submitted());
        REQUIRE(list.endPass());
        CHECK(device.submit(list));
        CHECK(list.submitted());
        noWarning(sink);

        // Submitted once: no second submit, no further recording.
        CHECK_FALSE(device.submit(list));
        oneWarning(sink);
        CHECK_FALSE(list.beginPass(clearPass(target, 0, ClearColor{})));
        oneWarning(sink);
        list.copyTexture(target, TextureRegion{ 0, 0, 0, 1, 1 }, target, 0, 1, 1);
        oneWarning(sink);

        // A list of an earlier frame is abandoned by beginFrame().
        CommandList abandoned = device.beginFrame();
        CommandList current = device.beginFrame();
        CHECK_FALSE(device.submit(abandoned));
        oneWarning(sink);
        CHECK(device.submit(current));
        noWarning(sink);

        device.destroyTexture(target);
        noWarning(sink);
    });
}

TEST_CASE("A command list belongs to the device that made it")
{
    for ( const DeviceFactory& factory : kDeviceFactories )
    {
        INFO("device: " << std::string(factory.name));
        const std::unique_ptr<Device> first = factory.create();
        const std::unique_ptr<Device> second = factory.create();
        if ( first == nullptr || second == nullptr )
        {
            MESSAGE("skipped, no two devices: " << std::string(factory.name));
            continue;
        }
        CaptureSink sink;
        CommandList list = first->beginFrame();
        (void)second->beginFrame();
        CHECK_FALSE(second->submit(list));
        oneWarning(sink);
        CHECK(first->submit(list));
        noWarning(sink);
    }
}

TEST_CASE("Passes with stale attachments are dropped whole at submit")
{
    forEachDevice([](Device& device)
    {
        CaptureSink sink;
        const TextureUsage usage = TextureUsage::ColorTarget | TextureUsage::Sampled;
        const TextureHandle kept = device.createTexture(TextureDesc{ 4, 4, 1,
            TextureFormat::Rgba8, usage });
        const TextureHandle gone = device.createTexture(TextureDesc{ 4, 4, 1,
            TextureFormat::Rgba8, usage });
        const TextureHandle sampledOnly = device.createTexture(TextureDesc{ 4, 4, 1,
            TextureFormat::Rgba8, TextureUsage::Sampled });
        const TextureHandle small = device.createTexture(TextureDesc{ 2, 2, 1,
            TextureFormat::Rgba8, usage });
        REQUIRE_FALSE(kept.isNull());
        REQUIRE_FALSE(gone.isNull());
        REQUIRE_FALSE(sampledOnly.isNull());
        REQUIRE_FALSE(small.isNull());
        const std::vector<std::byte> keptBytes = pattern(64, 2);
        REQUIRE(device.uploadTexture(kept, 0, keptBytes));

        // The stale pass goes with everything in it, the next pass runs.
        CommandList frame = device.beginFrame();
        PassDesc both;
        both.colorTargets = { ColorAttachment{ kept, 0, LoadOp::Clear, StoreOp::Store,
            ClearColor{ 1, 1, 1, 1 } }, attachment(gone) };
        REQUIRE(frame.beginPass(both));
        frame.setViewport(Viewport{ 0, 0, 4, 4 });
        REQUIRE(frame.endPass());
        device.destroyTexture(gone);
        REQUIRE(frame.beginPass(clearPass(small, 0, ClearColor{ 1, 1, 1, 1 })));
        REQUIRE(frame.endPass());
        CHECK(device.submit(frame));
        oneWarning(sink);
        CHECK(device.readTexture(kept, 0) == keptBytes);
        CHECK(device.readTexture(small, 0) == std::vector<std::byte>(16, std::byte{ 0xFF }));

        // A target without ColorTarget usage, attachments of different sizes
        // and a mip out of range are dropped the same way.
        for ( const PassDesc& pass : {
            clearPass(sampledOnly, 0, ClearColor{ 1, 1, 1, 1 }),
            PassDesc{ { attachment(kept), attachment(small) }, std::nullopt },
            clearPass(kept, 1, ClearColor{ 1, 1, 1, 1 }) } )
        {
            CommandList list = device.beginFrame();
            REQUIRE(list.beginPass(pass));
            REQUIRE(list.endPass());
            CHECK(device.submit(list));
            oneWarning(sink);
            CHECK(device.readTexture(kept, 0) == keptBytes);
        }

        device.destroyTexture(kept);
        device.destroyTexture(sampledOnly);
        device.destroyTexture(small);
        noWarning(sink);
    });
}

TEST_CASE("Frames run without a window, the swapchain passes included")
{
    forEachDevice([](Device& device)
    {
        if ( device.info().hasSwapchain )
        {
            MESSAGE("not headless, nothing to check: " << device.info().backend);
            return;
        }
        CaptureSink sink;
        const TextureFormat depthFormat = device.supportsFormat(TextureFormat::D24S8,
            TextureUsage::DepthTarget) ? TextureFormat::D24S8 : TextureFormat::D32F;
        const TextureHandle depth = device.createTexture(TextureDesc{ 64, 64, 1, depthFormat,
            TextureUsage::DepthTarget });
        REQUIRE_FALSE(depth.isNull());
        for ( int i = 0; i < 3; ++i )
        {
            CommandList frame = device.beginFrame();
            PassDesc pass = clearPass(TextureHandle{}, 0, ClearColor{ 0.0f, 0.0f, 0.0f, 1.0f });
            pass.depthTarget = DepthAttachment{ depth };
            REQUIRE(frame.beginPass(pass));
            frame.setViewport(Viewport{ 0, 0, 64, 64 });
            frame.setScissor(ScissorRect{ 0, 0, 32, 32 });
            REQUIRE(frame.endPass());
            CHECK(device.submit(frame));
        }
        noWarning(sink);
        device.destroyTexture(depth);
    });
}

TEST_CASE("updateBuffer writes in list order, outside passes and within the buffer")
{
    forEachDevice([](Device& device)
    {
        CaptureSink sink;
        const BufferHandle buffer = device.createBuffer(BufferDesc{ 64, BufferUsage::Vertex });
        const TextureHandle target = device.createTexture(TextureDesc{ 4, 4, 1,
            TextureFormat::Rgba8, TextureUsage::ColorTarget });
        REQUIRE_FALSE(buffer.isNull());
        REQUIRE_FALSE(target.isNull());
        const std::vector<std::byte> initial = pattern(64, 11);
        REQUIRE(device.uploadBuffer(buffer, 0, initial));

        // Each write lands at its place in the list, later ones over earlier
        // ones, passes between them or not; the upload before the submit
        // comes first.
        const std::vector<std::byte> first = pattern(16, 12);
        const std::vector<std::byte> second = pattern(16, 13);
        const std::vector<std::byte> third = pattern(4, 14);
        CommandList frame = device.beginFrame();
        frame.updateBuffer(buffer, 0, first);
        REQUIRE(frame.beginPass(clearPass(target, 0, ClearColor{})));
        REQUIRE(frame.endPass());
        frame.updateBuffer(buffer, 8, second);
        REQUIRE(frame.beginPass(clearPass(target, 0, ClearColor{})));
        REQUIRE(frame.endPass());
        frame.updateBuffer(buffer, 0, third);
        REQUIRE(device.submit(frame));
        std::vector<std::byte> expected = initial;
        std::copy(first.begin(), first.end(), expected.begin());
        std::copy(second.begin(), second.end(), expected.begin() + 8);
        std::copy(third.begin(), third.end(), expected.begin());
        CHECK(device.readBuffer(buffer, 0, 64) == expected);
        noWarning(sink);

        // An upload after the submit replaces what the list wrote.
        const std::vector<std::byte> later = pattern(8, 15);
        REQUIRE(device.uploadBuffer(buffer, 0, later));
        std::copy(later.begin(), later.end(), expected.begin());
        CHECK(device.readBuffer(buffer, 0, 64) == expected);
        noWarning(sink);

        // Refused while recording: inside a pass, no bytes, after submit.
        CommandList refused = device.beginFrame();
        REQUIRE(refused.beginPass(clearPass(target, 0, ClearColor{})));
        refused.updateBuffer(buffer, 0, first);
        oneWarning(sink);
        CHECK(refused.commands().size() == 1);
        REQUIRE(refused.endPass());
        refused.updateBuffer(buffer, 0, {});
        oneWarning(sink);
        CHECK(refused.commands().size() == 2);
        REQUIRE(device.submit(refused));
        noWarning(sink);
        refused.updateBuffer(buffer, 0, first);
        oneWarning(sink);

        // Dropped at submit: past the end and a stale buffer, one warning
        // each, nothing written, the rest of the list executed.
        const BufferHandle stale = device.createBuffer(BufferDesc{ 64, BufferUsage::Vertex });
        device.destroyBuffer(stale);
        const std::vector<std::byte> last = pattern(4, 16);
        CommandList bad = device.beginFrame();
        bad.updateBuffer(buffer, 60, pattern(8, 17));
        bad.updateBuffer(stale, 0, pattern(8, 18));
        bad.updateBuffer(buffer, 60, last);
        CHECK(device.submit(bad));
        REQUIRE(sink.records().size() == 2);
        CHECK(sink.records()[0].level == Level::Warning);
        CHECK(sink.records()[1].level == Level::Warning);
        sink.clear();
        std::copy(last.begin(), last.end(), expected.begin() + 60);
        CHECK(device.readBuffer(buffer, 0, 64) == expected);
        noWarning(sink);

        device.destroyBuffer(buffer);
        device.destroyTexture(target);
        noWarning(sink);
    });
}
