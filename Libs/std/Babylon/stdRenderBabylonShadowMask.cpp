// The caster-mask stage of the Babylon render adapter's character shadows (M11.2g): the frame's
// caster draws re-drawn against the scene depth into a scene-sized mask, so a character casts its
// shadow but does not receive it on its own visible surface.
// Part of the Babylon column's render frame owner; see stdRenderBabylon.cpp for the overview. Adapter code: no Fixed/Added markers inside.
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Handle.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Texture.h>
#include <babylon/shaders/stdBabylonShaders.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <std/General/std.h>

#include "std3DBabylonInternal.h"
#include "stdRenderBabylon.h"
#include "stdRenderBabylonInternal.h"

namespace render = babylon::render;
using namespace stdRenderBabylonInternal;

namespace
{
    // Added: the smallest colour format the device can render and sample, for the single-value
    // caster mask. The render module has no R8, so this is the 16-bit B5G6R5 where the device has
    // it and Rgba8 or the scene format otherwise.
    render::TextureFormat stdRenderBabylon_ChooseShadowMaskFormat(render::Device* pDevice)
    {
        const render::TextureUsage usage = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;
        render::TextureFormat smallest = render::TextureFormat::Rgba8;
        uint32_t smallestBytes = 0xFFFFFFFFu;
        for ( const render::TextureFormat candidate : render::kAllTextureFormats )
        {
            if ( render::isDepthFormat(candidate) || !pDevice->supportsFormat(candidate, usage) )
            {
                continue;
            }

            const uint32_t texelBytes = render::bytesPerTexel(candidate);
            if ( texelBytes < smallestBytes )
            {
                smallest      = candidate;
                smallestBytes = texelBytes;
            }
        }
        return smallest;
    }

    // Added: the caster mask's scene-sized target, made when its size changes. The scene-sized
    // colour target it is paired with is made first, so freeing either never leaves the other
    // behind; false, logging why, when it can't be created.
    bool stdRenderBabylon_EnsureShadowMaskTarget(uint32_t width, uint32_t height)
    {
        if ( width == 0 || height == 0 )
        {
            return false;
        }

        if ( !stdRenderBabylon_state.shadowMaskTarget.isNull()
            && stdRenderBabylon_state.shadowMaskWidth == width
            && stdRenderBabylon_state.shadowMaskHeight == height )
        {
            return true;
        }

        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.shadowMaskTarget);
        stdRenderBabylon_state.shadowMaskTarget = {};
        stdRenderBabylon_state.shadowMaskWidth  = 0;
        stdRenderBabylon_state.shadowMaskHeight = 0;
        stdRenderBabylon_state.bShadowMaskRan   = false;

        stdRenderBabylon_state.shadowMaskFormat =
            stdRenderBabylon_ChooseShadowMaskFormat(stdRenderBabylon_state.pDevice);

        render::TextureDesc desc;
        desc.width     = width;
        desc.height    = height;
        desc.mipLevels = 1;
        desc.format    = stdRenderBabylon_state.shadowMaskFormat;
        desc.usage     = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;

        const render::TextureHandle target = stdRenderBabylon_state.pDevice->createTexture(desc);
        if ( target.isNull() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the %ux%u caster-mask target.\n",
                width, height);
            return false;
        }

        stdRenderBabylon_state.shadowMaskTarget = target;
        stdRenderBabylon_state.shadowMaskWidth  = width;
        stdRenderBabylon_state.shadowMaskHeight = height;
        return true;
    }

    // Added: the caster mask's two pipelines, triangles of StdVertexTL or StdVertexLit with the
    // scene's vertex shaders. The depth test keeps only a caster's visible surface and never writes
    // depth; the fragment shader writes 1 wherever it passes. Made once, freed with the other
    // shadow resources.
    bool stdRenderBabylon_EnsureShadowMaskPipelines()
    {
        if ( !stdRenderBabylon_state.shadowMaskPipeline.isNull()
            && !stdRenderBabylon_state.shadowMaskLitPipeline.isNull() )
        {
            return true;
        }

        render::PipelineDesc desc;
        desc.fragmentShader = stdBabylon::shaders::std3dShadowMaskFrag();
        desc.primitive      = render::PrimitiveType::TriangleList;
        desc.colorTargets   = { render::ColorTargetDesc{ stdRenderBabylon_state.shadowMaskFormat, {} } };
        desc.depthFormat    = stdRenderBabylon_state.depthFormat;
        desc.depth          = render::DepthState{ true, false, render::CompareOp::LessOrEqual };
        desc.cull           = render::CullMode::None;

        if ( stdRenderBabylon_state.shadowMaskPipeline.isNull() )
        {
            desc.vertexShader = stdBabylon::shaders::std3dVert();
            desc.vertexLayout.bufferStrides = { (uint32_t)sizeof(StdVertexTL) };
            desc.vertexLayout.attributes    = {
                render::VertexAttribute{ 0, 0, render::VertexFormat::Float4, (uint32_t)offsetof(StdVertexTL, sx) },
                render::VertexAttribute{ 1, 0, render::VertexFormat::UByte4Norm, (uint32_t)offsetof(StdVertexTL, color) },
                render::VertexAttribute{ 2, 0, render::VertexFormat::UByte4Norm, (uint32_t)offsetof(StdVertexTL, specular) },
                render::VertexAttribute{ 3, 0, render::VertexFormat::Float2, (uint32_t)offsetof(StdVertexTL, tu) },
            };
            stdRenderBabylon_state.shadowMaskPipeline =
                stdRenderBabylon_state.pDevice->createPipeline(desc);
        }

        if ( stdRenderBabylon_state.shadowMaskLitPipeline.isNull() )
        {
            desc.vertexShader = stdBabylon::shaders::std3dLitVert();
            desc.vertexLayout.bufferStrides = { (uint32_t)sizeof(StdVertexLit) };
            desc.vertexLayout.attributes    = {
                render::VertexAttribute{ 0, 0, render::VertexFormat::Float4, (uint32_t)offsetof(StdVertexTL, sx) },
                render::VertexAttribute{ 1, 0, render::VertexFormat::UByte4Norm, (uint32_t)offsetof(StdVertexTL, color) },
                render::VertexAttribute{ 2, 0, render::VertexFormat::UByte4Norm, (uint32_t)offsetof(StdVertexTL, specular) },
                render::VertexAttribute{ 3, 0, render::VertexFormat::Float2, (uint32_t)offsetof(StdVertexTL, tu) },
                render::VertexAttribute{ 4, 0, render::VertexFormat::Float3, (uint32_t)offsetof(StdVertexLit, vx) },
                render::VertexAttribute{ 5, 0, render::VertexFormat::Float3, (uint32_t)offsetof(StdVertexLit, nx) },
            };
            stdRenderBabylon_state.shadowMaskLitPipeline =
                stdRenderBabylon_state.pDevice->createPipeline(desc);
        }

        if ( stdRenderBabylon_state.shadowMaskPipeline.isNull()
            || stdRenderBabylon_state.shadowMaskLitPipeline.isNull() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the caster-mask pipelines.\n");
            return false;
        }

        return true;
    }
}

