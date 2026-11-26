// The std3DBabylon draws and their recorded command stream.
// Part of the std3D Babylon adapter tests; see std3DBabylonTest.cpp for the fixture overview.
#include "std3DBabylonTestSupport.h"

using namespace std3DBabylonTest;

namespace
{
    template <class T>
    std::vector<std::byte> BytesOf(const T* pItems, size_t count)
    {
        const std::byte* pBytes = reinterpret_cast<const std::byte*>(pItems);
        return std::vector<std::byte>(pBytes, pBytes + count * sizeof(T));
    }

    // Fragment uniform block 0 of the std3D shaders: fog params, fog color, alpha reference / 255
    // and the surface tag in x (M9.16).
    std::vector<std::byte> FragmentUniforms(const float (&aFogParams)[4], const float (&aFogColor)[4], float alphaRef)
    {
        float aBlock[16] = { 0.0f };
        std::memcpy(aBlock, aFogParams, sizeof(aFogParams));
        std::memcpy(aBlock + 4, aFogColor, sizeof(aFogColor));
        aBlock[8]  = alphaRef;
        aBlock[12] = 1.0f;
        aBlock[13] = 1.0f; // M10.2: the screen fade factor (no fade)
        return BytesOf(aBlock, 16u);
    }

    std::vector<std::byte> NoFogUniforms(float alphaRef)
    {
        const float aZero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        return FragmentUniforms(aZero, aZero, alphaRef);
    }

    std::vector<std::byte> ViewportUniforms(float width, float height)
    {
        const float aViewport[4] = { 0.0f, 0.0f, width, height };
        return BytesOf(aViewport, 4u);
    }

    // The pipeline std3DX9.c's Direct3D state amounts to (InitRenderState, SetRenderState): blending
    // SRCALPHA, INVSRCALPHA for color and alpha, Z test LESSEQUAL, Z write unless
    // STD3D_RS_ZWRITE_DISABLED, no culling; StdVertexTL as the vertex layout. The normal target
    // (R10G10B10A2 on the Null device) is the second color target: replaced with Z write, kept by
    // blending ZERO, ONE without.
    render::PipelineDesc ExpectedPipeline(render::PrimitiveType primitive, bool bZWrite)
    {
        render::PipelineDesc desc;
        desc.vertexShader   = stdBabylon::shaders::std3dVert();
        desc.fragmentShader = primitive == render::PrimitiveType::TriangleList ? stdBabylon::shaders::std3dFrag()
                                                                               : stdBabylon::shaders::std3dWireframeFrag();
        desc.vertexLayout.bufferStrides = { 32u };
        desc.vertexLayout.attributes    = {
            render::VertexAttribute{ 0, 0, render::VertexFormat::Float4, 0 },
            render::VertexAttribute{ 1, 0, render::VertexFormat::UByte4Norm, 16 },
            render::VertexAttribute{ 2, 0, render::VertexFormat::UByte4Norm, 20 },
            render::VertexAttribute{ 3, 0, render::VertexFormat::Float2, 24 },
        };
        desc.primitive = primitive;
        render::BlendState blend;
        blend.enabled  = true;
        blend.srcColor = render::BlendFactor::SrcAlpha;
        blend.dstColor = render::BlendFactor::OneMinusSrcAlpha;
        blend.colorOp  = render::BlendOp::Add;
        blend.srcAlpha = render::BlendFactor::SrcAlpha;
        blend.dstAlpha = render::BlendFactor::OneMinusSrcAlpha;
        blend.alphaOp  = render::BlendOp::Add;
        render::BlendState keep;
        keep.enabled  = !bZWrite;
        keep.srcColor = render::BlendFactor::Zero;
        keep.dstColor = render::BlendFactor::One;
        keep.srcAlpha = render::BlendFactor::Zero;
        keep.dstAlpha = render::BlendFactor::One;
        desc.colorTargets = { render::ColorTargetDesc{ stdRenderBabylon_GetColorFormat(), blend },
            render::ColorTargetDesc{ render::TextureFormat::R10G10B10A2, bZWrite ? render::BlendState{} : keep } };
        desc.depthFormat  = render::TextureFormat::D24S8;
        desc.depth        = render::DepthState{ true, bZWrite, render::CompareOp::LessOrEqual };
        desc.cull         = render::CullMode::None;
        return desc;
    }

    // The fragment PushUniforms of the record, in order.
    std::vector<std::vector<std::byte>> RecordedFragmentUniforms()
    {
        std::vector<std::vector<std::byte>> aUniforms;
        for ( const render::RecordedCommand& command : Null().recorded() )
        {
            const render::PushUniforms* pPush = std::get_if<render::PushUniforms>(&command);
            if ( pPush && pPush->stage == render::ShaderStage::Fragment )
            {
                aUniforms.push_back(pPush->data);
            }
        }
        return aUniforms;
    }
}

namespace
{
    // The pipeline of lit triangles: ExpectedPipeline's triangles with the lit shaders and StdVertexLit
    // (56 bytes): the StdVertexTL attributes, then the view-space position at 32 and normal at 44.
    render::PipelineDesc ExpectedLitPipeline(bool bZWrite)
    {
        render::PipelineDesc desc = ExpectedPipeline(render::PrimitiveType::TriangleList, bZWrite);
        desc.vertexShader   = stdBabylon::shaders::std3dLitVert();
        desc.fragmentShader = stdBabylon::shaders::std3dLitFrag();
        desc.vertexLayout.bufferStrides = { 56u };
        desc.vertexLayout.attributes.push_back(render::VertexAttribute{ 4, 0, render::VertexFormat::Float3, 32 });
        desc.vertexLayout.attributes.push_back(render::VertexAttribute{ 5, 0, render::VertexFormat::Float3, 44 });
        return desc;
    }
}

namespace
{
    // The bytes of the light buffer for a list: the header (count, three zeros) and the lights as they are.
    std::vector<std::byte> LightBufferBytes(const StdLight* aLights, size_t numLights)
    {
        std::vector<std::byte> bytes(16u + numLights * sizeof(StdLight));
        const uint32_t count = (uint32_t)numLights;
        std::memcpy(bytes.data(), &count, sizeof(count));
        if ( numLights )
        {
            std::memcpy(bytes.data() + 16u, aLights, numLights * sizeof(StdLight));
        }

        return bytes;
    }

    bool AnyRecorded(bool (*pIs)(const render::RecordedCommand&))
    {
        return std::any_of(Null().recorded().begin(), Null().recorded().end(), pIs);
    }
}

