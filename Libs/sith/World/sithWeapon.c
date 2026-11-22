#include "sithWeapon.h"

#include <rdroid/Math/rdMatrix.h>
#include <rdroid/Math/rdVector.h>
#include <rdroid/Engine/rdLight.h>

#include <sith/AI/sithAI.h>
#include <sith/AI/sithAIAwareness.h>
#include <sith/AI/sithAIUtil.h>
#include <sith/Cog/sithCog.h>
#include <sith/Devices/sithComm.h>
#include <sith/Devices/sithConsole.h>
#include <sith/Devices/sithControl.h>
#include <sith/Devices/sithSound.h>
#include <sith/Devices/sithSoundMixer.h>
#include <sith/Dss/sithDSSThing.h>
#include <sith/Engine/sithAnimate.h>
#include <sith/Engine/sithCamera.h>
#include <sith/Engine/sithCollision.h>
#include <sith/Engine/sithPhysics.h>
#include <sith/Engine/sithPuppet.h>
#include <sith/Gameplay/sithInventory.h>
#include <sith/Gameplay/sithPlayer.h>
#include <sith/Gameplay/sithPlayerActions.h>
#include <sith/Gameplay/sithPlayerControls.h>
#include <sith/Gameplay/sithTime.h>
#include <sith/Gameplay/sithWhip.h>
#include <sith/World/sithMaterial.h>
#include <sith/World/sithModel.h>
#include <sith/World/sithSector.h>
#include <sith/World/sithSoundClass.h>
#include <sith/World/sithSurface.h>
#include <sith/World/sithTemplate.h>
#include <sith/World/sithThing.h>
#include <sith/World/sithWorld.h>

#include <std/General/stdConfig.h>
#include <std/General/stdControl.h>
#include <std/General/stdMath.h>
#include <std/General/stdUtil.h>
#include <std/General/stdComm.h>

#include <errno.h>
#include <stdlib.h>

#define SITHWEAPON_MAX_PROJECTILE_RICOCHETS 6

#define SITHWEAPON_TIMENOTSET -1.0f

static bool sithWeapon_bGenBloodSplatter                = true;
static bool sithWeapon_bProjectileFireFlashFx           = J3D_QOL_VALUE(true, false); // OG fire flash fx didn't work due to light range error, thus is disabled by default
static float sithWeapon_projectileFireFlashAmbThreshold = 0.65f;

// Added: Modern quick whip (W10d), an Enhanced option. Non-QOL builds always keep it off.
static int sithWeapon_bQuickWhipEnabled = J3D_QOL_VALUE(1, 0);

#define SITHWEAPON_QUICKWHIP_IDLE   0
#define SITHWEAPON_QUICKWHIP_ACTIVE 1

// Added: the quick whip's wait for a swing/climb move to start (the rule from W10d), and the
// safety wait for a normal attack. Both are game-time deadlines; the attack normally ends when the
// whip script's fire wait does.
#define SITHWEAPON_QUICKWHIP_TARGETWAIT 2.0f
#define SITHWEAPON_QUICKWHIP_ATTACKWAIT 8.0f

// Added: the quick whip follow-up state. It is deliberately not saved; a save made during a quick
// whip loads with the whip current.
static int sithWeapon_quickWhipState;
static SithWeaponId sithWeapon_quickWhipRestoreWeaponID;
static float sithWeapon_secQuickWhipDeadline;
static int sithWeapon_bQuickWhipMoveStarted;
static float sithWeapon_secQuickWhipAttackEndTime; // game time the no-target whip attack is over
static int sithWeapon_bQuickWhipAttackFired;      // 1 after the quick whip pressed fire on the whip

static const float sithWeapon_aMaxAimDistances[25] =
{
    [SITHWEAPON_NO_WEAPON] =     0.0f,
    [SITHWEAPON_FISTS]         = 0.0f,
    [SITHWEAPON_WHIP]          = 0.0f,
    [SITHWEAPON_PISTOL]        = 1.0f, // Pistol
    [SITHWEAPON_TOKAREV]       = 1.5f,
    [SITHWEAPON_MAUSER]        = 1.5f,
    [SITHWEAPON_SIMONOV]       = 2.5f,
    [SITHWEAPON_MACHETE]       = 0.0f,
    [SITHWEAPON_SUBMACHINE]    = 1.5f,
    [SITHWEAPON_SHOTGUN]       = 1.0f, // Shotgun
    [SITHWEAPON_BAZOOKA]       = 3.0f,
    [SITHWEAPON_GRENADE]       = 0.0f,
    [SITHWEAPON_SATCHEL]       = 0.0f,
    [SITHWEAPON_ZIPPO]         = 0.0f,
    [SITHWEAPON_IMP1]          = 0.0f,
    [SITHWEAPON_IMP2]          = 0.0f,
    [SITHWEAPON_IMP3]          = 0.0f,
    [SITHWEAPON_IMP4]          = 0.0f,
    [SITHWEAPON_IMP5]          = 0.0f,
    [SITHWEAPON_COMFISTS]      = 0.0f,
    [SITHWEAPON_COMTOKAREV]    = 1.5f,
    [SITHWEAPON_COMMAUSER]     = 1.5f,
    [SITHWEAPON_COMSIMONOV]    = 2.5f,
    [SITHWEAPON_COMSUBMACHINE] = 1.5f,
    [SITHWEAPON_COMSHOTGUN]    = 1.0f
};

static SithWeaponId sithWeapon_lastPlayerWeaponID;

static int sithWeapon_bPlayerWeaponActivated;
static int sithWeapon_bLocalPlayerAiming;

static int sithWeapon_deactivatedPlayerWeaponID;

static float sithWeapon_secMountWait;
static float sithWeapon_secWeaponActivationWaitEndTime;
static float sithWeapon_secPlayerWeaponDeactivationTime;

static int sithWeapon_playerWhipHolsterSwapRefNum;
static int sithWeapon_playerPistolHolsterSwapRefNum;
static int sithWeapon_playerBackHolsterSwapRefNum;

static SithControlFunction sithWeapon_bufferedWaponKeyId;
static SithWeaponActorKilledCallback sithWeapon_pfActorKilledCallback;

void J3DAPI sithWeapon_HandleImpact(SithThing* pWeapon);
int sithWeapon_IsLocalPlayerUnableToUseWeapon(void);

static int sithWeapon_GetWhipPressTarget(SithThing** ppTarget, int* pbSwing);
static int sithWeapon_ActivateWhipPress(SithThing* pThing, SithInventoryType* pItem, SithWeaponId curWeaponID);

static void J3DAPI sithWeapon_GenBloodsplort(SithThing* pHitThing); // Keeping for hooking on vanilla exe
static bool J3DAPI sithWeapon_GenBloodSplatterEx(SithThing* pHitThing); // Returns false if thing is vehicle, splort template not found, or fails to create sprite thing

// Added: clears the quick whip follow-up state. The enable switch is an option, so it is not reset.
static void sithWeapon_ResetQuickWhipState(void)
{
    sithWeapon_quickWhipState              = SITHWEAPON_QUICKWHIP_IDLE;
    sithWeapon_quickWhipRestoreWeaponID    = SITHWEAPON_NO_WEAPON;
    sithWeapon_secQuickWhipDeadline        = 0.0f;
    sithWeapon_bQuickWhipMoveStarted       = 0;
    sithWeapon_secQuickWhipAttackEndTime   = 0.0f;
    sithWeapon_bQuickWhipAttackFired       = 0;
}

void J3DAPI sithWeapon_SetQuickWhipEnabled(int bEnabled)
{
#ifdef J3D_QOL_IMPROVEMENTS
    sithWeapon_bQuickWhipEnabled = bEnabled != 0;
#else
    J3D_UNUSED(bEnabled);
#endif
}

int sithWeapon_IsQuickWhipEnabled(void)
{
    return sithWeapon_bQuickWhipEnabled;
}

void sithWeapon_ResetGlobals(void)
{
    // Added: quick whip's pending/restore state starts over.
    sithWeapon_ResetQuickWhipState();
}

void sithWeapon_Open(void)
{
    sithWeapon_secWeaponActivationWaitEndTime = 0.0f;

    // Added: quick whip's pending/restore state starts over.
    sithWeapon_ResetQuickWhipState();

    // Added: Added blood splatter config read
    sithWeapon_bGenBloodSplatter = stdConfig_GetBool(SITHWEAPON_CFG_GAMEPLAY_BLOODSPLATTER, sithWeapon_bGenBloodSplatter);
    if ( !stdConfig_Contains(SITHWEAPON_CFG_GAMEPLAY_BLOODSPLATTER) )
    {
        stdConfig_SetBool(SITHWEAPON_CFG_GAMEPLAY_BLOODSPLATTER, sithWeapon_bGenBloodSplatter);
    }

    // Added: quick whip config read. Enhanced defaults it on, the Retail profile always off.
    sithWeapon_bQuickWhipEnabled = J3D_QOL_VALUE(
        stdConfig_GetBool(SITHWEAPON_CFG_GAMEPLAY_QUICKWHIP, true), false);
    if ( !stdConfig_Contains(SITHWEAPON_CFG_GAMEPLAY_QUICKWHIP) )
    {
        stdConfig_SetBool(SITHWEAPON_CFG_GAMEPLAY_QUICKWHIP, sithWeapon_bQuickWhipEnabled);
    }

    // Added: Added weapon fire flash fx config read
    sithWeapon_bProjectileFireFlashFx = stdConfig_GetBool(SITHWEAPON_CFG_ENGINERENDERER_VFX_WEAPONFIREFLASH_ENABLED, sithWeapon_bProjectileFireFlashFx);
    if ( !stdConfig_Contains(SITHWEAPON_CFG_ENGINERENDERER_VFX_WEAPONFIREFLASH_ENABLED) )
    {
        stdConfig_SetBool(SITHWEAPON_CFG_ENGINERENDERER_VFX_WEAPONFIREFLASH_ENABLED, sithWeapon_bProjectileFireFlashFx);
    }

    sithWeapon_projectileFireFlashAmbThreshold = stdConfig_GetFloat(SITHWEAPON_CFG_ENGINERENDERER_VFX_WEAPONFIREFLASH_THRESHOLD, sithWeapon_projectileFireFlashAmbThreshold);
    if ( !stdConfig_Contains(SITHWEAPON_CFG_ENGINERENDERER_VFX_WEAPONFIREFLASH_THRESHOLD) )
    {
        stdConfig_SetFloat(SITHWEAPON_CFG_ENGINERENDERER_VFX_WEAPONFIREFLASH_THRESHOLD, sithWeapon_projectileFireFlashAmbThreshold);
    }
}

void sithWeapon_Close(void)
{}

void J3DAPI sithWeapon_InitalizeActorWeapon(SithThing* pThing)
{
    SITH_ASSERTREL((pThing->type == SITH_THING_ACTOR) || (pThing->type == SITH_THING_PLAYER));

    pThing->thingInfo.actorInfo.weaponInfo.secActivationStartTime   = SITHWEAPON_TIMENOTSET;
    pThing->thingInfo.actorInfo.weaponInfo.secLastRapidFireTime     = SITHWEAPON_TIMENOTSET;
    pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitEndTime = SITHWEAPON_TIMENOTSET;
    pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitTime    = SITHWEAPON_TIMENOTSET; // TODO: Check if this should be 0.0f, the field is used in some calculations without checking for -1.0f and may lead to issues (i.e. subtracting -1 second)
    pThing->thingInfo.actorInfo.weaponInfo.secAimWaitEndTime        = 0.0f;
    pThing->thingInfo.actorInfo.weaponInfo.secSwapTime              = SITHWEAPON_TIMENOTSET;
    pThing->thingInfo.actorInfo.weaponInfo.selectedWeaponID         = SITHWEAPON_NOWEAPONSELECTED;
    pThing->thingInfo.actorInfo.weaponInfo.curWeaponID              = SITHWEAPON_NO_WEAPON;
    pThing->thingInfo.actorInfo.weaponInfo.deselectedWeaponID       = SITHWEAPON_NOWEAPONDESELECTED;
    pThing->thingInfo.actorInfo.weaponInfo.swapRefNum               = -1;

    // Altered: Replaced with rdVector_Zero3
    rdVector_Zero3(&pThing->thingInfo.actorInfo.weaponInfo.vecUnknown0);

    sithWeapon_secWeaponActivationWaitEndTime = 0.0f;

    if ( pThing->type == SITH_THING_PLAYER )
    {
        sithWeapon_lastPlayerWeaponID              = SITHWEAPON_PISTOL;
        sithWeapon_bufferedWaponKeyId              = -1;
        sithWeapon_deactivatedPlayerWeaponID       = SITHWEAPON_NOWEAPONDESELECTED;
        sithWeapon_playerWhipHolsterSwapRefNum     = -1;
        sithWeapon_playerPistolHolsterSwapRefNum   = -1;
        sithWeapon_playerBackHolsterSwapRefNum     = -1;
        sithWeapon_secMountWait                    = 0.0f;
        sithWeapon_secPlayerWeaponDeactivationTime = 0.0f;
        sithWeapon_bPlayerWeaponActivated          = 0;
        sithWeapon_bLocalPlayerAiming              = 0;
    }
}

// Added: the whip-aim attachment test shared by the aim update and the quick whip availability.
// A player attaches either to a whip-aim surface or to a thing face flagged for whip aim.
static int sithWeapon_IsOnWhipAimSurface(const SithThing* pThing)
{
    return (pThing->attach.flags & SITH_ATTACH_SURFACE) != 0
            && (pThing->attach.attachedToStructure.pSurfaceAttached->flags & SITH_SURFACE_WHIPAIM) != 0
        || (pThing->attach.flags & SITH_ATTACH_THINGFACE) != 0
            && (pThing->attach.pFace->flags & RD_FF_3DO_WHIP_AIM) != 0;
}

void J3DAPI sithWeapon_UpdateActorWeapon(SithThing* pThing)
{
    if ( pThing->thingInfo.actorInfo.weaponInfo.secSwapTime > 0.0f && sithTime_g_secGameTime >= (double)pThing->thingInfo.actorInfo.weaponInfo.secSwapTime )
    {
        pThing->thingInfo.actorInfo.weaponInfo.secSwapTime = SITHWEAPON_TIMENOTSET;
        sithWeapon_SetWeaponModel(pThing, (SithWeaponId)pThing->thingInfo.actorInfo.weaponInfo.curWeaponID);
    }

    if ( sithWeapon_deactivatedPlayerWeaponID == SITHWEAPON_NOWEAPONDESELECTED || pThing->type != SITH_THING_PLAYER )
    {
        if ( pThing->type == SITH_THING_PLAYER && sithWeapon_IsLocalPlayerUnableToUseWeapon() )
        {
            if ( sithWeapon_bPlayerWeaponActivated == 1 )
            {
                sithWeapon_bPlayerWeaponActivated          = 0;
                sithWeapon_deactivatedPlayerWeaponID       = pThing->thingInfo.actorInfo.weaponInfo.curWeaponID;
                sithWeapon_secPlayerWeaponDeactivationTime = sithTime_g_secGameTime;
            }
        }
        else if ( pThing->thingInfo.actorInfo.weaponInfo.deselectedWeaponID == SITHWEAPON_NOWEAPONDESELECTED )
        {
            if ( pThing->thingInfo.actorInfo.weaponInfo.selectedWeaponID == SITHWEAPON_NOWEAPONSELECTED )
            {
                if ( pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitTime <= 0.0f )
                {
                    if ( pThing->type == SITH_THING_PLAYER && pThing->thingInfo.actorInfo.weaponInfo.curWeaponID == SITHWEAPON_WHIP )
                    {
                        // Altered: the attachment test is shared with sithWeapon_CanQuickWhip.
                        if ( sithWeapon_IsOnWhipAimSurface(pThing) )
                        {
                            sithWhip_UpdateWhipAim(pThing);
                        }
                        else if ( sithWeapon_bLocalPlayerAiming )
                        {
                            sithWeapon_SendMessageAim(pThing, 0);
                        }
                    }
                }
                else if ( sithTime_g_secGameTime >= (double)pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitEndTime )
                {
                    SithInventoryType* pItem = sithInventory_GetType(pThing->thingInfo.actorInfo.weaponInfo.curWeaponID);
                    if ( pItem->pCog )
                    {
                        sithCog_SendMessage(pItem->pCog, SITHCOG_MSG_FIRE, SITHCOG_SYM_REF_NONE, 0, SITHCOG_SYM_REF_THING, pThing->idx, 0);
                        pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitEndTime = sithTime_g_secGameTime
                            + pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitTime;
                    }

                    if ( pThing->type == SITH_THING_PLAYER && sithWeapon_bPlayerWeaponActivated != 1 )
                    {
                        sithWeapon_bPlayerWeaponActivated = 1;
                        sithWeapon_DeactivateCurrentWeapon(pThing);
                    }
                }
            }
            else if ( (pThing->type != SITH_THING_PLAYER || sithTime_g_secGameTime >= (double)sithWeapon_secMountWait)
                && sithTime_g_secGameTime >= (double)pThing->thingInfo.actorInfo.weaponInfo.secAimWaitEndTime )
            {
                SithInventoryType* pItem = sithInventory_GetType(pThing->thingInfo.actorInfo.weaponInfo.selectedWeaponID);
                if ( (pItem->flags & SITHINVENTORY_TYPE_WEAPON) != 0 && pItem->pCog )
                {
                    pThing->thingInfo.actorInfo.weaponInfo.secLastRapidFireTime = SITHWEAPON_TIMENOTSET;
                    sithCog_SendMessage(pItem->pCog, SITHCOG_MSG_SELECTED, SITHCOG_SYM_REF_NONE, 0, SITHCOG_SYM_REF_THING, pThing->idx, 0);
                }

                if ( pThing->type == SITH_THING_PLAYER )
                {
                    sithInventory_SetCurrentWeapon(pThing, (SithWeaponId)pThing->thingInfo.actorInfo.weaponInfo.selectedWeaponID);
                }

                pThing->thingInfo.actorInfo.weaponInfo.curWeaponID      = pThing->thingInfo.actorInfo.weaponInfo.selectedWeaponID;
                pThing->thingInfo.actorInfo.weaponInfo.selectedWeaponID = SITHWEAPON_NOWEAPONSELECTED;
            }
        }
        else if ( sithTime_g_secGameTime >= (double)pThing->thingInfo.actorInfo.weaponInfo.secAimWaitEndTime )
        {
            if ( pThing->type == SITH_THING_PLAYER && sithWeapon_bPlayerWeaponActivated == 1 )
            {
                sithWeapon_DeactivateCurrentWeapon(pThing);
            }
            else
            {
                SithInventoryType* pItem = sithInventory_GetType(pThing->thingInfo.actorInfo.weaponInfo.deselectedWeaponID);
                if ( pItem->pCog )
                {
                    sithCog_SendMessage(pItem->pCog, SITHCOG_MSG_DESELECTED, SITHCOG_SYM_REF_INT, pThing->thingInfo.actorInfo.weaponInfo.selectedWeaponID, SITHCOG_SYM_REF_THING, pThing->idx, 0);
                }

                if ( pThing->type == SITH_THING_PLAYER )
                {
                    sithWeapon_bLocalPlayerAiming = 0;
                    sithInventory_SetCurrentWeapon(pThing, SITHWEAPON_NO_WEAPON);
                    if ( (pItem->flags & SITHINVENTORY_TYPE_PLAYERWEAPON) != 0 )
                    {
                        sithWeapon_lastPlayerWeaponID = pThing->thingInfo.actorInfo.weaponInfo.deselectedWeaponID;
                    }
                }

                pThing->thingInfo.actorInfo.weaponInfo.curWeaponID        = SITHWEAPON_NO_WEAPON;
                pThing->thingInfo.actorInfo.weaponInfo.deselectedWeaponID = SITHWEAPON_NOWEAPONDESELECTED;
            }
        }
    }
    else if ( sithTime_g_secGameTime >= (double)sithWeapon_secPlayerWeaponDeactivationTime )
    {
        SithInventoryType* pItem = sithInventory_GetType(pThing->thingInfo.actorInfo.weaponInfo.curWeaponID);
        if ( pItem->pCog )
        {
            sithCog_SendMessage(pItem->pCog, SITHCOG_MSG_DEACTIVATED, SITHCOG_SYM_REF_NONE, 0, SITHCOG_SYM_REF_THING, pThing->idx, 0);
        }

        sithWeapon_deactivatedPlayerWeaponID = SITHWEAPON_NOWEAPONDESELECTED;
    }
}

