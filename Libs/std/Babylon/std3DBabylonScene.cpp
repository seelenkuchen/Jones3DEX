// Added: the Babylon column's std3D backend on the Babylon render device of the adapter context
// (ADR-0025), replacing std/Null/std3DNull.c in that column. It defines the std3DBackend_* functions
// through stdRenderBackend.h, so the capture forwarders of std3DCapture.c stay in front (ADR-0024).
// The behavior reference is the removed DirectX 9 backend (std/Win95/DX9/std3DX9.c before 82b8f607):
// the device list, texture formats, system textures, the texture cache with its frame counts, the
// fog and projection state and the scene clears follow it, with Babylon textures where it held
// Direct3D ones. Frames go through stdRenderBabylon.cpp, which owns the frame's command list and
// the back buffer; the display adapter (stdDisplayBabylon.cpp) opens it with the display, sizes the
// back buffer with the video mode and submits the frame at stdDisplay_Update, as std3DX9.c drew into
// the display's device and stdDisplayDX9.c presented it.
//
// Draws follow std3DX9.c's shader path (std3DX9.c with stdShaderDX9.c and Shaders/*.hlsl), which
// ran on every Direct3D 9 device with shader model 3: the shaders are ports of its default and
// wireframe shaders (Shaders/std3d*.hlsl), and the Direct3D state its SetRenderState and
// InitRenderState set becomes pipelines (blending, depth, primitive), samplers (filter, mip filter,
// anisotropy, clamp) and uniforms (viewport, fog, alpha reference), worked out from the render
// state bits at each draw (std3D_GetPipeline, std3D_GetSampler, std3D_GetFragmentUniforms).
//
// std3D_DrawRenderListLit draws like std3D_DrawRenderList, with the lit shaders (Shaders/std3d_lit*.hlsl):
// they compute the color exactly as the default shaders do and write each fragment's interpolated
// view-space normal into the normal target, where the default shaders write a constant. Lit vertices
// (StdVertexLit, 56 bytes) stream into a vertex buffer of their own, made on the first lit draw, so the
// classic draws' device calls stay what they were.
//
// std3D_SetLights keeps the frame's light list (StdLight, 48 bytes each) for the lit draws. A pass
// with lit draws, or the first pass after the list changed, copies it into the light buffer, a
// storage buffer made on first use (std3D_kLightBufferSize: a 16-byte header with the count, then
// up to STD3D_MAXLIGHTS lights as they are), and the pass binds it to fragment storage slot 0 of
// the lit draws. std3d_lit.frag.hlsl reads it for the vertices per-pixel lighting flags (M10.2);
// a Classic vertex adds nothing, so its pixel is unchanged. Without lit draws and std3D_SetLights
// (the litFaces option off) there is no light buffer and the classic draws' device calls stay what
// they were.
//
// A scene is one or more passes on the back buffer. CommandList::updateBuffer can't be recorded
// inside a pass, so a draw streams its vertices and indices into the frame's vertex and index
// buffer right away, at growing offsets like std3DX9.c's buffer path, and the pass that draws them
// is recorded when it ends: at std3D_EndScene and at a std3D_ClearZBuffer inside the scene.
//
// Adapter code: no Fixed/Added markers inside; divergences from std3DX9.c say so where they are.
// Scene and HUD passes of the Babylon std3D backend.
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
    // The frame: a scene is begun between StartScene and EndScene. The clears std3DX9.c made with
    // IDirect3DDevice9_Clear become the load operations of the next pass.
    bool std3D_bInScene           = false;
    bool std3D_bClearColorPending = false;
    bool std3D_bClearDepthPending = false;
    render::TextureHandle std3D_sceneColorTarget; // the back buffer the last scene drew to
    // The pass being collected: its load operations and its draws.
    render::LoadOp std3D_sceneColorLoad = render::LoadOp::Load;
    render::LoadOp std3D_sceneDepthLoad = render::LoadOp::Load;
    std::vector<Std3DBabylonDraw> std3D_aSceneDraws;

    // Added: the gameplay HUD section draws into its own display-sized layer, not the scene. It has
    // its own pass load operations because it is cleared transparent each frame.
    bool std3D_bHudLayer              = false; // draws go to the HUD layer until EndHudLayer
    bool std3D_bScenePassBegun        = false; // a scene pass is collected and not recorded yet
    bool std3D_bHudPassBegun          = false;
    render::LoadOp std3D_hudColorLoad = render::LoadOp::Clear;
    render::LoadOp std3D_hudDepthLoad = render::LoadOp::Clear;
}

