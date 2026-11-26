// Streams, pipelines and draws of the Babylon std3D backend.
// Part of the Babylon column's std3D backend; see std3DBabylon.cpp for the overview. Adapter code: no Fixed/Added markers inside.
#include <std/General/stdRenderBackend.h>
#include <std/General/std3D.h>
#include <std/General/stdDisplay.h>

#include <std/General/std.h>
#include <std/General/stdColor.h>
#include <std/General/stdConfig.h>
#include <std/General/stdEffect.h>
#include <std/General/stdMath.h>
#include <std/General/stdMemory.h>
#include <std/General/stdUtil.h>

#include <babylon/render/Buffer.h>
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Handle.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Sampler.h>
#include <babylon/render/Texture.h>
#include <babylon/shaders/stdBabylonShaders.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

#include "std3DBabylon.h"
#include "stdBabylon.h"
#include "stdRenderBabylon.h"
#include "std3DBabylonInternal.h"

namespace render = babylon::render;
using namespace std3DBabylonInternal;

namespace std3DBabylonInternal
{
    // The render state of the draws; see std3D_SetRenderState.
    Std3DRenderState std3D_renderState      = (Std3DRenderState)0;
#ifdef J3D_TEST
    // Added: M11.2c. The vertices recorded this frame by draws that carried STD3D_RS_SHADOW_CASTER.
    // The flag stays for M11.3's receiver mask; casters now come through std3D_AddShadowCasterPolygon.
    uint32_t std3D_casterVertexCount = 0;
#endif
    tSysTexture* std3D_pCurTex             = nullptr;
    // Created at std3D_Open (the white texture std3DX9.c bound for untextured draws, the stream
    // buffers) or on first use (pipelines, samplers), destroyed at std3D_Close.
    render::PipelineHandle std3D_aPipelines[std3D_kNumPrimitives][2][2]; // [primitive][HUD layer][z write]
    render::PipelineHandle std3D_aLitPipelines[2][2];                    // lit triangles, [HUD layer][z write]
    // Added: variants for a water surface drawn without Z write (M9.17). Their normal target keeps the
    // colour and replaces the alpha with the surface tag, so the water is tagged even though it does
    // not write depth. [primitive][HUD layer] and the lit form, [HUD layer].
    render::PipelineHandle std3D_aWaterPipelines[std3D_kNumPrimitives][2];
    render::PipelineHandle std3D_aLitWaterPipelines[2];
    render::TextureHandle std3D_whiteTexture;
    render::BufferHandle std3D_vertexBuffer;
    render::BufferHandle std3D_indexBuffer;
    uint32_t std3D_vertexBufferSize   = 0; // bytes
    uint32_t std3D_indexBufferSize    = 0;
    uint32_t std3D_vertexBufferOffset = 0; // bytes the frame has streamed so far
    uint32_t std3D_indexBufferOffset  = 0;
    render::BufferHandle std3D_litVertexBuffer; // made on the first lit draw
    uint32_t std3D_litVertexBufferSize   = 0;
    uint32_t std3D_litVertexBufferOffset = 0;
    // The fog values the shaders see. stdShader_SetFog copied them into the shader constants only
    // when the fog bit of the render state changed (std3DX9.c's SetRenderState), so they are kept
    // here and only std3D_SetRenderState updates them.
    float std3D_aShaderFogParams[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float std3D_aShaderFogColor[4]  = { 0.0f, 0.0f, 0.0f, 0.0f };
}

namespace
{
    // std3DX9.c's buffer sizes were STD3D_MAXVERTICES * STD3D_MAXFACEVERTICES vertices (64 MiB) and
    // three indices per vertex; these start smaller and grow when a frame needs more.
    constexpr uint32_t std3D_kInitialVertexBufferSize = 32768u * (uint32_t)sizeof(StdVertexTL);
    constexpr uint32_t std3D_kInitialIndexBufferSize  = 3u * 32768u * (uint32_t)sizeof(uint16_t);
    constexpr uint32_t std3D_kInitialLitVertexBufferSize = 32768u * (uint32_t)sizeof(StdVertexLit);

