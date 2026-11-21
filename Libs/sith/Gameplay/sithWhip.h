#ifndef SITH_SITHWHIP_H
#define SITH_SITHWHIP_H
#include <j3dcore/j3d.h>
#include <rdroid/types.h>
#include <sith/types.h>
#include <sith/Main/sithMain.h>
#include <std/types.h>

J3D_EXTERN_C_START

void J3DAPI sithWhip_UpdateWhipAim(SithThing* pThing);

signed int J3DAPI sithWhip_StartWhipSwing(SithThing* pThing);
void J3DAPI sithWhip_FinishWhipSwing(SithThing* pThing);

int J3DAPI sithWhip_StartWhipClimb(SithThing* pThing, SithThing* pWhippedThing);

void J3DAPI sithWhip_SetActorWhipClimbIdle(SithThing* pThing);

void J3DAPI sithWhip_FinishWhipClimbDismount(SithThing* pThing);

void J3DAPI sithWhip_ProcessWhipClimbMove(SithThing* pThing, float secDeltaTime);

SithThing* sithWhip_GetWhipSwingThing(void);
SithThing* sithWhip_GetWhipClimbThing(void);

// Added: the whip searches that set the sithWhip aim targets, shared with the tests.
int J3DAPI sithWhip_SearchWhipSwingThing(SithThing* pThing);
int J3DAPI sithWhip_SearchWhipClimbThing(SithThing* pThing);

// Added: the swing-before-climb choice and target flag rule the activation press uses. Returns 1
// when the target allows the action, and always reports the target the press would act on.
int J3DAPI sithWhip_ChooseWhipPressTarget(SithThing* pSwingThing, SithThing* pClimbThing, SithThing** ppTarget, int* pbSwing);

// Added: the target a quick whip press would use right now. It runs the whip searches inside
// sithCollision's read-only scope, applies the same target choice as the press, and changes no
// state (the sithWhip globals, the collision scratch and the things are left alone). Returns 1
// when the target allows the action. The outputs are cleared first, so they are always set.
int J3DAPI sithWhip_QueryQuickWhipTarget(SithThing* pThing, SithThing** ppTarget, int* pbSwing);

#ifdef J3D_TEST
// Added: installs the whip-aim targets without running the collision search, so the interaction
// query tests reach the same target choice the press uses.
void sithWhip_SetTargetsForTest(SithThing* pSwing, SithThing* pClimb);
#endif

void sithWhip_Reset(void);

int J3DAPI sithWhip_Save(DPID idTo, uint32_t outstream);
int J3DAPI sithWhip_Restore(const SithMessage* pMsg);

void J3DAPI sithWhip_WhipFirePuppetCallback(SithThing* pThing, int track, rdKeyMarkerType markerType);
void J3DAPI sithWhip_WhipClimbPuppetCallback(SithThing* pThing, int track, rdKeyMarkerType markerType);
void J3DAPI sithWhip_ClimbDismountPuppetCallback(SithThing* pThing, int track, rdKeyMarkerType mt);

void sithWhip_ResetGlobals(void);

J3D_EXTERN_C_END
#endif // SITH_SITHWHIP_H
