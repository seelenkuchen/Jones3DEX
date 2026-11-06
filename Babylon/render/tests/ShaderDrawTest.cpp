// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Draws with the test shaders in tests/shaders/, compiled by
// babylon_add_shaders(). The SDL GPU device rasterizes them into a 64x64
// Rgba8 target that is read back and compared with the expected image; the
// Null device, which doesn't rasterize, must accept the same pipelines and
// record exactly the same command stream.
//
// Coordinates: clip space x and y in -1..1 with +y up, so pixel column
// (x + 1) * 32 and row (1 - y) * 32. Quads have their edges on pixel
// boundaries, never through a pixel center, so their coverage is exact on
// every rasterizer; for the triangle only pixels well inside or outside are
// compared.

#include <babylon/render/Buffer.h>
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Handle.h>
#include <babylon/render/NullDevice.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Sampler.h>
#include <babylon/render/SdlGpuDevice.h>
#include <babylon/render/Texture.h>
#include <babylon/shaders/babylon_render_tests.h>

#include <babylon/log/CaptureSink.h>

#include "../backends/sdl/tests/GpuRequirement.h"
#include "../backends/sdl/tests/KeepGpuDriverLoaded.h"

#include <doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace
{
    using babylon::log::CaptureSink;
    using namespace babylon::render;
    namespace shaders = babylon::render::test::shaders;

    constexpr std::uint32_t kSize = 64;

    using Rgba = std::array<int, 4>;
    constexpr Rgba kBlack{ 0, 0, 0, 255 };
    constexpr Rgba kRed{ 255, 0, 0, 255 };
    constexpr Rgba kGreen{ 0, 255, 0, 255 };
    constexpr Rgba kBlue{ 0, 0, 255, 255 };

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

    template <typename T>
    std::vector<std::byte> bytesOf(const std::vector<T>& values)
    {
        const std::span<const std::byte> bytes = std::as_bytes(std::span(values));
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    }

    // The color vertex of color.vert.hlsl: position (location 0) and color
    // (location 1, UByte4Norm).
    struct ColorVertex
    {
        float x;
        float y;
        float z;
        std::uint8_t color[4];
    };
    static_assert(sizeof(ColorVertex) == 16);

    // The vertex of textured.vert.hlsl: position (location 0) and uv
    // (location 1).
    struct TexturedVertex
    {
        float x;
        float y;
        float u;
        float v;
    };
    static_assert(sizeof(TexturedVertex) == 16);

    ColorVertex colorVertex(float x, float y, float z, const Rgba& color)
    {
        return ColorVertex{ x, y, z, { static_cast<std::uint8_t>(color[0]),
            static_cast<std::uint8_t>(color[1]), static_cast<std::uint8_t>(color[2]),
            static_cast<std::uint8_t>(color[3]) } };
    }

    // Two triangles covering x0..x1, y0..y1 in clip space, at depth z.
    void appendQuad(std::vector<ColorVertex>& vertices, float x0, float y0, float x1, float y1,
        float z, const Rgba& color)
    {
        for ( const auto& [x, y] : { std::array{ x0, y1 }, std::array{ x1, y1 },
            std::array{ x0, y0 }, std::array{ x0, y0 }, std::array{ x1, y1 },
            std::array{ x1, y0 } } )
        {
            vertices.push_back(colorVertex(x, y, z, color));
        }
    }

    PipelineDesc colorPipeline(bool depthTest)
    {
        PipelineDesc desc;
        desc.vertexShader = shaders::colorVert();
        desc.fragmentShader = shaders::colorFrag();
        desc.vertexLayout.bufferStrides = { sizeof(ColorVertex) };
        desc.vertexLayout.attributes = {
            VertexAttribute{ 0, 0, VertexFormat::Float3, 0 },
            VertexAttribute{ 1, 0, VertexFormat::UByte4Norm, 12 },
        };
        desc.colorTargets = { ColorTargetDesc{ TextureFormat::Rgba8, {} } };
        if ( depthTest )
        {
            desc.depthFormat = TextureFormat::D32F;
            desc.depth = DepthState{ true, true, CompareOp::Less };
        }
        return desc;
    }

    PipelineDesc texturedPipeline()
    {
        PipelineDesc desc;
        desc.vertexShader = shaders::texturedVert();
        desc.fragmentShader = shaders::texturedFrag();
        desc.vertexLayout.bufferStrides = { sizeof(TexturedVertex) };
        desc.vertexLayout.attributes = {
            VertexAttribute{ 0, 0, VertexFormat::Float2, 0 },
            VertexAttribute{ 1, 0, VertexFormat::Float2, 8 },
        };
        desc.colorTargets = { ColorTargetDesc{ TextureFormat::Rgba8, {} } };
        return desc;
    }

    // texturedPipeline() with storage.frag.hlsl (the texture times element 1
    // of the storage buffer in fragment storage slot 0), as a strip.
    PipelineDesc storagePipeline()
    {
        PipelineDesc desc = texturedPipeline();
        desc.fragmentShader = shaders::storageFrag();
        desc.primitive = PrimitiveType::TriangleStrip;
        return desc;
    }

    PassDesc clearedPass(TextureHandle color, TextureHandle depth = {})
    {
        PassDesc pass;
        pass.colorTargets = { ColorAttachment{ color, 0, LoadOp::Clear, StoreOp::Store,
            ClearColor{ 0.0f, 0.0f, 0.0f, 1.0f } } };
        if ( !depth.isNull() )
        {
            pass.depthTarget = DepthAttachment{ depth, LoadOp::Clear, StoreOp::DontCare, 1.0f, 0 };
        }
        return pass;
    }

    TextureHandle colorTarget(Device& device)
    {
        const TextureHandle target = device.createTexture(TextureDesc{ kSize, kSize, 1,
            TextureFormat::Rgba8, TextureUsage::ColorTarget });
        REQUIRE_FALSE(target.isNull());
        return target;
    }

    // What a scene leaves for the checks: the target it drew to, the
    // pipeline descs it created in order, the commands it submitted.
    struct Drawn
    {
        TextureHandle target;
        TextureHandle second;   // the second color target, if the scene has one
        std::vector<PipelineHandle> pipelines;
        std::vector<PipelineDesc> pipelineDescs;
        std::vector<Command> commands;
        std::uint64_t frame = 0;
    };

    PipelineHandle createPipeline(Device& device, Drawn& drawn, const PipelineDesc& desc)
    {
        const PipelineHandle pipeline = device.createPipeline(desc);
        REQUIRE_FALSE(pipeline.isNull());
        drawn.pipelines.push_back(pipeline);
        drawn.pipelineDescs.push_back(desc);
        return pipeline;
    }

    void submit(Device& device, CommandList& list, Drawn& drawn, std::uint64_t frame)
    {
        drawn.commands = list.commands();
        drawn.frame = frame;
        REQUIRE(device.submit(list));
    }

    // A green triangle, its vertices streamed with updateBuffer.
    Drawn drawTriangle(Device& device)
    {
        Drawn drawn;
        drawn.target = colorTarget(device);
        const BufferHandle vertices = device.createBuffer(BufferDesc{ 3 * sizeof(ColorVertex),
            BufferUsage::Vertex });
        REQUIRE_FALSE(vertices.isNull());
        const PipelineHandle pipeline = createPipeline(device, drawn, colorPipeline(false));

        const std::vector<ColorVertex> triangle = {
            colorVertex(-0.75f, -0.75f, 0.5f, kGreen),
            colorVertex(0.75f, -0.75f, 0.5f, kGreen),
            colorVertex(0.0f, 0.75f, 0.5f, kGreen),
        };
        CommandList list = device.beginFrame();
        list.updateBuffer(vertices, 0, bytesOf(triangle));
        REQUIRE(list.beginPass(clearedPass(drawn.target)));
        list.setPipeline(pipeline);
        list.bindVertexBuffer(0, vertices);
        list.draw(3);
        REQUIRE(list.endPass());
        submit(device, list, drawn, 1);
        return drawn;
    }

    // A full-target quad textured with a 2x2 texture (red, green / blue,
    // white), nearest filtering, tinted (1, 0.5, 1, 1); indexed, uploaded
    // with uploadBuffer.
    Drawn drawTexturedQuad(Device& device)
    {
        Drawn drawn;
        drawn.target = colorTarget(device);
        const TextureHandle texture = device.createTexture(TextureDesc{ 2, 2, 1,
            TextureFormat::Rgba8, TextureUsage::Sampled });
        REQUIRE_FALSE(texture.isNull());
        const std::vector<std::uint8_t> texels = {
            255, 0, 0, 255,   0, 255, 0, 255,
            0, 0, 255, 255,   255, 255, 255, 255,
        };
        REQUIRE(device.uploadTexture(texture, 0, bytesOf(texels)));
        const SamplerHandle sampler = device.createSampler(SamplerDesc{ Filter::Nearest,
            Filter::Nearest, Filter::Nearest, AddressMode::ClampToEdge,
            AddressMode::ClampToEdge });
        REQUIRE_FALSE(sampler.isNull());

        const std::vector<TexturedVertex> quad = {
            { -1.0f, 1.0f, 0.0f, 0.0f },
            { 1.0f, 1.0f, 1.0f, 0.0f },
            { -1.0f, -1.0f, 0.0f, 1.0f },
            { 1.0f, -1.0f, 1.0f, 1.0f },
        };
        const std::vector<std::uint16_t> indices = { 0, 1, 2, 2, 1, 3 };
        const BufferHandle vertexBuffer = device.createBuffer(BufferDesc{
            static_cast<std::uint32_t>(quad.size() * sizeof(TexturedVertex)), BufferUsage::Vertex });
        const BufferHandle indexBuffer = device.createBuffer(BufferDesc{
            static_cast<std::uint32_t>(indices.size() * sizeof(std::uint16_t)), BufferUsage::Index });
        REQUIRE_FALSE(vertexBuffer.isNull());
        REQUIRE_FALSE(indexBuffer.isNull());
        REQUIRE(device.uploadBuffer(vertexBuffer, 0, bytesOf(quad)));
        REQUIRE(device.uploadBuffer(indexBuffer, 0, bytesOf(indices)));
        const PipelineHandle pipeline = createPipeline(device, drawn, texturedPipeline());

        const std::vector<float> tint = { 1.0f, 0.5f, 1.0f, 1.0f };
        CommandList list = device.beginFrame();
        REQUIRE(list.beginPass(clearedPass(drawn.target)));
        list.setPipeline(pipeline);
        list.bindVertexBuffer(0, vertexBuffer);
        list.bindIndexBuffer(indexBuffer, IndexType::Uint16);
        list.bindTexture(ShaderStage::Fragment, 0, texture, sampler);
        list.pushUniforms(ShaderStage::Fragment, 0, bytesOf(tint));
        list.drawIndexed(6);
        REQUIRE(list.endPass());
        submit(device, list, drawn, 1);
        return drawn;
    }

    // drawTexturedQuad() with storage.frag.hlsl: the tint is element 1 of a
    // storage buffer instead of a uniform. The buffer is uploaded with a
    // white element 1 first and then rewritten by an updateBuffer in the
    // frame's list, before the pass, so the draw shows that the shader reads
    // the bound buffer, the element it indexes and the list's update.
    Drawn drawStorageQuad(Device& device)
    {
        Drawn drawn;
        drawn.target = colorTarget(device);
        const TextureHandle texture = device.createTexture(TextureDesc{ 2, 2, 1,
            TextureFormat::Rgba8, TextureUsage::Sampled });
        REQUIRE_FALSE(texture.isNull());
        const std::vector<std::uint8_t> texels = {
            255, 0, 0, 255,   0, 255, 0, 255,
            0, 0, 255, 255,   255, 255, 255, 255,
        };
        REQUIRE(device.uploadTexture(texture, 0, bytesOf(texels)));
        const SamplerHandle sampler = device.createSampler(SamplerDesc{ Filter::Nearest,
            Filter::Nearest, Filter::Nearest, AddressMode::ClampToEdge,
            AddressMode::ClampToEdge });
        REQUIRE_FALSE(sampler.isNull());

        const std::vector<TexturedVertex> quad = {
            { -1.0f, 1.0f, 0.0f, 0.0f },
            { 1.0f, 1.0f, 1.0f, 0.0f },
            { -1.0f, -1.0f, 0.0f, 1.0f },
            { 1.0f, -1.0f, 1.0f, 1.0f },
        };
        const BufferHandle vertexBuffer = device.createBuffer(BufferDesc{
            static_cast<std::uint32_t>(quad.size() * sizeof(TexturedVertex)), BufferUsage::Vertex });
        REQUIRE_FALSE(vertexBuffer.isNull());
        REQUIRE(device.uploadBuffer(vertexBuffer, 0, bytesOf(quad)));

        const std::vector<float> white = {
            0.0f, 0.0f, 0.0f, 0.0f,   1.0f, 1.0f, 1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f,
        };
        const std::vector<float> data = {
            0.0f, 0.0f, 0.0f, 0.0f,   1.0f, 0.5f, 1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f,
        };
        const BufferHandle storage = device.createBuffer(BufferDesc{
            static_cast<std::uint32_t>(data.size() * sizeof(float)), BufferUsage::Storage });
        REQUIRE_FALSE(storage.isNull());
        REQUIRE(device.uploadBuffer(storage, 0, bytesOf(white)));
        const PipelineHandle pipeline = createPipeline(device, drawn, storagePipeline());

        CommandList list = device.beginFrame();
        list.updateBuffer(storage, 0, bytesOf(data));
        REQUIRE(list.beginPass(clearedPass(drawn.target)));
        list.setPipeline(pipeline);
        list.bindVertexBuffer(0, vertexBuffer);
        list.bindTexture(ShaderStage::Fragment, 0, texture, sampler);
        list.bindStorageBuffer(ShaderStage::Fragment, 0, storage);
        list.draw(4);
        REQUIRE(list.endPass());
        submit(device, list, drawn, 1);
        return drawn;
    }

    // Two overlapping quads with a D32F depth target and LESS: the near red
    // one (z 0.25, columns 8..39) drawn first, then the far blue one (z 0.75,
    // columns 24..55), so the overlap stays red; both on rows 16..47.
    Drawn drawDepthQuads(Device& device)
    {
        Drawn drawn;
        drawn.target = colorTarget(device);
        const TextureHandle depth = device.createTexture(TextureDesc{ kSize, kSize, 1,
            TextureFormat::D32F, TextureUsage::DepthTarget });
        REQUIRE_FALSE(depth.isNull());
        std::vector<ColorVertex> quads;
        appendQuad(quads, -0.75f, -0.5f, 0.25f, 0.5f, 0.25f, kRed);
        appendQuad(quads, -0.25f, -0.5f, 0.75f, 0.5f, 0.75f, kBlue);
        const BufferHandle vertices = device.createBuffer(BufferDesc{
            static_cast<std::uint32_t>(quads.size() * sizeof(ColorVertex)), BufferUsage::Vertex });
        REQUIRE_FALSE(vertices.isNull());
        const PipelineHandle pipeline = createPipeline(device, drawn, colorPipeline(true));

        CommandList list = device.beginFrame();
        list.updateBuffer(vertices, 0, bytesOf(quads));
        REQUIRE(list.beginPass(clearedPass(drawn.target, depth)));
        list.setPipeline(pipeline);
        list.bindVertexBuffer(0, vertices);
        list.draw(6, 0);
        list.draw(6, 6);
        REQUIRE(list.endPass());
        submit(device, list, drawn, 1);
        return drawn;
    }

    // The second color target of drawTwoTargets(): R10G10B10A2 where the
    // device can render to it, else Rgba8.
    TextureFormat secondTargetFormat(Device& device)
    {
        return device.supportsFormat(TextureFormat::R10G10B10A2, TextureUsage::ColorTarget)
            ? TextureFormat::R10G10B10A2 : TextureFormat::Rgba8;
    }

    // A red quad on rows and columns 16..47 into two color targets at once
    // (twotargets.frag.hlsl): red into the Rgba8 target, cleared to black,
    // and blue into the second, cleared to (0, 1, 0, 0).
    Drawn drawTwoTargets(Device& device)
    {
        Drawn drawn;
        drawn.target = colorTarget(device);
        const TextureFormat secondFormat = secondTargetFormat(device);
        drawn.second = device.createTexture(TextureDesc{ kSize, kSize, 1, secondFormat,
            TextureUsage::ColorTarget });
        REQUIRE_FALSE(drawn.second.isNull());
        std::vector<ColorVertex> quad;
        appendQuad(quad, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f, kRed);
        const BufferHandle vertices = device.createBuffer(BufferDesc{
            static_cast<std::uint32_t>(quad.size() * sizeof(ColorVertex)), BufferUsage::Vertex });
        REQUIRE_FALSE(vertices.isNull());
        PipelineDesc desc = colorPipeline(false);
        desc.fragmentShader = shaders::twotargetsFrag();
        desc.colorTargets.push_back(ColorTargetDesc{ secondFormat, {} });
        const PipelineHandle pipeline = createPipeline(device, drawn, desc);

        CommandList list = device.beginFrame();
        list.updateBuffer(vertices, 0, bytesOf(quad));
        PassDesc pass = clearedPass(drawn.target);
        pass.colorTargets.push_back(ColorAttachment{ drawn.second, 0, LoadOp::Clear,
            StoreOp::Store, ClearColor{ 0.0f, 1.0f, 0.0f, 0.0f } });
        REQUIRE(list.beginPass(pass));
        list.setPipeline(pipeline);
        list.bindVertexBuffer(0, vertices);
        list.draw(6);
        REQUIRE(list.endPass());
        submit(device, list, drawn, 1);
        return drawn;
    }

    // One texel of the second target of drawTwoTargets(), written out by
    // hand from Texture.h: the clear (0, 1, 0, 0) or opaque blue.
    std::vector<std::byte> secondTexel(TextureFormat format, bool blue)
    {
        const auto bytes = [](std::array<unsigned, 4> values)
        {
            return std::vector<std::byte>{ std::byte(values[0]), std::byte(values[1]),
                std::byte(values[2]), std::byte(values[3]) };
        };
        if ( format == TextureFormat::R10G10B10A2 )
        {
            return blue ? bytes({ 0x00, 0x00, 0xF0, 0xFF }) : bytes({ 0x00, 0xFC, 0x0F, 0x00 });
        }
        return blue ? bytes({ 0x00, 0x00, 0xFF, 0xFF }) : bytes({ 0x00, 0xFF, 0x00, 0x00 });
    }

    // Checks the second target of drawTwoTargets() texel by texel: blue
    // inside the quad when drawn, the clear everywhere else.
    void checkSecondTarget(const std::vector<std::byte>& bytes, TextureFormat format, bool drawn)
    {
        REQUIRE(bytes.size() == std::size_t{ kSize } * kSize * 4);
        int wrong = 0;
        for ( std::uint32_t y = 0; y < kSize; ++y )
        {
            for ( std::uint32_t x = 0; x < kSize; ++x )
            {
                const bool inside = drawn && x >= 16 && x < 48 && y >= 16 && y < 48;
                const std::vector<std::byte> want = secondTexel(format, inside);
                if ( std::memcmp(bytes.data() + (std::size_t{ y } * kSize + x) * 4, want.data(), 4) != 0 )
                {
                    ++wrong;
                }
            }
        }
        CHECK(wrong == 0);
    }

    struct Image
    {
        std::vector<std::byte> bytes;

        Rgba at(std::uint32_t x, std::uint32_t y) const
        {
            const std::size_t i = (std::size_t{ y } * kSize + x) * 4;
            return Rgba{ std::to_integer<int>(bytes[i]), std::to_integer<int>(bytes[i + 1]),
                std::to_integer<int>(bytes[i + 2]), std::to_integer<int>(bytes[i + 3]) };
        }
    };

    bool within(const Rgba& a, const Rgba& b, int tolerance)
    {
        for ( std::size_t i = 0; i < a.size(); ++i )
        {
            if ( std::abs(a[i] - b[i]) > tolerance )
            {
                return false;
            }
        }
        return true;
    }

    std::string text(const Rgba& c)
    {
        return "(" + std::to_string(c[0]) + ", " + std::to_string(c[1]) + ", "
            + std::to_string(c[2]) + ", " + std::to_string(c[3]) + ")";
    }

    // Compares every pixel with expected(x, y); reports the first few that
    // differ by more than tolerance and how many do.
    void checkImage(const Image& image, const std::function<Rgba(std::uint32_t, std::uint32_t)>& expected,
        int tolerance)
    {
        REQUIRE(image.bytes.size() == std::size_t{ kSize } * kSize * 4);
        int wrong = 0;
        std::string first;
        for ( std::uint32_t y = 0; y < kSize; ++y )
        {
            for ( std::uint32_t x = 0; x < kSize; ++x )
            {
                const Rgba want = expected(x, y);
                const Rgba got = image.at(x, y);
                if ( !within(got, want, tolerance) )
                {
                    if ( wrong < 4 )
                    {
                        first += " (" + std::to_string(x) + ", " + std::to_string(y) + "): "
                            + text(got) + " instead of " + text(want) + ";";
                    }
                    ++wrong;
                }
            }
        }
        INFO("first differing pixels:" << first);
        CHECK(wrong == 0);
    }

    // The Null device records each pipeline creation, then every submitted
    // command as it was recorded, then the frame's end.
    void checkNullRecord(const NullDevice& device, const Drawn& drawn)
    {
        std::vector<RecordedCommand> expected;
        for ( std::size_t i = 0; i < drawn.pipelines.size(); ++i )
        {
            expected.push_back(PipelineCreated{ drawn.pipelines[i], drawn.pipelineDescs[i] });
        }
        for ( const Command& command : drawn.commands )
        {
            std::visit([&](const auto& c) { expected.push_back(c); }, command);
        }
        expected.push_back(FrameSubmitted{ drawn.frame, false });
        REQUIRE(device.recorded().size() == expected.size());
        for ( std::size_t i = 0; i < expected.size(); ++i )
        {
            INFO("entry " << i);
            CHECK(device.recorded()[i] == expected[i]);
        }
    }

    using Scene = Drawn (*)(Device&);

    // Runs scene on a Null device: same pipelines accepted, same commands
    // recorded, no warning, and the target still holds the clear color.
    void runOnNull(Scene scene)
    {
        CaptureSink sink;
        const std::unique_ptr<NullDevice> device = createNullDevice();
        const Drawn drawn = scene(*device);
        INFO("captured log: " << joined(sink));
        CHECK(sink.records().empty());
        checkNullRecord(*device, drawn);
        const Image image{ device->readTexture(drawn.target, 0) };
        checkImage(image, [](std::uint32_t, std::uint32_t) { return kBlack; }, 0);
    }

    // Runs scene on a headless SDL GPU device and returns what it drew, or
    // nothing (skipped, or failed with BABYLON_REQUIRE_GPU=1) without one.
    std::optional<Image> runOnSdl(Scene scene)
    {
        test::keepGpuDriverLoaded();
        CaptureSink sink;
        const std::unique_ptr<Device> device = createSdlGpuDevice(nullptr);
        if ( device == nullptr )
        {
            test::noGpuDevice("no GPU device: " + joined(sink));
            return std::nullopt;
        }
        MESSAGE("drawing on " << device->info().backend);
        const Drawn drawn = scene(*device);
        Image image{ device->readTexture(drawn.target, 0) };
        INFO("captured log: " << joined(sink));
        CHECK(sink.records().empty());
        return image;
    }
}