void J3DAPI sithWeapon_Update(SithThing* pThing, float secDeltaTime)
{
    SITH_ASSERTREL(pThing);

    if ( (pThing->thingInfo.weaponInfo.flags & SITH_WF_INSTANTIMPACT) != 0 )
    {
        sithWeapon_HandleImpact(pThing);
        return;
    }

    if ( (pThing->thingInfo.weaponInfo.flags & SITH_WF_DAMAGEDECAY) != 0 && pThing->thingInfo.weaponInfo.damage > pThing->thingInfo.weaponInfo.minDamage )
    {
        pThing->thingInfo.weaponInfo.damage -= pThing->thingInfo.weaponInfo.rate * secDeltaTime;
        if ( pThing->thingInfo.weaponInfo.damage < pThing->thingInfo.weaponInfo.minDamage )
        {
            pThing->thingInfo.weaponInfo.damage = pThing->thingInfo.weaponInfo.minDamage;
        }
    }

    if ( (pThing->thingInfo.weaponInfo.flags & SITH_WF_DECAYEMITSOUNDAWARENESSEVENT) != 0 && SITH_ISFRAMECYCLE(pThing->idx, 8) ) // on every 8th frame
    {
        sithAIAwareness_CreateTransmittingEvent(pThing->pInSector, &pThing->pos, 2, 0.5f, pThing);
    }
}

int J3DAPI sithWeapon_SelectWeapon(SithThing* pThing, SithWeaponId typeId)
{
    SITH_ASSERTREL(pThing && ((pThing->type == SITH_THING_PLAYER) || (pThing->type == SITH_THING_ACTOR))); // Fixed: Move to beginning of the scope

    if ( pThing->type == SITH_THING_PLAYER )
    {
        pThing->thingInfo.actorInfo.weaponInfo.curWeaponID = sithInventory_GetCurrentWeapon(pThing);
    }

    if ( typeId == pThing->thingInfo.actorInfo.weaponInfo.curWeaponID || pThing->thingInfo.actorInfo.weaponInfo.selectedWeaponID != SITHWEAPON_NOWEAPONSELECTED )
    {
        return 0;
    }

    bool bHasWeapon = true;
    if ( pThing->type == SITH_THING_PLAYER )
    {
        bHasWeapon = sithInventory_GetInventory(pThing, typeId) != 0.0f && sithInventory_IsInventoryAvailable(pThing, typeId);
    }
    else
    {
        bHasWeapon = true;
        sithPlayerControls_ResetAimJoints(pThing);
    }

    if ( !bHasWeapon )
    {
        return 0;
    }

    if ( pThing->thingInfo.actorInfo.weaponInfo.secActivationStartTime != SITHWEAPON_TIMENOTSET && pThing->type == SITH_THING_PLAYER )
    {
        sithWeapon_deactivatedPlayerWeaponID       = pThing->thingInfo.actorInfo.weaponInfo.curWeaponID;
        sithWeapon_secPlayerWeaponDeactivationTime = pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitEndTime;
    }

    pThing->thingInfo.actorInfo.weaponInfo.deselectedWeaponID = pThing->thingInfo.actorInfo.weaponInfo.curWeaponID;
    pThing->thingInfo.actorInfo.weaponInfo.selectedWeaponID   = typeId;
    return 1;
}

static void sithWeapon_CreateExplosionAtThingCollision(SithThing* pWeapon, const SithThing* pHitThing, const SithCollision* pCollision)
{
    if ( pCollision && pCollision->pFaceCollided )
    {
        rdVector3 expDir;
        rdMatrix_TransformVector34(&expDir, &pCollision->pFaceCollided->normal, &pHitThing->orient);
        sithWeapon_CreateWeaponExplosion(pWeapon, pWeapon->thingInfo.weaponInfo.pExplosionTemplate, &expDir, /*bRotate=*/1);
    }
    else
    {
        sithWeapon_CreateWeaponExplosion(pWeapon, pWeapon->thingInfo.weaponInfo.pExplosionTemplate, NULL, /*bRotate=*/0);
    }
}

void J3DAPI sithWeapon_HandleImpact(SithThing* pWeapon)
{
    SithWeaponInfo* pWeaponInfo = &pWeapon->thingInfo.weaponInfo;
    float damage = pWeapon->thingInfo.actorInfo.maxHealth;

    int searchFlags = 0;
    if ( (pWeapon->thingInfo.weaponInfo.damageType & SITH_DAMAGE_MACHETE) != 0 )
    {
        searchFlags = 0x200;
    }

    rdVector3 moveNorm = pWeapon->orient.lvec;
    sithCollision_SearchForCollisions(pWeapon->pInSector, pWeapon, &pWeapon->pos, &moveNorm, pWeaponInfo->range, pWeapon->collide.movesize, searchFlags);

    while ( 1 )
    {
        SithCollision* pCollision = sithCollision_PopStack();
        if ( !pCollision )
        {
            break;
        }

        if ( (pWeaponInfo->flags & SITH_WF_DAMAGEDECAY) != 0 )
        {
            damage -= pWeaponInfo->rate * pCollision->distance;
            if ( damage < pWeaponInfo->minDamage )
            {
                damage = pWeaponInfo->minDamage;
            }
        }

        if ( (pCollision->type & SITHCOLLISION_THING) != 0 )
        {
            SithThing* pVictim = pCollision->pThingCollided;
            sithThing_DamageThing(pVictim, pWeapon, damage, pWeaponInfo->damageType);

            if ( pVictim->type == SITH_THING_ACTOR )
            {
                // Call actor killed cb if actor is killed
                if ( sithThing_GetThingParent(pWeapon) == sithPlayer_g_pLocalPlayerThing
                    && sithWeapon_pfActorKilledCallback
                    && pVictim->thingInfo.actorInfo.health == 0.0f )
                {
                    sithWeapon_pfActorKilledCallback(sithGetGameDifficulty());
                }

                // Generate bloodsplort and destroy projectile or explode projectile
                if ( (pWeaponInfo->flags & SITH_WF_BLOODSPLATTER) != 0
                    && (pVictim->thingInfo.actorInfo.flags & SITH_AF_DROID) == 0
                // Altered: With J3D_QOL_IMPROVEMENTS the dead-victim and non-player conditions are dropped, so non-killing hits and hits on the player
                //          also splatter and destroy the projectile. The original did so only for a killing hit on a non-player.
                #ifndef J3D_QOL_IMPROVEMENTS
                    && pVictim->thingInfo.actorInfo.health == 0.0f
                    && pVictim->type != SITH_THING_PLAYER
                #endif
                    && sithWeapon_bGenBloodSplatter
                    && sithWeapon_GenBloodSplatterEx(pVictim) )
                {
                    // Altered: Replaced with new sithWeapon_GenBloodSplatterEx
                    //          and added fallback logic if function call fails
                    sithThing_DestroyThing(pWeapon);
                }
                else
                {
                    sithWeapon_CreateExplosionAtThingCollision(pWeapon, pVictim, pCollision); // Altered: Replaced with the new sithWeapon_CreateExplosionAtThingCollision to rotate explosion sprite
                }
            }
            else
            {
                sithWeapon_CreateExplosionAtThingCollision(pWeapon, pVictim, pCollision); // Altered: Replaced with the new sithWeapon_CreateExplosionAtThingCollision to rotate explosion sprite
            }

            if ( pWeaponInfo->force != 0.0f && pVictim->moveType == SITH_MT_PHYSICS )
            {
                rdVector3 force;
                rdVector_Scale3(&force, &moveNorm, pWeaponInfo->force);
                sithPhysics_ApplyForce(pVictim, &force);
            }

            break;
        }

        if ( (pCollision->type & SITHCOLLISION_WORLD) != 0 )
        {
            sithWeapon_CreateWeaponExplosion(pWeapon, pWeaponInfo->pExplosionTemplate, NULL, /*bRotate=*/0);
            sithSurface_HandleThingImpact(pCollision->pSurfaceCollided, pWeapon, damage, pWeaponInfo->damageType);
            break;
        }
    }

    sithCollision_DecreaseStackLevel();

    if ( (pWeapon->flags & SITH_TF_COGLINKED) != 0 && (pWeapon->flags & SITH_TF_REMOTE) == 0 )
    {
        sithCog_ThingSendMessage(pWeapon, NULL, SITHCOG_MSG_REMOVED);
    }

    sithThing_RemoveThing(sithWorld_g_pCurrentWorld, pWeapon);
}

int J3DAPI sithWeapon_ParseArg(const StdConffileArg* pArg, SithThing* pThing, int adjNum)
{
    switch ( adjNum )
    {
        case SITHTHING_ARG_TYPEFLAGS:
        {
            if ( sscanf_s(pArg->argValue, "%x", &pThing->thingInfo.weaponInfo.flags) != 1 )
            {
                goto syntax_error;
            }
            return 1;
        }
        case SITHTHING_ARG_DAMAGE:
        {
            float damage = STD_STRTOF(pArg->argValue); // Changed: Use strtof instead atof
            // Only reject overflow, (float)atof of the original accepted underflowing values
            if ( STD_STRCONV_RANGE_ERROR() && fabsf(damage) > 1.0f )
            { // Added
                goto syntax_error;
            }
            pThing->thingInfo.weaponInfo.damage = damage;
            return 1;
        }
        case SITHTHING_ARG_MINDAMAGE:
        {
            float minDamage = STD_STRTOF(pArg->argValue); // Changed: Use strtof instead atof
            // Only reject overflow, (float)atof of the original accepted underflowing values
            if ( STD_STRCONV_RANGE_ERROR() && fabsf(minDamage) > 1.0f )
            { // Added
                goto syntax_error;
            }
            pThing->thingInfo.weaponInfo.minDamage = minDamage;
            return 1;
        }
        case SITHTHING_ARG_DAMAGECLASS:
        {
            if ( sscanf_s(pArg->argValue, "%x", &pThing->thingInfo.weaponInfo.damageType) != 1 )
            {
                goto syntax_error;
            }
            return 1;
        }
        case SITHTHING_ARG_EXPLODE:
        {
            pThing->thingInfo.weaponInfo.pExplosionTemplate = sithTemplate_GetTemplate(pArg->argValue);
            return 1;
        }
        case SITHTHING_ARG_FORCE:
        {
            float force = STD_STRTOF(pArg->argValue); // Changed: Use strtof instead atof
            // Only reject overflow, (float)atof of the original accepted underflowing values
            if ( STD_STRCONV_RANGE_ERROR() && fabsf(force) > 1.0f )
            { // Added
                goto syntax_error;
            }
            pThing->thingInfo.weaponInfo.force = force;
            return 1;
        }
        case SITHTHING_ARG_RANGE:
        {
            float range = STD_STRTOF(pArg->argValue); // Changed: Use strtof instead atof
            // Only reject overflow, (float)atof of the original accepted underflowing values
            if ( STD_STRCONV_RANGE_ERROR() && fabsf(range) > 1.0f )
            { // Added
                goto syntax_error;
            }
            pThing->thingInfo.weaponInfo.range = range;
            return 1;
        }
        case SITHTHING_ARG_RATE:
        {
            float rate = STD_STRTOF(pArg->argValue); // Changed: Use strtof instead atof
            // Only reject overflow, (float)atof of the original accepted underflowing values
            if ( STD_STRCONV_RANGE_ERROR() && fabsf(rate) > 1.0f )
            { // Added
                goto syntax_error;
            }
            pThing->thingInfo.weaponInfo.rate = rate;
            return 1;
        }
        default:
            return 0;
    }

syntax_error:
    SITHLOG_ERROR("Bad argument pair %s=%s line %d.  Skipped.\n", pArg->argName, pArg->argValue, stdConffile_GetLineNumber());
    return 1;
}

SithThing* J3DAPI sithWeapon_WeaponFire(SithThing* pShooter, const SithThing* pProjectileTemplate, const rdVector3* pFireDir, rdVector3* pFirePos, tSoundHandle hFireSnd, SithPuppetSubMode submode, float extra, SithFireProjectileFlags projectileFlags, float secDeltaTime)
{
    SITH_ASSERTREL(pShooter);

    // Added: every shot the local player fires gives a short gamepad kick (presentation only,
    // ADR-0028).
    if ( pShooter == sithPlayer_g_pLocalPlayerThing )
    {
        stdControl_Rumble(0.15f, 0.45f, 90);
    }

    if ( hFireSnd )
    {
        sithAIAwareness_CreateTransmittingEvent(pShooter->pInSector, &pShooter->pos, 0, 3.0f, pShooter);
    }

    if ( pShooter->type == SITH_THING_PLAYER && (pProjectileTemplate->thingInfo.weaponInfo.damageType & (SITH_DAMAGE_ELECTROWHIP | SITH_DAMAGE_WHIP | SITH_DAMAGE_MACHETE | SITH_DAMAGE_FISTS)) == 0 )
    {
        sithAIAwareness_CreateTransmittingEvent(pShooter->pInSector, &pShooter->pos, 3, 1.5f, pShooter);
    }

    SithThing* pProjectile = sithWeapon_WeaponFireProjectile(pShooter, pProjectileTemplate, pFireDir, pFirePos, hFireSnd, submode, extra, projectileFlags, secDeltaTime);
    if ( pProjectile && sithMessage_g_outputstream )
    {
        sithDSSThing_Fire(pShooter, pProjectileTemplate, pFireDir, pFirePos, hFireSnd, submode, extra, projectileFlags, secDeltaTime, pProjectile->guid, SITHMESSAGE_SENDTOJOINEDPLAYERS, SITHMESSAGE_STREAM_ALL);
    }

    return pProjectile;
}