    // D3DRS_ALPHAREF with STD3D_RS_ALPHAREF_SET (std3DX9.c's SetRenderState), else 0.
    constexpr uint32_t std3D_kAlphaRef = 160u;

    // Makes room for numBytes at *pOffset of a stream buffer. When the rest of the buffer is too
    // small for them, a buffer twice as big (or as big as numBytes) takes over from offset 0; the old
    // one is destroyed after the frame's submit, since the frame's earlier draws still read it.
    bool std3D_ReserveStream(render::BufferHandle* pBuffer, uint32_t* pSize, uint32_t* pOffset, uint64_t numBytes,
        render::BufferUsage usage, const char* pName)
    {
        if ( (uint64_t)*pOffset + numBytes <= *pSize )
        {
            return true;
        }

        const uint64_t maxSize = std3D_pRenderDevice->info().limits.maxBufferSize;
        if ( numBytes > maxSize )
        {
            STDLOG_ERROR("Error %llu bytes don't fit a %s buffer (limit %llu).\n", (unsigned long long)numBytes, pName, (unsigned long long)maxSize);
            return false;
        }

        const uint32_t newSize = (uint32_t)std::min(maxSize, std::max(2u * (uint64_t)*pSize, numBytes));
        const render::BufferHandle buffer = std3D_pRenderDevice->createBuffer(render::BufferDesc{ newSize, usage });
        if ( buffer.isNull() )
        {
            STDLOG_ERROR("Error creating a %u byte %s buffer.\n", newSize, pName);
            return false;
        }

        stdRenderBabylon_DestroyBuffer(*pBuffer);
        *pBuffer = buffer;
        *pSize   = newSize;
        *pOffset = 0;
        return true;
    }

    // std3DX9.c's Direct3D state as pipeline state: alpha blending with SRCALPHA, INVSRCALPHA for
    // color and alpha (D3DRS_ALPHABLENDENABLE, never turned off, without separate alpha blending); the
    // Z test LESSEQUAL (D3DRS_ZENABLE, D3DRS_ZFUNC), Z writes as STD3D_RS_ZWRITE_DISABLED says; no
    // culling. The normal target, the second color target, is replaced by draws that write Z and kept
    // (blended with ZERO, ONE) by the others, so it holds the normal of what the depth target holds.
    // Added: a water draw without Z write keeps the normal's colour but replaces its alpha with the
    // tag (ZERO, ONE on the colour, ONE, ZERO on the alpha), so translucent water is marked without
    // changing the image or the normals behind it (M9.17).
    void std3D_SetPipelineState(render::PipelineDesc& desc, bool bZWrite, bool bWaterTag, bool bHud)
    {
        render::BlendState blend;
        blend.enabled  = true;
        blend.srcColor = render::BlendFactor::SrcAlpha;
        blend.dstColor = render::BlendFactor::OneMinusSrcAlpha;
        blend.colorOp  = render::BlendOp::Add;
        // Added: the HUD layer is transparent until its draws, so its alpha channel must hold the
        // coverage the composite blends the scene with. The scene's color target is opaque, where the
        // original factors only fed an unused channel.
        blend.srcAlpha = bHud ? render::BlendFactor::One : render::BlendFactor::SrcAlpha;
        blend.dstAlpha = render::BlendFactor::OneMinusSrcAlpha;
        blend.alphaOp  = render::BlendOp::Add;
        render::BlendState normalBlend;
        if ( bZWrite )
        {
            // Replaced by the draw's normal and tag.
        }
        else
        {
            normalBlend.enabled  = true;
            normalBlend.srcColor = render::BlendFactor::Zero;
            normalBlend.dstColor = render::BlendFactor::One;
            if ( bWaterTag )
            {
                // Keep the normal's colour, take the shader's alpha tag.
                normalBlend.srcAlpha = render::BlendFactor::One;
                normalBlend.dstAlpha = render::BlendFactor::Zero;
            }
            else
            {
                normalBlend.srcAlpha = render::BlendFactor::Zero;
                normalBlend.dstAlpha = render::BlendFactor::One;
            }
            normalBlend.colorOp = render::BlendOp::Add;
            normalBlend.alphaOp = render::BlendOp::Add;
        }
        desc.colorTargets = { render::ColorTargetDesc{ stdRenderBabylon_GetColorFormat(), blend },
            render::ColorTargetDesc{ stdRenderBabylon_GetNormalFormat(), normalBlend } };
        desc.depthFormat  = stdRenderBabylon_GetDepthFormat();
        desc.depth        = render::DepthState{ true, bZWrite, render::CompareOp::LessOrEqual };
        desc.cull         = render::CullMode::None;
    }