TEST_CASE("babylon_add_shaders reflects the resource counts and builds every format")
{
    struct Expected
    {
        const char* name;
        const ShaderDesc& desc;
        std::uint32_t textures;
        std::uint32_t uniforms;
        std::uint32_t storageBuffers;
    };
    const Expected shadersExpected[] = {
        { "colorVert", shaders::colorVert(), 0, 0, 0 },
        { "colorFrag", shaders::colorFrag(), 0, 0, 0 },
        { "texturedVert", shaders::texturedVert(), 0, 0, 0 },
        { "texturedFrag", shaders::texturedFrag(), 1, 1, 0 },
        { "twotargetsFrag", shaders::twotargetsFrag(), 0, 0, 0 },
        { "storageFrag", shaders::storageFrag(), 1, 0, 1 },
    };
    for ( const Expected& expected : shadersExpected )
    {
        INFO("shader " << expected.name);
        CHECK(expected.desc.textureCount == expected.textures);
        CHECK(expected.desc.uniformCount == expected.uniforms);
        CHECK(expected.desc.storageBufferCount == expected.storageBuffers);
        REQUIRE(expected.desc.blobs.size() == 3);
        const ShaderBlob& spirv = expected.desc.blobs[0];
        const ShaderBlob& dxil = expected.desc.blobs[1];
        const ShaderBlob& msl = expected.desc.blobs[2];
        CHECK(spirv.format == ShaderFormat::SpirV);
        CHECK(dxil.format == ShaderFormat::Dxil);
        CHECK(msl.format == ShaderFormat::Msl);
        // SPIR-V starts with its magic word 0x07230203 (little endian), a
        // DXIL container with "DXBC"; the MSL names its entry point.
        REQUIRE(spirv.code.size() >= 4);
        CHECK(std::memcmp(spirv.code.data(), "\x03\x02\x23\x07", 4) == 0);
        REQUIRE(dxil.code.size() >= 4);
        CHECK(std::memcmp(dxil.code.data(), "DXBC", 4) == 0);
        CHECK(spirv.entryPoint == "main");
        CHECK(dxil.entryPoint == "main");
        const std::string mslText(reinterpret_cast<const char*>(msl.code.data()), msl.code.size());
        CHECK(msl.entryPoint == "main0");
        CHECK(mslText.find(" " + msl.entryPoint + "(") != std::string::npos);
    }
}