SithThing* J3DAPI sithWeapon_WeaponFireProjectile(SithThing* pShooter, const SithThing* pProjectileTemplate, const rdVector3* pFireDir, rdVector3* pFirePos, tSoundHandle hFireSnd, SithPuppetSubMode submode, float extra, SithFireProjectileFlags flags, float secDeltaTime)
{
    if ( !pShooter || !pFireDir )
    {
        SITHLOG_ERROR("Bad arguments passed.\n");
        return NULL;
    }

    SithThing* pProjectile = NULL;
    if ( pProjectileTemplate )
    {
        rdMatrix34 projectileOrient;
        rdMatrix_BuildFromLook34(&projectileOrient, pFireDir);

        if ( !rdVector_IsZero3(&pShooter->thingInfo.actorInfo.fireOffset)
            && pShooter->controlType != SITH_CT_AI )
        {
            rdVector3 fireOffset = pShooter->thingInfo.actorInfo.fireOffset;
            if ( ((uint8_t)flags & (uint8_t)SITHFIREPROJECTILE_RIGHTHAND_FIRE) != 0 )
            {
                int jointIdx = sithThing_GetThingJointIndex(pShooter, "inrhand");
                if ( jointIdx != -1 )
                {
                    rdMatrix_TransformVector34Acc(&fireOffset, &pShooter->renderData.paJointMatrices[jointIdx]);
                }
            }

            else if ( (flags & SITHFIREPROJECTILE_TORSO_FIRE) != 0 )
            {
                int jointIdx = sithThing_GetThingJointIndex(pShooter, "intorso");
                if ( jointIdx != -1 )
                {
                    rdMatrix_TransformVector34Acc(&fireOffset, &pShooter->renderData.paJointMatrices[jointIdx]);
                }
            }
            else
            {
                rdMatrix_TransformVector34Acc(&fireOffset, &pShooter->orient);
            }

            rdVector_Add3Acc(pFirePos, &fireOffset);
        }

        rdVector3 offsetPos = pShooter->pos;

        // Find offset sector
        rdVector3 shooterOffset;
        shooterOffset.x = 0.022f;
        shooterOffset.y = 0.0f;
        shooterOffset.z = 0.050000001f;
        rdMatrix_TransformVector34Acc(&shooterOffset, &pShooter->orient);
        rdVector_Add3Acc(&offsetPos, &shooterOffset);

        // Fixed: Check for pFirePos to be NAN and prevent engine crashing
        //        This can happen in some cases when for example thing->renderData.paJointMatrices[idx].dvec is NAN.
        //        e.g.: comminecar driving in-front of indy and firing back at indy (usually at the station when driving up hill)
        //        TODO: Investigate why paJointMatrices would have nan values
        if ( rdVector_IsNAN3(pFirePos) )
        {
            SITHLOG_ERROR("Uh-oh.  Received NAN fire position.  Weapon fire failed.\n");
            return NULL;
        }

        SithSector* pSector = sithCollision_FindSectorInRadius(pShooter->pInSector, &pShooter->pos, &offsetPos, 0.0f);
        if ( !pSector )
        {
            SITHLOG_ERROR("Uh-oh.  Failed to find offset sector.  Weapon fire failed.\n");
            return NULL;
        }

        // Find Fire point sector
        pSector = sithCollision_FindSectorAtThing(pShooter, pSector, &offsetPos, pFirePos, 0.0f);
        if ( !pSector )
        {
            SITHLOG_ERROR("Uh-oh.  Failed to find fire point sector.  Weapon fire failed.\n");
            return NULL;
        }

        pProjectile = sithThing_CreateThingAtPos(pProjectileTemplate, pFirePos, &projectileOrient, pSector, pShooter);
        if ( !pProjectile )
        {
            return NULL;
        }

        if ( pProjectileTemplate->pCog )
        {
            sithCog_SendMessage(pProjectileTemplate->pCog, SITHCOG_MSG_FIRE, SITHCOG_SYM_REF_THING, pProjectile->idx, SITHCOG_SYM_REF_THING, pShooter->idx, 0);
        }

        if ( (flags & SITHFIREPROJECTILE_SCALE_VELOCITY) != 0 )
        {
            rdVector_Scale3Acc(&pProjectile->moveInfo.physics.velocity, extra);
        }

        if ( (flags & SITHFIREPROJECTILE_SCALE_DAMAGE) != 0 )
        {
            pProjectile->thingInfo.weaponInfo.damage = pProjectile->thingInfo.weaponInfo.damage * extra;
        }

        if ( (flags & SITHFIREPROJECTILE_UNKNOWN_4) != 0 )
        {
            pProjectile->thingInfo.weaponInfo.unknown8 = pProjectile->thingInfo.weaponInfo.unknown8 * extra;
        }

        if ( (flags & SITHFIREPROJECTILE_UNKNOWN_8) != 0 )
        {
            pProjectile->thingInfo.weaponInfo.unknown7 = pProjectile->thingInfo.weaponInfo.unknown7 * extra;
        }

        rdVector3 moveNorm;
        if ( secDeltaTime > 0.02f ) // 0.02 - 50 fps
        {
            sithPhysics_UpdateThing(pProjectile, secDeltaTime);
            float moveDist = rdVector_Normalize3(&moveNorm, &pProjectile->moveInfo.physics.deltaVelocity);
            if ( moveDist > 0.0f )
            {
                sithCollision_MoveThing(pProjectile, &moveNorm, moveDist, pProjectile->moveInfo.physics.flags); // TODO: [BUG] Using physics.flags here is probably wrong???
            }
        }

        // Now search for projectile target
        float speed = rdVector_Normalize3(&moveNorm, &pProjectile->moveInfo.physics.velocity);
        float moveDist = J3DMIN(speed * 3.0f, 5.0f);

        sithCollision_SearchForCollisions(pProjectile->pInSector, pProjectile, &pProjectile->pos, &moveNorm, moveDist, pProjectile->collide.movesize, 2);
        SithCollision* pCollision = sithCollision_PopStack();
        sithCollision_DecreaseStackLevel();

        if ( pCollision && (pCollision->type & SITHCOLLISION_THING) != 0 )
        {
            if ( !stdComm_IsGameActive()
                && pShooter == sithPlayer_g_pLocalPlayerThing
                && (pProjectile->thingInfo.weaponInfo.flags & SITH_WF_EMITAITARGETEDEVENT) != 0
                && pCollision->pThingCollided->controlType == SITH_CT_AI )
            {
                sithAI_EmitEvent(pCollision->pThingCollided->controlInfo.aiControl.pLocal, SITHAI_EVENT_TARGETED, pProjectile);
            }
        }
        else
        {
            sithCog_ThingSendMessageEx(pShooter, NULL, SITHCOG_MSG_MISSED, (int32_t)pProjectile->thingInfo.weaponInfo.damage, pProjectile->thingInfo.weaponInfo.damageType, 0, 0);
        }
    }

    if ( (flags & SITHFIREPROJECTILE_FIRE_EFFECT) != 0 )
    {
        sithWeapon_CreateWeaponFireFx(pShooter, pFirePos);
    }

    if ( hFireSnd )
    {
        sithSoundMixer_PlaySoundThing(hFireSnd, pShooter, 1.0f, 1.0f, 10.0f, SOUNDPLAY_HIGH_PRIORITY | SOUNDPLAY_THING_POS);
    }

    if ( submode <= 0 )
    {
        return pProjectile;
    }

    if ( pShooter->pPuppetClass )
    {
        sithPuppet_PlayMode(pShooter, submode, NULL);
    }

    return pProjectile;
}

void J3DAPI sithWeapon_DamageWeapon(SithThing* pThing, const SithThing* pPurpetrator, float damage)
{
    J3D_UNUSED(pPurpetrator);
    if ( (pThing->thingInfo.weaponInfo.flags & SITH_WF_DAMAGEDESTROY) != 0 && damage > 1.0f && (!pThing->msecLifeLeft || pThing->msecLifeLeft > 250) )
    {
        pThing->msecLifeLeft = 250;
    }
}

int J3DAPI sithWeapon_ThingCollisionHandler(SithThing* pWeapon, SithThing* pThing, SithCollision* pCollision, int a5)
{
    if ( (pThing->flags & SITH_TF_NOWEAPONCOLLIDE) != 0 )
    {
        return 0;
    }

    if ( (pWeapon->thingInfo.weaponInfo.flags & SITH_WF_MOPHIABOMB) != 0 && pThing->type == SITH_THING_PLAYER )
    {
        if ( pThing->thingInfo.actorInfo.weaponInfo.curWeaponID == SITHWEAPON_MIRROR )
        {
            SithThing* pTemplate = sithTemplate_GetTemplate("+mardukhit");
            if ( pTemplate )
            {
                bool bNoProjectile = true;
                SithThing* pReboundProjectile =NULL;
                if ( sithWeapon_IsAiming(pThing) )
                {
                    pReboundProjectile = sithThing_CreateThing(pTemplate, pWeapon);
                    if ( pReboundProjectile )
                    {
                        rdVector3 projectDir;
                        rdVector_Sub3(&projectDir, &pWeapon->pParent->pos, &pWeapon->pos);
                        rdVector_Normalize3Acc(&projectDir);

                        // Set projectile LVec
                        pReboundProjectile->orient.lvec = projectDir;

                        // Calc projectile RVec
                        rdVector_Cross3(&pReboundProjectile->orient.rvec, &pReboundProjectile->orient.lvec, &pReboundProjectile->orient.uvec);
                        rdVector_Normalize3Acc(&pReboundProjectile->orient.rvec);

                        // Calc projectile UVec
                        rdVector_Cross3(&pReboundProjectile->orient.uvec, &pReboundProjectile->orient.lvec, &pReboundProjectile->orient.rvec);
                        rdVector_Normalize3Acc(&pReboundProjectile->orient.uvec);

                        float wepspeed = rdVector_Len3(&pWeapon->moveInfo.physics.velocity);
                        pReboundProjectile->moveInfo.physics.velocity.x = wepspeed * 0.75f * projectDir.x;
                        pReboundProjectile->moveInfo.physics.velocity.y = wepspeed * 0.75f * projectDir.y;
                        pReboundProjectile->moveInfo.physics.velocity.z = wepspeed * 0.75f * projectDir.z;
                        sithCollision_MoveThing(pReboundProjectile, &projectDir, 0.059999999f, 5);

                        bNoProjectile = false;
                    }
                }
                else
                {
                    rdVector3 wepdir;
                    float wepspeed = rdVector_Normalize3(&wepdir, &pWeapon->moveInfo.physics.velocity);

                    rdVector3 thdir = pThing->orient.lvec;
                    thdir.z += 0.039999999f;
                    rdVector_Normalize3Acc(&thdir);

                    float dot = rdVector_Dot3(&thdir, &wepdir);
                    if ( dot <= -0.73699999f )
                    {
                        pReboundProjectile = sithThing_CreateThing(pTemplate, pWeapon);
                        if ( pReboundProjectile )
                        {
                            float dist = fabsf(2.0f * dot);

                            rdVector3 projectDir;
                            rdVector_ScaleAdd3(&projectDir, &thdir, dist, &wepdir);
                            rdVector_Normalize3Acc(&projectDir);

                            // Calculate new orient
                            pReboundProjectile->orient.lvec = projectDir;

                            rdVector_Cross3(&pReboundProjectile->orient.rvec, &pReboundProjectile->orient.lvec, &pReboundProjectile->orient.uvec);
                            rdVector_Normalize3Acc(&pReboundProjectile->orient.rvec);

                            rdVector_Cross3(&pReboundProjectile->orient.uvec, &pReboundProjectile->orient.lvec, &pReboundProjectile->orient.rvec);
                            rdVector_Normalize3Acc(&pReboundProjectile->orient.uvec);

                            rdVector_Scale3(&pReboundProjectile->moveInfo.physics.velocity, &projectDir, wepspeed * 0.75f);
                            sithCollision_MoveThing(pReboundProjectile, &pThing->orient.lvec, 0.059999999f, 5);

                            sithThing_DamageThing(pThing, pWeapon, 25.0f, SITH_DAMAGE_LIGHTNING);

                            bNoProjectile = false;
                        }
                    }
                }

                if ( !bNoProjectile )
                {
                    pReboundProjectile->pParent = pThing;
                    pReboundProjectile->parentSignature = pThing->signature;
                    tSoundHandle hSnd = sithSound_Load(sithWorld_g_pCurrentWorld, "aet_mr_ball_rebound.wav");
                    if ( hSnd )
                    {
                        sithSoundMixer_PlaySoundThing(hSnd, pReboundProjectile, 1.0f, 0.5f, 2.0f, SOUNDPLAY_PLAYTHINGONCE | SOUNDPLAY_THING_POS);
                    }

                    rdVector_Zero3(&pWeapon->moveDir);
                    sithThing_DestroyThing(pWeapon);
                    return 1;
                }
            }
        }

        if ( sithGetPerformanceLevel() >= 2 )
        {
            tSoundHandle hSnd = sithSound_Load(sithWorld_g_pCurrentWorld, "aet_mr_hit_indy.wav");
            if ( hSnd )
            {
                sithSoundMixer_PlaySoundThing(hSnd, pThing, 1.0f, 0.5f, 2.0f, SOUNDPLAY_PLAYTHINGONCE | SOUNDPLAY_THING_POS);
            }
        }
    }

    if ( (pWeapon->thingInfo.weaponInfo.flags & SITH_WF_PROXIMITY) != 0 )
    {
        pWeapon->thingInfo.weaponInfo.flags &= ~SITH_WF_PROXIMITY;
        pWeapon->thingInfo.weaponInfo.flags |= SITH_WF_EXPLODE;

        pWeapon->collide.size = 0.0f;
        pWeapon->msecLifeLeft = 500;
        sithSoundClass_PlayModeFirst(pWeapon, SITHSOUNDCLASS_ACTIVATE);
        return 0;
    }

    if ( (pWeapon->thingInfo.weaponInfo.flags & SITH_WF_SURFACERICOCHET) != 0 && pThing->type == SITH_THING_COG )
    {
        size_t numRicochets = pWeapon->thingInfo.weaponInfo.numRicochets;
        pWeapon->thingInfo.weaponInfo.numRicochets = numRicochets + 1;
        if ( numRicochets < SITHWEAPON_MAX_PROJECTILE_RICOCHETS )
        {
            sithSoundClass_PlayModeRandom(pWeapon, SITHSOUNDCLASS_DEFLECTED);
        }
        else
        {
            pWeapon->moveInfo.physics.flags &= ~SITH_PF_SURFACEBOUNCE;
            pWeapon->moveInfo.physics.flags |= SITH_PF_FLOORSTICK;
        }
    }

    if ( pThing->type == SITH_THING_ACTOR || pThing->type == SITH_THING_PLAYER )
    {
        if ( pWeapon->thingInfo.weaponInfo.damage == 0.0f && (pWeapon->thingInfo.weaponInfo.flags & (SITH_WF_ATTACHTHING | SITH_WF_THINGHITEXPLODE)) == 0 )
        {
            return 0;
        }

        if ( !sithCollision_ThingCollisionHandler(pWeapon, pThing, pCollision, a5) )
        {
            return 0;
        }

        float damage = sithThing_ScaleCombatDamage(pWeapon, pThing);
        if ( damage != 0.0f )
        {
            sithThing_DamageThing(pThing, pWeapon, damage, pWeapon->thingInfo.weaponInfo.damageType);
        }

        if ( sithThing_GetThingParent(pWeapon) == sithPlayer_g_pLocalPlayerThing
            && sithWeapon_pfActorKilledCallback
            && pThing->thingInfo.actorInfo.health == 0.0f )
        {
            sithWeapon_pfActorKilledCallback(sithGetGameDifficulty());
        }

        if ( (pWeapon->thingInfo.weaponInfo.flags & SITH_WF_THINGHITEXPLODE) != 0 )
        {
            if ( (pWeapon->thingInfo.weaponInfo.flags & SITH_WF_BLOODSPLATTER) != 0
                && (pThing->thingInfo.actorInfo.flags & SITH_AF_DROID) == 0
            // Altered: With J3D_QOL_IMPROVEMENTS the dead-victim and non-player conditions are dropped, so non-killing hits and hits on the player
            //          also splatter and destroy the projectile. The original did so only for a killing hit on a non-player.
            #ifndef J3D_QOL_IMPROVEMENTS
                && pThing->thingInfo.actorInfo.health == 0.0f
                && pThing->type != SITH_THING_PLAYER
            #endif
                && sithWeapon_bGenBloodSplatter
                && sithWeapon_GenBloodSplatterEx(pThing) )

            {
                // Altered: Replaced with new sithWeapon_GenBloodSplatterEx
                //          and added fallback logic if function call fails
                sithThing_DestroyThing(pWeapon);
            }
            else
            {
                sithWeapon_CreateExplosionAtThingCollision(pWeapon, pThing, pCollision); // Altered: Replaced with the new sithWeapon_CreateExplosionAtThingCollision to rotate explosion sprite
            }

            return 1;
        }

        if ( (pWeapon->thingInfo.weaponInfo.flags & SITH_WF_ATTACHTHING) == 0 )
        {
            return 1;
        }

        sithPhysics_ResetThingMovement(pWeapon);
        sithThing_AttachThingToThing(pWeapon, pThing);
        pWeapon->attach.flags |= SITH_ATTACH_NOMOVE;
        pWeapon->moveInfo.physics.flags |= SITH_PF_USEGRAVITY;
        return 1;
    }

    if ( pWeapon->thingInfo.weaponInfo.damage != 0.0f )
    {
        sithThing_DamageThing(pThing, pWeapon, pWeapon->thingInfo.weaponInfo.damage, pWeapon->thingInfo.weaponInfo.damageType);
    }

    if ( (pWeapon->thingInfo.weaponInfo.flags & SITH_WF_FACEHITEXPLODE) != 0 )
    {
        if ( pCollision->pFaceCollided )
        {
            rdVector3 expDir;
            rdMatrix_TransformVector34(&expDir, &pCollision->pFaceCollided->normal, &pThing->orient);
            SithThing* pExplosion = sithWeapon_CreateWeaponExplosion(pWeapon, pWeapon->thingInfo.weaponInfo.pExplosionTemplate, &expDir, /*bRotate=*/1);
            if ( pExplosion && (pExplosion->thingInfo.weaponInfo.flags & SITH_WF_IMPACTSOUND) != 0 )
            {
                pExplosion->thingInfo.explosionInfo.pHitSurfaceMat = pCollision->pFaceCollided->pMaterial;
            }
        }
        else
        {
            // TODO: why rdroid_g_xVector3 if bRotate=0?
            sithWeapon_CreateWeaponExplosion(pWeapon, pWeapon->thingInfo.weaponInfo.pExplosionTemplate, &rdroid_g_xVector3, /*bRotate=*/0);
        }

        return 1;
    }

    if ( (pWeapon->thingInfo.weaponInfo.flags & SITH_WF_ATTACHFACE) != 0 )
    {
        sithPhysics_ResetThingMovement(pWeapon);
        sithSoundClass_StopMode(pWeapon, SITHSOUNDCLASS_CREATE);
        sithSoundClass_PlayModeFirst(pWeapon, SITHSOUNDCLASS_HITHARD);
        pWeapon->collide.movesize = 0.0f;
        if ( pCollision->pFaceCollided && pCollision->pMeshCollided )
        {
            sithThing_AttachThingToThing(pWeapon, pThing);
            sithPhysics_SetThingLook(pWeapon, &pCollision->hitNorm, 0.0f);
            pWeapon->attach.flags |= SITH_ATTACH_NOMOVE;
            pWeapon->moveInfo.physics.flags |= SITH_PF_USEGRAVITY;
        }
        else
        {
            sithThing_AttachThingToThing(pWeapon, pThing);
            sithPhysics_SetThingLook(pWeapon, &pCollision->hitNorm, 0.0f);
            pWeapon->attach.flags           |= SITH_ATTACH_NOMOVE;
            pWeapon->moveInfo.physics.flags |= SITH_PF_USEGRAVITY;
        }

        return 1;
    }

    // Fallback to default collision handler
    return sithCollision_ThingCollisionHandler(pWeapon, pThing, pCollision, a5);
}

