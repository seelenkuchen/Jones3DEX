// Projection, focus and camera input of the Babylon std3D backend.
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