TEST_CASE("The Null device accepts the shader pipelines and records their frames exactly")
{
    SUBCASE("triangle") { runOnNull(drawTriangle); }
    SUBCASE("textured quad") { runOnNull(drawTexturedQuad); }
    SUBCASE("quad tinted from a storage buffer") { runOnNull(drawStorageQuad); }
    SUBCASE("depth-tested quads") { runOnNull(drawDepthQuads); }
    SUBCASE("two color targets")
    {
        runOnNull(drawTwoTargets);
        CaptureSink sink;
        const std::unique_ptr<NullDevice> device = createNullDevice();
        const Drawn drawn = drawTwoTargets(*device);
        checkSecondTarget(device->readTexture(drawn.second, 0), secondTargetFormat(*device), false);
        CHECK(sink.records().empty());
    }
}

TEST_CASE("SDL GPU draws a vertex-colored triangle")
{
    const std::optional<Image> image = runOnSdl(drawTriangle);
    if ( !image.has_value() )
    {
        return;
    }
    REQUIRE(image->bytes.size() == std::size_t{ kSize } * kSize * 4);
    // Apex at row 8, base on row 56 from column 8 to 56. Pixels well inside
    // and outside, including two that tell the apex from the base.
    const std::array<std::array<std::uint32_t, 2>, 5> inside = { { { 32, 32 }, { 32, 12 },
        { 12, 52 }, { 52, 52 }, { 32, 54 } } };
    const std::array<std::array<std::uint32_t, 2>, 7> outside = { { { 4, 4 }, { 60, 4 },
        { 12, 12 }, { 52, 12 }, { 12, 20 }, { 52, 20 }, { 32, 60 } } };
    for ( const auto& [x, y] : inside )
    {
        INFO("inside pixel (" << x << ", " << y << ") is " << text(image->at(x, y)));
        CHECK(within(image->at(x, y), kGreen, 0));
    }
    for ( const auto& [x, y] : outside )
    {
        INFO("outside pixel (" << x << ", " << y << ") is " << text(image->at(x, y)));
        CHECK(within(image->at(x, y), kBlack, 0));
    }
}