namespace
{
    // The load operations of a scene pass whose targets are gone (std3D was shut down since).
    void AssertScenePassLoads(size_t index, render::LoadOp colorLoad, render::LoadOp depthLoad)
    {
        const render::BeginPass& begin = RecordedAt<render::BeginPass>(index);
        TEST_ASSERT_EQUAL_size_t(2u, begin.desc.colorTargets.size());
        TEST_ASSERT_FALSE(begin.desc.colorTargets[0].target.isNull());
        TEST_ASSERT_TRUE(begin.desc.colorTargets[0].load == colorLoad);
        TEST_ASSERT_FALSE(begin.desc.colorTargets[1].target.isNull());
        TEST_ASSERT_TRUE(begin.desc.colorTargets[1].load == depthLoad);
        TEST_ASSERT_TRUE(begin.desc.depthTarget.has_value());
        TEST_ASSERT_FALSE(begin.desc.depthTarget->target.isNull());
        TEST_ASSERT_TRUE(begin.desc.depthTarget->load == depthLoad);
    }
}

TEST_GROUP(std3DBabylon);

void TEST_std3DBabylon_SETUP(void);
void TEST_std3DBabylon_TEAR_DOWN(void);

TEST(std3DBabylon, BadAndEmptyDrawsRecordNothing)
{
    OpenStd3D();
    StdVertexTL aVerts[3];
    std::memset(aVerts, 0, sizeof(aVerts));
    uint16_t aIndices[3]    = { 0, 1, 2 };
    uint16_t aBadIndices[3] = { 0, 1, 3 };

    // Outside a scene std3DX9.c's draws failed (D3DERR_INVALIDCALL).
    std3D_DrawRenderList(nullptr, (Std3DRenderState)0, aVerts, 3u, aIndices, 3u);
    std3D_DrawLineStrip(aVerts, 3u);
    std3D_DrawPointList(aVerts, 3u);

    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_DrawRenderList(nullptr, (Std3DRenderState)0, aVerts, 3u, aBadIndices, 3u);
    std3D_DrawRenderList(nullptr, (Std3DRenderState)0, nullptr, 3u, aIndices, 3u);
    std3D_DrawRenderList(nullptr, (Std3DRenderState)0, aVerts, 3u, nullptr, 3u);
    std3D_DrawRenderList(nullptr, (Std3DRenderState)0, aVerts, std3D_g_maxVertices + 1u, aIndices, 3u);
    std3D_DrawRenderList(nullptr, (Std3DRenderState)0, aVerts, 3u, aIndices, 2u); // no whole triangle
    std3D_DrawRenderList(nullptr, (Std3DRenderState)0, aVerts, 0u, aIndices, 0u);
    std3D_DrawLineStrip(aVerts, 1u);                                               // no line
    std3D_DrawLineStrip(nullptr, 2u);
    std3D_DrawPointList(aVerts, 0u);
    std3D_DrawPointList(aVerts, std3D_g_maxVertices + 1u);
    std3D_SetWireframeRenderState();
    EndFrame();

    TEST_ASSERT_EQUAL_size_t(3u, Null().recorded().size());
    AssertScenePass(0, render::LoadOp::Clear, render::LoadOp::Clear);
    RecordedAt<render::EndPass>(1);
    AssertSubmitted(2);
}