    // The StdVertexTL attributes, at locations 0 to 3 of the vertex shaders, in a vertex of stride bytes.
    void std3D_SetVertexLayout(render::PipelineDesc& desc, uint32_t stride)
    {
        desc.vertexLayout.bufferStrides = { stride };
        desc.vertexLayout.attributes    = {
            render::VertexAttribute{ 0, 0, render::VertexFormat::Float4, (uint32_t)offsetof(StdVertexTL, sx) },
            render::VertexAttribute{ 1, 0, render::VertexFormat::UByte4Norm, (uint32_t)offsetof(StdVertexTL, color) },
            render::VertexAttribute{ 2, 0, render::VertexFormat::UByte4Norm, (uint32_t)offsetof(StdVertexTL, specular) },
            render::VertexAttribute{ 3, 0, render::VertexFormat::Float2, (uint32_t)offsetof(StdVertexTL, tu) },
        };
    }

    // std3DX9.c's Direct3D state as a pipeline (std3D_SetPipelineState): its default shader for
    // triangles, its wireframe shader for lines and points, StdVertexTL vertices. The pipeline of each
    // primitive and Z write is created once, on first use.
    render::PipelineHandle std3D_GetPipeline(Std3DBabylonPrimitive primitive, bool bZWrite,
        bool bWaterTag, bool bHud)
    {
        const bool bWaterAlpha = bWaterTag && !bZWrite;
        render::PipelineHandle& pipeline = bWaterAlpha
            ? std3D_aWaterPipelines[(size_t)primitive][bHud ? 1 : 0]
            : std3D_aPipelines[(size_t)primitive][bHud ? 1 : 0][bZWrite ? 1 : 0];
        if ( !pipeline.isNull() )
        {
            return pipeline;
        }

        render::PipelineDesc desc;
        desc.vertexShader   = stdBabylon::shaders::std3dVert();
        desc.fragmentShader = primitive == Std3DBabylonPrimitive::Triangles ? stdBabylon::shaders::std3dFrag() : stdBabylon::shaders::std3dWireframeFrag();
        std3D_SetVertexLayout(desc, (uint32_t)sizeof(StdVertexTL));

        switch ( primitive )
        {
            case Std3DBabylonPrimitive::Triangles:
                desc.primitive = render::PrimitiveType::TriangleList;
                break;
            case Std3DBabylonPrimitive::Lines:
                desc.primitive = render::PrimitiveType::LineStrip;
                break;
            case Std3DBabylonPrimitive::Points:
                desc.primitive = render::PrimitiveType::PointList;
                break;
        }

        std3D_SetPipelineState(desc, bZWrite, bWaterAlpha, bHud);
        pipeline = std3D_pRenderDevice->createPipeline(desc);
        if ( pipeline.isNull() )
        {
            STDLOG_ERROR("Error creating the pipeline of primitive %d.\n", (int)primitive);
        }

        return pipeline;
    }

