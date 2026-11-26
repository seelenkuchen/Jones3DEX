// Lights and fog of the Babylon std3D backend.
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