int J3DAPI sithWeapon_SurfaceCollisionHandler(SithThing* pThing, SithSurface* pSurf, SithCollision* pStack)
{
    SITH_ASSERTREL(pThing->type == SITH_THING_WEAPON);

    if ( pThing->moveType != SITH_MT_PHYSICS )
    {
        SITHLOG_ERROR("Non-physics weapon %s hit a wall.\n", pThing->aName);
        return 0;
    }

    // Print debug info
    if ( (sithMain_g_sith_mode.debugModeFlags & SITHDEBUG_TRACKSHOTS) != 0 )
    {
        const char* pMatName = "none";
        if ( pSurf->face.pMaterial )
        {
            pMatName = pSurf->face.pMaterial->aName;
        }

        int secIdx  = sithSector_GetSectorIndex(pSurf->pSector);
        int surfIdx = sithSurface_GetSurfaceIndex(pSurf);
        SITHCONSOLE_PRINTF("Weapon hit surface %d, sector %d, material '%s'.\n", surfIdx, secIdx, pMatName);
    }

    // If hit sky surface, destroy projectile
    if ( (pSurf->flags & (SITH_SURFACE_CEILINGSKY | SITH_SURFACE_HORIZONSKY)) != 0 )
    {
        sithThing_DestroyThing(pThing);
        return 1;
    }

    if ( (pThing->thingInfo.weaponInfo.flags & SITH_WF_SURFACERICOCHET) != 0 )
    {
        size_t numRicochets = pThing->thingInfo.weaponInfo.numRicochets;
        pThing->thingInfo.weaponInfo.numRicochets = numRicochets + 1;
        if ( numRicochets < SITHWEAPON_MAX_PROJECTILE_RICOCHETS )
        {
            sithSoundClass_PlayModeRandom(pThing, SITHSOUNDCLASS_DEFLECTED);
        }
        else
        {
            pThing->moveInfo.physics.flags &= ~SITH_PF_SURFACEBOUNCE;
            pThing->moveInfo.physics.flags |= SITH_PF_FLOORSTICK;
        }
    }

    if ( pThing->thingInfo.weaponInfo.damage != 0.0f )
    {
        sithSurface_HandleThingImpact(pSurf, pThing, pThing->thingInfo.weaponInfo.damage, pThing->thingInfo.weaponInfo.damageType);
    }

    if ( (pThing->thingInfo.weaponInfo.flags & SITH_WF_FACEHITEXPLODE) != 0 )
    {
        rdVector3 surfNormal = pSurf->face.normal;
        SithThing* pExplosion = sithWeapon_CreateWeaponExplosion(pThing, pThing->thingInfo.weaponInfo.pExplosionTemplate, &surfNormal, /*bRotate=*/1);
        if ( !pExplosion )
        {
            return 1;
        }

        if ( (pExplosion->thingInfo.explosionInfo.flags & SITH_EF_UPDATEDEBRISMATERIAL) != 0 )
        {
            pExplosion->thingInfo.explosionInfo.pHitSurfaceMat = pSurf->face.pMaterial;
        }

        return 1;
    }

    if ( (pThing->thingInfo.weaponInfo.flags & SITH_WF_ATTACHFACE) != 0 )
    {
        sithCollision_HandleThingHitSurface(pThing, pSurf, pStack);
        sithPhysics_ResetThingMovement(pThing);
        pThing->collide.movesize = 0.0f;

        sithThing_AttachThingToSurface(pThing, pSurf, 0);
        sithPhysics_SetThingLook(pThing, &pSurf->face.normal, 0.0f);
        pThing->moveInfo.physics.flags |= SITH_PF_NOTHRUST;
        return 1;
    }

    // Tallback to default collision handler
    return sithCollision_HandleThingHitSurface(pThing, pSurf, pStack);
}

void J3DAPI sithWeapon_DestroyWeapon(SithThing* pWeapon)
{
    SITH_ASSERTREL(pWeapon && (pWeapon->type == SITH_THING_WEAPON));

    if ( (pWeapon->thingInfo.weaponInfo.flags & SITH_WF_EXPLODE) != 0 )
    {
        rdMaterial* pMaterial = NULL;
        if ( (pWeapon->attach.flags & SITH_ATTACH_SURFACE) != 0 )
        {
            pMaterial = pWeapon->attach.attachedToStructure.pSurfaceAttached->face.pMaterial;
        }

        SithThing* pExplosion = sithWeapon_CreateWeaponExplosion(pWeapon, pWeapon->thingInfo.weaponInfo.pExplosionTemplate, NULL, /*bRotate=*/0);
        if ( pExplosion && pMaterial && (pExplosion->thingInfo.explosionInfo.flags & SITH_EF_UPDATEDEBRISMATERIAL) != 0 )
        {
            pExplosion->thingInfo.explosionInfo.pHitSurfaceMat = pMaterial;
        }
    }
    else
    {
        sithThing_DestroyThing(pWeapon);
    }
}

SithThing* J3DAPI sithWeapon_CreateWeaponExplosion(SithThing* pWeapon, SithThing* pExplosionTemplate, const rdVector3* pDir, int bRotate)
{
    SITH_ASSERT(!bRotate || pDir);
    SITH_ASSERTREL(pWeapon->type == SITH_THING_WEAPON);
    SITH_ASSERTREL(pWeapon->moveType == SITH_MT_PHYSICS);

    if ( (pWeapon->thingInfo.weaponInfo.flags & SITH_WF_EXPLODE) != 0
        && (pWeapon->flags & SITH_TF_SUBMERGED) != 0
        && (pWeapon->moveInfo.physics.flags & SITH_PF_ONWATERSURFACE) == 0 )
    {
        pExplosionTemplate = sithTemplate_GetTemplate("+uw_blast");
    }

    if ( !pExplosionTemplate )
    {
        sithThing_DestroyThing(pWeapon);
        return NULL;
    }

    rdVector3 pos = pWeapon->pos;
    rdMatrix34 orient = rdroid_g_identMatrix34;

    if ( bRotate )
    {
        rdVector3 offset = *pDir;
        rdVector_Scale3Acc(&offset, 0.001f); // offset /1000
        rdVector_Add3Acc(&pos, &offset);

        // SetUVec & LVec
        orient.uvec = rdroid_g_zVector3;
        orient.lvec = *pDir;

        // Calc RVect
        rdVector_Cross3(&orient.rvec, &orient.lvec, &orient.uvec);
    }

    SithThing* pParent    = sithThing_GetThingParent(pWeapon);
    SithThing* pExplosion = sithThing_CreateThingAtPos(pExplosionTemplate, &pos, &orient, pWeapon->pInSector, pParent);
    if ( !pExplosion )
    {
        SITHLOG_ERROR("No thing space to create explosion from weapon %d.\n", pWeapon->idx);
        sithThing_DestroyThing(pWeapon);
        return NULL;
    }

    SithInventoryType* pItem = sithInventory_GetType(pParent->thingInfo.actorInfo.weaponInfo.curWeaponID);
    if ( pItem->pCog )
    {
        sithCog_SendMessage(pItem->pCog, SITHCOG_MSG_CREATED, SITHCOG_SYM_REF_THING, pExplosion->idx, SITHCOG_SYM_REF_NONE, 0, 0);
    }

    if ( pParent == sithPlayer_g_pLocalPlayerThing )
    {
        sithAIAwareness_CreateTransmittingEvent(pExplosion->pInSector, &pExplosion->pos, 0, 1.0f, pParent);
    }

    if ( (pWeapon->flags & SITH_TF_REMOTE) != 0 )
    {
        pExplosion->flags |= SITH_TF_REMOTE;
    }

    sithThing_DestroyThing(pWeapon);
    return pExplosion;
}

float sithWeapon_GetMountWait(void)
{
    return sithWeapon_secMountWait;
}

void J3DAPI sithWeapon_SetMountWait(float secWait)
{
    sithWeapon_secMountWait = sithTime_g_secGameTime + secWait;
}

void J3DAPI sithWeapon_SetFireWait(SithThing* pThing, float waitTime)
{
    SITH_ASSERTREL(pThing && ((pThing->type == SITH_THING_PLAYER) || (pThing->type == SITH_THING_ACTOR)));
    if ( waitTime == SITHWEAPON_TIMENOTSET )
    {
        pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitEndTime = SITHWEAPON_TIMENOTSET;
        pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitTime    = SITHWEAPON_TIMENOTSET; // TODO: Bug? Should this be 0.0f?
    }
    else
    {
        pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitEndTime = sithTime_g_secGameTime + waitTime;
        pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitTime    = waitTime;
    }
}

void J3DAPI sithWeapon_SetAimWait(SithThing* pThing, float waitTime)
{
    SITH_ASSERTREL(pThing && ((pThing->type == SITH_THING_PLAYER) || (pThing->type == SITH_THING_ACTOR)));
    pThing->thingInfo.actorInfo.weaponInfo.secAimWaitEndTime = sithTime_g_secGameTime + waitTime;
}

void J3DAPI sithWeapon_ActivateWeapon(SithThing* pThing, SithCog* pCog, float waitTime)
{
    J3D_UNUSED(pCog);

    pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitTime  = waitTime;
    pThing->thingInfo.actorInfo.weaponInfo.secActivationStartTime = sithTime_g_secGameTime;
    if ( pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitTime > 0.0f )
    {
        pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitEndTime = sithTime_g_secGameTime + pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitTime;
    }
    else
    {
        pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitEndTime = SITHWEAPON_TIMENOTSET;
    }
}

float J3DAPI sithWeapon_DeactivateWeapon(SithThing* pThing)
{
    float waitTime = 0.0f;
    if ( pThing->thingInfo.actorInfo.weaponInfo.secActivationStartTime != SITHWEAPON_TIMENOTSET )
    {
        waitTime = sithTime_g_secGameTime - pThing->thingInfo.actorInfo.weaponInfo.secActivationStartTime;
    }

    pThing->thingInfo.actorInfo.weaponInfo.secActivationStartTime   = SITHWEAPON_TIMENOTSET;
    pThing->thingInfo.actorInfo.weaponInfo.secLastRapidFireTime     = SITHWEAPON_TIMENOTSET;
    pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitEndTime = SITHWEAPON_TIMENOTSET;
    pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitTime    = 0.0f;
    return waitTime;
}

// Added: the whip activation the ACT2 press runs. The quick whip fire and the ACT2 branch both use
// it, so they reset the movement and start the whip in the same order.
static int sithWeapon_ActivateWhipPress(SithThing* pThing, SithInventoryType* pItem, SithWeaponId curWeaponID)
{
    sithPhysics_ResetThingMovement(pThing);
    if ( curWeaponID == SITHWEAPON_WHIP && sithWeapon_ActivateWhip(pThing, pItem->pCog) == 1 )
    {
        sithWeapon_bLocalPlayerAiming          = 0;
        sithWeapon_deactivatedPlayerWeaponID   = SITHWEAPON_NOWEAPONDESELECTED;
        sithWeapon_bufferedWaponKeyId          = -1;
        return 1;
    }

    return 0;
}

// Added: the move statuses of every whip move, the swing and the whole whip climb.
static int sithWeapon_IsWhipMoveStatus(SithThingMoveStatus moveStatus)
{
    return moveStatus == SITHPLAYERMOVE_WHIPSWINGING
        || moveStatus == SITHPLAYERMOVE_WHIPCLIMBIDLE
        || moveStatus == SITHPLAYERMOVE_WHIPCLIMB_START
        || moveStatus == SITHPLAYERMOVE_WHIP_UNK2;
}

// Added: 1 when the quick whip binding (Modern gamepad LB) is pressed this frame.
static int sithWeapon_IsQuickWhipKeyPressed(void)
{
    int keyState = 0;
    return sithControl_GetKey(SITHCONTROL_QUICKWHIP, &keyState) && keyState;
}

// Added: 1 when the whip's own weapon-select key (keyboard 1) is pressed this frame. It keeps its
// old meaning: quick whip at a target, otherwise select the whip.
static int sithWeapon_IsWhipSelectKeyPressed(void)
{
    int keyState = 0;
    for ( SithControlFunction keyId = SITHCONTROL_WSELECT0; (unsigned int)keyId <= SITHCONTROL_WSELECT16; ++keyId )
    {
        keyState = 0;
        if ( sithControl_GetKey(keyId, &keyState) && keyState
            && sithWeapon_GetWeaponForControlKey(keyId) == SITHWEAPON_WHIP )
        {
            return 1;
        }
    }

    return 0;
}

// Added: sends an item script a message the normal weapon state machine sends. The item script is
// what sets the armed mode, the current weapon and the model callbacks.
static void sithWeapon_SendItemMessage(SithThing* pThing, SithWeaponId weaponID, SithCogMsgType messageType,
    SithCogSymbolRefType senderType, int senderIdx)
{
    SithInventoryType* pItem = sithInventory_GetType(weaponID);
    if ( pItem->pCog )
    {
        sithCog_SendMessage(pItem->pCog, messageType, senderType, senderIdx, SITHCOG_SYM_REF_THING, pThing->idx, 0);
    }
}

// Added: stops a weapon-change key when the thing has a puppet. The fixture has none.
static void sithWeapon_StopWeaponMode(SithThing* pThing, SithPuppetSubMode mode)
{
    if ( pThing->pPuppetClass && pThing->renderData.pPuppet )
    {
        sithPuppet_StopMode(pThing, mode, 0.0f);
    }
}

// Added: runs the callback marker a weapon-change key reaches, without playing the key. The item
// script swaps the hand or holster model and releases the capture selected/deselected set up, so
// skipping the key leaves the same script and model state behind.
static void sithWeapon_SendItemKeyMarker(SithThing* pThing, SithWeaponId weaponID, int markerType)
{
    SithInventoryType* pItem = sithInventory_GetType(weaponID);
    if ( pItem->pCog )
    {
        sithCog_SendMessageEx(pItem->pCog, SITHCOG_MSG_CALLBACK, SITHCOG_SYM_REF_THING, pThing->idx,
            SITHCOG_SYM_REF_NONE, 0, 0, 0, markerType, 0, 0);
    }
}

// Added: the thing an item script captured for an attack (the whip actor), if any. The no-target
// whip attack creates it in the script and only leaks it if its retract key is skipped.
static SithThing* sithWeapon_FindCapturedItemThing(const SithCog* pCog, const SithThing* pPlayer)
{
    SithWorld* pWorld = sithWorld_g_pCurrentWorld;
    if ( !pCog || !pWorld )
    {
        return NULL;
    }

    for ( size_t i = 0; i < pWorld->numThings; ++i )
    {
        SithThing* pThing = &pWorld->aThings[i];
        if ( pThing != pPlayer && pThing->type != SITH_THING_FREE && pThing->pCaptureCog == pCog )
        {
            return pThing;
        }
    }

    return NULL;
}

// Added: takes a weapon out of the hand without playing its holster key. The script still sees
// DESELECTED and its put marker, so the armed mode, model and capture state stay consistent.
static void sithWeapon_InstantDeselectWeapon(SithThing* pThing, SithWeaponId nextWeaponID)
{
    const SithWeaponId curWeaponID = pThing->thingInfo.actorInfo.weaponInfo.curWeaponID;
    if ( curWeaponID == SITHWEAPON_NO_WEAPON )
    {
        pThing->thingInfo.actorInfo.weaponInfo.deselectedWeaponID = SITHWEAPON_NOWEAPONDESELECTED;
        return;
    }

    SithInventoryType* pItem = sithInventory_GetType(curWeaponID);

    // A held fire key or a pending release is finished before the weapon changes, as the normal
    // deselect does.
    if ( pThing->type == SITH_THING_PLAYER )
    {
        if ( sithWeapon_bPlayerWeaponActivated == 1 || sithWeapon_deactivatedPlayerWeaponID == curWeaponID )
        {
            if ( pItem->pCog )
            {
                sithCog_SendMessage(pItem->pCog, SITHCOG_MSG_DEACTIVATED, SITHCOG_SYM_REF_NONE, 0, SITHCOG_SYM_REF_THING, pThing->idx, 0);
            }

            sithWeapon_bPlayerWeaponActivated = 0;
        }

        sithWeapon_deactivatedPlayerWeaponID = SITHWEAPON_NOWEAPONDESELECTED;
    }

    sithWeapon_SendItemMessage(pThing, curWeaponID, SITHCOG_MSG_DESELECTED, SITHCOG_SYM_REF_INT, nextWeaponID);

    // Cancel the put key and run its model swap and capture release now.
    sithWeapon_StopWeaponMode(pThing, SITHPUPPETSUBMODE_HOLSTERWEAPON);
    sithWeapon_StopWeaponMode(pThing, SITHPUPPETSUBMODE_DRAWWEAPON);
    sithWeapon_SendItemKeyMarker(pThing, curWeaponID, RDKEYMARKER_INVENTORYPUT);

    // The script set waits for a key that no longer plays.
    sithWeapon_secMountWait = sithTime_g_secGameTime;
    pThing->thingInfo.actorInfo.weaponInfo.secAimWaitEndTime = sithTime_g_secGameTime;

    if ( pThing->type == SITH_THING_PLAYER )
    {
        sithWeapon_bLocalPlayerAiming = 0;
        if ( (pItem->flags & SITHINVENTORY_TYPE_PLAYERWEAPON) != 0 )
        {
            sithWeapon_lastPlayerWeaponID = curWeaponID;
        }

        sithInventory_SetCurrentWeapon(pThing, SITHWEAPON_NO_WEAPON);
    }

    pThing->thingInfo.actorInfo.weaponInfo.curWeaponID        = SITHWEAPON_NO_WEAPON;
    pThing->thingInfo.actorInfo.weaponInfo.deselectedWeaponID = SITHWEAPON_NOWEAPONDESELECTED;
}

// Added: brings a weapon into the hand without playing its draw key. The script still sees
// SELECTED and its pull marker, so the armed mode, model and capture state stay consistent.
static void sithWeapon_InstantSelectWeapon(SithThing* pThing, SithWeaponId weaponID)
{
    if ( weaponID == SITHWEAPON_NO_WEAPON )
    {
        pThing->thingInfo.actorInfo.weaponInfo.selectedWeaponID = SITHWEAPON_NOWEAPONSELECTED;
        return;
    }

    sithWeapon_SendItemMessage(pThing, weaponID, SITHCOG_MSG_SELECTED, SITHCOG_SYM_REF_NONE, 0);

    // Cancel the pull key and run its model swap and capture release now.
    sithWeapon_StopWeaponMode(pThing, SITHPUPPETSUBMODE_DRAWWEAPON);
    sithWeapon_StopWeaponMode(pThing, SITHPUPPETSUBMODE_HOLSTERWEAPON);
    sithWeapon_SendItemKeyMarker(pThing, weaponID, RDKEYMARKER_INVENTORYPULL);

    // The script set waits for a key that no longer plays.
    sithWeapon_secMountWait = sithTime_g_secGameTime;
    pThing->thingInfo.actorInfo.weaponInfo.secAimWaitEndTime = sithTime_g_secGameTime;

    if ( pThing->type == SITH_THING_PLAYER )
    {
        sithInventory_SetCurrentWeapon(pThing, weaponID);
    }

    pThing->thingInfo.actorInfo.weaponInfo.curWeaponID        = weaponID;
    pThing->thingInfo.actorInfo.weaponInfo.selectedWeaponID   = SITHWEAPON_NOWEAPONSELECTED;
    pThing->thingInfo.actorInfo.weaponInfo.deselectedWeaponID = SITHWEAPON_NOWEAPONDESELECTED;
}

