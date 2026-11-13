// Added: the Babylon column's SNDDRIVER (`SNDDRIVER=insane_dsnd`), a silent stand-in for the DirectSound driver Dsnd.c
// (original 0x004E74B0-0x004E7D60) until real audio output arrives (M7). It publishes the same module and interface,
// subscribes the same services and runs the same block schedule, so the sound engine starts, SmushPlay_SysStartup
// succeeds and the engine's tracks are consumed at the stream's rate. Only the DirectSound buffer is gone: its play
// position is derived from the timer clock (the 22050 Hz stereo 16-bit stream format), each due block is mixed into a
// scratch buffer and dropped. Pacing is unchanged: the engine advances its audio clock by the frames consumed and
// syncs the timer to it (sound_Advance, timer_Sync), and with the play position on the timer clock both agree.
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <j3dcore/j3d.h>

#include <smush/insane/Sound/Dsnd.h>
#include <smush/insane/Sound/Engine.h>
#include <smush/insane/Sound/Mixer.h>
#include <smush/insane/Timer/Timer.h>
#include <smush/insane/lec.h>

#define DSND_DEFAULT_RATE     22050
#define DSND_MIXER_HEADROOM   4
#define DSND_MIXER_SIZE       0x400 // Mixer buffer size in frames
#define DSND_NUMBLOCKS        3     // Blocks of the (virtual) secondary buffer: Dsnd.c's DSND_MINBLOCKS
#define DSND_TIMERBLOCKFRAMES 1024  // The timer callback runs once per this many frames
#define DSND_FRAMEBYTES       4     // Stereo 16-bit

static LecModule dsnd_module = { dsnd_Register, dsnd_Unregister, dsnd_Startup, dsnd_Shutdown };
static SndDriver dsnd_interface = { dsnd_Start, dsnd_Stop, dsnd_SetVolume, dsnd_GetVolume, (int (*)(int))sound_Null, dsnd_GetRate };

static LecShare* dsnd_pShare            = NULL;
static void* dsnd_pParam                = NULL;
static TimerInterface* dsnd_pTimer      = NULL;
static MemoryDebugInterface* dsnd_pMixerTable = NULL;
static void (*dsnd_pfnFill)(int, SoundFeed) = NULL;
static int dsnd_cfg1       = 0;
static int dsnd_cfg2       = 0;
static int dsnd_cfg3       = 0;
static int dsnd_cfg4       = 0;
static void* dsnd_pDS      = NULL; // The game's sound object (dsnd_Configure tag 5): kept, never used
static int dsnd_rate       = DSND_DEFAULT_RATE;
static int dsnd_volume     = 127;
static int dsnd_hTimer     = 0;
static int dsnd_lastBlock  = -1;
static bool dsnd_bPlaying  = false;
static int dsnd_startTicks = 0;
static int16_t dsnd_scratch[DSND_BLOCKSIZE / sizeof(int16_t)]; // One mixed block, discarded

void Dsnd_ResetGlobals(void)
{
    dsnd_pShare      = NULL;
    dsnd_pParam      = NULL;
    dsnd_pTimer      = NULL;
    dsnd_pMixerTable = NULL;
    dsnd_pfnFill     = NULL;
    dsnd_cfg1        = 0;
    dsnd_cfg2        = 0;
    dsnd_cfg3        = 0;
    dsnd_cfg4        = 0;
    dsnd_pDS         = NULL;
    dsnd_rate        = DSND_DEFAULT_RATE;
    dsnd_volume      = 127;
    dsnd_hTimer      = 0;
    dsnd_lastBlock   = -1;
    dsnd_bPlaying    = false;
    dsnd_startTicks  = 0;
}

int J3DAPI dsnd_Register(LecShare* pShare, void* pParam)
{
    dsnd_pShare = pShare;
    dsnd_pParam = pParam;
    pShare->Publish("LECMODULE=insane_dsnd", &dsnd_module);
    dsnd_pShare->Publish("SNDDRIVER=insane_dsnd", &dsnd_interface);
    dsnd_cfg1 = -1;
    dsnd_cfg3 = -1;
    dsnd_cfg2 = -1;
    dsnd_cfg4 = -1;
    return 0;
}

int J3DAPI dsnd_Configure(int tag, ...)
{
    va_list args;
    va_start(args, tag);
    while ( tag )
    {
        switch ( tag )
        {
            case DSND_CONFIG_1:
                dsnd_cfg1 = va_arg(args, int);
                break;

            case DSND_CONFIG_2:
                dsnd_cfg2 = va_arg(args, int);
                break;

            case DSND_CONFIG_3:
                dsnd_cfg3 = va_arg(args, int);
                break;

            case DSND_CONFIG_4:
                dsnd_cfg4 = va_arg(args, int);
                break;

            case DSND_CONFIG_DSOUND:
                dsnd_pDS = va_arg(args, void*);
                break;

            default:
                va_end(args);
                return -1;
        }

        tag = va_arg(args, int);
    }

    va_end(args);
    return 0;
}

void dsnd_Unregister(void)
{
    dsnd_pShare->Unpublish("SNDDRIVER=insane_dsnd");
    dsnd_pShare->Unpublish("LECMODULE=insane_dsnd");
}