TEST_CASE("SDL GPU draws a textured, tinted quad")
{
    const std::optional<Image> image = runOnSdl(drawTexturedQuad);
    if ( !image.has_value() )
    {
        return;
    }
    // Each texel covers one 32x32 quadrant; green is halved by the tint,
    // 127.5 rounding either way.
    checkImage(*image, [](std::uint32_t x, std::uint32_t y)
    {
        const bool right = x >= 32;
        const bool bottom = y >= 32;
        if ( !bottom )
        {
            return right ? Rgba{ 0, 128, 0, 255 } : kRed;
        }
        return right ? Rgba{ 255, 128, 255, 255 } : kBlue;
    }, 1);
}

TEST_CASE("SDL GPU reads a fragment storage buffer at its slot and element")
{
    const std::optional<Image> image = runOnSdl(drawStorageQuad);
    if ( !image.has_value() )
    {
        return;
    }
    // drawTexturedQuad()'s image: element 1 of the updated buffer is its tint.
    checkImage(*image, [](std::uint32_t x, std::uint32_t y)
    {
        const bool right = x >= 32;
        const bool bottom = y >= 32;
        if ( !bottom )
        {
            return right ? Rgba{ 0, 128, 0, 255 } : kRed;
        }
        return right ? Rgba{ 255, 128, 255, 255 } : kBlue;
    }, 1);
}

