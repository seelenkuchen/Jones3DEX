// Shadow-caster collection of the Babylon std3D backend (M11.2e).
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
    // The cap keeps a broken scene from growing the per-frame list without bound. It is checked per
    // polygon, so a polygon is either added whole or not at all. Once it is hit the rest of the
    // frame's casters are dropped and one line is logged.
    constexpr size_t std3D_kMaxShadowCasterVertices = 300000u;

    bool std3D_bShadowCasterCapLogged = false;
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
}

void J3DAPI std3D_AddShadowCasterPolygon(const float* aPositions, size_t numVertices)
{
    // The same condition std3D_DrawsThingShadows reports, and a scene must be recording.
    if ( !stdRenderBabylon_IsOpen() || !stdRenderBabylon_GetShadows()
        || !stdRenderBabylonInternal::stdRenderBabylon_AreShadowObjectsOpen()
        || !std3D_bInScene || !aPositions || numVertices < 3u )
    {
        return;
    }

    // Validate the whole polygon first: one bad coordinate drops all of it, never a partial fan.
    for ( size_t i = 0; i < numVertices * 3u; ++i )
    {
        if ( !std::isfinite(aPositions[i]) )
        {
            return;
        }
    }

    const size_t numTriangles = numVertices - 2u;
    if ( std3D_shadowCasterPositions.size() / 3u + numTriangles * 3u
        > std3D_kMaxShadowCasterVertices )
    {
        if ( !std3D_bShadowCasterCapLogged )
        {
            STDLOG_WARNING("std3DBabylon: the shadow caster list is capped at %zu vertices; the rest of this frame's casters are dropped.\n",
                std3D_kMaxShadowCasterVertices);
            std3D_bShadowCasterCapLogged = true;
        }
        return;
    }

    // A fan from the first vertex. rdroid's camera space is x right, y forward, z up; the postfx
    // view convention is x right, y up, z forward.
    for ( size_t i = 1; i + 1u < numVertices; ++i )
    {
        const size_t aIndices[3] = { 0u, i, i + 1u };
        for ( size_t j = 0; j < 3u; ++j )
        {
            const size_t index = aIndices[j] * 3u;
            std3D_shadowCasterPositions.push_back(aPositions[index + 0u]);
            std3D_shadowCasterPositions.push_back(aPositions[index + 2u]);
            std3D_shadowCasterPositions.push_back(aPositions[index + 1u]);
        }
    }
}