int dsnd_Startup(void)
{
    dsnd_pTimer = dsnd_pShare->Subscribe("TIMER=clock", dsnd_pParam);
    if ( dsnd_pTimer == NULL )
    {
        dsnd_pShare->ReportError("DirectSoundDriver Module: Cannot allocate timer services.");
        return -1;
    }

    dsnd_pMixerTable = dsnd_pShare->Subscribe("MEMORY.mixertable", dsnd_pParam);
    if ( dsnd_pMixerTable == NULL )
    {
        dsnd_pShare->ReportError("DirectSoundDriver Module: Cannot allocate timer services."); // Same text as Dsnd.c
        return -1;
    }

    return 0;
}

void dsnd_Shutdown(void)
{
    dsnd_pShare->Unsubscribe(dsnd_pTimer);
    dsnd_pShare->Unsubscribe(dsnd_pMixerTable);
}

int J3DAPI dsnd_Start(int rate, int a2, void (*pfnFill)(int, SoundFeed))
{
    J3D_UNUSED(a2);
    if ( rate < DSND_DEFAULT_RATE )
    {
        rate = DSND_DEFAULT_RATE;
    }

    // Fixed: Do not report a running stream when device, mixer or timer setup fails.
    if ( dsnd_Init() ) goto error;
    if ( mixer_Init(dsnd_pMixerTable, DSND_MIXER_HEADROOM, DSND_MIXER_SIZE, 0, 1, 1) ) goto error;

    dsnd_pfnFill = pfnFill;
    dsnd_rate    = rate;
    dsnd_hTimer  = dsnd_pTimer->Alloc(dsnd_Callback, DSND_TICKRATE / (rate / DSND_TIMERBLOCKFRAMES), TIMER_FLAG_ONESHOT, 0);
    if ( dsnd_hTimer == 0 ) goto error;
    return 0;

error:
    mixer_Done(dsnd_pMixerTable);
    dsnd_Release();
    dsnd_pfnFill = NULL;
    return -1;
}

void dsnd_Stop(void)
{
    if ( dsnd_hTimer )
    {
        dsnd_pTimer->Free(dsnd_hTimer);
    }

    dsnd_hTimer = 0;
    dsnd_pfnFill = NULL;
    mixer_Done(dsnd_pMixerTable);
    dsnd_Release();
}

void J3DAPI dsnd_SetVolume(int volume)
{
    dsnd_volume = volume & 0x7F; // Dsnd.c also set the buffer's attenuation; there is no buffer
}

int dsnd_GetVolume(void)
{
    return dsnd_volume;
}

int dsnd_GetRate(void)
{
    return DSND_DEFAULT_RATE;
}

// Dsnd.c's block schedule unchanged: each run fills the block after the last one filled, until the block before the
// one playing; the play position comes from dsnd_GetPlayPos.
void J3DAPI dsnd_Callback(int index, intptr_t param)
{
    J3D_UNUSED(index);
    J3D_UNUSED(param);

    if ( dsnd_hTimer == 0 ) return;
    if ( !dsnd_pfnFill ) goto rearm;

    int cur = dsnd_GetPlayPos();
    if ( cur != -1 )
    {
        int lastBlock = dsnd_lastBlock;
        if ( cur != (lastBlock + 1) % DSND_NUMBLOCKS )
        {
            if ( lastBlock < 0 )
            {
                lastBlock = cur;
            }

            dsnd_lastBlock = (lastBlock + 1) % DSND_NUMBLOCKS;

            mixer_Begin(DSND_BLOCKFRAMES);
            dsnd_pfnFill(DSND_BLOCKFRAMES, mixer_Feed);
            mixer_Mix(dsnd_scratch, DSND_BLOCKFRAMES);
        }
    }

rearm:
    dsnd_pTimer->SetInterval(dsnd_hTimer, DSND_TICKRATE / (dsnd_rate / DSND_TIMERBLOCKFRAMES));
}

// Starts the virtual play position at the current timer time (Dsnd.c opened and started its looping secondary buffer).
int dsnd_Init(void)
{
    dsnd_lastBlock  = -1;
    dsnd_startTicks = dsnd_pTimer->GetTicks();
    dsnd_bPlaying   = true;
    return 0;
}

void J3DAPI dsnd_Print(const char* pFormat, ...)
{
    J3D_UNUSED(pFormat);
}

void J3DAPI dsnd_OpenSecondary(int blocks)
{
    J3D_UNUSED(blocks); // The block count is fixed at DSND_NUMBLOCKS
}

void dsnd_Release(void)
{
    dsnd_bPlaying  = false;
    dsnd_lastBlock = -1;
}

// The block the virtual buffer plays: the stream frames elapsed on the timer clock since dsnd_Init, in blocks, around
// the ring. -1 while stopped, like Dsnd.c without a secondary buffer.
int dsnd_GetPlayPos(void)
{
    if ( !dsnd_bPlaying )
    {
        return -1;
    }

    uint32_t elapsed = (uint32_t)dsnd_pTimer->GetTicks() - (uint32_t)dsnd_startTicks;
    uint64_t frames  = (uint64_t)elapsed * DSND_DEFAULT_RATE / DSND_TICKRATE;
    return (int)((frames * DSND_FRAMEBYTES / DSND_BLOCKSIZE) % DSND_NUMBLOCKS);
}
