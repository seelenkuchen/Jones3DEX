#ifndef SITH_SITHTIME_H
#define SITH_SITHTIME_H
#include <j3dcore/j3d.h>
#include <rdroid/types.h>
#include <sith/types.h>
#include <sith/Main/sithMain.h>
#include <std/types.h>

J3D_EXTERN_C_START

extern uint32_t sithTime_g_frameTime;
// extern unsigned int sithTime_g_frameTime;

extern float sithTime_g_frameTimeFlex;
// extern float sithTime_g_secFrameTime;

extern float sithTime_g_fps;
// extern float sithTime_g_fps;

extern uint32_t sithTime_g_msecGameTime;
// extern unsigned int sithTime_g_msecGameTime;

extern float sithTime_g_secGameTime;
// extern float sithTime_g_secGameTime;

// Added: M9.22 interpolated animation. Game time used by render-only animation (the underwater
// wobble). During a fixed-tick drawn frame it is prevTick + alpha * step; otherwise it follows
// sithTime_g_secGameTime.
extern float sithTime_g_secRenderGameTime;

extern uint32_t sithTime_g_clockTime;
// extern unsigned int sithTime_g_clockTime;

extern int sithTime_g_bPaused;
// extern int sithTime_g_bPaused;

// Added: M9.20 fixed simulation tick (ADR-0039). When set, sithTime_Advance stops sampling the
// wall clock and the caller supplies every step through sithTime_SetFrameTime;
// sithTime_g_clockTime then follows game time so the simulation no longer reads pacing-dependent
// wall time. Unset keeps the measured-frame-time behaviour exactly.
extern int sithTime_g_bFixedStep;

void J3DAPI sithTime_SetFixedStep(int bFixedStep); // Added: M9.20
void sithTime_Advance(void);
void sithTime_Pause(void);
void sithTime_Resume(void);
void J3DAPI sithTime_SetFrameTime(uint32_t frameTime); //Added
void sithTime_Reset(void);
void J3DAPI sithTime_SetGameTime(uint32_t msecTime);
int sithTime_IsPaused(void);

void sithTime_SetRenderGameTime(float secTime); // Added: M9.22, fixed-tick draw only
float sithTime_GetRenderGameTime(void);        // Added: M9.22, render-time game time

void sithTime_ResetGlobals(void);

J3D_EXTERN_C_END
#endif // SITH_SITHTIME_H
