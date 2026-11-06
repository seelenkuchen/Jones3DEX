// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/render/Buffer.h>
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Handle.h>
#include <babylon/render/NullDevice.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Texture.h>

#include <babylon/log/CaptureSink.h>
#include <babylon/log/Log.h>
#include <babylon/log/Sink.h>

#include <doctest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace
{
    using babylon::log::CaptureSink;
    using babylon::log::Level;
    using namespace babylon::render;

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

    void oneWarning(CaptureSink& sink)
    {
        INFO("captured log: " << joined(sink));
        REQUIRE(sink.records().size() == 1);
        CHECK(sink.records()[0].level == Level::Warning);
        sink.clear();
    }

    void noWarning(CaptureSink& sink)
    {
        INFO("captured log: " << joined(sink));
        CHECK(sink.records().empty());
        sink.clear();
    }

    // A color attachment with the defaults of ColorAttachment: clear to
    // opaque black, store.
    ColorAttachment attachment(TextureHandle target)
    {
        ColorAttachment result;
        result.target = target;
        return result;
    }

    ShaderDesc shader(unsigned tag, std::uint32_t textures, std::uint32_t uniforms)
    {
        ShaderDesc desc;
        desc.blobs.push_back(ShaderBlob{ ShaderFormat::SpirV,
            { std::byte{ 0x03 }, std::byte{ 0x02 }, std::byte{ 0x23 },
                static_cast<std::byte>(tag) }, "main" });
        desc.textureCount = textures;
        desc.uniformCount = uniforms;
        return desc;
    }

    // A pipeline for 32-byte pre-transformed vertices: position xyzw,
    // two packed colors and one texture coordinate pair, in one buffer.
    PipelineDesc transformedVertexPipeline(TextureFormat color, std::optional<TextureFormat> depth)
    {
        PipelineDesc desc;
        desc.vertexShader = shader(1, 0, 1);
        desc.fragmentShader = shader(2, 1, 1);
        desc.vertexLayout.bufferStrides = { 32 };
        desc.vertexLayout.attributes = {
            VertexAttribute{ 0, 0, VertexFormat::Float4, 0 },
            VertexAttribute{ 1, 0, VertexFormat::UByte4Norm, 16 },
            VertexAttribute{ 2, 0, VertexFormat::UByte4Norm, 20 },
            VertexAttribute{ 3, 0, VertexFormat::Float2, 24 },
        };
        desc.primitive = PrimitiveType::TriangleList;
        BlendState blend;
        blend.enabled = true;
        blend.srcColor = BlendFactor::SrcAlpha;
        blend.dstColor = BlendFactor::OneMinusSrcAlpha;
        desc.colorTargets = { ColorTargetDesc{ color, blend } };
        desc.depthFormat = depth;
        if ( depth.has_value() )
        {
            desc.depth = DepthState{ true, true, CompareOp::LessOrEqual };
        }
        return desc;
    }

    // The same pipeline with shaders that read no textures and no uniform
    // blocks, for draws that bind neither.
    PipelineDesc withoutShaderResources(PipelineDesc desc)
    {
        desc.vertexShader.textureCount = 0;
        desc.vertexShader.uniformCount = 0;
        desc.fragmentShader.textureCount = 0;
        desc.fragmentShader.uniformCount = 0;
        return desc;
    }

    std::vector<std::byte> bytes(std::size_t size, unsigned seed)
    {
        std::vector<std::byte> data(size);
        for ( std::size_t i = 0; i < size; ++i )
        {
            data[i] = static_cast<std::byte>((seed + i * 13u) & 0xFFu);
        }
        return data;
    }
}

