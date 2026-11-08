// Added: the Babylon column's SNDDRIVER (`SNDDRIVER=insane_dsnd`), which replaces the silent stand-in DsndSilent.c.
// It keeps that file's module, interface and services, and the same block schedule, so SmushPlay_SysStartup succeeds,
// the engine's tracks are consumed at the stream's rate and its audio clock still advances through sound_Advance and
// timer_Sync exactly as before. The only difference: each due block that DsndSilent.c mixed into dsnd_scratch and
// dropped is pushed into a Babylon stream voice (stereo Signed16 at dsnd_rate) instead, created on start and destroyed
// on stop. dsnd_SetVolume's 0..127 value becomes the voice gain through the DX9 curve Dsnd.c defines.
//
// Pacing stays on the timer clock: the play position is dsnd_GetPlayPos, so headless runs (a NullDevice that never
// consumes) and the retail-schedule comparison see the same fill sequence as DsndSilent.c.
//
// Latency and drift: the timer clock and the device clock drift apart slowly. The stream queue is kept between
// DSND_LOWMARKBLOCKS (two blocks) and DSND_HIGHMARKBLOCKS (six blocks). When the queue runs low it underruns into
// silence, which Babylon handles by not advancing the play position. When pushing the next block would take the queue
// above the high mark, the driver skips that push: the block was already mixed, so the engine's schedule and pacing are
// unchanged, and only the newest block is dropped, costing one block (about 70 ms at 22050 Hz) instead of the roughly
// 0.4 s a queue flush would. The voice is recreated only if its handle has gone stale (it was lost), never for drift.
// Skipping also bounds memory when the output is a NullDevice or no output is open, where nothing consumes the queue,
// and it never blocks.
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmath>
#include <optional>
#include <span>

#include <j3dcore/j3d.h>

#include <smush/insane/Sound/Dsnd.h>
#include <smush/insane/Sound/Engine.h>
#include <smush/insane/Sound/Mixer.h>
#include <smush/insane/Timer/Timer.h>
#include <smush/insane/lec.h>

#include <std/Babylon/stdBabylon.h>

#include <babylon/audio/Device.h>
#include <babylon/audio/Handle.h>
#include <babylon/audio/Stream.h>

#define DSND_DEFAULT_RATE     22050
#define DSND_MIXER_HEADROOM   4
#define DSND_MIXER_SIZE       0x400 // Mixer buffer size in frames
#define DSND_NUMBLOCKS        3     // Blocks of the (virtual) secondary buffer: Dsnd.c's DSND_MINBLOCKS
#define DSND_TIMERBLOCKFRAMES 1024  // The timer callback runs once per this many frames
#define DSND_FRAMEBYTES       4     // Stereo 16-bit
#define DSND_LOWMARKBLOCKS    2     // Below this the queue is left to underrun into silence
#define DSND_HIGHMARKBLOCKS   6     // Above this the queue is dropped by recreating the voice

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
static int16_t dsnd_scratch[DSND_BLOCKSIZE / sizeof(int16_t)]; // One mixed block, pushed into the stream
static int dsnd_volTable[128];
static bool dsnd_bVolTableReady = false;
static babylon::audio::Device* dsnd_pDevice = NULL;
static babylon::audio::VoiceHandle dsnd_voice;

#ifdef J3D_TEST
static int dsnd_numVoicesCreated = 0;
static int dsnd_numPushes        = 0;
static int dsnd_numSkipped       = 0;
static int dsnd_numRecreates     = 0;
static uint64_t dsnd_numBytesPushed = 0;
#endif

static void dsnd_InitVolumeTable(void);
static float dsnd_GetGain(void);
static void dsnd_ApplyVolume(void);
static void dsnd_CreateVoice(void);
static void dsnd_RecreateVoice(void);
static void dsnd_ReleaseVoice(void);
static void dsnd_PushBlock(void);