    // The pipeline of lit triangles: the state of std3D_GetPipeline's triangles with the lit shaders
    // and StdVertexLit vertices, the view-space position and normal at locations 4 and 5. Created once
    // per Z write, on first use.
    render::PipelineHandle std3D_GetLitPipeline(bool bZWrite, bool bWaterTag, bool bHud)
    {
        const bool bWaterAlpha = bWaterTag && !bZWrite;
        render::PipelineHandle& pipeline = bWaterAlpha
            ? std3D_aLitWaterPipelines[bHud ? 1 : 0]
            : std3D_aLitPipelines[bHud ? 1 : 0][bZWrite ? 1 : 0];
        if ( !pipeline.isNull() )
        {
            return pipeline;
        }

        render::PipelineDesc desc;
        desc.vertexShader   = stdBabylon::shaders::std3dLitVert();
        desc.fragmentShader = stdBabylon::shaders::std3dLitFrag();
        std3D_SetVertexLayout(desc, (uint32_t)sizeof(StdVertexLit));
        desc.vertexLayout.attributes.push_back(render::VertexAttribute{ 4, 0, render::VertexFormat::Float3, (uint32_t)offsetof(StdVertexLit, vx) });
        desc.vertexLayout.attributes.push_back(render::VertexAttribute{ 5, 0, render::VertexFormat::Float3, (uint32_t)offsetof(StdVertexLit, nx) });
        desc.primitive = render::PrimitiveType::TriangleList;

        std3D_SetPipelineState(desc, bZWrite, bWaterAlpha, bHud);
        pipeline = std3D_pRenderDevice->createPipeline(desc);
        if ( pipeline.isNull() )
        {
            STDLOG_ERROR("Error creating the pipeline of lit triangles.\n");
        }

        return pipeline;
    }

    // The fragment uniforms of a draw: the shader fog std3D_SetRenderState keeps and the alpha
    // reference of the render state.
    Std3DBabylonFragmentUniforms std3D_GetFragmentUniforms()
    {
        Std3DBabylonFragmentUniforms uniforms;
        STD_ZEROMEM(&uniforms, sizeof(uniforms));
        std::memcpy(uniforms.aFogParams, std3D_aShaderFogParams, sizeof(uniforms.aFogParams));
        std::memcpy(uniforms.aFogColor, std3D_aShaderFogColor, sizeof(uniforms.aFogColor));
        uniforms.aAlphaTest[0] = (std3D_renderState & STD3D_RS_ALPHAREF_SET) != 0 ? (float)std3D_kAlphaRef / 255.0f : 0.0f;
        // Added: the surface tag of the normal target's alpha (M9.16). The tags are read-only state
        // carried from the face flags through rdCache; sky and water are mutually exclusive here.
        uniforms.aSurfaceTag[0] = (std3D_renderState & STD3D_RS_SURFACE_SKY) != 0
            ? stdRenderBabylon_kSurfaceTags.sky
            : ((std3D_renderState & STD3D_RS_SURFACE_WATER) != 0
                ? stdRenderBabylon_kSurfaceTags.water
                : stdRenderBabylon_kSurfaceTags.ordinary);
        // Added: M10.2. rdCache applies this factor to the base colour; the per-pixel light sum uses
        // it too, so a fading scene fades its dynamic lighting with it.
        const tStdFadeFactor* pFadeFactor = stdEffect_GetFadeFactor();
        uniforms.aSurfaceTag[1] = pFadeFactor->bEnabled ? pFadeFactor->factor : 1.0f;
        return uniforms;
    }