// Added: ends a fired no-target whip attack the way releasing the fire key does, then runs the
// retract key's end markers by hand. The quick whip skips the visible retract animation, but the
// script still releases its capture and destroys the whip actor it created.
static void sithWeapon_FinishQuickWhipAttack(SithThing* pThing)
{
    SithInventoryType* pItem = sithInventory_GetType(SITHWEAPON_WHIP);

    // The script's own attack state must be torn down even if the engine already cleared the
    // activated flag (for example when the player became unable to use a weapon).
    if ( pItem->pCog )
    {
        sithCog_SendMessage(pItem->pCog, SITHCOG_MSG_DEACTIVATED, SITHCOG_SYM_REF_NONE, 0, SITHCOG_SYM_REF_THING, pThing->idx, 0);
    }

    SithThing* pWhipThing = sithWeapon_FindCapturedItemThing(pItem->pCog, pThing);
    if ( pWhipThing )
    {
        sithWeapon_StopWeaponMode(pWhipThing, SITHPUPPETSUBMODE_FIRE4);
        sithCog_ThingSendMessageEx(pWhipThing, NULL, SITHCOG_MSG_CALLBACK, 0, RDKEYMARKER_TURNOFF, 0, 0);
    }

    sithWeapon_StopWeaponMode(pThing, SITHPUPPETSUBMODE_FIRE4);
    sithWeapon_StopWeaponMode(pThing, SITHPUPPETSUBMODE_FIRE3);
    sithWeapon_StopWeaponMode(pThing, SITHPUPPETSUBMODE_FIRE);
    sithWeapon_StopWeaponMode(pThing, SITHPUPPETSUBMODE_AIMWEAPON);

    // The retract key's first callback releases the player; do it without the key.
    sithCog_ThingSendMessageEx(pThing, NULL, SITHCOG_MSG_CALLBACK, 0, RDKEYMARKER_ATTACKFINISH, 0, 0);

    sithWeapon_bQuickWhipAttackFired     = 0;
    sithWeapon_bPlayerWeaponActivated    = 0;
    sithWeapon_deactivatedPlayerWeaponID = SITHWEAPON_NOWEAPONDESELECTED;
}

// Added: switches the remembered weapon back, or puts the hands away when they were empty, in the
// same instant way the whip came out: no holster or draw key.
static void sithWeapon_RestoreQuickWhipWeapon(SithThing* pThing)
{
    const SithWeaponId restoreWeaponID = sithWeapon_quickWhipRestoreWeaponID;

    if ( sithWeapon_bQuickWhipAttackFired )
    {
        sithWeapon_FinishQuickWhipAttack(pThing);
    }

    sithWeapon_bPlayerWeaponActivated    = 0;
    sithWeapon_deactivatedPlayerWeaponID = SITHWEAPON_NOWEAPONDESELECTED;
    sithWeapon_bLocalPlayerAiming        = 0;

    if ( restoreWeaponID == SITHWEAPON_NO_WEAPON )
    {
        SITHLOG_STATUS("Quick whip: restoring empty hands at %.3f.\n",
            (double)sithTime_g_secGameTime);
    }
    else
    {
        SITHLOG_STATUS("Quick whip: restoring weapon %d at %.3f.\n",
            restoreWeaponID, (double)sithTime_g_secGameTime);
    }

    sithWeapon_InstantDeselectWeapon(pThing, restoreWeaponID);
    if ( restoreWeaponID != SITHWEAPON_NO_WEAPON )
    {
        sithWeapon_InstantSelectWeapon(pThing, restoreWeaponID);
    }

    sithWeapon_ResetQuickWhipState();
}

// Added: ends an armed quick whip and puts the remembered weapon back.
static void sithWeapon_CancelQuickWhip(SithThing* pThing)
{
    if ( sithWeapon_quickWhipState == SITHWEAPON_QUICKWHIP_IDLE )
    {
        return;
    }

    sithWeapon_RestoreQuickWhipWeapon(pThing);
}

// Added: starts a quick whip. The whip is taken in hand at once, without the draw key or mount
// wait, and then either the normal swing/climb activation runs or one no-target attack is pressed
// on the same tick. The previous weapon is remembered for the instant restore.
static void sithWeapon_BeginQuickWhip(SithThing* pThing, int bTarget)
{
    sithWeapon_quickWhipRestoreWeaponID  = pThing->thingInfo.actorInfo.weaponInfo.curWeaponID;
    sithWeapon_quickWhipState            = SITHWEAPON_QUICKWHIP_ACTIVE;
    sithWeapon_bQuickWhipMoveStarted     = 0;
    sithWeapon_bQuickWhipAttackFired     = 0;
    sithWeapon_secQuickWhipAttackEndTime = 0.0f;

    SITHLOG_STATUS("Quick whip: pressed at %.3f, remembered weapon %d.\n",
        (double)sithTime_g_secGameTime, sithWeapon_quickWhipRestoreWeaponID);

    sithWeapon_InstantDeselectWeapon(pThing, SITHWEAPON_WHIP);
    sithWeapon_InstantSelectWeapon(pThing, SITHWEAPON_WHIP);

    SITHLOG_STATUS("Quick whip: whip is current at %.3f.\n", (double)sithTime_g_secGameTime);

    SithInventoryType* pItem = sithInventory_GetType(SITHWEAPON_WHIP);
    if ( bTarget )
    {
        // Fill the swing/climb globals the drawn whip's aim update normally fills, then run the
        // same activation the Activate key runs. Without a puppet there is no move to start.
        if ( pThing->pPuppetClass && pThing->renderData.pPuppet
            && (sithWhip_SearchWhipSwingThing(pThing) || sithWhip_SearchWhipClimbThing(pThing)) )
        {
            sithWeapon_ActivateWhipPress(pThing, pItem, SITHWEAPON_WHIP);
        }

        sithWeapon_bQuickWhipMoveStarted = sithWeapon_IsWhipMoveStatus(pThing->moveStatus);
        sithWeapon_secQuickWhipDeadline  = sithTime_g_secGameTime + SITHWEAPON_QUICKWHIP_TARGETWAIT;
        SITHLOG_STATUS("Quick whip: started the whip target at %.3f.\n",
            (double)sithTime_g_secGameTime);
        return;
    }

    // No target: one normal whip attack, pressed at once. The script's ACTIVATE sets the attack
    // and fire waits; the restore follows them in game time. The fire key is not held, so the
    // engine's usual single-shot release tears the attack down after the first shot instead of
    // repeating it while the quick whip waits for the animation to end.
    if ( pItem->pCog )
    {
        sithCog_SendMessage(pItem->pCog, SITHCOG_MSG_ACTIVATE, SITHCOG_SYM_REF_NONE, 0, SITHCOG_SYM_REF_THING, pThing->idx, 0);
    }

    sithWeapon_bufferedWaponKeyId              = -1;
    sithWeapon_secWeaponActivationWaitEndTime  = pThing->thingInfo.actorInfo.weaponInfo.secAimWaitEndTime
        + pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitTime;

    sithWeapon_secQuickWhipAttackEndTime = sithWeapon_secWeaponActivationWaitEndTime;
    if ( pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitEndTime != SITHWEAPON_TIMENOTSET
        && pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitEndTime > sithWeapon_secQuickWhipAttackEndTime )
    {
        sithWeapon_secQuickWhipAttackEndTime = pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitEndTime;
    }

    if ( pThing->thingInfo.actorInfo.weaponInfo.secAimWaitEndTime > sithWeapon_secQuickWhipAttackEndTime )
    {
        sithWeapon_secQuickWhipAttackEndTime = pThing->thingInfo.actorInfo.weaponInfo.secAimWaitEndTime;
    }

    sithWeapon_secQuickWhipDeadline  = sithTime_g_secGameTime + SITHWEAPON_QUICKWHIP_ATTACKWAIT;
    sithWeapon_bQuickWhipAttackFired = 1;
    SITHLOG_STATUS("Quick whip: started the whip attack without a target at %.3f.\n",
        (double)sithTime_g_secGameTime);
}

// Added: drives the quick whip follow-up. A target swing or climb holds the whip until its move
// ends; a no-target attack holds it until the script's fire wait is over, then the retract key is
// finished without playing it and the previous weapon is back in the same instant. It claims the
// controls frame so the normal weapon loop cannot fight the swap. The state is not saved.
static int sithWeapon_ProcessQuickWhip(SithThing* pThing)
{
    if ( !sithWeapon_bQuickWhipEnabled )
    {
        sithWeapon_CancelQuickWhip(pThing);
        return 0;
    }

    if ( sithWeapon_quickWhipState == SITHWEAPON_QUICKWHIP_IDLE )
    {
        return 0;
    }

    // A swing or climb owns the player until its move ends, whatever the controls say.
    if ( sithWeapon_IsWhipMoveStatus(pThing->moveStatus) )
    {
        sithWeapon_bQuickWhipMoveStarted = 1;
        return 1;
    }

    // The player or a script put another weapon in the hand; their choice wins, but a fired
    // attack still has to release its script state.
    if ( pThing->thingInfo.actorInfo.weaponInfo.curWeaponID != SITHWEAPON_WHIP )
    {
        if ( sithWeapon_bQuickWhipAttackFired )
        {
            sithWeapon_FinishQuickWhipAttack(pThing);
        }

        sithWeapon_ResetQuickWhipState();
        return 0;
    }

    if ( sithWeapon_bQuickWhipAttackFired )
    {
        // The script extends its fire wait while the attack runs; follow it in game time.
        float attackEndTime = sithWeapon_secQuickWhipAttackEndTime;
        if ( pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitEndTime != SITHWEAPON_TIMENOTSET
            && pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitEndTime > attackEndTime )
        {
            attackEndTime = pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitEndTime;
        }

        if ( pThing->thingInfo.actorInfo.weaponInfo.secAimWaitEndTime > attackEndTime )
        {
            attackEndTime = pThing->thingInfo.actorInfo.weaponInfo.secAimWaitEndTime;
        }

        sithWeapon_secQuickWhipAttackEndTime = attackEndTime;

        if ( sithTime_g_secGameTime < (double)attackEndTime
            && sithTime_g_secGameTime < (double)sithWeapon_secQuickWhipDeadline )
        {
            return 1;
        }

        SITHLOG_STATUS("Quick whip: attack over at %.3f.\n", (double)sithTime_g_secGameTime);
        sithWeapon_RestoreQuickWhipWeapon(pThing);
        return 1;
    }

    // Target case: once a move has started, wait for it to end.
    if ( sithWeapon_bQuickWhipMoveStarted )
    {
        SITHLOG_STATUS("Quick whip: target move over at %.3f.\n", (double)sithTime_g_secGameTime);
        sithWeapon_RestoreQuickWhipWeapon(pThing);
        return 1;
    }

    // The activation did not start a move. Give it the target wait, then restore.
    if ( sithTime_g_secGameTime < (double)sithWeapon_secQuickWhipDeadline )
    {
        return 1;
    }

    SITHLOG_STATUS("Quick whip: target did not start at %.3f.\n", (double)sithTime_g_secGameTime);
    sithWeapon_RestoreQuickWhipWeapon(pThing);
    return 1;
}

int J3DAPI sithWeapon_ProcessWeaponControls(SithThing* pThing, float secDeltaTime)
{
    J3D_UNUSED(secDeltaTime);
    SITH_ASSERTREL(pThing->type == SITH_THING_PLAYER);

    // Added: run the armed quick whip first, so a pending whip can cancel and restore even while
    // another move has turned weapon use off.
    if ( sithWeapon_ProcessQuickWhip(pThing) )
    {
        return 0;
    }

    if ( sithWeapon_IsLocalPlayerUnableToUseWeapon() )
    {
        sithWeapon_bufferedWaponKeyId = -1;
        return 0;
    }

    if ( sithPlayerControls_g_bCutsceneMode == 1 || (pThing->flags & SITH_TF_DYING) != 0 || (pThing->moveInfo.physics.flags & SITH_PF_MINECAR) != 0 )
    {
        if ( sithWeapon_bPlayerWeaponActivated == 1 )
        {
            sithWeapon_DeactivateCurrentWeapon(pThing);
        }

        sithWeapon_bufferedWaponKeyId = -1;
        return 0;
    }

    // Altered: a quick whip press arms the follow-up instead of drawing the whip as a plain select.
    // The quick whip binding works wherever the whip can be used normally; the whip's select key
    // only quick-whips at a swing or climb target and otherwise selects the whip as before.
    if ( sithWeapon_bQuickWhipEnabled )
    {
        const int bQuickWhipKey  = sithWeapon_IsQuickWhipKeyPressed();
        const int bWhipSelectKey = !bQuickWhipKey && sithWeapon_IsWhipSelectKeyPressed();
        if ( bQuickWhipKey || bWhipSelectKey )
        {
            SithThing* pQuickWhipTarget = NULL;
            int bQuickWhipSwing = 0;
            if ( sithWeapon_CanQuickWhip(pThing, &pQuickWhipTarget, &bQuickWhipSwing)
                && (bQuickWhipKey || pQuickWhipTarget != NULL) )
            {
                // Altered: the whip is taken in hand and the attack starts on this press, without
                // the normal select's draw key or mount wait.
                sithWeapon_BeginQuickWhip(pThing, pQuickWhipTarget != NULL);
                return 0;
            }
        }
    }

    SithWeaponId curWeaponID = pThing->thingInfo.actorInfo.weaponInfo.curWeaponID;
    SithInventoryType* pItem = sithInventory_GetType(curWeaponID);
    if ( (pThing->thingInfo.actorInfo.flags & SITH_AF_CONTROLSDISABLED) != 0 && (pItem->flags & SITHINVENTORY_TYPE_UNKNOWN_200) == 0 )
    {
        if ( sithWeapon_bPlayerWeaponActivated == 1 )
        {
            sithWeapon_DeactivateCurrentWeapon(pThing);
        }

        return 0;
    }

    if ( sithWeapon_IsMountingWeapon(pThing) )
    {
        int keyState;
        for ( SithControlFunction keyId = SITHCONTROL_WSELECT0; (unsigned int)keyId <= SITHCONTROL_WSELECT16; ++keyId )
        {
            sithControl_GetKey(keyId, &keyState);
            if ( keyState )
            {
                sithWeapon_bufferedWaponKeyId = keyId;
                return 0;
            }
        }

        if ( curWeaponID > SITHWEAPON_NO_WEAPON
            && sithTime_g_secGameTime >= (double)sithWeapon_secWeaponActivationWaitEndTime
            && (sithControl_GetKey(SITHCONTROL_ACT2, &keyState), keyState)
            && !sithWeapon_bPlayerWeaponActivated )
        {
            sithWeapon_secWeaponActivationWaitEndTime = sithTime_g_secGameTime + 0.5f;
            return 0;
        }

        sithControl_GetKey(SITHCONTROL_WEAPONTOGGLE, &keyState);
        if ( keyState )
        {
            sithWeapon_bufferedWaponKeyId = SITHCONTROL_WEAPONTOGGLE;
            return 0;
        }

        sithControl_GetKey(SITHCONTROL_LIGHTERTOGGLE, &keyState);
        if ( keyState )
        {
            sithWeapon_bufferedWaponKeyId = SITHCONTROL_LIGHTERTOGGLE;
            return 0;
        }

        sithControl_GetKey(SITHCONTROL_NEXTWEAPON, &keyState);
        if ( keyState )
        {
            sithWeapon_bufferedWaponKeyId = SITHCONTROL_NEXTWEAPON;
            return 0;
        }

        sithControl_GetKey(SITHCONTROL_PREVWEAPON, &keyState);
        if ( keyState )
        {
            sithWeapon_bufferedWaponKeyId = SITHCONTROL_PREVWEAPON;
        }

        return 0;
    }

    int keyState;
    for ( SithControlFunction keyId = SITHCONTROL_WSELECT0; keyId <= SITHCONTROL_WSELECT16; ++keyId )
    {
        sithControl_GetKey(keyId, &keyState);
        if ( keyState )
        {
            SithWeaponId weaponID = sithWeapon_GetWeaponForControlKey(keyId);
            sithWeapon_SelectWeapon(pThing, weaponID);
            sithWeapon_bufferedWaponKeyId = -1;
            return 0;
        }
    }

    if ( curWeaponID > SITHWEAPON_NO_WEAPON && sithTime_g_secGameTime >= sithWeapon_secWeaponActivationWaitEndTime )
    {
        if ( sithControl_GetKey(SITHCONTROL_ACT2, &keyState) )
        {
            if ( !sithWeapon_bPlayerWeaponActivated )
            {
                if ( (pItem->flags & SITHINVENTORY_TYPE_UNKNOWN_200) != 0 )
                {
                    if ( pThing->moveStatus > (unsigned int)SITHPLAYERMOVE_RUNNING || pThing->thingInfo.actorInfo.bControlsDisabled == 1 )
                    {
                        sithWeapon_bufferedWaponKeyId = SITHCONTROL_ACT2;
                        return 0;
                    }

                    // Altered: the whip activation is shared with the quick whip fire.
                    // Fixed: [v1.2] sithWeapon_ActivateWhipPress also clears the deactivated player weapon id when the whip is activated, like the buffered key id
                    if ( sithWeapon_ActivateWhipPress(pThing, pItem, curWeaponID) )
                    {
                        return 0;
                    }
                }

                sithWeapon_bPlayerWeaponActivated = 1;
                if ( pItem->pCog )
                {
                    sithCog_SendMessage(pItem->pCog, SITHCOG_MSG_ACTIVATE, SITHCOG_SYM_REF_NONE, 0, SITHCOG_SYM_REF_THING, pThing->idx, 0);
                }

                sithWeapon_bufferedWaponKeyId = -1;
                sithWeapon_secWeaponActivationWaitEndTime = pThing->thingInfo.actorInfo.weaponInfo.secAimWaitEndTime
                    + pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitTime;
            }
        }
        else if ( sithWeapon_bPlayerWeaponActivated == 1 )
        {
            sithWeapon_DeactivateCurrentWeapon(pThing);
        }
    }

    sithControl_GetKey(SITHCONTROL_WEAPONTOGGLE, &keyState);
    if ( keyState )
    {
        if ( curWeaponID )
        {
            sithWeapon_DeselectWeapon(pThing);
        }

        else if ( (pThing->flags & SITH_TF_SUBMERGED) != 0 )
        {
            sithWeapon_SelectWeapon(pThing, SITHWEAPON_MACHETE);
        }
        else
        {
            sithWeapon_SelectWeapon(pThing, sithWeapon_lastPlayerWeaponID);
        }

        sithWeapon_bufferedWaponKeyId = -1;
    }

    sithControl_GetKey(SITHCONTROL_LIGHTERTOGGLE, &keyState);
    if ( keyState )
    {
        if ( curWeaponID == SITHWEAPON_ZIPPO )
        {
            sithWeapon_DeselectWeapon(pThing);
        }
        else
        {
            sithWeapon_SelectWeapon(pThing, SITHWEAPON_ZIPPO);
        }

        sithWeapon_bufferedWaponKeyId = -1;
    }

    sithControl_GetKey(SITHCONTROL_NEXTWEAPON, &keyState);
    if ( keyState )
    {
        sithWeapon_SelectNextWeapon(pThing);
        sithWeapon_bufferedWaponKeyId = -1;
    }

    sithControl_GetKey(SITHCONTROL_PREVWEAPON, &keyState);
    if ( keyState )
    {
        sithWeapon_SelectPreviousWeapon(pThing);
        sithWeapon_bufferedWaponKeyId = -1;
    }

    int bKeyProcessed = sithControl_GetKey(SITHCONTROL_CHALK, &keyState);
    if ( bKeyProcessed )
    {
        sithInventory_SetCurrentItem(pThing, SITHWEAPON_CHALK);
        sithPlayerActions_Activate(pThing);
        sithInventory_SetCurrentItem(pThing, SITHWEAPON_NO_WEAPON);
        sithWeapon_bufferedWaponKeyId = -1;
    }

    if ( sithWeapon_bufferedWaponKeyId != -1 && !sithWeapon_bPlayerWeaponActivated )
    {
        switch ( sithWeapon_bufferedWaponKeyId )
        {
            case SITHCONTROL_LIGHTERTOGGLE:
                if ( curWeaponID == SITHWEAPON_ZIPPO )
                {
                    sithWeapon_DeselectWeapon(pThing);
                }
                else
                {
                    sithWeapon_SelectWeapon(pThing, SITHWEAPON_ZIPPO);
                }
                break;

            case SITHCONTROL_WEAPONTOGGLE:
                if ( curWeaponID <= SITHWEAPON_NO_WEAPON || curWeaponID >= SITHWEAPON_ZIPPO )
                {
                    sithWeapon_SelectWeapon(pThing, sithWeapon_lastPlayerWeaponID);
                }
                else
                {
                    sithWeapon_DeselectWeapon(pThing);
                }

                break;

            case SITHCONTROL_WSELECT0:
            case SITHCONTROL_WSELECT1:
            case SITHCONTROL_WSELECT2:
            case SITHCONTROL_WSELECT3:
            case SITHCONTROL_WSELECT4:
            case SITHCONTROL_WSELECT5:
            case SITHCONTROL_WSELECT6:
            case SITHCONTROL_WSELECT7:
            case SITHCONTROL_WSELECT8:
            case SITHCONTROL_WSELECT9:
            case SITHCONTROL_WSELECT10:
            case SITHCONTROL_WSELECT11:
            case SITHCONTROL_WSELECT12:
            case SITHCONTROL_WSELECT13:
            case SITHCONTROL_WSELECT14:
            case SITHCONTROL_WSELECT15:
            case SITHCONTROL_WSELECT16:
                SithWeaponId weaponID = sithWeapon_GetWeaponForControlKey(sithWeapon_bufferedWaponKeyId);
                sithWeapon_SelectWeapon(pThing, weaponID);
                break;

            case SITHCONTROL_NEXTWEAPON:
                sithWeapon_SelectNextWeapon(pThing);
                break;

            case SITHCONTROL_PREVWEAPON:
                sithWeapon_SelectPreviousWeapon(pThing);
                break;

            default:
                break;
        }

        SITHLOG_DEBUG("Processed buffered weapon key %d.\n", sithWeapon_bufferedWaponKeyId); // Altered: Change log level to debug from status
        sithWeapon_bufferedWaponKeyId = -1;
        return 0;
    }

    return bKeyProcessed;
}