TEST_CASE("SDL GPU keeps the nearer of two quads with a LESS depth test")
{
    const std::optional<Image> image = runOnSdl(drawDepthQuads);
    if ( !image.has_value() )
    {
        return;
    }
    checkImage(*image, [](std::uint32_t x, std::uint32_t y)
    {
        if ( y < 16 || y >= 48 || x < 8 || x >= 56 )
        {
            return kBlack;
        }
        return x < 40 ? kRed : kBlue;
    }, 0);
}

TEST_CASE("SDL GPU draws into two color targets in one pass")
{
    test::keepGpuDriverLoaded();
    CaptureSink sink;
    const std::unique_ptr<Device> device = createSdlGpuDevice(nullptr);
    if ( device == nullptr )
    {
        test::noGpuDevice("no GPU device: " + joined(sink));
        return;
    }
    const Drawn drawn = drawTwoTargets(*device);
    const Image image{ device->readTexture(drawn.target, 0) };
    const std::vector<std::byte> second = device->readTexture(drawn.second, 0);
    INFO("captured log: " << joined(sink));
    CHECK(sink.records().empty());
    checkImage(image, [](std::uint32_t x, std::uint32_t y)
    {
        return x >= 16 && x < 48 && y >= 16 && y < 48 ? kRed : kBlack;
    }, 0);
    MESSAGE("second target format " << static_cast<int>(secondTargetFormat(*device)));
    checkSecondTarget(second, secondTargetFormat(*device), true);
}

