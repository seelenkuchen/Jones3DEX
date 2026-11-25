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
// Projection, focus and camera input of the Babylon std3D backend.
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
    float std3D_zDepth                     = 0.0f;
    // Added: the last projection's planes, for std3D_SetFocus's depth linearisation.
    float std3D_nearPlane                  = 0.1f;
    float std3D_farPlane                   = 1000.0f;
    bool std3D_bRenderFog                  = true;
}

// The constant std3DX9.c's SetProjection returns on bad arguments; no Windows header here.
#ifndef E_INVALIDARG
#define E_INVALIDARG ((int)0x80070057L)
#endif

int J3DAPI std3D_SetProjection(float fov, float nearPlane, float farPlane)
{
    float distance = farPlane - nearPlane;
    if ( std::fabs(distance) < 0.009999999776482582 )
    {
        return E_INVALIDARG;
    }

    float hfov    = fov / 2.0f;
    float sinhfov = std::sin(hfov);
    if ( std::fabs(sinhfov) < 0.009999999776482582f )
    {
        return E_INVALIDARG;
    }

    // The projection matrix std3DX9.c set is unused by its pre-transformed vertices; only its
    // fixed-function fog table used the depth scale.
    std3D_zDepth = distance * 10.0f;

    // Added: the depth-of-field stage linearises the depth target with these planes.
    std3D_nearPlane = nearPlane;
    std3D_farPlane  = farPlane;

    // Added: the ambient-occlusion stage reconstructs view-space positions from the projection.
    stdRenderBabylon_SetProjection(fov, nearPlane, farPlane);
    return 0;
}

void J3DAPI std3D_SetFocus(float distance, bool bCutscene, float deltaTime)
{
    // Added: presentation only; a backend without depth of field ignores the call.
    if ( !std3D_pRenderDevice || !stdRenderBabylon_IsOpen() )
    {
        return;
    }

    stdRenderBabylon_SetFocus(distance, bCutscene, std3D_nearPlane, std3D_farPlane, deltaTime);
}

void J3DAPI std3D_SetFrameTime(float deltaTime)
{
    // Added: the light-shaft stage's fade (M9.16); a backend without it ignores the call.
    if ( !std3D_pRenderDevice || !stdRenderBabylon_IsOpen() )
    {
        return;
    }

    stdRenderBabylon_SetFrameTime(deltaTime);
}

void J3DAPI std3D_SetCameraTransform(const float aMatrix[12], bool bCut)
{
    // Added: the motion-blur stage's per-frame camera input (M9.19); a backend without motion blur
    // ignores the call.
    if ( !std3D_pRenderDevice || !stdRenderBabylon_IsOpen() )
    {
        return;
    }

    stdRenderBabylon_SetCameraTransform(aMatrix, bCut);
}

