// Added: resolve the camera's sector for a frame drawn between ticks. The interpolated pose may lie
// in the sector the camera just left, and rendering portals from that sector makes parts of other
// rooms flash on screen. This reads the world only; the tick sector is restored after the draw.
#include "JonesFixedTick.h"

#include <sith/Engine/sithCollision.h>
#include <sith/World/sithWorld.h>

SithSector* JonesFixedTick_ResolveCameraSector(SithSector* pFromSector, const rdVector3* pFromPos, const rdVector3* pPos)
{
    if ( !pFromSector || !pFromPos || !pPos )
    {
        return pFromSector;
    }

    if ( !sithWorld_g_pCurrentWorld )
    {
        return pFromSector;
    }

    // sithCollision_FindSectorInRadius may clamp its endPos argument, so it works on a copy.
    rdVector3 pos = *pPos;
    SithSector* pSector = sithCollision_FindSectorInRadius(pFromSector, pFromPos, &pos, 0.0f);
    return pSector ? pSector : pFromSector;
}