TEST_CASE("The Null device records pipeline creations and a submitted frame exactly")
{
    CaptureSink sink;
    const std::unique_ptr<NullDevice> device = createNullDevice();
    CHECK(device->info().backend == "null");
    CHECK_FALSE(device->info().hasSwapchain);

    const TextureHandle depth = device->createTexture(TextureDesc{ 64, 64, 1,
        TextureFormat::D24S8, TextureUsage::DepthTarget });
    const TextureHandle texture = device->createTexture(TextureDesc{ 8, 8, 4,
        TextureFormat::B5G6R5, TextureUsage::Sampled });
    const TextureHandle offscreen = device->createTexture(TextureDesc{ 8, 8, 1,
        TextureFormat::B5G6R5, TextureUsage::Sampled | TextureUsage::ColorTarget });
    const BufferHandle vertices = device->createBuffer(BufferDesc{ 32 * 6, BufferUsage::Vertex });
    const BufferHandle indices = device->createBuffer(BufferDesc{ 2 * 6, BufferUsage::Index });
    const SamplerHandle sampler = device->createSampler(SamplerDesc{});
    const PipelineDesc lineDesc = [&]
    {
        PipelineDesc desc = transformedVertexPipeline(TextureFormat::Bgra8, TextureFormat::D24S8);
        desc.primitive = PrimitiveType::LineStrip;
        return desc;
    }();
    const PipelineDesc triangleDesc =
        transformedVertexPipeline(TextureFormat::Bgra8, TextureFormat::D24S8);
    const PipelineHandle lines = device->createPipeline(lineDesc);
    const PipelineHandle triangles = device->createPipeline(triangleDesc);
    REQUIRE_FALSE(lines.isNull());
    REQUIRE_FALSE(triangles.isNull());
    noWarning(sink);

    const std::vector<std::byte> uniforms = bytes(64, 5);
    CommandList frame = device->beginFrame();
    PassDesc pass;
    pass.colorTargets = { ColorAttachment{ TextureHandle{}, 0, LoadOp::Clear, StoreOp::Store,
        ClearColor{ 0.0f, 0.0f, 0.0f, 1.0f } } };
    pass.depthTarget = DepthAttachment{ depth, LoadOp::Clear, StoreOp::DontCare, 1.0f, 0 };
    REQUIRE(frame.beginPass(pass));
    frame.setPipeline(triangles);
    frame.setViewport(Viewport{ 0, 0, 64, 64, 0, 1 });
    frame.setScissor(ScissorRect{ 8, 8, 48, 48 });
    frame.bindVertexBuffer(0, vertices);
    frame.bindIndexBuffer(indices, IndexType::Uint16);
    frame.bindTexture(ShaderStage::Fragment, 0, texture, sampler);
    frame.pushUniforms(ShaderStage::Vertex, 0, uniforms);
    frame.pushUniforms(ShaderStage::Fragment, 0, uniforms);
    frame.drawIndexed(6);
    frame.draw(0);   // nothing, and not recorded
    frame.setPipeline(lines);
    frame.draw(4, 2);
    REQUIRE(frame.endPass());
    frame.copyTexture(texture, TextureRegion{ 0, 0, 0, 8, 8 }, offscreen, 0, 0, 0);
    REQUIRE(device->submit(frame));
    noWarning(sink);

    const std::vector<RecordedCommand> expected = {
        PipelineCreated{ lines, lineDesc },
        PipelineCreated{ triangles, triangleDesc },
        BeginPass{ pass },
        SetPipeline{ triangles },
        SetViewport{ Viewport{ 0, 0, 64, 64, 0, 1 } },
        SetScissor{ ScissorRect{ 8, 8, 48, 48 } },
        BindVertexBuffer{ 0, vertices, 0 },
        BindIndexBuffer{ indices, IndexType::Uint16, 0 },
        BindTexture{ ShaderStage::Fragment, 0, texture, sampler },
        PushUniforms{ ShaderStage::Vertex, 0, uniforms },
        PushUniforms{ ShaderStage::Fragment, 0, uniforms },
        DrawIndexed{ 6, 0, 0 },
        SetPipeline{ lines },
        Draw{ 4, 2 },
        EndPass{},
        CopyTexture{ texture, TextureRegion{ 0, 0, 0, 8, 8 }, offscreen, 0, 0, 0 },
        FrameSubmitted{ 1, false },
    };
    REQUIRE(device->recorded().size() == expected.size());
    for ( std::size_t i = 0; i < expected.size(); ++i )
    {
        INFO("entry " << i << ", expected alternative " << expected[i].index()
            << ", recorded " << device->recorded()[i].index());
        CHECK(device->recorded()[i] == expected[i]);
    }

    // clearRecorded() starts over; an empty frame is just its end.
    device->clearRecorded();
    CHECK(device->recorded().empty());
    CommandList empty = device->beginFrame();
    REQUIRE(device->submit(empty));
    CHECK(device->recorded() == std::vector<RecordedCommand>{ FrameSubmitted{ 2, false } });
    noWarning(sink);
}