TEST(std3DBabylon, TwoDrawsRecordTheirStreamExactly)
{
    OpenStd3D();
    static tSystemTexture texture;
    MakeCachedTexture(&texture, stdColor_cfARGB8888, 3);
    const render::TextureHandle textureHandle = CachedHandle(texture);

    // What the pipelines are built from: the shaders' resources as std3DBabylon.cpp binds them.
    TEST_ASSERT_EQUAL_UINT32(0u, stdBabylon::shaders::std3dVert().textureCount);
    TEST_ASSERT_EQUAL_UINT32(1u, stdBabylon::shaders::std3dVert().uniformCount);
    TEST_ASSERT_EQUAL_UINT32(1u, stdBabylon::shaders::std3dFrag().textureCount);
    TEST_ASSERT_EQUAL_UINT32(1u, stdBabylon::shaders::std3dFrag().uniformCount);
    TEST_ASSERT_EQUAL_UINT32(0u, stdBabylon::shaders::std3dWireframeFrag().textureCount);
    TEST_ASSERT_EQUAL_UINT32(1u, stdBabylon::shaders::std3dWireframeFrag().uniformCount);

    const StdVertexTL aTriangle[3] = { Vertex(1.0f, 2.0f, 0.5f, 1.0f, 0xFF102030u, 0.0f, 0.0f),
        Vertex(9.0f, 2.0f, 0.5f, 1.0f, 0xFF405060u, 1.0f, 0.0f), Vertex(1.0f, 7.0f, 0.5f, 1.0f, 0xFF708090u, 0.0f, 1.0f) };
    const uint16_t aTriangleIndices[3] = { 0u, 1u, 2u };
    const StdVertexTL aQuad[4] = { Vertex(0.0f, 0.0f, 0.25f, 0.5f, 0x80FFFFFFu), Vertex(4.0f, 0.0f, 0.25f, 0.5f, 0x80FFFFFFu),
        Vertex(4.0f, 4.0f, 0.25f, 0.5f, 0x80FFFFFFu), Vertex(0.0f, 4.0f, 0.25f, 0.5f, 0x80FFFFFFu) };
    const uint16_t aQuadIndices[7] = { 0u, 1u, 2u, 0u, 2u, 3u, 1u }; // the 7th makes no whole triangle: left out

    const Std3DRenderState textured   = STD3D_RS_TEXFILTER_BILINEAR;
    const Std3DRenderState untextured = (Std3DRenderState)(STD3D_RS_ZWRITE_DISABLED | STD3D_RS_ALPHAREF_SET | STD3D_RS_TEX_CPAMP_U);

    render::PipelineHandle texturedPipeline;
    render::PipelineHandle untexturedPipeline;
    render::BufferHandle vertexBuffer;
    render::BufferHandle indexBuffer;
    render::SamplerHandle texturedSampler;
    render::SamplerHandle untexturedSampler;
    render::TextureHandle whiteTexture;
    for ( int frameNum = 0; frameNum < 2; ++frameNum )
    {
        Null().clearRecorded();
        TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
        std3D_UpdateFrameCount(&texture);
        std3D_DrawRenderList(texture.pCachedTexture, textured, const_cast<StdVertexTL*>(aTriangle), 3u, const_cast<uint16_t*>(aTriangleIndices), 3u);
        std3D_DrawRenderList(nullptr, untextured, const_cast<StdVertexTL*>(aQuad), 4u, const_cast<uint16_t*>(aQuadIndices), 7u);
        EndFrame();

        // The pipelines are created in the first frame only.
        const size_t numCreated = frameNum == 0 ? 2u : 0u;
        TEST_ASSERT_EQUAL_size_t(numCreated + 18u, Null().recorded().size());
        if ( frameNum == 0 )
        {
            const render::PipelineCreated& first  = RecordedAt<render::PipelineCreated>(0);
            const render::PipelineCreated& second = RecordedAt<render::PipelineCreated>(1);
            TEST_ASSERT_TRUE(first.desc == ExpectedPipeline(render::PrimitiveType::TriangleList, true));
            TEST_ASSERT_TRUE(second.desc == ExpectedPipeline(render::PrimitiveType::TriangleList, false));
            texturedPipeline   = first.pipeline;
            untexturedPipeline = second.pipeline;

            vertexBuffer      = RecordedAt<render::UpdateBuffer>(2).buffer;
            indexBuffer       = RecordedAt<render::UpdateBuffer>(3).buffer;
            texturedSampler   = RecordedAt<render::BindTexture>(12).sampler;
            untexturedSampler = RecordedAt<render::BindTexture>(16).sampler;
            whiteTexture      = RecordedAt<render::BindTexture>(16).texture;
            TEST_ASSERT_FALSE(vertexBuffer == indexBuffer);
            TEST_ASSERT_FALSE(texturedSampler == untexturedSampler);
            TEST_ASSERT_FALSE(whiteTexture == textureHandle);

            // std3DX9.c's white texture for untextured draws: one opaque white texel.
            AssertBytes(std::vector<std::byte>(4u, std::byte{ 0xFF }), Null().readTexture(whiteTexture, 0));
        }

        // Each frame streams from the start of the buffers, each draw after the one before.
        const size_t i = numCreated;
        TEST_ASSERT_TRUE(RecordedAt<render::UpdateBuffer>(i) == (render::UpdateBuffer{ vertexBuffer, 0u, BytesOf(aTriangle, 3u) }));
        TEST_ASSERT_TRUE(RecordedAt<render::UpdateBuffer>(i + 1u) == (render::UpdateBuffer{ indexBuffer, 0u, BytesOf(aTriangleIndices, 3u) }));
        TEST_ASSERT_TRUE(RecordedAt<render::UpdateBuffer>(i + 2u) == (render::UpdateBuffer{ vertexBuffer, 96u, BytesOf(aQuad, 4u) }));
        TEST_ASSERT_TRUE(RecordedAt<render::UpdateBuffer>(i + 3u) == (render::UpdateBuffer{ indexBuffer, 6u, BytesOf(aQuadIndices, 6u) }));

        const render::LoadOp load = frameNum == 0 ? render::LoadOp::Clear : render::LoadOp::Load;
        AssertScenePass(i + 4u, load, load);
        TEST_ASSERT_TRUE(RecordedAt<render::PushUniforms>(i + 5u) == (render::PushUniforms{ render::ShaderStage::Vertex, 0u, ViewportUniforms(640.0f, 480.0f) }));
        TEST_ASSERT_TRUE(RecordedAt<render::SetPipeline>(i + 6u) == render::SetPipeline{ texturedPipeline });
        TEST_ASSERT_TRUE(RecordedAt<render::BindVertexBuffer>(i + 7u) == (render::BindVertexBuffer{ 0u, vertexBuffer, 0u }));
        TEST_ASSERT_TRUE(RecordedAt<render::PushUniforms>(i + 8u) == (render::PushUniforms{ render::ShaderStage::Fragment, 0u, NoFogUniforms(0.0f) }));
        TEST_ASSERT_TRUE(RecordedAt<render::BindIndexBuffer>(i + 9u) == (render::BindIndexBuffer{ indexBuffer, render::IndexType::Uint16, 0u }));
        TEST_ASSERT_TRUE(RecordedAt<render::BindTexture>(i + 10u) == (render::BindTexture{ render::ShaderStage::Fragment, 0u, textureHandle, texturedSampler }));
        TEST_ASSERT_TRUE(RecordedAt<render::DrawIndexed>(i + 11u) == (render::DrawIndexed{ 3u, 0u, 0 }));
        TEST_ASSERT_TRUE(RecordedAt<render::SetPipeline>(i + 12u) == render::SetPipeline{ untexturedPipeline });
        TEST_ASSERT_TRUE(RecordedAt<render::PushUniforms>(i + 13u) == (render::PushUniforms{ render::ShaderStage::Fragment, 0u, NoFogUniforms(160.0f / 255.0f) }));
        TEST_ASSERT_TRUE(RecordedAt<render::BindTexture>(i + 14u) == (render::BindTexture{ render::ShaderStage::Fragment, 0u, whiteTexture, untexturedSampler }));
        TEST_ASSERT_TRUE(RecordedAt<render::DrawIndexed>(i + 15u) == (render::DrawIndexed{ 6u, 3u, 3 }));
        RecordedAt<render::EndPass>(i + 16u);
        AssertSubmitted(i + 17u);
    }

    std3D_ClearSystemTexture(&texture);
}

// Added: M11.2c. STD3D_RS_SHADOW_CASTER changes nothing about the draw it marks: the same pipeline,
// sampler and commands as the same state without it. The test counter counts the marked vertices.
TEST(std3DBabylon, ShadowCasterStateDrawsLikeThePlainState)
{
    OpenStd3D();
    const StdVertexTL aTriangle[3] = { Vertex(1.0f, 2.0f, 0.5f, 1.0f, 0xFF102030u, 0.0f, 0.0f),
        Vertex(9.0f, 2.0f, 0.5f, 1.0f, 0xFF405060u, 1.0f, 0.0f), Vertex(1.0f, 7.0f, 0.5f, 1.0f, 0xFF708090u, 0.0f, 1.0f) };
    const uint16_t aIndices[3]    = { 0u, 1u, 2u };
    const Std3DRenderState plain  = (Std3DRenderState)(STD3D_RS_ZWRITE_DISABLED | STD3D_RS_ALPHAREF_SET | STD3D_RS_TEX_CPAMP_U);
    const Std3DRenderState caster = (Std3DRenderState)(plain | STD3D_RS_SHADOW_CASTER);

    // Pipelines and buffers are made on first use; warm them up so the two recorded frames match.
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_DrawRenderList(nullptr, plain, const_cast<StdVertexTL*>(aTriangle), 3u, const_cast<uint16_t*>(aIndices), 3u);
    EndFrame();

    // The submit carries the frame number, which differs; every command before it must match.
    const auto commandsWithoutSubmit = [](const std::vector<render::RecordedCommand>& record) {
        std::vector<render::RecordedCommand> commands;
        for ( const render::RecordedCommand& command : record )
        {
            if ( !std::holds_alternative<render::FrameSubmitted>(command) )
            {
                commands.push_back(command);
            }
        }
        return commands;
    };

    Null().clearRecorded();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_DrawRenderList(nullptr, plain, const_cast<StdVertexTL*>(aTriangle), 3u, const_cast<uint16_t*>(aIndices), 3u);
    TEST_ASSERT_EQUAL_UINT32(0u, std3DBabylon_GetCasterVertexCountForTest());
    EndFrame();
    const std::vector<render::RecordedCommand> aPlain = commandsWithoutSubmit(Null().recorded());

    Null().clearRecorded();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_DrawRenderList(nullptr, caster, const_cast<StdVertexTL*>(aTriangle), 3u, const_cast<uint16_t*>(aIndices), 3u);
    TEST_ASSERT_EQUAL_UINT32(3u, std3DBabylon_GetCasterVertexCountForTest());
    EndFrame();
    const std::vector<render::RecordedCommand> aCaster = commandsWithoutSubmit(Null().recorded());

    TEST_ASSERT_EQUAL_size_t(aPlain.size(), aCaster.size());
    TEST_ASSERT_TRUE(aPlain == aCaster);
}