extern "C"
{

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
    dsnd_bVolTableReady = false;
    dsnd_pDevice     = NULL;
    dsnd_voice       = babylon::audio::VoiceHandle{};
#ifdef J3D_TEST
    dsnd_numVoicesCreated = 0;
    dsnd_numPushes        = 0;
    dsnd_numSkipped       = 0;
    dsnd_numRecreates     = 0;
    dsnd_numBytesPushed   = 0;
#endif
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
    dsnd_pTimer = (TimerInterface*)dsnd_pShare->Subscribe("TIMER=clock", dsnd_pParam);
    if ( dsnd_pTimer == NULL )
    {
        dsnd_pShare->ReportError("DirectSoundDriver Module: Cannot allocate timer services.");
        return -1;
    }

    dsnd_pMixerTable = (MemoryDebugInterface*)dsnd_pShare->Subscribe("MEMORY.mixertable", dsnd_pParam);
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

    dsnd_ReleaseVoice();
    StdBabylonContext* pContext = stdBabylon_GetContext();
    dsnd_pDevice = pContext ? pContext->pAudioDevice : NULL;

    if ( dsnd_Init() )
    {
        return -1;
    }

    if ( mixer_Init(dsnd_pMixerTable, DSND_MIXER_HEADROOM, DSND_MIXER_SIZE, 0, 1, 1) )
    {
        return -1;
    }

    dsnd_pfnFill = pfnFill;
    dsnd_rate    = rate;
    dsnd_CreateVoice();
    dsnd_hTimer  = dsnd_pTimer->Alloc(dsnd_Callback, DSND_TICKRATE / (rate / DSND_TIMERBLOCKFRAMES), TIMER_FLAG_ONESHOT, 0);
    return 0;
}

void dsnd_Stop(void)
{
    if ( dsnd_hTimer )
    {
        dsnd_pTimer->Free(dsnd_hTimer);
    }

    dsnd_hTimer = 0;
    mixer_Done(dsnd_pMixerTable);
    dsnd_Release();
}

void J3DAPI dsnd_SetVolume(int volume)
{
    dsnd_volume = volume & 0x7F; // Dsnd.c also set the buffer's attenuation; here it is the stream voice's gain
    dsnd_ApplyVolume();
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
// one playing; the play position comes from dsnd_GetPlayPos. The mixed block goes into the stream voice instead of the
// discarded scratch buffer.
void J3DAPI dsnd_Callback(int index, intptr_t param)
{
    J3D_UNUSED(index);
    J3D_UNUSED(param);

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
            dsnd_PushBlock();
        }
    }

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
    dsnd_ReleaseVoice();
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

#ifdef J3D_TEST
void DsndBabylon_TestGetVoice(uint32_t* pIndex, uint32_t* pGeneration)
{
    if ( pIndex )
    {
        *pIndex = dsnd_voice.index;
    }

    if ( pGeneration )
    {
        *pGeneration = dsnd_voice.generation;
    }
}

int DsndBabylon_TestGetVoicesCreated(void)
{
    return dsnd_numVoicesCreated;
}

int DsndBabylon_TestGetPushes(void)
{
    return dsnd_numPushes;
}

int DsndBabylon_TestGetSkipped(void)
{
    return dsnd_numSkipped;
}

int DsndBabylon_TestGetRecreates(void)
{
    return dsnd_numRecreates;
}

uint64_t DsndBabylon_TestGetBytesPushed(void)
{
    return dsnd_numBytesPushed;
}
#endif

} // extern "C"