TEST_CASE("The Null device refuses invalid pipeline descs with one warning each")
{
    CaptureSink sink;
    const std::unique_ptr<NullDevice> device = createNullDevice();
    const PipelineDesc valid = transformedVertexPipeline(TextureFormat::Bgra8, TextureFormat::D32F);

    const std::function<void(PipelineDesc&)> breakers[] = {
        [](PipelineDesc& d) { d.vertexShader.blobs.clear(); },
        [](PipelineDesc& d) { d.fragmentShader.blobs[0].code.clear(); },
        [](PipelineDesc& d) { d.fragmentShader.blobs[0].entryPoint.clear(); },
        [](PipelineDesc& d) { d.vertexShader.blobs[0].format = ShaderFormat::SpirV | ShaderFormat::Msl; },
        [](PipelineDesc& d) { d.vertexShader.blobs[0].format = ShaderFormat::None; },
        [](PipelineDesc& d) { d.fragmentShader.textureCount = kMaxTextureSlots + 1; },
        [](PipelineDesc& d) { d.vertexShader.uniformCount = kMaxUniformSlots + 1; },
        [](PipelineDesc& d) { d.fragmentShader.storageBufferCount = kMaxStorageBufferSlots + 1; },
        [](PipelineDesc& d)
        {
            const ColorTargetDesc first = d.colorTargets[0];   // assign() may not take a reference into the vector
            d.colorTargets.assign(kMaxColorTargets + 1, first);
        },
        [](PipelineDesc& d) { d.colorTargets.clear(); d.depthFormat.reset(); d.depth = {}; },
        [](PipelineDesc& d) { d.colorTargets[0].format = TextureFormat::D24S8; },
        [](PipelineDesc& d) { d.depthFormat = TextureFormat::Rgba8; },
        [](PipelineDesc& d) { d.depthFormat.reset(); },   // depth test still on
        [](PipelineDesc& d) { d.vertexLayout.attributes[3].offset = 28; },   // past 32
        [](PipelineDesc& d) { d.vertexLayout.attributes[2].location = 1; },
        [](PipelineDesc& d) { d.vertexLayout.attributes[0].bufferSlot = 1; },
        [](PipelineDesc& d) { d.vertexLayout.attributes[0].location = kMaxVertexAttributes; },
        [](PipelineDesc& d) { d.vertexLayout.bufferStrides.assign(kMaxVertexBuffers + 1, 32); },
    };
    for ( std::size_t i = 0; i < std::size(breakers); ++i )
    {
        INFO("breaker " << i);
        PipelineDesc desc = valid;
        breakers[i](desc);
        CHECK(device->createPipeline(desc).isNull());
        oneWarning(sink);
    }
    CHECK(device->recorded().empty());

    // The unbroken desc and a depth-only one are accepted, and so is a
    // device without a format refusing a pipeline that renders to it.
    CHECK_FALSE(device->createPipeline(valid).isNull());
    PipelineDesc depthOnly = valid;
    depthOnly.colorTargets.clear();
    CHECK_FALSE(device->createPipeline(depthOnly).isNull());
    noWarning(sink);

    const std::unique_ptr<NullDevice> lacking =
        createNullDevice(NullDeviceDesc{ { TextureFormat::B4G4R4A4 } });
    CHECK_FALSE(lacking->supportsFormat(TextureFormat::B4G4R4A4, TextureUsage::Sampled));
    CHECK(lacking->createPipeline(transformedVertexPipeline(TextureFormat::B4G4R4A4,
        std::nullopt)).isNull());
    oneWarning(sink);
    CHECK(lacking->createTexture(TextureDesc{ 4, 4, 1, TextureFormat::B4G4R4A4,
        TextureUsage::Sampled }).isNull());
    oneWarning(sink);
}