TEST(std3DBabylon, AFrameThatNeedsMoreGrowsTheBuffers)
{
    OpenStd3D();

    // Two draws of 20000 points: 1.28 MB of vertices, more than the first 1 MiB vertex buffer holds.
    static StdVertexTL aPoints[20000];
    for ( size_t i = 0; i < std::size(aPoints); ++i )
    {
        aPoints[i] = Vertex((float)(i % 640u), (float)(i / 640u), 0.5f, 1.0f, 0xFFFFFFFFu);
    }

    std3D_SetWireframeRenderState();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_DrawPointList(aPoints, std::size(aPoints));
    std3D_DrawPointList(aPoints, std::size(aPoints));
    EndFrame();

    // The second draw goes to a bigger buffer from its start; the frame's first draw keeps the old one.
    TEST_ASSERT_EQUAL_size_t(13u, Null().recorded().size());
    const render::PipelineCreated& created = RecordedAt<render::PipelineCreated>(0);
    TEST_ASSERT_TRUE(created.desc == ExpectedPipeline(render::PrimitiveType::PointList, true));
    const render::BufferHandle oldBuffer = RecordedAt<render::UpdateBuffer>(1).buffer;
    const render::BufferHandle newBuffer = RecordedAt<render::UpdateBuffer>(2).buffer;
    TEST_ASSERT_FALSE(oldBuffer == newBuffer);
    TEST_ASSERT_TRUE(RecordedAt<render::UpdateBuffer>(1) == (render::UpdateBuffer{ oldBuffer, 0u, BytesOf(aPoints, 20000u) }));
    TEST_ASSERT_TRUE(RecordedAt<render::UpdateBuffer>(2) == (render::UpdateBuffer{ newBuffer, 0u, BytesOf(aPoints, 20000u) }));
    AssertScenePass(3, render::LoadOp::Clear, render::LoadOp::Clear);
    RecordedAt<render::PushUniforms>(4);
    TEST_ASSERT_TRUE(RecordedAt<render::SetPipeline>(5) == render::SetPipeline{ created.pipeline });
    TEST_ASSERT_TRUE(RecordedAt<render::BindVertexBuffer>(6) == (render::BindVertexBuffer{ 0u, oldBuffer, 0u }));
    RecordedAt<render::PushUniforms>(7);
    TEST_ASSERT_TRUE(RecordedAt<render::Draw>(8) == (render::Draw{ 20000u, 0u }));
    TEST_ASSERT_TRUE(RecordedAt<render::BindVertexBuffer>(9) == (render::BindVertexBuffer{ 0u, newBuffer, 0u }));
    TEST_ASSERT_TRUE(RecordedAt<render::Draw>(10) == (render::Draw{ 20000u, 0u }));
    RecordedAt<render::EndPass>(11);
    AssertSubmitted(12);

    // The old buffer went with the submit; the next frame streams into the new one from its start.
    size_t numWarnings = 0;
    {
        babylon::log::CaptureSink sink;
        (void)Null().readBuffer(oldBuffer, 0, 32u);
        numWarnings = sink.records().size();
    }
    TEST_ASSERT_EQUAL_size_t(1u, numWarnings);

    Null().clearRecorded();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_DrawPointList(aPoints, 2u);
    EndFrame();
    TEST_ASSERT_TRUE(RecordedAt<render::UpdateBuffer>(0) == (render::UpdateBuffer{ newBuffer, 0u, BytesOf(aPoints, 2u) }));
}

TEST(std3DBabylon, WireframeDrawsUseTheirPipelinesAndNoTexture)
{
    OpenStd3D(320u, 200u);
    const StdVertexTL aLine[3] = { Vertex(1.0f, 1.0f, 0.5f, 1.0f, 0xFF00FF00u), Vertex(9.0f, 1.0f, 0.5f, 1.0f, 0xFF00FF00u),
        Vertex(9.0f, 9.0f, 0.5f, 1.0f, 0xFF00FF00u) };
    const StdVertexTL aPoints[2] = { Vertex(3.0f, 4.0f, 0.5f, 1.0f, 0xFFFF0000u), Vertex(5.0f, 6.0f, 0.5f, 1.0f, 0xFFFF0000u) };

    // The wireframe state keeps Z write off from the state before and drops fog.
    std3D_SetRenderState((Std3DRenderState)(STD3D_RS_ZWRITE_DISABLED | STD3D_RS_FOG_ENABLED | STD3D_RS_ALPHAREF_SET));
    std3D_SetWireframeRenderState();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_DrawLineStrip(const_cast<StdVertexTL*>(aLine), 3u);
    std3D_DrawPointList(const_cast<StdVertexTL*>(aPoints), 2u);
    EndFrame();

    TEST_ASSERT_EQUAL_size_t(14u, Null().recorded().size());
    const render::PipelineCreated& lines  = RecordedAt<render::PipelineCreated>(0);
    const render::PipelineCreated& points = RecordedAt<render::PipelineCreated>(1);
    TEST_ASSERT_TRUE(lines.desc == ExpectedPipeline(render::PrimitiveType::LineStrip, false));
    TEST_ASSERT_TRUE(points.desc == ExpectedPipeline(render::PrimitiveType::PointList, false));
    const render::BufferHandle vertexBuffer = RecordedAt<render::UpdateBuffer>(2).buffer;
    TEST_ASSERT_TRUE(RecordedAt<render::UpdateBuffer>(2) == (render::UpdateBuffer{ vertexBuffer, 0u, BytesOf(aLine, 3u) }));
    TEST_ASSERT_TRUE(RecordedAt<render::UpdateBuffer>(3) == (render::UpdateBuffer{ vertexBuffer, 96u, BytesOf(aPoints, 2u) }));
    AssertScenePass(4, render::LoadOp::Clear, render::LoadOp::Clear);
    TEST_ASSERT_TRUE(RecordedAt<render::PushUniforms>(5) == (render::PushUniforms{ render::ShaderStage::Vertex, 0u, ViewportUniforms(320.0f, 200.0f) }));
    TEST_ASSERT_TRUE(RecordedAt<render::SetPipeline>(6) == render::SetPipeline{ lines.pipeline });
    TEST_ASSERT_TRUE(RecordedAt<render::BindVertexBuffer>(7) == (render::BindVertexBuffer{ 0u, vertexBuffer, 0u }));
    TEST_ASSERT_TRUE(RecordedAt<render::PushUniforms>(8) == (render::PushUniforms{ render::ShaderStage::Fragment, 0u, NoFogUniforms(160.0f / 255.0f) }));
    TEST_ASSERT_TRUE(RecordedAt<render::Draw>(9) == (render::Draw{ 3u, 0u }));
    TEST_ASSERT_TRUE(RecordedAt<render::SetPipeline>(10) == render::SetPipeline{ points.pipeline });
    TEST_ASSERT_TRUE(RecordedAt<render::Draw>(11) == (render::Draw{ 2u, 3u }));
    RecordedAt<render::EndPass>(12);
    AssertSubmitted(13);
}


