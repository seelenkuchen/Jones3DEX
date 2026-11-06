// Added: the Babylon column's platform layer of the timer service (TimerSys.h). The original runs timer_Tick on a
// TIME_CRITICAL Windows thread at TIMER_THREAD_RATE, scheduled on timeGetTime with an ldiv remainder so the rate holds
// exactly. Here there is no thread: the same schedule runs on stdPlatform_GetTimeMsec (the Babylon clock, or the
// headless run's virtual clock), and timer_SysUpdate delivers every tick that is due on the caller's thread. The
// animation player polls it from animplay_Pace, timer_Wait from its loop, so ticks arrive in the same order relative
// to each other as with the thread, and nothing runs concurrently with the player.
#include <j3dcore/j3d.h>

#include <stdbool.h>
#include <stdint.h>

#include <smush/insane/Timer/Timer.h>
#include <smush/insane/Timer/TimerSys.h>

#include <std/General/stdPlatform.h>

#define TIMER_POLLED_STEP_MS   (1000 / TIMER_THREAD_RATE) // whole milliseconds per tick (16)
#define TIMER_POLLED_STEP_FRAC (1000 % TIMER_THREAD_RATE) // remainder, carried like the Win32 thread's ldiv (40/60)
#define TIMER_POLLED_MAXLAG    TIMER_THREAD_RATE          // ticks: after a stall of more than a second the backlog is dropped
#define TIMER_POLLED_IDLE_MS   1u                         // the yield of an update that found nothing due

static bool timer_bPolledRunning = false;
static bool timer_bPolledInUpdate = false;
static tStdTime timer_polledNext  = 0; // time the next tick is due
static int timer_polledFrac       = 0;

// The clock the schedule runs on: the platform's, or a test's fake one (TimerPolled_SetClockForTest)
static tStdTime (*timer_pfPolledNow)(void)             = stdPlatform_GetTimeMsec;
static void (J3DAPI* timer_pfPolledSleep)(uint32_t msec) = stdPlatform_Sleep;

void timer_SysStartThread(void)
{
    if ( timer_bPolledRunning )
    {
        return;
    }

    // The Win32 thread ticks at once (its first wait has a timeout of 0), then every step after; so does this one.
    timer_polledFrac     = 0;
    timer_polledNext     = timer_pfPolledNow();
    timer_bPolledRunning = true;
}

void timer_SysStopThread(void)
{
    timer_bPolledRunning = false;
}

int timer_SysUpdate(void)
{
    if ( !timer_bPolledRunning || timer_bPolledInUpdate )
    {
        return 0;
    }

    timer_bPolledInUpdate = true;

    int numTicks = 0;
    tStdTime now = timer_pfPolledNow();
    // Signed distance, so the 2^32 wrap of the millisecond clock keeps the order
    while ( timer_bPolledRunning && (int32_t)(now - timer_polledNext) >= 0 )
    {
        if ( numTicks >= TIMER_POLLED_MAXLAG )
        {
            // A stall (a debugger, a blocked present): catching up a backlog this long would only run the movie in a
            // burst. Resume the schedule from now instead.
            timer_polledFrac = 0;
            timer_polledNext = now + TIMER_POLLED_STEP_MS;
            break;
        }

        timer_Tick();
        ++numTicks;

        timer_polledNext += TIMER_POLLED_STEP_MS;
        timer_polledFrac += TIMER_POLLED_STEP_FRAC;
        if ( timer_polledFrac >= TIMER_THREAD_RATE )
        {
            timer_polledFrac -= TIMER_THREAD_RATE;
            timer_polledNext++;
        }
    }

    timer_bPolledInUpdate = false;

    // The caller spins on this (the movie loop calls Pace back to back, as in the original): give the CPU back for a
    // millisecond when nothing was due. On the headless run's virtual clock the sleep advances the time, which is
    // what lets a movie play there at all.
    if ( numTicks == 0 && timer_bPolledRunning )
    {
        timer_pfPolledSleep(TIMER_POLLED_IDLE_MS);
    }

    return numTicks;
}

void timer_SysResetGlobals(void)
{
    timer_bPolledRunning  = false;
    timer_bPolledInUpdate = false;
    timer_polledNext      = 0;
    timer_polledFrac      = 0;
}

#if defined(J3D_TEST)
void TimerPolled_SetClockForTest(tStdTime (*pfNow)(void), void (J3DAPI* pfSleep)(uint32_t msec))
{
    timer_pfPolledNow   = pfNow ? pfNow : stdPlatform_GetTimeMsec;
    timer_pfPolledSleep = pfSleep ? pfSleep : stdPlatform_Sleep;
}
#endif