namespace
{
    // Starts collecting the next pass of the scene. std3DX9.c drew into the device's back buffer
    // directly; here the clears it made with IDirect3DDevice9_Clear are the load operations.
    void std3D_BeginScenePass(bool bClearColor, bool bClearDepth)
    {
        std3D_sceneColorLoad = bClearColor ? render::LoadOp::Clear : render::LoadOp::Load;
        std3D_sceneDepthLoad = bClearDepth ? render::LoadOp::Clear : render::LoadOp::Load;
        std3D_aSceneDraws.clear();
        std3D_bScenePassBegun = true;
    }

    // Records the collected pass into the frame: the back buffer with the pass's load operations,
    // then its draws, each with only the bindings and uniforms that changed since the draw before.
    // The normal target is the second color target; it belongs with the depth, so it is cleared to
    // 0 (nothing drawn) when depth is. bHud records the display-sized HUD layer instead, with its own
    // targets and load operations. Returns false when the command list refuses the pass. The pass is
    // done either way.
    bool std3D_RecordCollectedPass(bool bHud)
    {
        render::CommandList& frame = stdRenderBabylon_GetFrame();

        const render::TextureHandle colorTarget = bHud
            ? stdRenderBabylon_GetHudColorTarget() : stdRenderBabylon_GetColorTarget();
        const render::TextureHandle normalTarget = bHud
            ? stdRenderBabylon_GetHudNormalTarget() : stdRenderBabylon_GetNormalTarget();
        const render::TextureHandle depthTarget = bHud
            ? stdRenderBabylon_GetHudDepthTarget() : stdRenderBabylon_GetDepthTarget();
        const render::LoadOp colorLoad = bHud ? std3D_hudColorLoad : std3D_sceneColorLoad;
        const render::LoadOp depthLoad = bHud ? std3D_hudDepthLoad : std3D_sceneDepthLoad;

        render::PassDesc pass;
        pass.colorTargets.push_back(render::ColorAttachment{ colorTarget, 0,
            colorLoad, render::StoreOp::Store, render::ClearColor{ 0.0f, 0.0f, 0.0f, 0.0f } });
        pass.colorTargets.push_back(render::ColorAttachment{ normalTarget, 0,
            depthLoad, render::StoreOp::Store, render::ClearColor{ 0.0f, 0.0f, 0.0f, 0.0f } });
        pass.depthTarget = render::DepthAttachment{ depthTarget,
            depthLoad, render::StoreOp::Store, 1.0f, 0 };

        const bool bLitDraws = std::any_of(std3D_aSceneDraws.begin(), std3D_aSceneDraws.end(), [](const Std3DBabylonDraw& draw) { return draw.bLit; });
        const bool bLightBuffer = (bLitDraws || std3D_bLightsDirty) && std3D_UpdateLightBuffer(frame);

        bool bRecorded = frame.beginPass(pass);
        if ( bRecorded && !std3D_aSceneDraws.empty() )
        {
            // std3DX9.c's g_viewport, set at each BeginScene: the rectangle the screen vertices are in.
            // It stays the display/video-mode size at every render scale (GetBackBufferWidth/Height),
            // so the same screen position maps to the whole scaled target and a larger target
            // supersamples the scene; the GPU's viewport is the target itself.
            const float aViewport[4] = { 0.0f, 0.0f, (float)stdRenderBabylon_GetBackBufferWidth(), (float)stdRenderBabylon_GetBackBufferHeight() };
            frame.pushUniforms(render::ShaderStage::Vertex, 0, std::as_bytes(std::span<const float>(aViewport)));

            const Std3DBabylonDraw* pPrev = nullptr;
            render::BufferHandle boundIndexBuffer;
            render::TextureHandle boundTexture;
            render::SamplerHandle boundSampler;
            bool bLightsBound = false;
            for ( const Std3DBabylonDraw& draw : std3D_aSceneDraws )
            {
                if ( draw.bLit && bLightBuffer && !bLightsBound )
                {
                    frame.bindStorageBuffer(render::ShaderStage::Fragment, 0, std3D_lightBuffer);
                    bLightsBound = true;
                }

                if ( !pPrev || pPrev->pipeline != draw.pipeline )
                {
                    frame.setPipeline(draw.pipeline);
                }

                if ( !pPrev || pPrev->vertexBuffer != draw.vertexBuffer )
                {
                    frame.bindVertexBuffer(0, draw.vertexBuffer, 0);
                }

                if ( !pPrev || std::memcmp(&pPrev->uniforms, &draw.uniforms, sizeof(draw.uniforms)) != 0 )
                {
                    frame.pushUniforms(render::ShaderStage::Fragment, 0, std::as_bytes(std::span<const Std3DBabylonFragmentUniforms, 1>(&draw.uniforms, 1)));
                }

                if ( draw.primitive == Std3DBabylonPrimitive::Triangles )
                {
                    if ( boundIndexBuffer != draw.indexBuffer )
                    {
                        frame.bindIndexBuffer(draw.indexBuffer, render::IndexType::Uint16, 0);
                        boundIndexBuffer = draw.indexBuffer;
                    }

                    if ( boundTexture != draw.texture || boundSampler != draw.sampler )
                    {
                        frame.bindTexture(render::ShaderStage::Fragment, 0, draw.texture, draw.sampler);
                        boundTexture = draw.texture;
                        boundSampler = draw.sampler;
                    }

                    frame.drawIndexed(draw.numIndices, draw.firstIndex, (int32_t)draw.firstVertex);
                }
                else
                {
                    frame.draw(draw.numVertices, draw.firstVertex);
                }

                pPrev = &draw;
            }
        }

        std3D_aSceneDraws.clear();
        if ( bHud )
        {
            std3D_bHudPassBegun = false;
        }
        else
        {
            // A later scene pass this frame loads what this one drew instead of clearing again.
            std3D_bScenePassBegun = false;
            std3D_sceneColorLoad  = render::LoadOp::Load;
            std3D_sceneDepthLoad  = render::LoadOp::Load;
        }

        return bRecorded && frame.endPass();
    }