    // Streams one draw's vertices and indices into the frame and adds the draw to the pass being
    // collected, with the pipeline, texture, sampler and uniforms of the current render state.
    // Lit draws (triangles of StdVertexLit) take the lit pipeline and stream into the lit vertex
    // buffer; the others StdVertexTL into the vertex buffer. Logs and draws nothing outside a scene
    // (std3DX9.c's draws failed with D3DERR_INVALIDCALL there) and when a resource can't be made.
    void std3D_RecordDraw(Std3DBabylonPrimitive primitive, bool bLit, render::TextureHandle texture, const void* aVerts, size_t numVerts,
        const uint16_t* aIndices, size_t numIndices)
    {
        if ( !std3D_bInScene )
        {
            STDLOG_ERROR("Error drawing outside a scene.\n");
            return;
        }

        const bool bTriangles = primitive == Std3DBabylonPrimitive::Triangles;
        const bool bZWrite    = (std3D_renderState & STD3D_RS_ZWRITE_DISABLED) == 0;
        const bool bHud       = std3D_bHudLayer;
        // Added: a translucent water surface is drawn without Z write, so its tag is written through a
        // separate pipeline that keeps the normal's colour and replaces only alpha (M9.17).
        const bool bWaterTag  = (std3D_renderState & STD3D_RS_SURFACE_WATER) != 0;
        const render::PipelineHandle pipeline = bLit
            ? std3D_GetLitPipeline(bZWrite, bWaterTag, bHud)
            : std3D_GetPipeline(primitive, bZWrite, bWaterTag, bHud);
        const render::SamplerHandle sampler   = bTriangles ? std3D_GetSampler(std3D_renderState) : render::SamplerHandle{};
        if ( pipeline.isNull() || (bTriangles && sampler.isNull()) )
        {
            return;
        }

        render::BufferHandle* pVertexBuffer = bLit ? &std3D_litVertexBuffer : &std3D_vertexBuffer;
        uint32_t* pVertexBufferSize         = bLit ? &std3D_litVertexBufferSize : &std3D_vertexBufferSize;
        uint32_t* pVertexBufferOffset       = bLit ? &std3D_litVertexBufferOffset : &std3D_vertexBufferOffset;
        const uint32_t stride               = bLit ? (uint32_t)sizeof(StdVertexLit) : (uint32_t)sizeof(StdVertexTL);
        if ( bLit && pVertexBuffer->isNull() )
        {
            *pVertexBufferSize   = std3D_kInitialLitVertexBufferSize;
            *pVertexBufferOffset = 0;
            *pVertexBuffer       = std3D_pRenderDevice->createBuffer(render::BufferDesc{ *pVertexBufferSize, render::BufferUsage::Vertex });
            if ( pVertexBuffer->isNull() )
            {
                STDLOG_ERROR("Failed to create the lit vertex buffer.\n");
                *pVertexBufferSize = 0;
                return;
            }
        }

        const uint64_t numVertexBytes = (uint64_t)numVerts * stride;
        const uint64_t numIndexBytes  = (uint64_t)numIndices * sizeof(uint16_t);
        if ( !std3D_ReserveStream(pVertexBuffer, pVertexBufferSize, pVertexBufferOffset, numVertexBytes, render::BufferUsage::Vertex, "vertex")
            || (bTriangles && !std3D_ReserveStream(&std3D_indexBuffer, &std3D_indexBufferSize, &std3D_indexBufferOffset, numIndexBytes, render::BufferUsage::Index, "index")) )
        {
            return;
        }

        render::CommandList& frame = stdRenderBabylon_GetFrame();
        frame.updateBuffer(*pVertexBuffer, *pVertexBufferOffset, std::span<const std::byte>(static_cast<const std::byte*>(aVerts), (size_t)numVertexBytes));

        Std3DBabylonDraw draw;
        draw.primitive    = primitive;
        draw.pipeline     = pipeline;
        draw.vertexBuffer = *pVertexBuffer;
        draw.texture      = bTriangles ? texture : render::TextureHandle{};
        draw.sampler      = sampler;
        draw.uniforms     = std3D_GetFragmentUniforms();
        draw.bLit         = bLit;
        draw.firstVertex  = *pVertexBufferOffset / stride;
        draw.numVertices  = (uint32_t)numVerts;
        draw.firstIndex   = 0;
        draw.numIndices   = 0;
        *pVertexBufferOffset += (uint32_t)numVertexBytes;

        if ( bTriangles )
        {
            frame.updateBuffer(std3D_indexBuffer, std3D_indexBufferOffset, std::span<const std::byte>(reinterpret_cast<const std::byte*>(aIndices), (size_t)numIndexBytes));
            draw.indexBuffer = std3D_indexBuffer;
            draw.firstIndex  = std3D_indexBufferOffset / (uint32_t)sizeof(uint16_t);
            draw.numIndices  = (uint32_t)numIndices;
            std3D_indexBufferOffset += (uint32_t)numIndexBytes;
        }

        std3D_aSceneDraws.push_back(draw);
#ifdef J3D_TEST
        if ( (std3D_renderState & STD3D_RS_SHADOW_CASTER) != 0 )
        {
            std3D_casterVertexCount += (uint32_t)numVerts;
        }
#endif
        if ( bHud )
        {
            std3D_bHudPassBegun = true;
        }
        else
        {
            std3D_bScenePassBegun = true;
        }
    }