void J3DAPI sithWeapon_DeselectWeapon(SithThing* pThing)
{
    int typeId = pThing->thingInfo.actorInfo.weaponInfo.curWeaponID;
    if ( typeId )
    {
        if ( (sithInventory_GetType(typeId)->flags & SITHINVENTORY_TYPE_WEAPON) != 0 )
        {
            pThing->thingInfo.actorInfo.weaponInfo.deselectedWeaponID = pThing->thingInfo.actorInfo.weaponInfo.curWeaponID;
            pThing->thingInfo.actorInfo.weaponInfo.selectedWeaponID   = SITHWEAPON_NO_WEAPON;
        }
    }
}

int J3DAPI sithWeapon_IsMountingWeapon(const SithThing* pThing)
{
    if ( pThing->type == SITH_THING_PLAYER && sithTime_g_secGameTime < (double)sithWeapon_secMountWait )
    {
        return 1;
    }

    if ( pThing->thingInfo.actorInfo.weaponInfo.selectedWeaponID != SITHWEAPON_NOWEAPONSELECTED )
    {
        return 1;
    }

    if ( sithTime_g_secGameTime < (double)pThing->thingInfo.actorInfo.weaponInfo.secAimWaitEndTime )
    {
        return 1;
    }

    return sithTime_g_secGameTime < (double)pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitEndTime;
}

int J3DAPI sithWeapon_HasWeaponSelected(const SithThing* pThing)
{
    SITH_ASSERTREL(pThing);
    if ( pThing->type == SITH_THING_PLAYER
        && pThing->thingInfo.actorInfo.weaponInfo.curWeaponID >= SITHWEAPON_FISTS
        && pThing->thingInfo.actorInfo.weaponInfo.curWeaponID <= SITHWEAPON_BAZOOKA )
    {
        return 1;
    }

    if ( pThing->type != SITH_THING_ACTOR )
    {
        return 0;
    }

    if ( sithPhysics_IsVehicleThing(pThing) )
    {
        return 0;
    }

    return pThing->thingInfo.actorInfo.weaponInfo.curWeaponID >= SITHWEAPON_COMTOKAREV && pThing->thingInfo.actorInfo.weaponInfo.curWeaponID <= SITHWEAPON_COMSHOTGUN;
}

int J3DAPI sithWeapon_GetAimOrient(rdMatrix34* pOutOrient, SithThing* pShooter, const rdMatrix34* pStartOrient, const rdVector3* pFireOffset, float autoAimFovX, float autoAimFovZ)
{
    SITH_ASSERTREL(pShooter);

    *pOutOrient      = *pStartOrient;
    pOutOrient->dvec = *pFireOffset;

    int curWeaponID = pShooter->thingInfo.actorInfo.weaponInfo.curWeaponID;
    if ( curWeaponID <= SITHWEAPON_WHIP || curWeaponID >= SITHWEAPON_GRENADE || curWeaponID == SITHWEAPON_MACHETE )
    {
        if ( autoAimFovX == 0.0f || autoAimFovZ == 0.0f )
        {
            return 0;
        }

        if ( sithCamera_g_pCurCamera - sithCamera_g_aCameras == 1 )
        {
            autoAimFovX = autoAimFovX * 2.0f;
            autoAimFovZ = autoAimFovZ * 2.0f;
        }

        SithThing* aTarget[16] = { NULL }; // Added init to 0
        size_t numThings = sithAIUtil_GetThingsInView(pShooter->pInSector, pOutOrient, autoAimFovX, autoAimFovZ, STD_ARRAYLEN(aTarget), aTarget, SITHTHING_TYPEMASK(SITH_THING_ACTOR, SITH_THING_PLAYER), 5.0f);// 0x404 - player | actor
        if ( numThings == 0 )
        {
            return 0;
        }

        int targetNum = -1;
        float curDot  = -1.0f;
        for ( size_t i = 0; i < numThings; ++i )
        {
            if ( aTarget[i] && aTarget[i] != pShooter && (aTarget[i]->thingInfo.actorInfo.flags & SITH_AF_NOTARGET) == 0 ) // Fixed: Added check for aTarget[i] != NULL
            {
                if ( sithCollision_CheckThingLOS(pShooter, aTarget[i], 0) )
                {
                    rdVector3 toDargetDir;
                    rdVector_Sub3(&toDargetDir, &aTarget[i]->pos, &pShooter->pos);
                    if ( rdVector_Len3(&toDargetDir) > 0.0f )
                    {
                        rdVector3 curDir = pOutOrient->lvec;
                        rdVector_Normalize3Acc(&toDargetDir);
                        rdVector_Normalize3Acc(&curDir);

                        float dot = fabsf(rdVector_Dot3(&curDir, &toDargetDir));
                        if ( targetNum < 0 || dot >(double)curDot )
                        {
                            curDot    = dot;
                            targetNum = i;
                        }
                    }
                }
            }
        }

        if ( targetNum >= 0 )
        {
            rdMatrix_LookAt(pOutOrient, pFireOffset, &aTarget[targetNum]->pos, 0.0f);
            return 1;
        }

        return 0;
    }

    SithThing* pTarget = sithPlayerControls_GetTargetThing();
    if ( !pTarget )
    {
        return 0;
    }

    if ( (pTarget->thingInfo.actorInfo.flags & SITH_AF_NOTARGET) != 0 )
    {
        return 0;
    }

    rdVector3 targetPos = pTarget->pos;

    float zOffset = pTarget->thingInfo.actorInfo.eyeOffset.z;
    if ( (pTarget->thingInfo.actorInfo.flags & SITH_AF_HUMAN) != 0 )
    {
        zOffset *= 0.25f; // don't aim head
    }
    else if ( curWeaponID == SITHWEAPON_SUBMACHINE || curWeaponID == SITHWEAPON_SHOTGUN )
    {
        zOffset *=  0.5f; // aim at torso
    }

    targetPos.z += zOffset;

    if ( rdVector_Dist3(&targetPos, pFireOffset) > 0.0f )
    {
        rdMatrix_LookAt(pOutOrient, pFireOffset, &targetPos, 0.0f);
        return 1;
    }

    return 0;
}

SithThing* J3DAPI sithWeapon_FireProjectile(SithThing* pShooter, const SithThing* pProjectile, tSoundHandle hFireSnd, SithPuppetSubMode submode, rdVector3* pFireOffset, rdVector3* pAimError, float extra, SithFireProjectileFlags flags, float autoAimFovX, float autoAimFovZ)
{
    SITH_ASSERTREL(pShooter);
    if ( pShooter->type != SITH_THING_ACTOR && pShooter->type != SITH_THING_PLAYER )
    {
        SITHLOG_ERROR("Bad thing in sithWeapon_FireProjectile.\n");
        return 0;
    }

    rdVector3 fireOffset = { 0 }; // Fixed: Init to zero. OG didn't inited and the bullet could be fired in random direction and in worse case caused assertion error in sithCollision system
    if ( pShooter->type == SITH_THING_ACTOR )
    {
        fireOffset = pShooter->thingInfo.actorInfo.weaponInfo.vecUnknown0;
        rdVector3 targetPos = pShooter->controlInfo.aiControl.pLocal->targetPos;
        return sithWeapon_FireProjectileEx(pShooter, pProjectile, hFireSnd, submode, pFireOffset, &targetPos, extra, flags, autoAimFovX, autoAimFovZ, &fireOffset, /*bUseFireOffset*/1);
    }

    // Player from here on

    rdMatrix34 fireOrient = pShooter->orient;
    if ( (flags & SITHFIREPROJECTILE_RIGHTHAND_FIRE) != 0 )
    {
        int jointIdx = sithThing_GetThingJointIndex(pShooter, "inrhand");
        if ( jointIdx == -1 )
        {
            SITHLOG_ERROR("Bad hand node in sithWeapon_FireProjectile.\n");
            return 0;
        }

        fireOrient = pShooter->renderData.paJointMatrices[jointIdx];
        rdVector_Set3(&fireOffset, 0.0f, -90.0f, 0.0f);
        rdMatrix_PreRotate34(&fireOrient, &fireOffset);
    }
    else if ( (flags & SITHFIREPROJECTILE_TORSO_FIRE) != 0 )
    {
        int jointIdx = sithThing_GetThingJointIndex(pShooter, "intorso");
        if ( jointIdx == -1 )
        {
            SITHLOG_ERROR("Bad hand node in sithWeapon_FireProjectile.\n");
            return 0;
        }

        fireOrient = pShooter->renderData.paJointMatrices[jointIdx];
        rdMatrix_PreRotate34(&fireOrient, &fireOffset);
    }

    if ( rdVector_IsZero3(pFireOffset) )
    {
        *pFireOffset = pShooter->pos;
    }

    rdMatrix34 matAimError;
    if ( (flags & SITHFIREPROJECTILE_AIM_ERROR) != 0 )
    {
        if ( !sithWeapon_GetAimOrient(&matAimError, pShooter, &fireOrient, pFireOffset, autoAimFovX, autoAimFovZ)
            && (pAimError->x != 0.0f || pAimError->y != 0.0f || pAimError->z != 0.0f) )
        {
            rdMatrix_PreRotate34(&matAimError, pAimError);
        }
    }
    else
    {
        matAimError = fireOrient;
        if ( pAimError->x != 0.0f || pAimError->y != 0.0f || pAimError->z != 0.0f )
        {
            rdMatrix_PreRotate34(&matAimError, pAimError);
        }
    }

    fireOffset = matAimError.lvec;
    float secDeltaTime = 0.0f;
    if ( (flags & SITHFIREPROJECTILE_RAPID_FIRE) != 0 )
    {
        float numExpectedFires = 1.0f;
        if ( pShooter->thingInfo.actorInfo.weaponInfo.secLastRapidFireTime != SITHWEAPON_TIMENOTSET && pShooter->thingInfo.actorInfo.weaponInfo.secActivationWaitTime > 0.0f )
        {
            numExpectedFires = (sithTime_g_secGameTime - pShooter->thingInfo.actorInfo.weaponInfo.secLastRapidFireTime)
                / pShooter->thingInfo.actorInfo.weaponInfo.secActivationWaitTime
                - 1.0f;
        }

        pShooter->thingInfo.actorInfo.weaponInfo.secLastRapidFireTime = sithTime_g_secGameTime;
        SITH_ASSERTREL(numExpectedFires < 10);
        while ( numExpectedFires > 1.0f )
        {
            numExpectedFires = numExpectedFires - 1.0f;
            secDeltaTime = pShooter->thingInfo.actorInfo.weaponInfo.secActivationWaitTime * numExpectedFires;
            sithWeapon_WeaponFire(pShooter, pProjectile, &fireOffset, pFireOffset, 0, submode, extra, flags, secDeltaTime);
        }
    }
    else
    {
        pShooter->thingInfo.actorInfo.weaponInfo.secLastRapidFireTime = SITHWEAPON_TIMENOTSET;
    }

    return sithWeapon_WeaponFire(pShooter, pProjectile, &fireOffset, pFireOffset, hFireSnd, submode, extra, flags, secDeltaTime);
}

void J3DAPI sithWeapon_DeactivateCurrentWeapon(SithThing* pThing)
{
    SITH_ASSERTREL(pThing && ((pThing->type == SITH_THING_PLAYER) || (pThing->type == SITH_THING_ACTOR)));

    if ( pThing->type == SITH_THING_PLAYER )
    {
        if ( sithWeapon_bPlayerWeaponActivated == 1 )
        {
            sithWeapon_bPlayerWeaponActivated          = 0;
            sithWeapon_deactivatedPlayerWeaponID       = pThing->thingInfo.actorInfo.weaponInfo.curWeaponID;
            sithWeapon_secPlayerWeaponDeactivationTime = pThing->thingInfo.actorInfo.weaponInfo.secActivationWaitEndTime;
        }
    }
    else // Actor
    {
        SithInventoryType* pItem = sithInventory_GetType(pThing->thingInfo.actorInfo.weaponInfo.curWeaponID);
        if ( pItem->pCog )
        {
            sithCog_SendMessage(pItem->pCog, SITHCOG_MSG_DEACTIVATED, SITHCOG_SYM_REF_NONE, 0, SITHCOG_SYM_REF_THING, pThing->idx, 0);
        }
    }
}

int J3DAPI sithWeapon_SelectNextWeapon(SithThing* pThing)
{
    SITH_ASSERTREL(pThing);
    SITH_ASSERTREL(pThing->type == SITH_THING_PLAYER);

    int curWeaponID = sithWeapon_lastPlayerWeaponID;
    if ( pThing->thingInfo.actorInfo.weaponInfo.curWeaponID != SITHWEAPON_NO_WEAPON )
    {
        curWeaponID = pThing->thingInfo.actorInfo.weaponInfo.curWeaponID;
        if ( curWeaponID > sithInventory_FindNextTypeID(pThing, curWeaponID, SITHINVENTORY_TYPE_PLAYERWEAPON) )
        {
            sithWeapon_DeselectWeapon(pThing);
            return 0;
        }
    }

    int weaponID = sithInventory_FindNextTypeID(pThing, curWeaponID, SITHINVENTORY_TYPE_PLAYERWEAPON);
    if ( weaponID == -1 )
    {
        return -1;
    }

    return sithWeapon_SelectWeapon(pThing, (SithWeaponId)weaponID);
}

int J3DAPI sithWeapon_SelectPreviousWeapon(SithThing* pThing)
{
    SITH_ASSERTREL(pThing);
    SITH_ASSERTREL(pThing->type == SITH_THING_PLAYER);

    int curWeaponID = sithWeapon_lastPlayerWeaponID;
    if ( pThing->thingInfo.actorInfo.weaponInfo.curWeaponID != SITHWEAPON_NO_WEAPON )
    {
        curWeaponID = pThing->thingInfo.actorInfo.weaponInfo.curWeaponID;
        if ( curWeaponID < sithInventory_FindPreviousTypeID(pThing, curWeaponID, SITHINVENTORY_TYPE_PLAYERWEAPON) )
        {
            sithWeapon_DeselectWeapon(pThing);
            return 0;
        }
    }

    int weaponId = sithInventory_FindPreviousTypeID(pThing, curWeaponID, SITHINVENTORY_TYPE_PLAYERWEAPON);
    if ( weaponId == -1 )
    {
        return -1;
    }

    return sithWeapon_SelectWeapon(pThing, (SithWeaponId)weaponId);
}

void J3DAPI sithWeapon_SetWeaponModel(SithThing* pThing, SithWeaponId weaponID)
{
    SITH_ASSERTREL(pThing);
    SITH_ASSERTREL((pThing->type == SITH_THING_PLAYER) || (pThing->type == SITH_THING_ACTOR));
    SITH_ASSERTREL(sithInventory_g_aTypes[weaponID].pModel);

    int meshIdx = sithThing_GetThingMeshIndex(pThing, "inrhand");
    if ( meshIdx != -1 )
    {
        pThing->thingInfo.actorInfo.weaponInfo.swapRefNum = sithThing_AddSwapEntry(pThing, meshIdx, sithInventory_g_aTypes[weaponID].pModel, 0);
    }
}

