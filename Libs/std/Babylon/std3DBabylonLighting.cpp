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
// Lights and fog of the Babylon std3D backend.
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
    float std3D_fogStartDepth              = 0.0f;
    float std3D_fogEndDepth                = 0.0f;
    float std3D_fogDepthFactor             = 0.0f;
    float std3D_aFogColor[4]               = { 0.0f, 0.0f, 0.0f, 0.0f };

    // The light list of std3D_SetLights and the storage buffer the lit draws see it in; bLightsDirty
    // when the list changed since the buffer last got it.
    StdLight std3D_aLights[STD3D_MAXLIGHTS];
    size_t std3D_numLights = 0;
    bool std3D_bLightsDirty = false;
    render::BufferHandle std3D_lightBuffer; // made on first use
}

namespace
{
    // The light buffer: a header of four u32 (the count, then three reserved zeros) and the lights.
    // As float4 elements of the shader's StructuredBuffer: element 0 the header, light i at 1 + 3i.
    constexpr uint32_t std3D_kLightBufferHeaderSize = 16u;
    constexpr uint32_t std3D_kLightBufferSize       = std3D_kLightBufferHeaderSize + STD3D_MAXLIGHTS * (uint32_t)sizeof(StdLight);
    static_assert(sizeof(StdLight) % 16u == 0, "a StdLight must be whole float4 elements of the light buffer");
}

namespace std3DBabylonInternal
{
    // Makes the light buffer on first use and, when the list changed since (or the buffer is new),
    // records the copy of the header and the lights into the frame, outside the pass. False when the
    // buffer can't be made; the lit draws are then dropped by the device with a warning.
    bool std3D_UpdateLightBuffer(render::CommandList& frame)
    {
        if ( std3D_lightBuffer.isNull() )
        {
            std3D_lightBuffer = std3D_pRenderDevice->createBuffer(render::BufferDesc{ std3D_kLightBufferSize, render::BufferUsage::Storage });
            if ( std3D_lightBuffer.isNull() )
            {
                STDLOG_ERROR("Failed to create the light buffer.\n");
                return false;
            }

            std3D_bLightsDirty = true;
        }

        if ( std3D_bLightsDirty )
        {
            std::vector<std::byte> bytes(std3D_kLightBufferHeaderSize + std3D_numLights * sizeof(StdLight));
            const uint32_t aHeader[4] = { (uint32_t)std3D_numLights, 0u, 0u, 0u };
            std::memcpy(bytes.data(), aHeader, sizeof(aHeader));
            if ( std3D_numLights )
            {
                std::memcpy(bytes.data() + std3D_kLightBufferHeaderSize, std3D_aLights, std3D_numLights * sizeof(StdLight));
            }

            frame.updateBuffer(std3D_lightBuffer, 0, bytes);
            std3D_bLightsDirty = false;
        }

        return true;
    }
}

void J3DAPI std3D_SetLights(const StdLight* aLights, size_t numLights)
{
    std3D_bLightsDirty = true;
    if ( numLights > STD3D_MAXLIGHTS || (numLights && !aLights) )
    {
        STDLOG_ERROR("Error setting %zu lights: at most %u, from an array.\n", numLights, STD3D_MAXLIGHTS);
        std3D_numLights = 0;
        return;
    }

    if ( numLights )
    {
        std::memcpy(std3D_aLights, aLights, numLights * sizeof(StdLight));
    }

    std3D_numLights = numLights;
}

void J3DAPI std3D_EnableFog(int bEnabled, float density)
{
    // std3DX9.c's shader path: the density is kept only with an open device.
    std3D_bRenderFog = bEnabled != 0;
    if ( !std3D_pCurDevice )
    {
        return;
    }

    std3D_g_fogDensity = density;
}

void J3DAPI std3D_SetFog(float red, float green, float blue, float startDepth, float endDepth)
{
    // std3DX9.c's shader path: the values reach the shaders at the next change of the fog bit
    // (std3D_SetRenderState).
    std3D_EnableFog(std3D_bRenderFog, std3D_g_fogDensity);

    std3D_fogStartDepth  = startDepth;
    std3D_fogEndDepth    = (2.0f - std3D_g_fogDensity) * endDepth;
    std3D_fogDepthFactor = 1.0f / (std3D_fogEndDepth - std3D_fogStartDepth);

    std3D_aFogColor[0] = red;
    std3D_aFogColor[1] = green;
    std3D_aFogColor[2] = blue;
    std3D_aFogColor[3] = 1.0f;
}