TEST(std3DBabylon, LitDrawsStreamApartWithTheLitPipeline)
{
    OpenStd3D();

    // The lit shaders bind what the default ones do.
    TEST_ASSERT_EQUAL_UINT32(0u, stdBabylon::shaders::std3dLitVert().textureCount);
    TEST_ASSERT_EQUAL_UINT32(1u, stdBabylon::shaders::std3dLitVert().uniformCount);
    TEST_ASSERT_EQUAL_UINT32(1u, stdBabylon::shaders::std3dLitFrag().textureCount);
    TEST_ASSERT_EQUAL_UINT32(1u, stdBabylon::shaders::std3dLitFrag().uniformCount);
    // The light buffer is bound for the lit draws; the per-pixel lit fragment shader reads it (M10.2).
    TEST_ASSERT_EQUAL_UINT32(0u, stdBabylon::shaders::std3dLitVert().storageBufferCount);
    TEST_ASSERT_EQUAL_UINT32(1u, stdBabylon::shaders::std3dLitFrag().storageBufferCount);

    const StdVertexLit aLit[3] = { LitVertex(Vertex(1.0f, 2.0f, 0.5f, 1.0f, 0xFF102030u), -1.0f, 10.0f, 2.0f, 0.0f, -1.0f, 0.0f),
        LitVertex(Vertex(9.0f, 2.0f, 0.5f, 1.0f, 0xFF405060u), 3.0f, 10.0f, 2.0f, 0.6f, 0.0f, 0.8f),
        LitVertex(Vertex(1.0f, 7.0f, 0.5f, 1.0f, 0xFF708090u), -1.0f, 10.0f, -1.0f, 0.0f, 0.0f, 0.0f) };
    const StdVertexTL aClassic[3] = { Vertex(0.0f, 0.0f, 0.25f, 1.0f, 0xFFFFFFFFu), Vertex(4.0f, 0.0f, 0.25f, 1.0f, 0xFFFFFFFFu),
        Vertex(0.0f, 4.0f, 0.25f, 1.0f, 0xFFFFFFFFu) };
    const uint16_t aIndices[3] = { 0u, 1u, 2u };
    const uint16_t aBadIndices[3] = { 0u, 1u, 3u };

    render::PipelineHandle litPipeline;
    render::PipelineHandle classicPipeline;
    render::BufferHandle litBuffer;
    render::BufferHandle vertexBuffer;
    render::BufferHandle indexBuffer;
    render::BufferHandle lightBuffer;
    for ( int frameNum = 0; frameNum < 2; ++frameNum )
    {
        Null().clearRecorded();
        TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
        std3D_DrawRenderListLit(nullptr, (Std3DRenderState)0, const_cast<StdVertexLit*>(aLit), 3u, const_cast<uint16_t*>(aIndices), 3u);
        std3D_DrawRenderList(nullptr, (Std3DRenderState)0, const_cast<StdVertexTL*>(aClassic), 3u, const_cast<uint16_t*>(aIndices), 3u);
        // Refused like std3D_DrawRenderList: an index past the vertices, no vertices, no whole triangle.
        std3D_DrawRenderListLit(nullptr, (Std3DRenderState)0, const_cast<StdVertexLit*>(aLit), 3u, const_cast<uint16_t*>(aBadIndices), 3u);
        std3D_DrawRenderListLit(nullptr, (Std3DRenderState)0, nullptr, 3u, const_cast<uint16_t*>(aIndices), 3u);
        std3D_DrawRenderListLit(nullptr, (Std3DRenderState)0, const_cast<StdVertexLit*>(aLit), 3u, const_cast<uint16_t*>(aIndices), 2u);
        EndFrame();

        // The two pipelines are created in the first frame only, and so is the light buffer, which gets
        // its empty list once (no std3D_SetLights) and is bound for the lit draws of every pass.
        const size_t numCreated = frameNum == 0 ? 2u : 0u;
        const size_t numLightUpdates = frameNum == 0 ? 1u : 0u;
        TEST_ASSERT_EQUAL_size_t(numCreated + numLightUpdates + 18u, Null().recorded().size());
        if ( frameNum == 0 )
        {
            const render::PipelineCreated& lit     = RecordedAt<render::PipelineCreated>(0);
            const render::PipelineCreated& classic = RecordedAt<render::PipelineCreated>(1);
            TEST_ASSERT_TRUE(lit.desc == ExpectedLitPipeline(true));
            TEST_ASSERT_TRUE(classic.desc == ExpectedPipeline(render::PrimitiveType::TriangleList, true));
            litPipeline     = lit.pipeline;
            classicPipeline = classic.pipeline;
            litBuffer       = RecordedAt<render::UpdateBuffer>(2).buffer;
            indexBuffer     = RecordedAt<render::UpdateBuffer>(3).buffer;
            vertexBuffer    = RecordedAt<render::UpdateBuffer>(4).buffer;
            TEST_ASSERT_FALSE(litBuffer == vertexBuffer);
            TEST_ASSERT_FALSE(litBuffer == indexBuffer);
        }

        // The lit vertices go to their own buffer, all 56 bytes of each; the indices follow each other.
        const size_t i = numCreated;
        TEST_ASSERT_TRUE(RecordedAt<render::UpdateBuffer>(i) == (render::UpdateBuffer{ litBuffer, 0u, BytesOf(aLit, 3u) }));
        TEST_ASSERT_TRUE(RecordedAt<render::UpdateBuffer>(i + 1u) == (render::UpdateBuffer{ indexBuffer, 0u, BytesOf(aIndices, 3u) }));
        TEST_ASSERT_TRUE(RecordedAt<render::UpdateBuffer>(i + 2u) == (render::UpdateBuffer{ vertexBuffer, 0u, BytesOf(aClassic, 3u) }));
        TEST_ASSERT_TRUE(RecordedAt<render::UpdateBuffer>(i + 3u) == (render::UpdateBuffer{ indexBuffer, 6u, BytesOf(aIndices, 3u) }));
        if ( frameNum == 0 )
        {
            lightBuffer = RecordedAt<render::UpdateBuffer>(i + 4u).buffer;
            TEST_ASSERT_TRUE(RecordedAt<render::UpdateBuffer>(i + 4u) == (render::UpdateBuffer{ lightBuffer, 0u, std::vector<std::byte>(16u) }));
        }

        const size_t p = i + 4u + numLightUpdates;
        const render::LoadOp load = frameNum == 0 ? render::LoadOp::Clear : render::LoadOp::Load;
        AssertScenePass(p, load, load);
        TEST_ASSERT_TRUE(RecordedAt<render::PushUniforms>(p + 1u) == (render::PushUniforms{ render::ShaderStage::Vertex, 0u, ViewportUniforms(640.0f, 480.0f) }));
        TEST_ASSERT_TRUE(RecordedAt<render::BindStorageBuffer>(p + 2u) == (render::BindStorageBuffer{ render::ShaderStage::Fragment, 0u, lightBuffer }));
        TEST_ASSERT_TRUE(RecordedAt<render::SetPipeline>(p + 3u) == render::SetPipeline{ litPipeline });
        TEST_ASSERT_TRUE(RecordedAt<render::BindVertexBuffer>(p + 4u) == (render::BindVertexBuffer{ 0u, litBuffer, 0u }));
        TEST_ASSERT_TRUE(RecordedAt<render::PushUniforms>(p + 5u) == (render::PushUniforms{ render::ShaderStage::Fragment, 0u, NoFogUniforms(0.0f) }));
        TEST_ASSERT_TRUE(RecordedAt<render::BindIndexBuffer>(p + 6u) == (render::BindIndexBuffer{ indexBuffer, render::IndexType::Uint16, 0u }));
        RecordedAt<render::BindTexture>(p + 7u);
        TEST_ASSERT_TRUE(RecordedAt<render::DrawIndexed>(p + 8u) == (render::DrawIndexed{ 3u, 0u, 0 }));
        TEST_ASSERT_TRUE(RecordedAt<render::SetPipeline>(p + 9u) == render::SetPipeline{ classicPipeline });
        TEST_ASSERT_TRUE(RecordedAt<render::BindVertexBuffer>(p + 10u) == (render::BindVertexBuffer{ 0u, vertexBuffer, 0u }));
        TEST_ASSERT_TRUE(RecordedAt<render::DrawIndexed>(p + 11u) == (render::DrawIndexed{ 3u, 3u, 0 }));
        RecordedAt<render::EndPass>(p + 12u);
        AssertSubmitted(p + 13u);
    }
}