void J3DAPI sithWeapon_ResetWeaponModel(SithThing* pThing)
{
    SITH_ASSERTREL(pThing);
    SITH_ASSERTREL((pThing->type == SITH_THING_PLAYER) || (pThing->type == SITH_THING_ACTOR));
    sithThing_RemoveSwapEntry(pThing, pThing->thingInfo.actorInfo.weaponInfo.swapRefNum);
}

void J3DAPI sithWeapon_SetHolsterModel(SithThing* pThing, SithWeaponId weaponID, signed int meshNum)
{
    SITH_ASSERTREL(pThing);
    SITH_ASSERTREL(sithInventory_g_aTypes[weaponID].pHolsterModel);
    if ( pThing->type == SITH_THING_PLAYER )
    {
        switch ( weaponID )
        {
            case SITHWEAPON_WHIP:
                sithWeapon_playerWhipHolsterSwapRefNum = sithThing_AddSwapEntry(pThing, meshNum, sithInventory_g_aTypes[weaponID].pHolsterModel, /*meshNumSrc=*/0);
                return;

            case SITHWEAPON_PISTOL:
            case SITHWEAPON_TOKAREV:
            case SITHWEAPON_MAUSER:
                sithWeapon_playerPistolHolsterSwapRefNum = sithThing_AddSwapEntry(pThing, meshNum, sithInventory_g_aTypes[weaponID].pHolsterModel, /*meshNumSrc=*/0);
                return;

            case SITHWEAPON_SIMONOV:
            case SITHWEAPON_MACHETE:
            case SITHWEAPON_SUBMACHINE:
            case SITHWEAPON_SHOTGUN:
            case SITHWEAPON_BAZOOKA:
                sithWeapon_playerBackHolsterSwapRefNum = sithThing_AddSwapEntry(pThing, meshNum, sithInventory_g_aTypes[weaponID].pHolsterModel, /*meshNumSrc=*/0);
                return;

            default:
                return;
        }
    }
    else if ( pThing->type == SITH_THING_ACTOR )
    {
        sithThing_AddSwapEntry(pThing, meshNum, sithInventory_g_aTypes[weaponID].pHolsterModel, /*meshNumSrc=*/0);
    }
}

void J3DAPI sithWeapon_ResetHolsterModel(SithThing* pThing, int holsterNum)
{
    SITH_ASSERTREL(pThing);

    if ( holsterNum == SITHWEAPON_HOLSTWEWHIP ) // whipholster
    {
        if ( sithWeapon_playerWhipHolsterSwapRefNum != -1 )
        {
            sithThing_RemoveSwapEntry(pThing, sithWeapon_playerWhipHolsterSwapRefNum);
        }
    }
    else if ( holsterNum == SITHWEAPON_HOLSTERPISTOL ) // pistol holster
    {
        if ( sithWeapon_playerPistolHolsterSwapRefNum != -1 )
        {
            sithThing_RemoveSwapEntry(pThing, sithWeapon_playerPistolHolsterSwapRefNum);
        }
    }
    else if ( holsterNum == SITHWEAPON_HOLSTERBACK && sithWeapon_playerBackHolsterSwapRefNum != -1 ) // back holster
    {
        sithThing_RemoveSwapEntry(pThing, sithWeapon_playerBackHolsterSwapRefNum);
    }
}

bool sithWeapon_IsBloodSplatterEnabled(void)
{
    return sithWeapon_bGenBloodSplatter;
}

void J3DAPI sithWeapon_EnableBloodSplatter(bool bEnable)
{
    if ( sithWeapon_bGenBloodSplatter != bEnable )
    {
        sithWeapon_bGenBloodSplatter = bEnable;
        stdConfig_SetBool(SITHWEAPON_CFG_GAMEPLAY_BLOODSPLATTER, bEnable); // Added
    }
}

void J3DAPI sithWeapon_SendMessageAim(SithThing* pThing, int bAim)
{
    // TODO: COG messages could be sent via thing cog overloads

    int curWeaponID = pThing->thingInfo.actorInfo.weaponInfo.curWeaponID;
    SithInventoryType* pWaponItem = sithInventory_GetType(curWeaponID);
    if ( !pWaponItem || !pWaponItem->pCog )
    {
        return;
    }

    if ( pThing->type == SITH_THING_PLAYER )
    {
        // Fixed: Added sithPlayer_g_pLocalPlayerThing null check.
        // This is just quick & dirty fix for sithWeapon_IsLocalPlayerUnableToUseWeapon,
        // required when system is closing and indy is still aiming with the whip at whippable thing
        if ( !sithWeapon_IsMountingWeapon(pThing) && sithPlayer_g_pLocalPlayerThing && !sithWeapon_IsLocalPlayerUnableToUseWeapon() )
        {
            if ( bAim )
            {
                if ( !sithWeapon_bLocalPlayerAiming )
                {
                    sithCog_SendMessageEx(pWaponItem->pCog, SITHCOG_MSG_AIM, SITHCOG_SYM_REF_NONE, 0, SITHCOG_SYM_REF_THING, pThing->idx, 0, 1, 0, 0, 0);
                    sithWeapon_bLocalPlayerAiming = 1;
                }
            }
            else if ( pThing->thingInfo.actorInfo.weaponInfo.curWeaponID != SITHWEAPON_WHIP || !sithWhip_GetWhipSwingThing() && !sithWhip_GetWhipClimbThing() )
            {
                if ( sithWeapon_bLocalPlayerAiming )
                {
                    sithWeapon_bLocalPlayerAiming = 0;
                }

                if ( sithPuppet_IsModeOnTrack(pThing, SITHPUPPETSUBMODE_AIMWEAPON) )
                {
                    sithCog_SendMessageEx(pWaponItem->pCog, SITHCOG_MSG_AIM, SITHCOG_SYM_REF_NONE, 0, SITHCOG_SYM_REF_THING, pThing->idx, 0, 0, 0, 0, 0);
                }
            }
        }
    }
    else if ( pThing->type == SITH_THING_ACTOR )
    {
        if ( bAim )
        {
            if ( !sithWeapon_IsMountingWeapon(pThing) )
            {
                sithCog_SendMessageEx(pWaponItem->pCog, SITHCOG_MSG_AIM, SITHCOG_SYM_REF_NONE, 0, SITHCOG_SYM_REF_THING, pThing->idx, 0, 1, 0, 0, 0);
            }
        }
        else
        {
            if ( sithWeapon_IsMountingWeapon(pThing) )
            {
                if ( curWeaponID >= SITHWEAPON_COMFISTS && curWeaponID <= SITHWEAPON_COMSHOTGUN )
                {
                    sithWeapon_DeactivateCurrentWeapon(pThing);
                }
                else
                {
                    sithWeapon_DeactivateWeapon(pThing);
                }
            }

            sithCog_SendMessageEx(pWaponItem->pCog, SITHCOG_MSG_AIM, SITHCOG_SYM_REF_NONE, 0, SITHCOG_SYM_REF_THING, pThing->idx, 0, 0, 0, 0, 0);
        }
    }
}

void J3DAPI sithWeapon_SetActorKilledCallback(SithWeaponActorKilledCallback pfCallback)
{
    sithWeapon_pfActorKilledCallback = pfCallback;
}

int J3DAPI sithWeapon_IsAiming(SithThing* pThing)
{
    SITH_ASSERTREL(pThing);

    if ( pThing->type == SITH_THING_PLAYER )
    {
        return sithWeapon_bLocalPlayerAiming;
    }

    if ( pThing->type != SITH_THING_ACTOR )
    {
        return 0;
    }

    bool bWeaponSelected = pThing->thingInfo.actorInfo.weaponInfo.curWeaponID >= SITHWEAPON_COMTOKAREV && pThing->thingInfo.actorInfo.weaponInfo.curWeaponID <= SITHWEAPON_COMSHOTGUN;
    if ( bWeaponSelected && sithPuppet_IsModeOnTrack(pThing, SITHPUPPETSUBMODE_AIMWEAPON) )
    {
        return 1;
    }

    return 0;
}

void J3DAPI sithWeapon_GenBloodsplort(SithThing* pHitThing)
{
    sithWeapon_GenBloodSplatterEx(pHitThing);
}

static bool J3DAPI sithWeapon_GenBloodSplatterEx(SithThing* pHitThing)
{
    SITH_ASSERTREL(pHitThing != NULL);
    SITH_ASSERTREL(pHitThing->renderData.data.pModel3 != NULL);
    SITH_ASSERTREL((pHitThing->type == SITH_THING_ACTOR) || (pHitThing->type == SITH_THING_CORPSE) || (pHitThing->type == SITH_THING_PLAYER));

    SithThing* pTemplate = NULL;
    rdMaterial* pMat = NULL;
    if ( (pHitThing->thingInfo.actorInfo.flags & SITH_AF_ARACHNID) != 0 )
    {
        pTemplate = sithTemplate_GetTemplate("+vulcansplort");
        pMat = sithMaterial_GetMaterialByIndex(SITHWORLD_STATICINDEX(420)); // gen_a4sprite_blood_grn.mat
    }
    else
    {
        pTemplate = sithTemplate_GetTemplate("+bloodsplort");
        pMat = sithMaterial_GetMaterialByIndex(SITHWORLD_STATICINDEX(179)); // gen_a4sprite_blood.mat
    }

    if ( !pTemplate )
    {
        return false;
    }

    SithThing* pSprite = sithThing_CreateThingAtPos(pTemplate, &pHitThing->pos, &pHitThing->orient, pHitThing->pInSector, NULL);
    if ( !pSprite )
    {
        return false;
    }

    int meshIdx = sithThing_GetThingMeshIndex(pHitThing, "intorso");
    if ( meshIdx == -1 )
    {
        if ( strneq(pHitThing->aName, "snak", 4u) )
        {
            meshIdx = sithThing_GetThingMeshIndex(pHitThing, "snhead");
        }
        else if ( strneq(pHitThing->aName, "spid", 4u) )
        {
            meshIdx = sithThing_GetThingMeshIndex(pHitThing, "spbelly");
        }
        else if ( strneq(pHitThing->aName, "scor", 4u) )
        {
            meshIdx = sithThing_GetThingMeshIndex(pHitThing, "scsect1");
        }
        else if ( strneq(pHitThing->aName, "komo", 4u) )
        {
            meshIdx = sithThing_GetThingMeshIndex(pHitThing, "lineck");
        }
        else if ( strneq(pHitThing->aName, "cent", 4u) )
        {
            meshIdx = sithThing_GetThingMeshIndex(pHitThing, "cphead");
        }

        if ( meshIdx == -1 )
        {
            // Altered: Added qol to not gen splatter for special actor things
        #ifdef J3D_QOL_IMPROVEMENTS
            if ( (pHitThing->flags & (SITH_TF_METAL | SITH_TF_EARTH | SITH_TF_SNOW | SITH_TF_WOOD)) != 0
                || sithPhysics_IsVehicleThing(pHitThing) )
            {
                // We have either vehicle that has no human exposed to outside of vehicle
                // or some other (non-human) actor
                // Destroy the sprite created above, which is not attached here
                sithThing_DestroyThing(pSprite);
                return false;
            }
        #endif

            meshIdx = 0;
        }

    #if defined(J3D_DEBUG) || !defined(J3D_QOL_IMPROVEMENTS) // Altered: debug check
        SITHLOG_DEBUG("Blood splort attached to mesh number %d.\n", meshIdx); // Altered log level to debug from status
    #endif
    }

    sithThing_AttachThingToThingMesh(pSprite, pHitThing, meshIdx);
    if ( pMat )
    {
        sithAnimate_StartMaterialAnim(pMat, 16.0f, (SithAnimateFlags)0);
    }

    float size = pHitThing->renderData.data.pModel3->size / J3D_QOL_VALUE(5.0f, 2.0f);    // Altered
    size = STDMATH_CLAMP(size, J3D_QOL_VALUE(0.001f, 0.01f), J3D_QOL_VALUE(0.02f, 0.1f)); // Altered

    rdVector3 start;
    start.x = size;
    start.y = size;
    start.z = 1.0f;

    rdVector3 end;
    size = size * 2.0f;
    end.x = size;
    end.y = size;
    end.z = J3D_QOL_VALUE(0.8f, 1.0f); // Altered: End alpha is 0.8, the original uses 1.0
    sithAnimate_StartAnimateSpriteSize(pSprite, &start, &end, 0.5f);

    return true;
}

SithWeaponId sithWeapon_GetLastWeapon(void)
{
    return sithWeapon_lastPlayerWeaponID;
}

void J3DAPI sithWeapon_SetLastWeapon(SithWeaponId weaponNum)
{
    SITH_ASSERTREL((weaponNum >= SITHWEAPON_NO_WEAPON) && (weaponNum <= SITHWEAPON_IMP5));
    sithWeapon_lastPlayerWeaponID = weaponNum;
}

float J3DAPI sithWeapon_GetWeaponMaxAimDistance(SithWeaponId weaponId)
{
    if ( weaponId >= SITHWEAPON_NO_WEAPON && weaponId <= SITHWEAPON_BAZOOKA )
    {
        return sithWeapon_aMaxAimDistances[weaponId];
    }

    if ( weaponId >= SITHWEAPON_COMFISTS && weaponId <= SITHWEAPON_COMSHOTGUN )
    {
        return sithWeapon_aMaxAimDistances[weaponId];
    }

    if ( weaponId == SITHWEAPON_MIRROR )
    {
        return 4.0f;
    }

    return 0.0f;
}

float J3DAPI sithWeapon_GetWeaponCollideSize(SithWeaponId typeId)
{
    if ( typeId < SITHWEAPON_COMTOKAREV )
    {
        return 0.0f;
    }

    if ( typeId <= SITHWEAPON_COMSUBMACHINE )
    {
        return 0.02f;
    }

    if ( typeId == SITHWEAPON_COMSHOTGUN )
    {
        return 0.065f;
    }

    return 0.0f;
}

int sithWeapon_IsLocalPlayerUnableToUseWeapon(void)
{
    // TODO: Why sithPlayer_g_pLocalPlayerThing is used? Verify if player thing could be passed instead to this function
    SITH_ASSERTREL(sithPlayer_g_pLocalPlayerThing);
    if ( sithPuppet_IsModeOnTrack(sithPlayer_g_pLocalPlayerThing, SITHPUPPETSUBMODE_PUSHPULLREADY) )
    {
        return 1;
    }

    switch ( sithPlayer_g_pLocalPlayerThing->moveStatus )
    {
        case SITHPLAYERMOVE_STILL:
        case SITHPLAYERMOVE_WALKING:
        case SITHPLAYERMOVE_RUNNING:
        case SITHPLAYERMOVE_SWIMIDLE:
        case SITHPLAYERMOVE_WALK2STAND:
        case SITHPLAYERMOVE_STAND2WALK:
        case SITHPLAYERMOVE_UNKNOWN_78:
        case SITHPLAYERMOVE_STAND2RUN:
        case SITHPLAYERMOVE_UNKNOWN_80:
        case SITHPLAYERMOVE_UNKNOWN_81:
            return 0;
        default:
            break;
    }

    return 1;
}

int J3DAPI sithWeapon_Save(tFileHandle fh)
{
    J3D_ASSERT_SIZE32(sizeof(sithWeapon_bPlayerWeaponActivated) == 4, "sizeof(sithWeapon_bPlayerWeaponActivated) == 4");
    J3D_ASSERT_SIZE32(sizeof(sithWeapon_bLocalPlayerAiming) == 4, "sizeof(sithWeapon_bLocalPlayerAiming) == 4");
    J3D_ASSERT_SIZE32(sizeof(sithWeapon_lastPlayerWeaponID) == 4, "sizeof(sithWeapon_lastPlayerWeaponID) == 4");
    J3D_ASSERT_SIZE32(sizeof(sithWeapon_bufferedWaponKeyId) == 4, "sizeof(sithWeapon_bufferedWaponKeyId) == 4");
    J3D_ASSERT_SIZE32(sizeof(sithWeapon_deactivatedPlayerWeaponID) == 4, "sizeof(sithWeapon_deactivatedPlayerWeaponID) == 4");
    J3D_ASSERT_SIZE32(sizeof(sithWeapon_playerWhipHolsterSwapRefNum) == 4, "sizeof(sithWeapon_playerWhipHolsterSwapRefNum) == 4");
    J3D_ASSERT_SIZE32(sizeof(sithWeapon_playerPistolHolsterSwapRefNum) == 4, "sizeof(sithWeapon_playerPistolHolsterSwapRefNum) == 4");
    J3D_ASSERT_SIZE32(sizeof(sithWeapon_playerBackHolsterSwapRefNum) == 4, "sizeof(sithWeapon_playerBackHolsterSwapRefNum) == 4");
    J3D_ASSERT_SIZE32(sizeof(sithWeapon_secMountWait) == 4, "sizeof(sithWeapon_secMountWait) == 4");
    J3D_ASSERT_SIZE32(sizeof(sithWeapon_secPlayerWeaponDeactivationTime) == 4, "sizeof(sithWeapon_secPlayerWeaponDeactivationTime) == 4");

    return sith_g_pHS->pFileWrite(fh, &sithWeapon_bPlayerWeaponActivated, sizeof(sithWeapon_bPlayerWeaponActivated)) != sizeof(sithWeapon_bPlayerWeaponActivated)
        || sith_g_pHS->pFileWrite(fh, &sithWeapon_bLocalPlayerAiming, sizeof(sithWeapon_bLocalPlayerAiming)) != sizeof(sithWeapon_bLocalPlayerAiming)
        || sith_g_pHS->pFileWrite(fh, &sithWeapon_lastPlayerWeaponID, sizeof(sithWeapon_lastPlayerWeaponID)) != sizeof(sithWeapon_lastPlayerWeaponID)
        || sith_g_pHS->pFileWrite(fh, &sithWeapon_bufferedWaponKeyId, sizeof(sithWeapon_bufferedWaponKeyId)) != sizeof(sithWeapon_bufferedWaponKeyId)
        || sith_g_pHS->pFileWrite(fh, &sithWeapon_deactivatedPlayerWeaponID, sizeof(sithWeapon_deactivatedPlayerWeaponID)) != sizeof(sithWeapon_deactivatedPlayerWeaponID)
        || sith_g_pHS->pFileWrite(fh, &sithWeapon_playerWhipHolsterSwapRefNum, sizeof(sithWeapon_playerWhipHolsterSwapRefNum)) != sizeof(sithWeapon_playerWhipHolsterSwapRefNum)
        || sith_g_pHS->pFileWrite(fh, &sithWeapon_playerPistolHolsterSwapRefNum, sizeof(sithWeapon_playerPistolHolsterSwapRefNum)) != sizeof(sithWeapon_playerPistolHolsterSwapRefNum)
        || sith_g_pHS->pFileWrite(fh, &sithWeapon_playerBackHolsterSwapRefNum, sizeof(sithWeapon_playerBackHolsterSwapRefNum)) != sizeof(sithWeapon_playerBackHolsterSwapRefNum)
        || sith_g_pHS->pFileWrite(fh, &sithWeapon_secMountWait, sizeof(sithWeapon_secMountWait)) != sizeof(sithWeapon_secMountWait)
        || sith_g_pHS->pFileWrite(fh, &sithWeapon_secPlayerWeaponDeactivationTime, sizeof(sithWeapon_secPlayerWeaponDeactivationTime)) != sizeof(sithWeapon_secPlayerWeaponDeactivationTime);
}

