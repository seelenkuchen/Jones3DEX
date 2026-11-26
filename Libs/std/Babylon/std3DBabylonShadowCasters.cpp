// Shadow-caster collection of the Babylon std3D backend (M11.2d).
// Part of the Babylon column's std3D backend; see std3DBabylon.cpp for the overview. Adapter code: no Fixed/Added markers inside.
#include <std/General/stdRenderBackend.h>
#include <std/General/std3D.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <std/General/std.h>

#include "std3DBabylonInternal.h"
#include "stdRenderBabylonInternal.h"

using namespace std3DBabylonInternal;

namespace
{
    // The cap keeps a broken scene from growing the per-frame list without bound. Once it is hit the
    // rest of the frame's casters are dropped and one line is logged.
    constexpr size_t std3D_kMaxShadowCasterVertices = 300000u;

    bool std3D_bShadowCasterCapLogged = false;

    // rdroid's rdClip_FaceToPlaneEx writes StdVertexTL from view space (x right, y forward, z up):
    // sx = x * scale + center.x, sy = center.y - z * scale and rhw = (1 / y) / 32. The factor 32 is
    // part of the original fixed-point-ready projection, so the forward distance is 1 / (32 * rhw),
    // not 1 / rhw. The screen-to-view tangents are the ones the receive stage reconstructs receivers
    // with (stdRenderBabylon_SsaoProjection); at the square aspect the unit test uses they are the
    // exact rdCamera tangents too.
    bool std3D_TLToViewImpl(const StdVertexTL& vertex, uint32_t width, uint32_t height,
        float tanHalfX, float tanHalfY, float aPosition[3])
    {
        if ( width == 0 || height == 0 || !(vertex.rhw > 0.0f)
            || !std::isfinite(vertex.rhw) || !std::isfinite(vertex.sx)
            || !std::isfinite(vertex.sy) )
        {
            return false;
        }

        const float z = 1.0f / (32.0f * vertex.rhw);
        const float xNdc = (vertex.sx / (float)width) * 2.0f - 1.0f;
        const float yNdc = 1.0f - (vertex.sy / (float)height) * 2.0f;
        aPosition[0] = xNdc * tanHalfX * z;
        aPosition[1] = yNdc * tanHalfY * z;
        aPosition[2] = z;
        return std::isfinite(aPosition[0]) && std::isfinite(aPosition[1])
            && std::isfinite(aPosition[2]);
    }
}

namespace std3DBabylonInternal
{
    // Added: the casters of the frame being recorded, in the postfx view convention. Cleared at
    // std3D_StartScene and read by the shadow stage at the presenting submit.
    std::vector<float> std3D_shadowCasterPositions;

    void std3DBabylon_ResetShadowCasters()
    {
        std3D_shadowCasterPositions.clear();
    }

    const std::vector<float>& std3DBabylon_GetShadowCasterPositions()
    {
        return std3D_shadowCasterPositions;
    }

    bool std3DBabylon_TLToView(const StdVertexTL& vertex, uint32_t width, uint32_t height,
        float tanHalfX, float tanHalfY, float aPosition[3])
    {
        return std3D_TLToViewImpl(vertex, width, height, tanHalfX, tanHalfY, aPosition);
    }

    void std3DBabylon_CollectShadowCasters(bool bLit, const void* aVerts,
        const uint16_t* aIndices, size_t numIndices)
    {
        if ( !aVerts || !aIndices || numIndices == 0 )
        {
            return;
        }

        const uint32_t width  = stdRenderBabylon_GetBackBufferWidth();
        const uint32_t height = stdRenderBabylon_GetBackBufferHeight();
        if ( width == 0 || height == 0 )
        {
            return;
        }

        float tanHalfX = 0.0f;
        float tanHalfY = 0.0f;
        stdRenderBabylonInternal::stdRenderBabylon_SsaoProjection(width, height, &tanHalfX, &tanHalfY);

        const StdVertexTL*  aTL      = bLit ? nullptr : static_cast<const StdVertexTL*>(aVerts);
        const StdVertexLit* aLitVerts = bLit ? static_cast<const StdVertexLit*>(aVerts) : nullptr;

        for ( size_t i = 0; i < numIndices; ++i )
        {
            if ( std3D_shadowCasterPositions.size() / 3u >= std3D_kMaxShadowCasterVertices )
            {
                if ( !std3D_bShadowCasterCapLogged )
                {
                    STDLOG_WARNING("std3DBabylon: the shadow caster list is capped at %zu vertices; the rest of this frame's casters are dropped.\n",
                        std3D_kMaxShadowCasterVertices);
                    std3D_bShadowCasterCapLogged = true;
                }
                return;
            }

            const uint16_t index = aIndices[i];
            float aPosition[3];
            if ( aLitVerts )
            {
                // rdroid's camera space is x right, y forward, z up; the postfx view convention is
                // x right, y up, z forward.
                const StdVertexLit& vertex = aLitVerts[index];
                aPosition[0] = vertex.vx;
                aPosition[1] = vertex.vz;
                aPosition[2] = vertex.vy;
                if ( !std::isfinite(aPosition[0]) || !std::isfinite(aPosition[1])
                    || !std::isfinite(aPosition[2]) )
                {
                    continue;
                }
            }
            else if ( !std3D_TLToViewImpl(aTL[index], width, height, tanHalfX, tanHalfY,
                    aPosition) )
            {
                continue;
            }

            std3D_shadowCasterPositions.push_back(aPosition[0]);
            std3D_shadowCasterPositions.push_back(aPosition[1]);
            std3D_shadowCasterPositions.push_back(aPosition[2]);
        }
    }
}