TEST_CASE("The Null device drops commands with bad resources at submit")
{
    CaptureSink sink;
    const std::unique_ptr<NullDevice> device = createNullDevice();
    const TextureHandle target = device->createTexture(TextureDesc{ 16, 16, 1,
        TextureFormat::Rgba8, TextureUsage::ColorTarget | TextureUsage::Sampled });
    const TextureHandle bgraTarget = device->createTexture(TextureDesc{ 16, 16, 1,
        TextureFormat::Bgra8, TextureUsage::ColorTarget });
    const BufferHandle vertices = device->createBuffer(BufferDesc{ 32 * 3, BufferUsage::Vertex });
    const BufferHandle indices = device->createBuffer(BufferDesc{ 2 * 3, BufferUsage::Index });
    const SamplerHandle sampler = device->createSampler(SamplerDesc{});
    const PipelineHandle pipeline = device->createPipeline(
        withoutShaderResources(transformedVertexPipeline(TextureFormat::Rgba8, std::nullopt)));
    REQUIRE_FALSE(pipeline.isNull());
    noWarning(sink);

    // Records one frame into a pass on target with the pipeline bound and
    // the buffers bound, adds what record adds, submits and returns what the
    // device executed between beginPass and endPass.
    const auto run = [&](TextureHandle passTarget,
        const std::function<void(CommandList&)>& record) -> std::vector<RecordedCommand>
    {
        device->clearRecorded();
        CommandList list = device->beginFrame();
        REQUIRE(list.beginPass(PassDesc{ { attachment(passTarget) }, std::nullopt }));
        record(list);
        REQUIRE(list.endPass());
        REQUIRE(device->submit(list));
        std::vector<RecordedCommand> inner(device->recorded().begin(), device->recorded().end());
        REQUIRE(inner.size() >= 3);   // beginPass, endPass, frame
        return std::vector<RecordedCommand>(inner.begin() + 1, inner.end() - 2);
    };
    const auto bindAll = [&](CommandList& list)
    {
        list.setPipeline(pipeline);
        list.bindVertexBuffer(0, vertices);
        list.bindIndexBuffer(indices, IndexType::Uint16);
    };

    // In bounds: all executed.
    CHECK(run(target, [&](CommandList& list)
    {
        bindAll(list);
        list.draw(3);
        list.drawIndexed(3);
    }).size() == 5);
    noWarning(sink);

    // A pipeline whose formats differ from the pass's: dropped, and the
    // pass's draws with it, warning once.
    CHECK(run(bgraTarget, [&](CommandList& list)
    {
        bindAll(list);
        list.draw(3);
        list.drawIndexed(3);
    }) == std::vector<RecordedCommand>{ BindVertexBuffer{ 0, vertices, 0 },
        BindIndexBuffer{ indices, IndexType::Uint16, 0 } });
    oneWarning(sink);

    // Draws past the end of the vertex or index buffer: one warning each.
    CHECK(run(target, [&](CommandList& list) { bindAll(list); list.draw(3, 1); }).size() == 3);
    oneWarning(sink);
    CHECK(run(target, [&](CommandList& list) { bindAll(list); list.drawIndexed(2, 2); }).size() == 3);
    oneWarning(sink);
    CHECK(run(target, [&](CommandList& list)
    {
        list.setPipeline(pipeline);
        list.bindVertexBuffer(0, vertices, 4);
        list.draw(3);
    }).size() == 2);
    oneWarning(sink);

    // A draw without its vertex buffer bound.
    CHECK(run(target, [&](CommandList& list) { list.setPipeline(pipeline); list.draw(1); }).size() == 1);
    oneWarning(sink);

    // Wrong usages and misaligned offsets poison the pass's later draws.
    CHECK(run(target, [&](CommandList& list)
    {
        bindAll(list);
        list.bindVertexBuffer(1, indices);
        list.draw(3);
        list.draw(3);
    }).size() == 3);
    oneWarning(sink);
    CHECK(run(target, [&](CommandList& list)
    {
        bindAll(list);
        list.bindIndexBuffer(indices, IndexType::Uint16, 1);
        list.drawIndexed(1);
    }).size() == 3);
    oneWarning(sink);
    CHECK(run(target, [&](CommandList& list)
    {
        bindAll(list);
        list.bindTexture(ShaderStage::Fragment, 0, bgraTarget, sampler);   // not Sampled
        list.draw(3);
    }).size() == 3);
    oneWarning(sink);

    // A storage buffer is executed as bound; one without Storage usage and a
    // stale one are dropped and poison the pass's later draws.
    const BufferHandle storage = device->createBuffer(BufferDesc{ 64, BufferUsage::Storage });
    const BufferHandle staleStorage = device->createBuffer(BufferDesc{ 64, BufferUsage::Storage });
    device->destroyBuffer(staleStorage);
    noWarning(sink);
    CHECK(run(target, [&](CommandList& list)
    {
        bindAll(list);
        list.bindStorageBuffer(ShaderStage::Fragment, 1, storage);
        list.draw(3);
    }) == std::vector<RecordedCommand>{ SetPipeline{ pipeline }, BindVertexBuffer{ 0, vertices, 0 },
        BindIndexBuffer{ indices, IndexType::Uint16, 0 },
        BindStorageBuffer{ ShaderStage::Fragment, 1, storage }, Draw{ 3, 0 } });
    noWarning(sink);
    CHECK(run(target, [&](CommandList& list)
    {
        bindAll(list);
        list.bindStorageBuffer(ShaderStage::Fragment, 0, vertices);   // no Storage usage
        list.draw(3);
    }).size() == 3);
    oneWarning(sink);
    CHECK(run(target, [&](CommandList& list)
    {
        bindAll(list);
        list.bindStorageBuffer(ShaderStage::Vertex, 0, staleStorage);
        list.draw(3);
    }).size() == 3);
    oneWarning(sink);

    // Stale buffers, samplers and pipelines.
    const SamplerHandle staleSampler = device->createSampler(SamplerDesc{});
    device->destroySampler(staleSampler);
    CHECK(run(target, [&](CommandList& list)
    {
        bindAll(list);
        list.bindTexture(ShaderStage::Fragment, 0, target, staleSampler);
        list.draw(3);
    }).size() == 3);
    oneWarning(sink);
    const PipelineHandle stalePipeline =
        device->createPipeline(transformedVertexPipeline(TextureFormat::Rgba8, std::nullopt));
    device->destroyPipeline(stalePipeline);
    noWarning(sink);
    CHECK(run(target, [&](CommandList& list)
    {
        list.setPipeline(stalePipeline);
        list.draw(3);
    }).empty());
    oneWarning(sink);

    // Destroying the stale pipeline again is a logged no-op.
    device->destroyPipeline(stalePipeline);
    oneWarning(sink);
}

