#ifndef SMUSH_INSANE_TIMER_TIMERSYS_H
#define SMUSH_INSANE_TIMER_TIMERSYS_H

/*
 * Platform layer of the timer service, private to the Timer module. Added: two layers implement it: TimerNull.c (the
 * Null column) calls timer_Tick from a tick thread, like the original's Windows thread; TimerPolled.c (the Babylon
 * column) has no thread and delivers the due ticks from timer_SysUpdate, which the player polls. No platform types
 * cross this boundary.
 */

#include <stdint.h>
#include <j3dcore/j3d.h>

#ifdef __cplusplus
extern "C" {
#endif

// Starts the tick thread, which calls timer_Tick at TIMER_THREAD_RATE until timer_SysStopThread
void timer_SysStartThread(void);

// Signals the tick thread, waits for it to exit and releases its resources
void timer_SysStopThread(void);

// Added: calls timer_Tick once for every tick that is due on the platform clock and returns how many it called. Only
// TimerPolled.c ticks here (and yields the CPU for a millisecond when nothing was due); with a tick thread it returns 0.
// Reentrant calls (from a timer callback) return 0.
int timer_SysUpdate(void);

// Clears the platform layer's state
void timer_SysResetGlobals(void);

#if defined(J3D_TEST) && defined(J3D_PLATFORM_BABYLON)
// Added: test seam of TimerPolled.c (compiled into the smush test executable with J3D_TEST): runs the tick schedule on
// the given clock and idle sleep instead of stdPlatform_GetTimeMsec and stdPlatform_Sleep; NULL restores either.
void TimerPolled_SetClockForTest(unsigned int (*pfNow)(void), void (J3DAPI* pfSleep)(uint32_t msec));
#endif

#ifdef __cplusplus
}
#endif

#endif // SMUSH_INSANE_TIMER_TIMERSYS_H