    // Arguments of the draw calls.
    bool std3D_CheckVertices(const void* aVerts, size_t numVerts)
    {
        if ( numVerts > std3D_g_maxVertices )
        {
            STDLOG_ERROR("Error %zu > %zu maxVertices.\n", numVerts, std3D_g_maxVertices);
            return false;
        }

        if ( numVerts && !aVerts )
        {
            STDLOG_ERROR("Error %zu vertices without a vertex array.\n", numVerts);
            return false;
        }

        return true;
    }

    // std3D_DrawRenderList and std3D_DrawRenderListLit: aVerts holds numVerts StdVertexTL, or
    // StdVertexLit when bLit.
    void std3D_DrawTriangleList(tSysTexture* pTex, Std3DRenderState rdflags, bool bLit, const void* aVerts, size_t numVerts,
        const uint16_t* aIndices, size_t numIndices)
    {
        if ( !std3D_CheckVertices(aVerts, numVerts) )
        {
            return;
        }

        std3D_SetRenderState(rdflags);
        std3D_pCurTex = pTex;

        if ( numIndices && !aIndices )
        {
            STDLOG_ERROR("Error %zu indices without an index array.\n", numIndices);
            return;
        }

        for ( size_t i = 0; i < numIndices; ++i )
        {
            if ( aIndices[i] >= numVerts )
            {
                STDLOG_ERROR("Error index %u at %zu is not below %zu vertices.\n", (unsigned)aIndices[i], i, numVerts);
                return;
            }
        }

        // numIndices / 3 triangles, as std3DX9.c drew; an untextured draw samples the white texture.
        const size_t numTriangleIndices = numIndices / 3u * 3u;
        if ( !numVerts || !numTriangleIndices )
        {
            return;
        }

        std3D_RecordDraw(Std3DBabylonPrimitive::Triangles, bLit, pTex ? pTex->texture : std3D_whiteTexture, aVerts, numVerts, aIndices,
            numTriangleIndices);
    }
}

namespace std3DBabylonInternal
{
    // The resources of the draws made at std3D_Open: the white texture std3DX9.c's
    // CreateWhiteTexture made for untextured draws in shader mode (one opaque white texel), and the
    // stream buffers.
    bool std3D_InitDrawResources()
    {
        render::TextureDesc whiteDesc;
        whiteDesc.width     = 1;
        whiteDesc.height    = 1;
        whiteDesc.mipLevels = 1;
        whiteDesc.format    = render::TextureFormat::Bgra8;
        whiteDesc.usage     = render::TextureUsage::Sampled;

        const std::byte aWhite[4] = { std::byte{ 0xFF }, std::byte{ 0xFF }, std::byte{ 0xFF }, std::byte{ 0xFF } };
        std3D_whiteTexture = std3D_pRenderDevice->createTexture(whiteDesc);
        if ( std3D_whiteTexture.isNull() || !std3D_pRenderDevice->uploadTexture(std3D_whiteTexture, 0, std::span<const std::byte>(aWhite)) )
        {
            STDLOG_ERROR("Failed to create white texture for shader\n");
            return false;
        }

        std3D_vertexBuffer       = std3D_pRenderDevice->createBuffer(render::BufferDesc{ std3D_kInitialVertexBufferSize, render::BufferUsage::Vertex });
        std3D_indexBuffer        = std3D_pRenderDevice->createBuffer(render::BufferDesc{ std3D_kInitialIndexBufferSize, render::BufferUsage::Index });
        std3D_vertexBufferSize   = std3D_kInitialVertexBufferSize;
        std3D_indexBufferSize    = std3D_kInitialIndexBufferSize;
        std3D_vertexBufferOffset = 0;
        std3D_indexBufferOffset  = 0;
        if ( std3D_vertexBuffer.isNull() || std3D_indexBuffer.isNull() )
        {
            STDLOG_ERROR("Failed to create the vertex and index buffers.\n");
            return false;
        }

        return true;
    }