TEST_CASE("Null device handles reuse slots with a new generation")
{
    CaptureSink sink;
    const std::unique_ptr<NullDevice> device = createNullDevice();
    const BufferHandle first = device->createBuffer(BufferDesc{ 4, BufferUsage::Storage });
    device->destroyBuffer(first);
    const BufferHandle second = device->createBuffer(BufferDesc{ 4, BufferUsage::Storage });
    CHECK(second.index == first.index);
    CHECK(second.generation == first.generation + 1);
    CHECK(device->readBuffer(second, 0, 4) == std::vector<std::byte>(4));   // zero-filled
    noWarning(sink);
    CHECK(device->readBuffer(first, 0, 4).empty());
    oneWarning(sink);
}

TEST_CASE("The Null device executes buffer updates between the draws they feed")
{
    CaptureSink sink;
    const std::unique_ptr<NullDevice> device = createNullDevice();
    const BufferHandle vertices = device->createBuffer(BufferDesc{ 32 * 3, BufferUsage::Vertex });
    const PipelineHandle pipeline = device->createPipeline(
        withoutShaderResources(transformedVertexPipeline(TextureFormat::Bgra8, std::nullopt)));
    REQUIRE_FALSE(pipeline.isNull());
    device->clearRecorded();

    // Two draws stream their vertices through the same bytes of one
    // buffer: each update sits right before the pass of its draw.
    const std::vector<std::byte> firstDraw = bytes(32 * 3, 1);
    const std::vector<std::byte> secondDraw = bytes(32 * 3, 2);
    PassDesc pass;
    pass.colorTargets = { attachment(TextureHandle{}) };
    CommandList frame = device->beginFrame();
    for ( const std::vector<std::byte>* data : { &firstDraw, &secondDraw } )
    {
        frame.updateBuffer(vertices, 0, *data);
        REQUIRE(frame.beginPass(pass));
        frame.setPipeline(pipeline);
        frame.bindVertexBuffer(0, vertices);
        frame.draw(3);
        REQUIRE(frame.endPass());
    }
    REQUIRE(device->submit(frame));
    noWarning(sink);

    std::vector<RecordedCommand> expected;
    for ( const std::vector<std::byte>* data : { &firstDraw, &secondDraw } )
    {
        expected.emplace_back(UpdateBuffer{ vertices, 0, *data });
        expected.emplace_back(BeginPass{ pass });
        expected.emplace_back(SetPipeline{ pipeline });
        expected.emplace_back(BindVertexBuffer{ 0, vertices, 0 });
        expected.emplace_back(Draw{ 3, 0 });
        expected.emplace_back(EndPass{});
    }
    expected.emplace_back(FrameSubmitted{ 1, false });
    CHECK(device->recorded() == expected);
    CHECK(device->readBuffer(vertices, 0, 32 * 3) == secondDraw);
}