// std3D_SetLights (M6.9c): the list reaches a storage buffer in the first pass after it changed,
// before the pass, and the buffer is bound to fragment storage slot 0 for the lit draws only. Without
// lights and lit draws (the litFaces option off) no light buffer is made or bound.
TEST(std3DBabylon, LightsReachTheLightBufferOfTheLitDraws)
{
    OpenStd3D();

    const StdLight aLights[2] = {
        { 1.0f, 2.0f, -4.0f, 8.0f, 0.5f, 0.25f, 1.0f, 0.0f, 3.0f, 0.2f, 0.1f, STD_LIGHT_ENABLED },
        { -0.5f, 12.0f, 0.75f, 2.5f, 1.0f, 1.0f, 1.0f, 0.4f, 2.5f, 0.2f, 0.1f, 0u },
    };
    const StdVertexLit aLit[3] = { LitVertex(Vertex(1.0f, 2.0f, 0.5f, 1.0f, 0xFF102030u), -1.0f, 10.0f, 2.0f, 0.0f, -1.0f, 0.0f),
        LitVertex(Vertex(9.0f, 2.0f, 0.5f, 1.0f, 0xFF405060u), 3.0f, 10.0f, 2.0f, 0.6f, 0.0f, 0.8f),
        LitVertex(Vertex(1.0f, 7.0f, 0.5f, 1.0f, 0xFF708090u), -1.0f, 10.0f, -1.0f, 0.0f, 0.0f, 0.0f) };
    const StdVertexTL aClassic[3] = { Vertex(0.0f, 0.0f, 0.25f, 1.0f, 0xFFFFFFFFu), Vertex(4.0f, 0.0f, 0.25f, 1.0f, 0xFFFFFFFFu),
        Vertex(0.0f, 4.0f, 0.25f, 1.0f, 0xFFFFFFFFu) };
    const uint16_t aIndices[3] = { 0u, 1u, 2u };
    const auto isStorageBind = [](const render::RecordedCommand& c) { return std::holds_alternative<render::BindStorageBuffer>(c); };

    // Classic draws only, no lights: no light buffer.
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_DrawRenderList(nullptr, (Std3DRenderState)0, const_cast<StdVertexTL*>(aClassic), 3u, const_cast<uint16_t*>(aIndices), 3u);
    EndFrame();
    TEST_ASSERT_FALSE(AnyRecorded(isStorageBind));
    TEST_ASSERT_EQUAL_size_t(2u, (size_t)std::count_if(Null().recorded().begin(), Null().recorded().end(),
        [](const render::RecordedCommand& c) { return std::holds_alternative<render::UpdateBuffer>(c); }));

    // Lights set before the scene, then a lit and a classic draw: the list goes into the buffer
    // before the pass, which binds it before the lit draw.
    Null().clearRecorded();
    std3D_SetLights(aLights, 2u);
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_DrawRenderListLit(nullptr, (Std3DRenderState)0, const_cast<StdVertexLit*>(aLit), 3u, const_cast<uint16_t*>(aIndices), 3u);
    std3D_DrawRenderList(nullptr, (Std3DRenderState)0, const_cast<StdVertexTL*>(aClassic), 3u, const_cast<uint16_t*>(aIndices), 3u);
    EndFrame();

    size_t numCreated = 1u; // the lit pipeline
    const render::UpdateBuffer& upload = RecordedAt<render::UpdateBuffer>(numCreated + 4u);
    const render::BufferHandle lightBuffer = upload.buffer;
    TEST_ASSERT_TRUE(upload == (render::UpdateBuffer{ lightBuffer, 0u, LightBufferBytes(aLights, 2u) }));
    RecordedAt<render::BeginPass>(numCreated + 5u);
    TEST_ASSERT_TRUE(RecordedAt<render::BindStorageBuffer>(numCreated + 7u) == (render::BindStorageBuffer{ render::ShaderStage::Fragment, 0u, lightBuffer }));
    TEST_ASSERT_TRUE(RecordedAt<render::SetPipeline>(numCreated + 8u).pipeline == RecordedAt<render::PipelineCreated>(0).pipeline);
    TEST_ASSERT_EQUAL_size_t(1u, (size_t)std::count_if(Null().recorded().begin(), Null().recorded().end(), isStorageBind));
    AssertBytes(LightBufferBytes(aLights, 2u), Null().readBuffer(lightBuffer, 0u, 16u + 2u * (uint32_t)sizeof(StdLight)));

    // The list again, then an empty list set inside the scene before its first pass ends (at
    // std3D_ClearZBuffer): that pass uploads the latest list only, the empty one, and the second
    // pass, with nothing new, binds the buffer without an upload.
    Null().clearRecorded();
    std3D_SetLights(aLights, 2u);
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_DrawRenderListLit(nullptr, (Std3DRenderState)0, const_cast<StdVertexLit*>(aLit), 3u, const_cast<uint16_t*>(aIndices), 3u);
    std3D_SetLights(nullptr, 0u);
    std3D_ClearZBuffer();
    std3D_DrawRenderListLit(nullptr, (Std3DRenderState)0, const_cast<StdVertexLit*>(aLit), 3u, const_cast<uint16_t*>(aIndices), 3u);
    EndFrame();
    std::vector<render::UpdateBuffer> lightUploads;
    for ( const render::RecordedCommand& command : Null().recorded() )
    {
        if ( const auto* const pUpdate = std::get_if<render::UpdateBuffer>(&command); pUpdate && pUpdate->buffer == lightBuffer )
        {
            lightUploads.push_back(*pUpdate);
        }
    }

    TEST_ASSERT_EQUAL_size_t(1u, lightUploads.size());
    TEST_ASSERT_TRUE(lightUploads[0] == (render::UpdateBuffer{ lightBuffer, 0u, std::vector<std::byte>(16u) }));
    TEST_ASSERT_EQUAL_size_t(2u, (size_t)std::count_if(Null().recorded().begin(), Null().recorded().end(), isStorageBind));

    // Refused lists set the empty list.
    std::vector<StdLight> tooMany(STD3D_MAXLIGHTS + 1u, aLights[0]);
    for ( const bool bNull : { false, true } )
    {
        std3D_SetLights(aLights, 2u);
        TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
        std3D_DrawRenderListLit(nullptr, (Std3DRenderState)0, const_cast<StdVertexLit*>(aLit), 3u, const_cast<uint16_t*>(aIndices), 3u);
        EndFrame();
        TEST_ASSERT_EQUAL_UINT8(2u, std::to_integer<uint8_t>(Null().readBuffer(lightBuffer, 0u, 4u)[0]));

        std3D_SetLights(bNull ? nullptr : tooMany.data(), bNull ? 1u : tooMany.size());
        TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
        EndFrame();
        AssertBytes(std::vector<std::byte>(4u), Null().readBuffer(lightBuffer, 0u, 4u));
    }
}

