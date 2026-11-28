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

    // Added: M11.3. The level's static casters in world space, and whether the GPU set must be uploaded
    // again. The upload happens on the first frame that needs it, not here, so this works with no device.
    std::vector<float> std3D_staticShadowCasterPositions;
    bool std3D_bStaticShadowCastersDirty = true;

    void std3DBabylon_ResetShadowCasters()
    {
        std3D_shadowCasterPositions.clear();
    }

    const std::vector<float>& std3DBabylon_GetShadowCasterPositions()
    {
        return std3D_shadowCasterPositions;
    }

    const std::vector<float>& std3DBabylon_GetStaticShadowCasterPositions()
    {
        return std3D_staticShadowCasterPositions;
    }

    bool std3DBabylon_AreStaticShadowCastersDirty()
    {
        return std3D_bStaticShadowCastersDirty;
    }

    void std3DBabylon_MarkStaticShadowCastersClean()
    {
        std3D_bStaticShadowCastersDirty = false;
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

void J3DAPI std3D_SetStaticShadowCasters(const float* aPositions, size_t numVertices)
{
    // Added: M11.3. The level's static casters are presentation input and may arrive before the device or
    // the shadow stage exists, so they are only validated and copied here; the stage uploads them lazily.
    std3D_staticShadowCasterPositions.clear();
    std3D_bStaticShadowCastersDirty = true;

    if ( !aPositions || numVertices == 0u )
    {
        return;
    }

    if ( numVertices % 3u != 0u )
    {
        STDLOG_WARNING("std3DBabylon: the static shadow casters need whole triangles; %zu vertices is not a multiple of 3.\n",
            numVertices);
        return;
    }

    if ( numVertices > SIZE_MAX / 3u )
    {
        STDLOG_WARNING("std3DBabylon: the static shadow caster count overflows the vertex list.\n");
        return;
    }

    const size_t numFloats = numVertices * 3u;
    for ( size_t i = 0; i < numFloats; ++i )
    {
        if ( !std::isfinite(aPositions[i]) )
        {
            STDLOG_WARNING("std3DBabylon: the static shadow casters contain a non-finite coordinate; they are cleared.\n");
            return;
        }
    }

    std3D_staticShadowCasterPositions.assign(aPositions, aPositions + numFloats);
}