TEST_CASE("A Null device that doesn't record keeps nothing but still executes")
{
    CaptureSink sink;
    NullDeviceDesc desc;
    desc.record = false;
    const std::unique_ptr<NullDevice> device = createNullDevice(desc);
    const BufferHandle vertices = device->createBuffer(BufferDesc{ 32 * 3, BufferUsage::Vertex });
    const TextureHandle target = device->createTexture(TextureDesc{ 2, 1, 1,
        TextureFormat::Rgba8, TextureUsage::ColorTarget });
    const PipelineHandle pipeline = device->createPipeline(
        withoutShaderResources(transformedVertexPipeline(TextureFormat::Rgba8, std::nullopt)));
    REQUIRE_FALSE(pipeline.isNull());

    // Frames of streamed vertices: buffer updates and clears are applied,
    // but no pipeline creation, command or frame end is kept.
    PassDesc pass;
    pass.colorTargets = { ColorAttachment{ target, 0, LoadOp::Clear, StoreOp::Store,
        ClearColor{ 1.0f, 0.0f, 0.0f, 1.0f } } };
    std::vector<std::byte> data;
    for ( unsigned frameNum = 0; frameNum < 3; ++frameNum )
    {
        data = bytes(32 * 3, frameNum);
        CommandList frame = device->beginFrame();
        frame.updateBuffer(vertices, 0, data);
        REQUIRE(frame.beginPass(pass));
        frame.setPipeline(pipeline);
        frame.bindVertexBuffer(0, vertices);
        frame.draw(3);
        REQUIRE(frame.endPass());
        REQUIRE(device->submit(frame));
        CHECK(device->recorded().empty());
    }
    noWarning(sink);
    CHECK(device->readBuffer(vertices, 0, 32 * 3) == data);
    const std::byte red[] = { std::byte{ 0xFF }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 0xFF } };
    CHECK(device->readTexture(target, 0) == std::vector<std::byte>{ red[0], red[1], red[2], red[3],
        red[0], red[1], red[2], red[3] });

    // Recording stays on by default.
    CHECK(NullDeviceDesc{}.record);
}