int J3DAPI sithWeapon_Restore(tFileHandle fh)
{
    return sith_g_pHS->pFileRead(fh, &sithWeapon_bPlayerWeaponActivated, sizeof(sithWeapon_bPlayerWeaponActivated)) != sizeof(sithWeapon_bPlayerWeaponActivated)
        || sith_g_pHS->pFileRead(fh, &sithWeapon_bLocalPlayerAiming, sizeof(sithWeapon_bLocalPlayerAiming)) != sizeof(sithWeapon_bLocalPlayerAiming)
        || sith_g_pHS->pFileRead(fh, &sithWeapon_lastPlayerWeaponID, sizeof(sithWeapon_lastPlayerWeaponID)) != sizeof(sithWeapon_lastPlayerWeaponID)
        || sith_g_pHS->pFileRead(fh, &sithWeapon_bufferedWaponKeyId, sizeof(sithWeapon_bufferedWaponKeyId)) != sizeof(sithWeapon_bufferedWaponKeyId)
        || sith_g_pHS->pFileRead(fh, &sithWeapon_deactivatedPlayerWeaponID, sizeof(sithWeapon_deactivatedPlayerWeaponID)) != sizeof(sithWeapon_deactivatedPlayerWeaponID)
        || sith_g_pHS->pFileRead(fh, &sithWeapon_playerWhipHolsterSwapRefNum, sizeof(sithWeapon_playerWhipHolsterSwapRefNum)) != sizeof(sithWeapon_playerWhipHolsterSwapRefNum)
        || sith_g_pHS->pFileRead(fh, &sithWeapon_playerPistolHolsterSwapRefNum, sizeof(sithWeapon_playerPistolHolsterSwapRefNum)) != sizeof(sithWeapon_playerPistolHolsterSwapRefNum)
        || sith_g_pHS->pFileRead(fh, &sithWeapon_playerBackHolsterSwapRefNum, sizeof(sithWeapon_playerBackHolsterSwapRefNum)) != sizeof(sithWeapon_playerBackHolsterSwapRefNum)
        || sith_g_pHS->pFileRead(fh, &sithWeapon_secMountWait, sizeof(sithWeapon_secMountWait)) != sizeof(sithWeapon_secMountWait)
        || sith_g_pHS->pFileRead(fh, &sithWeapon_secPlayerWeaponDeactivationTime, sizeof(sithWeapon_secPlayerWeaponDeactivationTime)) != sizeof(sithWeapon_secPlayerWeaponDeactivationTime);
}

SithWeaponId J3DAPI sithWeapon_GetWeaponForControlKey(SithControlFunction keyID)
{
    switch ( keyID )
    {
        case SITHCONTROL_WSELECT0:
            return SITHWEAPON_FISTS;

        case SITHCONTROL_WSELECT1:
            return SITHWEAPON_WHIP;

        case SITHCONTROL_WSELECT2:
            return SITHWEAPON_PISTOL;

        case SITHCONTROL_WSELECT3:
            return SITHWEAPON_TOKAREV;

        case SITHCONTROL_WSELECT4:
            return SITHWEAPON_MAUSER;

        case SITHCONTROL_WSELECT5:
            return SITHWEAPON_SIMONOV;

        case SITHCONTROL_WSELECT6:
            return SITHWEAPON_MACHETE;

        case SITHCONTROL_WSELECT7:
            return SITHWEAPON_SUBMACHINE;

        case SITHCONTROL_WSELECT8:
            return SITHWEAPON_SHOTGUN;

        case SITHCONTROL_WSELECT9:
            return SITHWEAPON_BAZOOKA;

        case SITHCONTROL_WSELECT10:
            return SITHWEAPON_GRENADE;

        case SITHCONTROL_WSELECT11:
            return SITHWEAPON_SATCHEL;

        case SITHCONTROL_WSELECT12:
            return SITHWEAPON_IMP1;

        case SITHCONTROL_WSELECT13:
            return SITHWEAPON_IMP2;

        case SITHCONTROL_WSELECT14:
            return SITHWEAPON_IMP3;

        case SITHCONTROL_WSELECT15:
            return SITHWEAPON_IMP4;

        case SITHCONTROL_WSELECT16:
            return SITHWEAPON_IMP5;

        default:
            break;
    }

    return -1;
}

SithThing* J3DAPI sithWeapon_FireProjectileEx(SithThing* pShooter, const SithThing* pProjectile, tSoundHandle hSndFire, SithPuppetSubMode submode, rdVector3* pFirePos, const rdVector3* pTargetPos, float extra, SithFireProjectileFlags flags, float autoAimFovX, float autoAimFovZ, const rdVector3* pFireOffset, int bUseFireOffset)
{
    J3D_UNUSED(autoAimFovX);
    J3D_UNUSED(autoAimFovZ);
    SITH_ASSERTREL(pShooter != NULL);

    if ( pShooter->type != SITH_THING_ACTOR )
    {
        SITHLOG_ERROR("Bad thing in sithWeapon_FireProjectileEx.\n");
        return 0;
    }

    const SithAIControlBlock* pLocal = pShooter->controlInfo.aiControl.pLocal;
    if ( pShooter->controlType == SITH_CT_AI
        && rdVector_Dot3(&pLocal->toTarget, &pShooter->orient.lvec) < pLocal->fireDot )
    {
        return 0;
    }

    if ( rdVector_IsZero3(pFirePos) )
    {
        *pFirePos = pShooter->pos;
    }

    rdVector3 fireDir = pShooter->orient.lvec;
    if ( bUseFireOffset == 1 )
    {
        if ( !rdVector_IsZero3(&pShooter->thingInfo.actorInfo.fireOffset) )
        {
            rdVector3 shooterFireOffset = pShooter->thingInfo.actorInfo.fireOffset;
            rdMatrix_TransformVector34Acc(&shooterFireOffset, &pShooter->orient);
            rdVector_Add3Acc(pFirePos, &shooterFireOffset);
        }

        rdVector_Sub3(&fireDir, pTargetPos, pFirePos);
        rdVector_Add3Acc(&fireDir, pFireOffset);
        rdVector_Normalize3Acc(&fireDir);
    }

    float secDeltaTime = 0.0f;
    if ( (flags & SITHFIREPROJECTILE_RAPID_FIRE) != 0 )
    {
        float numExpectedFires = 1.0f;
        if ( pShooter->thingInfo.actorInfo.weaponInfo.secLastRapidFireTime != SITHWEAPON_TIMENOTSET && pShooter->thingInfo.actorInfo.weaponInfo.secActivationWaitTime > 0.0f )
        {
            numExpectedFires = (sithTime_g_secGameTime - pShooter->thingInfo.actorInfo.weaponInfo.secLastRapidFireTime)
                / pShooter->thingInfo.actorInfo.weaponInfo.secActivationWaitTime
                - 1.0f;
        }

        pShooter->thingInfo.actorInfo.weaponInfo.secLastRapidFireTime = sithTime_g_secGameTime;
        SITH_ASSERTREL(numExpectedFires < 10);
        while ( numExpectedFires > 1.0f )
        {
            numExpectedFires = numExpectedFires - 1.0f;
            secDeltaTime     = pShooter->thingInfo.actorInfo.weaponInfo.secActivationWaitTime * numExpectedFires;
            sithWeapon_WeaponFire(pShooter, pProjectile, &fireDir, pFirePos, 0, submode, extra, flags, secDeltaTime);
        }
    }
    else
    {
        pShooter->thingInfo.actorInfo.weaponInfo.secLastRapidFireTime = SITHWEAPON_TIMENOTSET;
    }

    return sithWeapon_WeaponFire(pShooter, pProjectile, &fireDir, pFirePos, hSndFire, submode, extra, flags, secDeltaTime);
}

void J3DAPI sithWeapon_CreateWeaponFireFx(SithThing* pThing, rdVector3* pos)
{
    SITH_ASSERTREL(pThing != NULL);
    SITH_ASSERTREL((pThing->type == SITH_THING_PLAYER) || (pThing->type == SITH_THING_ACTOR));

    rdModel3* pHandModel = NULL;
    switch ( pThing->thingInfo.actorInfo.weaponInfo.curWeaponID )
    {
        case SITHWEAPON_PISTOL:
            pHandModel = sithModel_GetModelByIndex(SITHWORLD_STATICINDEX(83u)); // weap_revolver_fire.3do
            break;

        case SITHWEAPON_TOKAREV:
        case SITHWEAPON_COMTOKAREV:
            pHandModel = sithModel_GetModelByIndex(SITHWORLD_STATICINDEX(85u)); // weap_tokarev_fire.3do
            break;

        case SITHWEAPON_MAUSER:
        case SITHWEAPON_COMMAUSER:
            pHandModel = sithModel_GetModelByIndex(SITHWORLD_STATICINDEX(81u)); // weap_mauser_fire.3do
            break;

        case SITHWEAPON_SIMONOV:
        case SITHWEAPON_COMSIMONOV:
            pHandModel = sithModel_GetModelByIndex(SITHWORLD_STATICINDEX(84u)); // weap_simonov_fire.3do
            break;

        case SITHWEAPON_SUBMACHINE:
        case SITHWEAPON_COMSUBMACHINE:
            pHandModel = sithModel_GetModelByIndex(SITHWORLD_STATICINDEX(82u)); // weap_ppsh41_fire.3do
            break;

        case SITHWEAPON_SHOTGUN:
        case SITHWEAPON_COMSHOTGUN:
            pHandModel = sithModel_GetModelByIndex(SITHWORLD_STATICINDEX(86u)); // weap_toz34_fire.3do
            break;

        default:
            return;
    }

    int meshIdx = sithThing_GetThingMeshIndex(pThing, "inrhand");
    if ( pHandModel && meshIdx != -1 )
    {
        pThing->thingInfo.actorInfo.weaponInfo.swapRefNum  = sithThing_AddSwapEntry(pThing, meshIdx, pHandModel, 0);
        pThing->thingInfo.actorInfo.weaponInfo.secSwapTime = sithTime_g_secGameTime + 0.025f;

        // Altered: Added check for flash fx enabled and ambient light intensity
        if ( sithWeapon_bProjectileFireFlashFx
            && rdLight_GetIntensity(&pThing->pInSector->ambientLight) < sithWeapon_projectileFireFlashAmbThreshold )
        {
            SithThing* pGhostTpl = sithTemplate_GetTemplate("ghost");
            if ( pGhostTpl ) // Fixed: Added null check
            {
                SithThing* pFlash = sithThing_CreateThingAtPos(pGhostTpl, pos, &pThing->orient, pThing->pInSector, pThing->pParent);
                if ( pFlash )
                {
                    pFlash->flags |= SITH_TF_EMITLIGHT;
                    pFlash->msecLifeLeft = 50;

                    pFlash->light.color.red   = 0.5f;
                    pFlash->light.color.green = 0.5f;
                    pFlash->light.color.blue  = 0.5f;
                    pFlash->light.color.alpha = 0.2f; // Fixed: Assign alpha/range for the flesh effect to appear.
                                                      //        OG didn't assign alpha range value and the flash light wasn't visible

                    pFlash->light.minRadius   = 0.2f;
                    pFlash->light.maxRadius   = 0.40000001f;
                }
            }
        }
    }
}

// Added: the target the whip activation press would act on and whether that action is a swing
// (1) or a climb (0). Returns 1 when the target allows the action. The press and the interaction
// query share it, so they cannot disagree. A target that refuses the action is still returned
// because the press sends its damage message before it stops.
static int sithWeapon_GetWhipPressTarget(SithThing** ppTarget, int* pbSwing)
{
    // Altered: the swing-before-climb choice and flag rule are shared with the quick whip query.
    return sithWhip_ChooseWhipPressTarget(sithWhip_GetWhipSwingThing(), sithWhip_GetWhipClimbThing(), ppTarget, pbSwing);
}

// Added: all the availability conditions of a quick whip press. Returns 1 when the quick whip
// binding would act, wherever the whip can be used normally. The optional target outputs report
// the swing or climb the press would use; they stay empty away from a whip-aim point, because
// only there does the drawn whip find and keep a swing or climb target. It changes no state.
int J3DAPI sithWeapon_CanQuickWhip(SithThing* pThing, SithThing** ppTarget, int* pbSwing)
{
    if ( ppTarget )
    {
        *ppTarget = NULL;
    }

    if ( pbSwing )
    {
        *pbSwing = 0;
    }

    if ( !pThing || pThing->type != SITH_THING_PLAYER || !sithWeapon_bQuickWhipEnabled )
    {
        return 0;
    }

    if ( sithWeapon_IsLocalPlayerUnableToUseWeapon()
        || sithPlayerControls_g_bCutsceneMode == 1
        || (pThing->flags & SITH_TF_DYING) != 0
        || pThing->moveStatus > (unsigned int)SITHPLAYERMOVE_RUNNING
        || pThing->thingInfo.actorInfo.bControlsDisabled == 1 )
    {
        return 0;
    }

    if ( pThing->thingInfo.actorInfo.weaponInfo.curWeaponID == SITHWEAPON_WHIP
        || pThing->thingInfo.actorInfo.weaponInfo.selectedWeaponID != SITHWEAPON_NOWEAPONSELECTED
        || pThing->thingInfo.actorInfo.weaponInfo.deselectedWeaponID != SITHWEAPON_NOWEAPONDESELECTED )
    {
        return 0;
    }

    if ( sithInventory_GetInventory(pThing, SITHWEAPON_WHIP) == 0.0f
        || !sithInventory_IsInventoryAvailable(pThing, SITHWEAPON_WHIP) )
    {
        return 0;
    }

    // The target is only a prompt and keyboard-select concern: the drawn whip refreshes its
    // swing and climb targets at a whip-aim point, and the press without one fires normally.
    if ( (ppTarget || pbSwing) && sithWeapon_IsOnWhipAimSurface(pThing) )
    {
        SithThing* pTarget;
        int bSwing;
        if ( sithWhip_QueryQuickWhipTarget(pThing, &pTarget, &bSwing) && pTarget )
        {
            if ( ppTarget )
            {
                *ppTarget = pTarget;
            }

            if ( pbSwing )
            {
                *pbSwing = bSwing;
            }
        }
    }

    return 1;
}

// Added: 1 when a press of the activation key would start a whip swing (*pbSwing = 1) or whip
// climb (*pbSwing = 0) right now, 0 otherwise. It uses the press's own guards (whip drawn, the
// activation wait passed, not already activated, the inventory flag, moveStatus at most RUNNING,
// controls enabled) and the same target choice. It changes no state.
int J3DAPI sithWeapon_QueryWhipPress(const SithThing* pThing, int* pbSwing)
{
    if ( !pThing || !pbSwing )
    {
        return 0;
    }

    const SithWeaponId curWeaponID = pThing->thingInfo.actorInfo.weaponInfo.curWeaponID;
    if ( curWeaponID != SITHWEAPON_WHIP )
    {
        return 0;
    }

    const SithInventoryType* pItem = sithInventory_GetType(curWeaponID);
    if ( sithTime_g_secGameTime < (double)sithWeapon_secWeaponActivationWaitEndTime
        || sithWeapon_bPlayerWeaponActivated
        || (pItem->flags & SITHINVENTORY_TYPE_UNKNOWN_200) == 0
        || pThing->moveStatus > (unsigned int)SITHPLAYERMOVE_RUNNING
        || pThing->thingInfo.actorInfo.bControlsDisabled == 1 )
    {
        return 0;
    }

    SithThing* pWhipTarget;
    int bSwing;
    if ( !sithWeapon_GetWhipPressTarget(&pWhipTarget, &bSwing) || !pWhipTarget )
    {
        return 0;
    }

    *pbSwing = bSwing;
    return 1;
}

int J3DAPI sithWeapon_ActivateWhip(SithThing* pThing, SithCog* pCog)
{
    SITH_ASSERTREL(pCog != NULL);

    bool bSuccess = false;

    SithThing* pWhipTarget;
    int bSwing;
    // Altered: The target choice is shared with sithWeapon_QueryWhipPress, so the prompt and this
    // press cannot disagree. The calls below still run in the original order.
    bool bValidTarget = sithWeapon_GetWhipPressTarget(&pWhipTarget, &bSwing) != 0;
    if ( bValidTarget )
    {
        bSuccess = bSwing ? sithWhip_StartWhipSwing(pThing) : sithWhip_StartWhipClimb(pThing, pWhipTarget);
    }

    if ( pWhipTarget )
    {
        if ( (pThing->thingInfo.actorInfo.flags & SITH_AF_ELECTRICWHIP) != 0 )
        {
            sithCog_ThingSendMessageEx(pWhipTarget, pThing, SITHCOG_MSG_DAMAGED, 0, SITH_DAMAGE_ELECTROWHIP, 0, 0);
        }
        else
        {
            sithCog_ThingSendMessageEx(pWhipTarget, pThing, SITHCOG_MSG_DAMAGED, 0, SITH_DAMAGE_WHIP, 0, 0);
        }

        if ( !bValidTarget )
        {
            return 1;
        }
    }

    if ( !bValidTarget || !bSuccess )
    {
        return 0;
    }

    sithCog_SendMessage(pCog, SITHCOG_MSG_ACTIVATE, SITHCOG_SYM_REF_THING, -99, SITHCOG_SYM_REF_THING, pThing->idx, 0);
    return 1;
}