    void std3D_ReleaseDrawResources()
    {
        render::Device* pDevice = stdRenderBabylon_GetDevice();
        if ( pDevice )
        {
            for ( auto& aPipelines : std3D_aPipelines )
            {
                for ( auto& aLayerPipelines : aPipelines )
                {
                    for ( render::PipelineHandle& pipeline : aLayerPipelines )
                    {
                        pDevice->destroyPipeline(pipeline);
                    }
                }
            }

            for ( auto& aLayerPipelines : std3D_aLitPipelines )
            {
                for ( render::PipelineHandle& pipeline : aLayerPipelines )
                {
                    pDevice->destroyPipeline(pipeline);
                }
            }

            for ( auto& aLayerPipelines : std3D_aWaterPipelines )
            {
                for ( render::PipelineHandle& pipeline : aLayerPipelines )
                {
                    pDevice->destroyPipeline(pipeline);
                }
            }

            for ( render::PipelineHandle& pipeline : std3D_aLitWaterPipelines )
            {
                pDevice->destroyPipeline(pipeline);
            }

            for ( render::SamplerHandle& sampler : std3D_aSamplers )
            {
                pDevice->destroySampler(sampler);
            }

            stdRenderBabylon_DestroyTexture(std3D_whiteTexture);
            stdRenderBabylon_DestroyBuffer(std3D_vertexBuffer);
            stdRenderBabylon_DestroyBuffer(std3D_indexBuffer);
            stdRenderBabylon_DestroyBuffer(std3D_litVertexBuffer);
            stdRenderBabylon_DestroyBuffer(std3D_lightBuffer);
        }

        for ( auto& aPipelines : std3D_aPipelines )
        {
            for ( auto& aLayerPipelines : aPipelines )
            {
                for ( render::PipelineHandle& pipeline : aLayerPipelines )
                {
                    pipeline = {};
                }
            }
        }

        for ( auto& aLayerPipelines : std3D_aLitPipelines )
        {
            for ( render::PipelineHandle& pipeline : aLayerPipelines )
            {
                pipeline = {};
            }
        }

        for ( auto& aLayerPipelines : std3D_aWaterPipelines )
        {
            for ( render::PipelineHandle& pipeline : aLayerPipelines )
            {
                pipeline = {};
            }
        }

        for ( render::PipelineHandle& pipeline : std3D_aLitWaterPipelines )
        {
            pipeline = {};
        }

        for ( render::SamplerHandle& sampler : std3D_aSamplers )
        {
            sampler = {};
        }

        std3D_whiteTexture       = {};
        std3D_vertexBuffer       = {};
        std3D_indexBuffer        = {};
        std3D_vertexBufferSize   = 0;
        std3D_indexBufferSize    = 0;
        std3D_vertexBufferOffset = 0;
        std3D_indexBufferOffset  = 0;
        std3D_litVertexBuffer       = {};
        std3D_litVertexBufferSize   = 0;
        std3D_litVertexBufferOffset = 0;
        std3D_lightBuffer           = {};
        std3D_numLights             = 0;
        std3D_bLightsDirty          = false;
        std3D_aSceneDraws.clear();
        STD_ZEROMEM(std3D_aShaderFogParams, sizeof(std3D_aShaderFogParams));
        STD_ZEROMEM(std3D_aShaderFogColor, sizeof(std3D_aShaderFogColor));
    }
}

void J3DAPI std3D_DrawRenderList(tSysTexture* pTex, Std3DRenderState rdflags, StdVertexTL* aVerts, size_t numVerts, uint16_t* aIndices, size_t numIndices)
{
    std3D_DrawTriangleList(pTex, rdflags, false, aVerts, numVerts, aIndices, numIndices);
}

void J3DAPI std3D_DrawRenderListLit(tSysTexture* pTex, Std3DRenderState rdflags, StdVertexLit* aVerts, size_t numVerts, uint16_t* aIndices, size_t numIndices)
{
    std3D_DrawTriangleList(pTex, rdflags, true, aVerts, numVerts, aIndices, numIndices);
}

void std3D_SetWireframeRenderState(void)
{
    Std3DRenderState rdstate = (Std3DRenderState)(std3D_renderState & ~(STD3D_RS_FOG_ENABLED | STD3D_RS_UNKNOWN_400 | STD3D_RS_UNKNOWN_200 | STD3D_RS_SURFACE_SKY | STD3D_RS_SURFACE_WATER | STD3D_RS_SHADOW_CASTER));
    std3D_SetRenderState(rdstate);
    std3D_pCurTex = nullptr;
}

void J3DAPI std3D_DrawLineStrip(StdVertexTL* aVerts, size_t numVerts)
{
    if ( !std3D_CheckVertices(aVerts, numVerts) )
    {
        return;
    }

    // numVerts - 1 lines in the current render state (std3D_SetWireframeRenderState sets it), with
    // std3DX9.c's wireframe shader.
    if ( numVerts < 2u )
    {
        return;
    }

    std3D_RecordDraw(Std3DBabylonPrimitive::Lines, false, render::TextureHandle{}, aVerts, numVerts, nullptr, 0);
}

void J3DAPI std3D_DrawPointList(StdVertexTL* aVerts, size_t numVerts)
{
    if ( !std3D_CheckVertices(aVerts, numVerts) )
    {
        return;
    }

    // Points of one pixel, as Direct3D 9's default point size drew them, with std3DX9.c's wireframe
    // shader.
    if ( !numVerts )
    {
        return;
    }

    std3D_RecordDraw(Std3DBabylonPrimitive::Points, false, render::TextureHandle{}, aVerts, numVerts, nullptr, 0);
}

void J3DAPI std3D_SetRenderState(Std3DRenderState rdflags)
{
    // Added: the shadow-caster bit is read-only state for the draw path; it must not make a state
    // change take another route, so the comparison ignores it while the flag is still kept.
    const Std3DRenderState compareMask = (Std3DRenderState)~STD3D_RS_SHADOW_CASTER;
    if ( (std3D_renderState & compareMask) == (rdflags & compareMask) )
    {
        std3D_renderState = rdflags;
        return;
    }

    // std3DX9.c translated each changed bit into Direct3D render and sampler state here. The draws
    // read Z write, clamping, filtering and the alpha reference from the bits themselves
    // (std3D_GetPipeline, std3D_GetSampler, std3D_GetFragmentUniforms), which ends in the same
    // state. Fog is the exception: the shader fog took the fog values of the moment the fog bit
    // changed (stdShader_SetFog, stdShader_DisableFog), and kept them until it changed again.
    if ( ((std3D_renderState ^ rdflags) & STD3D_RS_FOG_ENABLED) != 0 )
    {
        if ( (rdflags & STD3D_RS_FOG_ENABLED) != 0 && std3D_bRenderFog )
        {
            std3D_aShaderFogParams[0] = std3D_fogStartDepth;
            std3D_aShaderFogParams[1] = std3D_fogEndDepth;
            std3D_aShaderFogParams[2] = std3D_fogDepthFactor;
            std3D_aShaderFogParams[3] = 1.0f;
            std::memcpy(std3D_aShaderFogColor, std3D_aFogColor, sizeof(std3D_aShaderFogColor));
        }
        else
        {
            STD_ZEROMEM(std3D_aShaderFogParams, sizeof(std3D_aShaderFogParams)); // the color stays
        }
    }

    std3D_renderState = rdflags;
}

#ifdef J3D_TEST
uint32_t std3DBabylon_GetCasterVertexCountForTest(void)
{
    return std3D_casterVertexCount;
}
#endif

