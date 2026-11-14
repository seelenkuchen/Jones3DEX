#ifndef SITH_SITHPLAYERCONTROLS_H
#define SITH_SITHPLAYERCONTROLS_H
#include <j3dcore/j3d.h>
#include <rdroid/types.h>
#include <sith/types.h>
#include <sith/Gameplay/sithTime.h>
#include <sith/Main/sithMain.h>
#include <std/types.h>

J3D_EXTERN_C_START

extern int sithPlayerControls_g_bCutsceneMode;
// extern int sithPlayerControls_g_bCutsceneMode;

extern bool sithPlayerControls_bActionKeyActive;

/**
 * Resets internal state
 * @note new function
 */
void sithPlayerControls_Reset(void);

void J3DAPI sithPlayerControls_PuppetCallback(SithThing* pThing, int track, rdKeyMarkerType markerType);
int J3DAPI sithPlayerControls_Process(SithThing* pPlayerThing, float secDeltaTime);

int J3DAPI sithPlayerControls_BoardVehicle(SithThing* pThing, int bBoard);
void J3DAPI sithPlayerControls_ExitVehicle(SithThing* pThing);

void J3DAPI sithPlayerControls_RotateAimJoints(SithThing* pThing, float pitch, float yaw);
void J3DAPI sithPlayerControls_RotateAimJointsEx(SithThing* pThing, float pitch, float yaw, float secDeltaTime); // New

void J3DAPI sithPlayerControls_ResetAimJoints(SithThing* pThing);
void J3DAPI sithPlayerControls_ResetAimJointsEx(SithThing* pThing, float secDeltaTime); //New

void J3DAPI sithPlayerControls_ProcessWeaponAim(SithThing* pThing, float secDeltaTime);

SithThing* sithPlayerControls_GetTargetThing(void);

// Added: Modern camera-relative movement skips the original stand-to-walk/run and
// walk/run-to-stand transition animations. Start reaches the walk or run move status, forward
// thrust and puppet mode at once; Stop reaches the stand status and puppet mode at once.
bool J3DAPI sithPlayerControls_StartModernMove(SithThing* pThing, bool bRun, float moveFactor);
void J3DAPI sithPlayerControls_StopModernMove(SithThing* pThing);

// Added: the original underwater swim code's pitch limit, shared with Modern swim steering.
float J3DAPI sithPlayerControls_GetSwimMaxPitchAngle(void);

// Added: target lock for the Modern control style (ADR-0029). These reuse the auto-aim
// filters and acquisition cone. The pure selector takes candidate things and the free camera's
// world yaw so tests need no collision world; FindLockTarget gathers the same candidates the
// auto-aim search does.
// Added: the sight test receives the camera eye, not Indy, because the lock only needs the
// camera to see the target. The selector and keep-alive still measure the track range from Indy.
typedef bool (J3DAPI *SithLockSightTest)(SithSector* pEyeSector, const rdVector3* pEyePos, const rdVector3* pTargetPos);
SithThing* J3DAPI sithPlayerControls_SelectLockTargetEx(SithThing* pThing, float cameraYaw,
    SithSector* pEyeSector, const rdVector3* pEyePos, SithLockSightTest pfSight,
    SithThing* const* apCandidates, size_t numCandidates); // Added
SithThing* J3DAPI sithPlayerControls_SelectLockTarget(SithThing* pThing, float cameraYaw, SithThing* const* apCandidates, size_t numCandidates); // Added
SithThing* J3DAPI sithPlayerControls_FindLockTarget(SithThing* pThing, float cameraYaw); // Added
bool J3DAPI sithPlayerControls_IsLockTargetAlive(const SithThing* pTarget); // Added
bool J3DAPI sithPlayerControls_IsLockTargetVisible(SithThing* pThing, SithThing* pTarget); // Added

SithThing* J3DAPI sithPlayerControls_GetVehicleBoardedThing();
void J3DAPI sithPlayerControls_SetVehicleBoardedThing(SithThing* pThing);

/**
 * Calculates linear move thrust based on actor max thrust and extra speed
 * @param pActor      - Pointer to actor info
 * @param direction   - Direction factor. Should be negative for backward movement
 * @param speedFactor - Extra speed factor.
 * @return
 */
static inline float sithPlayerControls_CalculateThrust(SithActorInfo* pActor, float direction, float speedFactor)
{
    return (pActor->maxThrust + pActor->extraSpeed) * direction * speedFactor;
}

/**
* Calculates accelerated linear move thrust based on actor max thrust and time delta
* @param pActor       - Pointer to actor info
* @param direction    - Direction factor. Should be negative for backward movement
* @param speedFactor  - Extra speed factor.
* @param secDeltaTime - Frame time delta
*/
static inline float sithPlayerControls_CalculateAcceleratedThrust(SithActorInfo* pActor, float direction, float speedFactor, float secDeltaTime)
{
    // TODO: Would make sens to refactor this formula and remove frame time dependency altogether
    return (pActor->maxThrust * direction * speedFactor) + (direction * secDeltaTime);
}

/*
* Calculates angular velocity based on actor max rotation velocity
* @param pActor        - Actor info
* @param axisDirection - Axis direction. Should be negative for right turn
* @param keyDirection  - Key direction. Should be negative for right turn
* @param speedFactor   - Extra speed factor. Should be <= 1.0f
*/
static inline float sithPlayerControls_CalculateAngularVelocity(SithActorInfo* pActor, float axisDirection, float keyDirection, float speedFactor)
{
    speedFactor = J3DMIN(speedFactor, 1.0f);
    // TODO: Replace sithTime_g_fps with fixed step, e.g. 25.0f
    //       Would probably make sense to remove left part of the formula altogether and rely only on right part.
    return (axisDirection * sithTime_g_fps) + (pActor->maxRotVelocity * keyDirection * speedFactor);
}

void sithPlayerControls_ResetGlobals(void);

J3D_EXTERN_C_END
#endif // SITH_SITHPLAYERCONTROLS_H