// The light buffer goes with std3D_Close; lights set after the next std3D_Open reach a new one.
TEST(std3DBabylon, TheLightBufferIsReleasedAtClose)
{
    const StdLight light = { 1.0f, 2.0f, 3.0f, 4.0f, 0.5f, 0.5f, 0.5f, 0.0f, 2.0f, 0.2f, 0.1f, STD_LIGHT_ENABLED };
    OpenStd3D();
    std3D_SetLights(&light, 1u);
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    EndFrame();
    const render::BufferHandle first = RecordedAt<render::UpdateBuffer>(0).buffer;
    AssertBytes(LightBufferBytes(&light, 1u), Null().readBuffer(first, 0u, 16u + (uint32_t)sizeof(StdLight)));
    CloseStd3D();

    OpenStd3D();
    Null().clearRecorded();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    EndFrame();
    // The list went with the buffer: nothing is uploaded without new lights or lit draws.
    TEST_ASSERT_EQUAL_size_t(0u, (size_t)std::count_if(Null().recorded().begin(), Null().recorded().end(),
        [](const render::RecordedCommand& c) { return std::holds_alternative<render::UpdateBuffer>(c); }));
}

TEST(std3DBabylon, ShaderFogChangesWithTheFogBitOnly)
{
    OpenStd3D();
    StdVertexTL aVerts[3] = { Vertex(0.0f, 0.0f, 0.5f, 1.0f, 0xFFFFFFFFu), Vertex(8.0f, 0.0f, 0.5f, 1.0f, 0xFFFFFFFFu),
        Vertex(0.0f, 8.0f, 0.5f, 1.0f, 0xFFFFFFFFu) };
    uint16_t aIndices[3] = { 0u, 1u, 2u };
    const Std3DRenderState fog   = STD3D_RS_FOG_ENABLED;
    const Std3DRenderState noFog = (Std3DRenderState)0;

    // std3DX9.c's shader fog: the end scaled by 2 - density, the values taken when the fog bit
    // changes (stdShader_SetFog), zeroed when it goes off (stdShader_DisableFog, the color stays).
    std3D_EnableFog(1, 1.0f);
    std3D_SetFog(0.25f, 0.5f, 0.75f, 2.0f, 50.0f);
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_DrawRenderList(nullptr, fog, aVerts, 3u, aIndices, 3u);
    std3D_SetFog(1.0f, 0.0f, 0.0f, 10.0f, 20.0f);
    std3D_DrawRenderList(nullptr, fog, aVerts, 3u, aIndices, 3u);   // the bit stayed: the old values, nothing pushed
    std3D_DrawRenderList(nullptr, noFog, aVerts, 3u, aIndices, 3u);
    std3D_DrawRenderList(nullptr, fog, aVerts, 3u, aIndices, 3u);   // now the new ones
    std3D_DrawRenderList(nullptr, noFog, aVerts, 3u, aIndices, 3u);
    std3D_EnableFog(0, 1.0f);
    std3D_DrawRenderList(nullptr, fog, aVerts, 3u, aIndices, 3u);   // fog is off: zeros, as pushed before
    EndFrame();

    const float aFog1[4]      = { 2.0f, 50.0f, 1.0f / 48.0f, 1.0f };
    const float aFogColor1[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
    const float aFog2[4]      = { 10.0f, 20.0f, 1.0f / 10.0f, 1.0f };
    const float aFogColor2[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    const float aOff[4]       = { 0.0f, 0.0f, 0.0f, 0.0f };
    const std::vector<std::vector<std::byte>> aExpected = {
        FragmentUniforms(aFog1, aFogColor1, 0.0f),
        FragmentUniforms(aOff, aFogColor1, 0.0f),
        FragmentUniforms(aFog2, aFogColor2, 0.0f),
        FragmentUniforms(aOff, aFogColor2, 0.0f),
    };
    const std::vector<std::vector<std::byte>> aPushed = RecordedFragmentUniforms();
    TEST_ASSERT_EQUAL_size_t(aExpected.size(), aPushed.size());
    for ( size_t i = 0; i < aExpected.size(); ++i )
    {
        AssertBytes(aExpected[i], aPushed[i]);
    }
}

TEST(std3DBabylon, ProjectionAndFogFollowDX9)
{
    TEST_ASSERT_EQUAL_INT((int)0x80070057L, std3D_SetProjection(1.0f, 1.0f, 1.001f));
    TEST_ASSERT_EQUAL_INT((int)0x80070057L, std3D_SetProjection(0.0f, 1.0f, 10.0f));
    TEST_ASSERT_EQUAL_INT(0, std3D_SetProjection(1.5f, 0.1f, 100.0f));

    // std3DX9.c keeps the fog density only with an open device.
    std3D_EnableFog(1, 0.5f);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, std3D_g_fogDensity);
    OpenStd3D();
    std3D_EnableFog(1, 0.5f);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, std3D_g_fogDensity);
    std3D_SetFog(0.1f, 0.2f, 0.3f, 4.0f, 100.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, std3D_g_fogDensity);
    TEST_ASSERT_EQUAL_INT(0, std3D_SetMipmapFilter(STD3D_MIPMAPFILTER_TRILINEAR));
}