// Dsnd.c's DX9 volume table: hundredths of a dB for the 0..127 volume steps.
static void dsnd_InitVolumeTable(void)
{
    if ( dsnd_bVolTableReady )
    {
        return;
    }

    dsnd_volTable[0] = -10000;
    dsnd_volTable[1] = -7000;
    dsnd_volTable[2] = -6000;
    dsnd_volTable[3] = -5200;
    dsnd_volTable[4] = -5000;
    dsnd_volTable[5] = -4500;
    dsnd_volTable[6] = -4200;
    dsnd_volTable[7] = -4100;
    dsnd_volTable[8] = -4000;

    int* pEntry = &dsnd_volTable[8];
    for ( int v = 0; v < 48000; v += 2000 )
    {
        *pEntry++ = v / 23 - 4000;
    }

    pEntry = &dsnd_volTable[32];
    for ( int v = 0; v < 32000; v += 1000 )
    {
        *pEntry++ = v / 31 - 2000;
    }

    pEntry = &dsnd_volTable[64];
    for ( int v = 0; v < 64000; v += 1000 )
    {
        *pEntry++ = v / 63 - 1000;
    }

    dsnd_bVolTableReady = true;
}

// DirectSound attenuation is in hundredths of a dB: gain = 10^(dB / 2000).
static float dsnd_GetGain(void)
{
    dsnd_InitVolumeTable();
    return (float)std::pow(10.0, (double)dsnd_volTable[dsnd_volume] / 2000.0);
}

static void dsnd_ApplyVolume(void)
{
    if ( !dsnd_pDevice || dsnd_voice.isNull() )
    {
        return;
    }

    static_cast<void>(dsnd_pDevice->setVoiceGain(dsnd_voice, dsnd_GetGain()));
}

static void dsnd_CreateVoice(void)
{
    if ( !dsnd_pDevice )
    {
        return;
    }

    const babylon::audio::StreamFormat format{ babylon::audio::SampleFormat::Signed16, (uint32_t)dsnd_rate, 2 };
    dsnd_voice = dsnd_pDevice->createStreamVoice(format);
    if ( dsnd_voice.isNull() )
    {
        return;
    }

    dsnd_ApplyVolume();
    static_cast<void>(dsnd_pDevice->play(dsnd_voice));
#ifdef J3D_TEST
    ++dsnd_numVoicesCreated;
#endif
}

// Replaces a stale (lost) voice handle with a fresh stream voice. Drift never comes here; it only skips pushes.
static void dsnd_RecreateVoice(void)
{
    if ( !dsnd_pDevice )
    {
        return;
    }

    dsnd_voice = babylon::audio::VoiceHandle{};
    dsnd_CreateVoice();
#ifdef J3D_TEST
    ++dsnd_numRecreates;
#endif
}

static void dsnd_ReleaseVoice(void)
{
    if ( dsnd_pDevice && !dsnd_voice.isNull() )
    {
        dsnd_pDevice->destroyVoice(dsnd_voice);
    }

    dsnd_voice = babylon::audio::VoiceHandle{};
}

static void dsnd_PushBlock(void)
{
    if ( !dsnd_pDevice || dsnd_voice.isNull() )
    {
        return;
    }

    const std::optional<babylon::audio::StreamState> state = dsnd_pDevice->streamState(dsnd_voice);
    if ( !state )
    {
        // The handle went stale: this is the only reason to replace a stream voice.
        dsnd_RecreateVoice();
        if ( dsnd_voice.isNull() )
        {
            return;
        }
    }
    else if ( state->queuedFrames + DSND_BLOCKFRAMES > (uint64_t)DSND_HIGHMARKBLOCKS * DSND_BLOCKFRAMES )
    {
        // The block is already mixed, so skipping the push leaves the schedule and pacing unchanged.
#ifdef J3D_TEST
        ++dsnd_numSkipped;
#endif
        return;
    }

    const std::span<const std::byte> data(reinterpret_cast<const std::byte*>(dsnd_scratch), DSND_BLOCKSIZE);
    if ( dsnd_pDevice->pushStream(dsnd_voice, data) )
    {
#ifdef J3D_TEST
        ++dsnd_numPushes;
        dsnd_numBytesPushed += DSND_BLOCKSIZE;
#endif
    }
}