namespace
{
    // The textured quad's pipeline drawn with its texture or its tint left
    // out: the draw is dropped with one warning and the target keeps its
    // clear color, on every device.
    void checkUnfilledSlotsDropDraws(Device& device, CaptureSink& sink)
    {
        const TextureHandle target = colorTarget(device);
        const TextureHandle texture = device.createTexture(TextureDesc{ 2, 2, 1,
            TextureFormat::Rgba8, TextureUsage::Sampled });
        const SamplerHandle sampler = device.createSampler(SamplerDesc{});
        const std::vector<TexturedVertex> quad = {
            { -1.0f, 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 0.0f },
            { -1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, -1.0f, 1.0f, 1.0f },
        };
        const BufferHandle vertices = device.createBuffer(BufferDesc{
            static_cast<std::uint32_t>(quad.size() * sizeof(TexturedVertex)), BufferUsage::Vertex });
        REQUIRE(device.uploadBuffer(vertices, 0, bytesOf(quad)));
        PipelineDesc desc = texturedPipeline();
        desc.primitive = PrimitiveType::TriangleStrip;
        const PipelineHandle pipeline = device.createPipeline(desc);
        REQUIRE_FALSE(pipeline.isNull());
        const std::vector<float> tint = { 1.0f, 1.0f, 1.0f, 1.0f };
        sink.clear();

        for ( const bool bindTheTexture : { false, true } )
        {
            INFO("texture bound: " << bindTheTexture);
            CommandList list = device.beginFrame();
            REQUIRE(list.beginPass(clearedPass(target)));
            list.setPipeline(pipeline);
            list.bindVertexBuffer(0, vertices);
            if ( bindTheTexture )
            {
                list.bindTexture(ShaderStage::Fragment, 0, texture, sampler);
            }
            else
            {
                list.pushUniforms(ShaderStage::Fragment, 0, bytesOf(tint));
            }
            list.draw(4);
            REQUIRE(list.endPass());
            REQUIRE(device.submit(list));
            INFO("captured log: " << joined(sink));
            REQUIRE(sink.records().size() == 1);
            CHECK(sink.records()[0].message == (bindTheTexture
                ? "render: submit: dropped draw: the fragment shader reads 1 uniform blocks, "
                    "slot 0 was not pushed"
                : "render: submit: dropped draw: the fragment shader reads 1 textures, "
                    "slot 0 is not bound"));
            sink.clear();
            const Image image{ device.readTexture(target, 0) };
            checkImage(image, [](std::uint32_t, std::uint32_t) { return kBlack; }, 0);
        }
    }
}