    bool std3D_EndScenePass()
    {
        return std3D_RecordCollectedPass(false);
    }

    // Added: records the HUD draws collected since BeginHudLayer into the HUD layer, clearing it
    // transparent and its depth the first time.
    bool std3D_EndHudPass()
    {
        return std3D_RecordCollectedPass(true);
    }

}

int std3D_StartScene(void)
{
    ++std3D_frameCount;
    std3D_pCurTex = nullptr;

    // std3DX9.c's BeginScene error paths: no device, or a scene already begun (D3DERR_INVALIDCALL).
    if ( !std3D_bOpen )
    {
        STDLOG_ERROR("Error beginning scene: std3D is not open.\n");
        return 1;
    }

    if ( std3D_bInScene )
    {
        STDLOG_ERROR("Error beginning scene: the scene is already begun.\n");
        return 1;
    }

    // The back buffer follows the display's size; new targets start cleared like the DX9 viewport,
    // also when the display made them (a new video mode).
    uint32_t width  = 0;
    uint32_t height = 0;
    bool bRecreated = false;
    stdDisplay_GetBackBufferSize(&width, &height);
    if ( !stdRenderBabylon_SetBackBufferSize(width, height, &bRecreated) )
    {
        STDLOG_ERROR("Error beginning scene: no %ux%u back buffer.\n", width, height);
        return 1;
    }

    if ( stdRenderBabylon_GetColorTarget() != std3D_sceneColorTarget )
    {
        bRecreated             = true;
        std3D_sceneColorTarget = stdRenderBabylon_GetColorTarget();
    }

    if ( bRecreated )
    {
        std3D_bClearColorPending = true;
        std3D_bClearDepthPending = true;
    }

    // A new frame streams its vertices and indices from the start of the buffers again.
    if ( !stdRenderBabylon_HasFrame() )
    {
        std3D_vertexBufferOffset    = 0;
        std3D_indexBufferOffset     = 0;
        std3D_litVertexBufferOffset = 0;
    }

    if ( stdRenderBabylon_GetFrame().inPass() )
    {
        STDLOG_ERROR("Error beginning scene: the frame has a pass open.\n");
        return 1;
    }

    std3D_BeginScenePass(std3D_bClearColorPending, std3D_bClearDepthPending);
    std3D_bClearColorPending = false;
    std3D_bClearDepthPending = false;
    std3D_bInScene           = true;
    std3D_bHudLayer          = false;
    std3D_bHudPassBegun      = false;
    return 0;
}