TEST(std3DBabylon, DisplayEnvironmentListsTheRenderDevice)
{
    StdDisplayEnvironment* pEnv = std3D_BuildDisplayEnvironment();
    TEST_ASSERT_NOT_NULL(pEnv);
    TEST_ASSERT_EQUAL_size_t(1u, pEnv->numInfos);
    TEST_ASSERT_EQUAL_size_t(1u, pEnv->aDisplayInfos[0].numDevices);
    TEST_ASSERT_EQUAL_STRING("Jones3D Babylon Display (Babylon render, null)", pEnv->aDisplayInfos[0].aDevices[0].deviceDescription);
    std3D_FreeDisplayEnvironment(pEnv);
}

TEST(std3DBabylon, ACaptureReplaysIntoTheBabylonBackend)
{
    static StdVertexTL aVertices[3] = {
        {  1.0f,  2.0f, 4.0f, 0.5f, 0x11223344u, 0x55667788u,  0.25f, -0.5f },
        {  0.0f,  0.0f, 0.0f, 0.0f, 0xFFFFFFFFu, 0x00000000u,  0.0f,   0.0f },
        { -1.0f,  1.5f, 0.0f, 1.0f, 0x00FF00FFu, 0xDEADBEEFu,  1.0f,   2.0f },
    };
    static uint16_t aIndices[3] = { 0u, 1u, 2u };

    // Recorded through the forwarders on this backend, with a cached texture drawn in two scenes.
    TEST_ASSERT_TRUE(stdDisplayCapture_Open(kCapturePath, "babylon"));
    OpenStd3D();
    static tSystemTexture texture;
    MakeCachedTexture(&texture, stdColor_cfARGB8888, 7);
    for ( int frame = 0; frame < 2; ++frame )
    {
        std3D_ClearZBuffer();
        TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
        std3D_UpdateFrameCount(&texture);
        std3D_DrawRenderList(texture.pCachedTexture, STD3D_RS_ZWRITE_DISABLED, aVertices, 3u, aIndices, 3u);
        EndFrame();
    }
    std3D_ClearSystemTexture(&texture);
    CloseStd3D();
    stdDisplayCapture_Close();
    TEST_ASSERT_FALSE(stdRenderCapture_IsOpen());

    // The replay drives the same backends: no error, the mode set's clear and present, the same two
    // frames, the same capture again.
    UseNullDevice();
    TEST_ASSERT_EQUAL_INT(0, stdRenderReplay_Run(kCapturePath, kReplayPath));
    TEST_ASSERT_EQUAL_size_t(28u, Null().recorded().size());
    RecordedAt<render::BeginPass>(0);
    RecordedAt<render::EndPass>(1);
    AssertSubmitted(2);
    RecordedAt<render::PipelineCreated>(3); // once, in the first frame
    for ( size_t frame = 0; frame < 2; ++frame )
    {
        const size_t first = 4u + frame * 12u;
        RecordedAt<render::UpdateBuffer>(first);
        RecordedAt<render::UpdateBuffer>(first + 1u);
        AssertScenePassLoads(first + 2u, frame == 0 ? render::LoadOp::Clear : render::LoadOp::Load, render::LoadOp::Clear);
        const render::DrawIndexed& draw = RecordedAt<render::DrawIndexed>(first + 9u);
        TEST_ASSERT_EQUAL_UINT32(3u, draw.indexCount);
        RecordedAt<render::EndPass>(first + 10u);
        AssertSubmitted(first + 11u);
    }

    const std::vector<char> in  = ReadFile(kCapturePath);
    const std::vector<char> out = ReadFile(kReplayPath);
    TEST_ASSERT_TRUE(in.size() > 16u);
    TEST_ASSERT_EQUAL_size_t(in.size(), out.size());
    TEST_ASSERT_EQUAL_MEMORY(in.data(), out.data(), in.size());
}


TEST(std3DBabylon, TheNormalTargetComesWithTheBackBufferAndClearsWithDepth)
{
    OpenStd3D(32u, 16u);
    const render::TextureHandle normal = stdRenderBabylon_GetNormalTarget();
    TEST_ASSERT_FALSE(normal.isNull());
    TEST_ASSERT_FALSE(normal == stdRenderBabylon_GetColorTarget());
    TEST_ASSERT_TRUE(stdRenderBabylon_GetNormalFormat() == render::TextureFormat::R10G10B10A2);

    // The first scene clears it to 0 with depth; a later one without a depth clear loads it.
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    EndFrame();
    AssertBytes(std::vector<std::byte>(32u * 16u * 4u, std::byte{ 0 }), Null().readTexture(normal, 0));
    const std::vector<std::byte> aPattern(32u * 16u * 4u, std::byte{ 0x5A });
    TEST_ASSERT_TRUE(Null().uploadTexture(normal, 0, aPattern));
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    EndFrame();
    AssertBytes(aPattern, Null().readTexture(normal, 0));
    std3D_ClearZBuffer();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    EndFrame();
    AssertBytes(std::vector<std::byte>(32u * 16u * 4u, std::byte{ 0 }), Null().readTexture(normal, 0));

    // It goes and comes with the back buffer.
    stdDisplay_SetDefaultResolution(8u, 8u);
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_SetMode(0u, 0, 1u));
    TEST_ASSERT_FALSE(stdRenderBabylon_GetNormalTarget() == normal);
    TEST_ASSERT_TRUE(ReadTexture(normal, 0).empty());
    TEST_ASSERT_EQUAL_size_t(8u * 8u * 4u, Null().readTexture(stdRenderBabylon_GetNormalTarget(), 0).size());
}

TEST(std3DBabylon, TheNormalTargetIsRgba8WithoutR10G10B10A2)
{
    UseNullDevice(render::NullDeviceDesc{ { render::TextureFormat::R10G10B10A2 } });
    OpenStd3D();
    TEST_ASSERT_TRUE(stdRenderBabylon_GetNormalFormat() == render::TextureFormat::Rgba8);
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    StdVertexTL aVerts[3] = { Vertex(0.0f, 0.0f, 0.5f, 1.0f, 0xFFFFFFFFu), Vertex(8.0f, 0.0f, 0.5f, 1.0f, 0xFFFFFFFFu),
        Vertex(0.0f, 8.0f, 0.5f, 1.0f, 0xFFFFFFFFu) };
    uint16_t aIndices[3] = { 0u, 1u, 2u };
    std3D_DrawRenderList(nullptr, (Std3DRenderState)0, aVerts, 3u, aIndices, 3u);
    EndFrame();

    // The pipeline names the format, and the device accepted the draw.
    const render::PipelineCreated& created = RecordedAt<render::PipelineCreated>(0);
    TEST_ASSERT_EQUAL_size_t(2u, created.desc.colorTargets.size());
    TEST_ASSERT_TRUE(created.desc.colorTargets[1].format == render::TextureFormat::Rgba8);
    bool bDrawn = false;
    for ( const render::RecordedCommand& command : Null().recorded() )
    {
        bDrawn = bDrawn || std::holds_alternative<render::DrawIndexed>(command);
    }
    TEST_ASSERT_TRUE(bDrawn);
}