TEST_CASE("A draw whose shader slots weren't filled is dropped on every device")
{
    SUBCASE("null")
    {
        CaptureSink sink;
        const std::unique_ptr<NullDevice> device = createNullDevice();
        checkUnfilledSlotsDropDraws(*device, sink);
        // Nothing of the pass's draws was executed.
        for ( const RecordedCommand& command : device->recorded() )
        {
            CHECK_FALSE(std::holds_alternative<Draw>(command));
        }
    }
    SUBCASE("sdl-gpu headless")
    {
        test::keepGpuDriverLoaded();
        CaptureSink sink;
        const std::unique_ptr<Device> device = createSdlGpuDevice(nullptr);
        if ( device == nullptr )
        {
            test::noGpuDevice("no GPU device: " + joined(sink));
            return;
        }
        checkUnfilledSlotsDropDraws(*device, sink);
        CHECK(sink.records().empty());
    }
}

namespace
{
    // The storage quad's pipeline drawn without its storage buffer, and with
    // a buffer that lacks Storage usage in its slot: either way the draw is
    // dropped after one warning and the target keeps its clear color.
    void checkUnboundStorageDropsDraws(Device& device, CaptureSink& sink)
    {
        const TextureHandle target = colorTarget(device);
        const TextureHandle texture = device.createTexture(TextureDesc{ 2, 2, 1,
            TextureFormat::Rgba8, TextureUsage::Sampled });
        const SamplerHandle sampler = device.createSampler(SamplerDesc{});
        const std::vector<TexturedVertex> quad = {
            { -1.0f, 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 0.0f },
            { -1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, -1.0f, 1.0f, 1.0f },
        };
        const BufferHandle vertices = device.createBuffer(BufferDesc{
            static_cast<std::uint32_t>(quad.size() * sizeof(TexturedVertex)), BufferUsage::Vertex });
        REQUIRE(device.uploadBuffer(vertices, 0, bytesOf(quad)));
        const PipelineHandle pipeline = device.createPipeline(storagePipeline());
        REQUIRE_FALSE(pipeline.isNull());
        sink.clear();

        for ( const bool bindVertexBufferAsStorage : { false, true } )
        {
            INFO("a vertex buffer in the storage slot: " << bindVertexBufferAsStorage);
            CommandList list = device.beginFrame();
            REQUIRE(list.beginPass(clearedPass(target)));
            list.setPipeline(pipeline);
            list.bindVertexBuffer(0, vertices);
            list.bindTexture(ShaderStage::Fragment, 0, texture, sampler);
            if ( bindVertexBufferAsStorage )
            {
                list.bindStorageBuffer(ShaderStage::Fragment, 0, vertices);
            }
            list.draw(4);
            REQUIRE(list.endPass());
            REQUIRE(device.submit(list));
            INFO("captured log: " << joined(sink));
            REQUIRE(sink.records().size() == 1);
            CHECK(sink.records()[0].message == (bindVertexBufferAsStorage
                ? "render: submit: dropped bindStorageBuffer: the buffer has no Storage usage"
                : "render: submit: dropped draw: the fragment shader reads 1 storage buffers, "
                    "slot 0 is not bound"));
            sink.clear();
            const Image image{ device.readTexture(target, 0) };
            checkImage(image, [](std::uint32_t, std::uint32_t) { return kBlack; }, 0);
        }
    }
}

TEST_CASE("A draw whose storage buffer slot isn't filled is dropped on every device")
{
    SUBCASE("null")
    {
        CaptureSink sink;
        const std::unique_ptr<NullDevice> device = createNullDevice();
        checkUnboundStorageDropsDraws(*device, sink);
        for ( const RecordedCommand& command : device->recorded() )
        {
            CHECK_FALSE(std::holds_alternative<Draw>(command));
            CHECK_FALSE(std::holds_alternative<BindStorageBuffer>(command));
        }
    }
    SUBCASE("sdl-gpu headless")
    {
        test::keepGpuDriverLoaded();
        CaptureSink sink;
        const std::unique_ptr<Device> device = createSdlGpuDevice(nullptr);
        if ( device == nullptr )
        {
            test::noGpuDevice("no GPU device: " + joined(sink));
            return;
        }
        checkUnboundStorageDropsDraws(*device, sink);
        CHECK(sink.records().empty());
    }
}