void std3D_EndScene(void)
{
    std3D_pCurTex = nullptr;
    if ( !std3D_bInScene )
    {
        STDLOG_ERROR("Error ending scene: no scene begun.\n");
        return;
    }

    std3D_bInScene = false;

    // Added: a HUD layer left open by a missing EndHudLayer ends here, before the frame is presented.
    bool bRecorded = true;
    if ( std3D_bHudLayer )
    {
        if ( !std3D_aSceneDraws.empty() )
        {
            bRecorded = std3D_EndHudPass();
        }
        std3D_bHudLayer = false;
    }
    else if ( std3D_bScenePassBegun )
    {
        bRecorded = std3D_EndScenePass();
    }

    if ( !bRecorded )
    {
        STDLOG_ERROR("Error ending scene.\n");
    }

    // The frame stays open: stdDisplay_Update submits and presents it, like stdDisplayDX9.c's Present.
}

// Added: switches the draws to the HUD layer. The scene draws collected so far are recorded first,
// so they keep the scene targets and their post effects.
void std3D_BeginHudLayer(void)
{
    if ( !std3D_bInScene )
    {
        STDLOG_ERROR("Error beginning the HUD layer: no scene begun.\n");
        return;
    }

    if ( std3D_bHudLayer )
    {
        STDLOG_ERROR("Error beginning the HUD layer: it is already begun.\n");
        return;
    }

    if ( std3D_bScenePassBegun || !std3D_aSceneDraws.empty() )
    {
        if ( !std3D_EndScenePass() )
        {
            STDLOG_ERROR("Error beginning the HUD layer: the scene pass failed.\n");
        }
    }

    std3D_bHudLayer      = true;
    std3D_bHudPassBegun  = true;
    std3D_hudColorLoad   = render::LoadOp::Clear;
    std3D_hudDepthLoad   = render::LoadOp::Clear;
    std3D_aSceneDraws.clear();
}

// Added: records the HUD layer draws. Without any, no pass is recorded and the previous layer is not
// composited (stdRenderBabylon only draws a layer this frame recorded).
void std3D_EndHudLayer(void)
{
    if ( !std3D_bHudLayer )
    {
        return;
    }

    if ( !std3D_aSceneDraws.empty() )
    {
        if ( !std3D_EndHudPass() )
        {
            STDLOG_ERROR("Error ending the HUD layer.\n");
        }
    }

    std3D_bHudLayer     = false;
    std3D_bHudPassBegun = false;
}

bool std3D_IsInScene(void)
{
    return std3D_bInScene;
}

void std3D_ClearZBuffer(void)
{
    if ( !std3D_bOpen )
    {
        return;
    }

    // std3DX9.c cleared depth (and stencil) of the viewport at once. A pass can only clear when it
    // begins, so inside a scene the pass is ended and another one begun with depth cleared; outside
    // one the next scene's pass clears it.
    if ( !std3D_bInScene )
    {
        std3D_bClearDepthPending = true;
        return;
    }

    // Added: inside the HUD layer the collected HUD pass is recorded and a new one begins with its
    // depth cleared, keeping the color already drawn.
    if ( std3D_bHudLayer )
    {
        bool bRecorded = true;
        if ( !std3D_aSceneDraws.empty() )
        {
            bRecorded = std3D_EndHudPass();
        }
        if ( !bRecorded )
        {
            STDLOG_ERROR("Error when clearing Z.\n");
        }

        // The first pass of the section still clears the layer's color; later ones keep it.
        std3D_hudColorLoad = std3D_bHudPassBegun ? render::LoadOp::Clear : render::LoadOp::Load;
        std3D_hudDepthLoad = render::LoadOp::Clear;
        std3D_bHudPassBegun = true;
        return;
    }

    if ( !std3D_EndScenePass() )
    {
        STDLOG_ERROR("Error when clearing Z.\n");
    }

    std3D_BeginScenePass(false, true);
}