namespace stdRenderBabylonInternal
{
    // Added: records the caster mask after the scene and before the character-shadow receive pass.
    // It re-draws exactly this frame's caster draws with the scene's own vertex and index buffers,
    // ranges and vertex shaders, against the scene depth target read-only, into a scene-sized target
    // cleared to 0. False, logging nothing, when the frame's map was not recorded, no caster draw was
    // collected, or a target, pipeline or pass can't be made; bShadowMaskRan stays false then and the
    // receive pass gets no mask.
    bool stdRenderBabylon_RecordShadowMask(render::CommandList& list)
    {
        if ( !stdRenderBabylon_state.bShadowMapRecorded )
        {
            return false;
        }

        const std::vector<std3DBabylonInternal::Std3DBabylonDraw>& aCasterDraws =
            std3DBabylonInternal::std3DBabylon_GetShadowCasterDraws();
        if ( aCasterDraws.empty()
            || stdRenderBabylon_state.depthTarget.isNull()
            || !stdRenderBabylon_state.pDevice )
        {
            return false;
        }

        // Added: the receive pass samples the same scene depth target the scene pass wrote, so the
        // mask is drawn against it directly, with no resolve or copy in between.
        const uint32_t width  = stdRenderBabylon_state.width;
        const uint32_t height = stdRenderBabylon_state.height;
        if ( !stdRenderBabylon_EnsureShadowTargets(width, height)
            || !stdRenderBabylon_EnsureShadowMaskTarget(width, height)
            || !stdRenderBabylon_EnsureShadowMaskPipelines() )
        {
            return false;
        }

        render::PassDesc maskPass;
        maskPass.colorTargets.push_back(render::ColorAttachment{
            stdRenderBabylon_state.shadowMaskTarget, 0, render::LoadOp::Clear,
            render::StoreOp::Store, render::ClearColor{ 0.0f, 0.0f, 0.0f, 0.0f } });
        maskPass.depthTarget = render::DepthAttachment{
            stdRenderBabylon_state.depthTarget, render::LoadOp::Load,
            render::StoreOp::Store, 1.0f, 0 };
        if ( !list.beginPass(maskPass) )
        {
            return false;
        }

        // std3DX9.c's g_viewport, as the scene pass pushes it: the display size the screen vertices
        // are in, not the scaled target.
        const float aViewport[4] = { 0.0f, 0.0f,
            (float)stdRenderBabylon_GetBackBufferWidth(),
            (float)stdRenderBabylon_GetBackBufferHeight() };
        list.pushUniforms(render::ShaderStage::Vertex, 0,
            std::as_bytes(std::span<const float>(aViewport)));

        for ( const std3DBabylonInternal::Std3DBabylonDraw& casterDraw : aCasterDraws )
        {
            if ( casterDraw.primitive != std3DBabylonInternal::Std3DBabylonPrimitive::Triangles
                || casterDraw.numIndices == 0 )
            {
                continue;
            }

            list.setPipeline(casterDraw.bLit
                ? stdRenderBabylon_state.shadowMaskLitPipeline
                : stdRenderBabylon_state.shadowMaskPipeline);
            list.bindVertexBuffer(0, casterDraw.vertexBuffer, 0);
            list.bindIndexBuffer(casterDraw.indexBuffer, render::IndexType::Uint16, 0);
            list.drawIndexed(casterDraw.numIndices, casterDraw.firstIndex,
                (int32_t)casterDraw.firstVertex);
        }

        if ( !list.endPass() )
        {
            return false;
        }

        stdRenderBabylon_state.bShadowMaskRan = true;
        return true;
    }
}