TEST_CASE("The Null device drops draws whose shaders read slots the pass didn't fill")
{
    CaptureSink sink;
    const std::unique_ptr<NullDevice> device = createNullDevice();
    const TextureHandle target = device->createTexture(TextureDesc{ 16, 16, 1,
        TextureFormat::Bgra8, TextureUsage::ColorTarget });
    const TextureHandle texture = device->createTexture(TextureDesc{ 4, 4, 1,
        TextureFormat::Rgba8, TextureUsage::Sampled });
    const BufferHandle vertices = device->createBuffer(BufferDesc{ 32 * 3, BufferUsage::Vertex });
    const SamplerHandle sampler = device->createSampler(SamplerDesc{});
    // Vertex: 1 uniform block; fragment: 1 texture, 1 uniform block.
    const PipelineHandle pipeline =
        device->createPipeline(transformedVertexPipeline(TextureFormat::Bgra8, std::nullopt));
    // Fragment: 2 textures, nothing else.
    PipelineDesc twoTexturesDesc =
        withoutShaderResources(transformedVertexPipeline(TextureFormat::Bgra8, std::nullopt));
    twoTexturesDesc.fragmentShader.textureCount = 2;
    const PipelineHandle twoTextures = device->createPipeline(twoTexturesDesc);
    REQUIRE_FALSE(pipeline.isNull());
    REQUIRE_FALSE(twoTextures.isNull());
    noWarning(sink);

    const std::vector<std::byte> uniforms = bytes(16, 3);
    const auto bindTexture = [&](CommandList& list, std::uint32_t slot)
    {
        list.bindTexture(ShaderStage::Fragment, slot, texture, sampler);
    };
    const auto pushVertex = [&](CommandList& list)
    {
        list.pushUniforms(ShaderStage::Vertex, 0, uniforms);
    };
    const auto pushFragment = [&](CommandList& list)
    {
        list.pushUniforms(ShaderStage::Fragment, 0, uniforms);
    };

    // Records a frame: an optional first pass that only runs before, then
    // a pass that runs fill, sets the pipeline, runs after and draws.
    // Returns true when the draw executed.
    const auto drew = [&](PipelineHandle drawPipeline,
        const std::function<void(CommandList&)>& before,
        const std::function<void(CommandList&)>& after) -> bool
    {
        device->clearRecorded();
        CommandList list = device->beginFrame();
        const PassDesc pass{ { attachment(target) }, std::nullopt };
        if ( before )
        {
            REQUIRE(list.beginPass(pass));
            before(list);
            REQUIRE(list.endPass());
        }
        REQUIRE(list.beginPass(pass));
        list.setPipeline(drawPipeline);
        list.bindVertexBuffer(0, vertices);
        after(list);
        list.draw(3);
        REQUIRE(list.endPass());
        REQUIRE(device->submit(list));
        for ( const RecordedCommand& command : device->recorded() )
        {
            if ( std::holds_alternative<Draw>(command) )
            {
                return true;
            }
        }
        return false;
    };
    const auto lastWarning = [&]
    {
        INFO("captured log: " << joined(sink));
        REQUIRE(sink.records().size() == 1);
        const std::string message = sink.records()[0].message;
        oneWarning(sink);
        return message;
    };

    // Everything filled: drawn. Before or after setPipeline doesn't matter.
    CHECK(drew(pipeline, {}, [&](CommandList& list)
    {
        bindTexture(list, 0);
        pushVertex(list);
        pushFragment(list);
    }));
    noWarning(sink);
    {
        device->clearRecorded();
        CommandList list = device->beginFrame();
        REQUIRE(list.beginPass(PassDesc{ { attachment(target) }, std::nullopt }));
        bindTexture(list, 0);
        pushVertex(list);
        pushFragment(list);
        list.setPipeline(pipeline);
        list.bindVertexBuffer(0, vertices);
        list.draw(3);
        list.setPipeline(pipeline);   // a new setPipeline keeps what was filled
        list.draw(3);
        REQUIRE(list.endPass());
        REQUIRE(device->submit(list));
        std::size_t draws = 0;
        for ( const RecordedCommand& command : device->recorded() )
        {
            draws += std::holds_alternative<Draw>(command) ? 1 : 0;
        }
        CHECK(draws == 2);
        noWarning(sink);
    }

    // Each missing slot drops the draw with one warning naming it.
    CHECK_FALSE(drew(pipeline, {}, [&](CommandList& list) { pushVertex(list); pushFragment(list); }));
    CHECK(lastWarning() == "render: submit: dropped draw: the fragment shader reads 1 textures, "
        "slot 0 is not bound");
    CHECK_FALSE(drew(pipeline, {}, [&](CommandList& list) { bindTexture(list, 0); pushFragment(list); }));
    CHECK(lastWarning() == "render: submit: dropped draw: the vertex shader reads 1 uniform "
        "blocks, slot 0 was not pushed");
    CHECK_FALSE(drew(pipeline, {}, [&](CommandList& list) { bindTexture(list, 0); pushVertex(list); }));
    CHECK(lastWarning() == "render: submit: dropped draw: the fragment shader reads 1 uniform "
        "blocks, slot 0 was not pushed");
    CHECK_FALSE(drew(twoTextures, {}, [&](CommandList& list) { bindTexture(list, 1); }));
    CHECK(lastWarning() == "render: submit: dropped draw: the fragment shader reads 2 textures, "
        "slot 0 is not bound");
    CHECK(drew(twoTextures, {}, [&](CommandList& list) { bindTexture(list, 1); bindTexture(list, 0); }));
    noWarning(sink);

    // What an earlier pass filled doesn't count.
    CHECK_FALSE(drew(pipeline, [&](CommandList& list)
    {
        bindTexture(list, 0);
        pushVertex(list);
        pushFragment(list);
    }, [](CommandList&) {}));
    // The vertex stage is checked first.
    CHECK(lastWarning() == "render: submit: dropped draw: the vertex shader reads 1 uniform "
        "blocks, slot 0 was not pushed");
}
