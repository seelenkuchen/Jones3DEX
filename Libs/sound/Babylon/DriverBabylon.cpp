// Added: the Babylon column's sound driver. It implements the recovered SoundDriver_* functions of
// sound/Driver.h on the babylon::audio::Device the composition root installs in the adapter
// context, in place of DriverNull.c (ADR-0007, ADR-0010). The DirectSound driver is the behavior
// reference: buffers become one immutable Babylon sound shared by a reference count and one voice
// per handle, Stop pauses a voice and Play resumes it, and the dB/pan tables are the same ones the
// DirectSound driver uses. There is no hardware 3D, so every voice stays 2D and the sound layer's
// software listener mix runs exactly as DriverNull.c runs it (ADR-0027). Time stays on the
// platform clock, never on the device (ADR-0027 amendment).
#include <sound/Driver.h>
#include <sound/DriverOutput.h>

#include <math.h>

#include <sound/AudioLib.h>

#include <rdroid/Math/rdVector.h>

#include <std/General/std.h>
#include <std/General/stdMemory.h>
#include <std/General/stdPlatform.h>
#include <std/General/stdUtil.h>

#include <std/Babylon/stdBabylon.h>

#include <babylon/audio/Device.h>
#include <babylon/audio/Handle.h>
#include <babylon/audio/Sound.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

#define SOUNDDRIVER_FARVOLUMETHRESHOLD 0.01f

// The Babylon sound and voice handles are shared by every duplicate of one buffer, so the PCM data
// and its format live in a slot the handles reference and the last release destroys.
struct SoundDriverSoundSlot
{
    babylon::audio::SoundHandle sound;
    babylon::audio::SampleFormat format;
    uint32_t sampleRate;
    uint32_t bitsPerSample;
    uint32_t channels;
    size_t dataSize;
    uint64_t frameCount;
    int refCount;
};

struct sSysSoundBuffer
{
    SoundDriverSoundSlot* pSlot;
    babylon::audio::VoiceHandle voice;
    int dbVolume;       // hundredths of a dB, the DirectSound buffer state
    int pan;            // hundredths of a dB, the DirectSound buffer state
    int frequency;      // Hz, the DirectSound buffer state
    bool bStarted;      // the voice has been played at least once
    bool bPaused;       // a Stop paused it, so Play resumes instead of restarting
    bool bLoop;         // the loop flag of the current (or last) play
    struct sSysSoundBuffer* pNext;
};

struct sSysSoundDevice
{
    int unused;
};

static float SoundDriver_maxVolume                     = 1.0f;
static float SoundDriver_masterGain                    = 1.0f;
static int SoundDriver_bUseGlobalFocusBuf              = 0;
static tDirectSound* SoundDriver_pDirectSound           = NULL;
static tSysSoundBuffer* SoundDriver_pBuffers            = NULL;
static babylon::audio::Device* SoundDriver_pDevice      = NULL;
static bool SoundDriver_bOutputLost                     = false;
static uint32_t SoundDriver_msecNextReopen              = 0;
static uint32_t SoundDriver_msecNextConfiguredCheck     = 0;
static std::string SoundDriver_aConfiguredOutputDevice;
static std::string SoundDriver_aCurrentOutputDevice;
static std::vector<babylon::audio::OutputDevice> SoundDriver_outputDevices;
static int SoundDriver_aPanTable[256];
static rdVector3 SoundDriver_curListenerPos             = { 0.0f, 0.0f, 0.0f };
static float SoundDriver_frontOrientATan                = 0.0f;
static SoundCalcListenerSoundMixFunc SoundDriver_pfCalcListenerSoundMix;
static SoundDriverGetSoundBufferDataFunc SoundDriver_pfGetSoundBufferData;
static tPrintfFunc SoundDriver_pfLogError;

static const int SoundDriver_aDBTable[256] = {
    -9640, -4810, -4210, -3850, -3600, -3410, -3250, -3120, -3000, -2900, -2810, -2730, -2650, -2580, -2520, -2460,
    -2400, -2350, -2300, -2250, -2210, -2160, -2120, -2080, -2050, -2010, -1980, -1950, -1910, -1880, -1850, -1830,
    -1800, -1770, -1750, -1720, -1700, -1670, -1650, -1630, -1600, -1580, -1560, -1540, -1520, -1500, -1480, -1460,
    -1450, -1430, -1410, -1390, -1380, -1360, -1340, -1330, -1310, -1300, -1280, -1270, -1250, -1240, -1220, -1210,
    -1200, -1180, -1170, -1160, -1140, -1130, -1120, -1110, -1090, -1080, -1070, -1060, -1050, -1040, -1020, -1010,
    -1000,  -990,  -980,  -970,  -960,  -950,  -940,  -930,  -920,  -910,  -900,  -890,  -880,  -870,  -860,  -850,
     -840,  -830,  -830,  -820,  -810,  -800,  -790,  -780,  -770,  -770,  -760,  -750,  -740,  -730,  -730,  -720,
     -710,  -700,  -690,  -690,  -680,  -670,  -660,  -660,  -650,  -640,  -640,  -630,  -620,  -610,  -610,  -600,
     -590,  -590,  -580,  -570,  -570,  -560,  -550,  -550,  -540,  -530,  -530,  -520,  -520,  -510,  -500,  -500,
     -490,  -490,  -480,  -470,  -470,  -460,  -460,  -450,  -440,  -440,  -430,  -430,  -420,  -420,  -410,  -410,
     -400,  -390,  -390,  -380,  -380,  -370,  -370,  -360,  -360,  -350,  -350,  -340,  -340,  -330,  -330,  -320,
     -320,  -310,  -310,  -300,  -300,  -290,  -290,  -280,  -280,  -270,  -270,  -260,  -260,  -260,  -250,  -250,
     -240,  -240,  -230,  -230,  -220,  -220,  -210,  -210,  -210,  -200,  -200,  -190,  -190,  -180,  -180,  -180,
     -170,  -170,  -160,  -160,  -160,  -150,  -150,  -140,  -140,  -140,  -130,  -130,  -120,  -120,  -120,  -110,
     -110,  -100,  -100,  -100,   -90,   -90,   -80,   -80,   -80,   -70,   -70,   -70,   -60,   -60,   -50,   -50,
      -50,   -40,   -40,   -40,   -30,   -30,   -30,   -20,   -20,   -20,   -10,   -10,   -10,   -10,   -10,   -10
};

static void SoundDriver_InitPanTable(void);
static void SoundDriver_LogError(const char* pText);
static float SoundDriver_DecibelsToGain(int db);
static uint32_t SoundDriver_BytesPerFrame(const SoundDriverSoundSlot* pSlot);
static void SoundDriver_ApplyVolume(tSysSoundBuffer* pBuffer);
static void SoundDriver_ApplyPan(tSysSoundBuffer* pBuffer);
static void SoundDriver_ApplyPitch(tSysSoundBuffer* pBuffer);
static void SoundDriver_LinkBuffer(tSysSoundBuffer* pBuffer);
static void SoundDriver_UnlinkBuffer(tSysSoundBuffer* pBuffer);
static void SoundDriver_StartOrResume(tSysSoundBuffer* pBuffer, int bLoop);
static void SoundDriver_PollDevice(void);
static void SoundDriver_OpenConfiguredOutput(void);
static bool SoundDriver_IsOutputDeviceAvailable(const std::string& id);
static void SoundDriver_CopyDeviceString(char* pDst, size_t size, const std::string& src);
static void SoundDriver_CalcListeneSoundMix(float x, float y, float z, float volume, float pitch, float* newVolume, float* pan, float* newPitch, float minRadius, float maxRadius, SoundEnvFlags envflags);

extern "C"
{

void SoundDriver_ResetGlobals(void)
{
    SoundDriver_aConfiguredOutputDevice.clear();
    SoundDriver_aCurrentOutputDevice.clear();
    SoundDriver_outputDevices.clear();
    SoundDriver_msecNextConfiguredCheck = 0;
}

int J3DAPI SoundDriver_Open(int bNoSound3D, int bGlobalFocus, tSysWindow hwnd, tDirectSound* pDirectSound, SoundCalcListenerSoundMixFunc pfCalcListenerSoundMix, SoundDriverGetSoundBufferDataFunc pfGetSoundBufferData, tPrintfFunc pfLogError)
{
    J3D_UNUSED(bNoSound3D);
    J3D_UNUSED(hwnd);

    SoundDriver_pfLogError             = pfLogError;
    SoundDriver_bUseGlobalFocusBuf     = bGlobalFocus;
    SoundDriver_pfCalcListenerSoundMix = pfCalcListenerSoundMix;
    SoundDriver_pfGetSoundBufferData   = pfGetSoundBufferData;

    SoundDriver_InitPanTable();

    SoundDriver_pDirectSound = pDirectSound;

    StdBabylonContext* pContext = stdBabylon_GetContext();
    SoundDriver_pDevice = pContext ? pContext->pAudioDevice : NULL;

    SoundDriver_bOutputLost             = false;
    SoundDriver_msecNextReopen          = 0;
    SoundDriver_msecNextConfiguredCheck = 0;
    SoundDriver_aCurrentOutputDevice.clear();

    if ( SoundDriver_pDevice )
    {
        // Added: the master gain carries the same hundredths-of-a-dB curve as a channel volume.
        SoundDriver_masterGain = SoundDriver_DecibelsToGain(SoundDriver_GetDecibelVolume(SoundDriver_maxVolume));
        static_cast<void>(SoundDriver_pDevice->setMasterGain(SoundDriver_masterGain));
        // Added: open the configured output. A missing one falls back to the default and keeps
        // the configured id, so a periodic check can switch back when it returns.
        SoundDriver_OpenConfiguredOutput();
    }
    else
    {
        SoundDriver_LogError("SoundDriver: no Babylon audio device; sound is silent.\n");
    }

    // The DirectSound driver returns whether a device is usable. The Babylon driver keeps the same
    // success answer whether or not an output could be opened, so the game runs silently instead.
    return 1;
}

void SoundDriver_Close(void)
{
    while ( SoundDriver_pBuffers )
    {
        SoundDriver_Release(SoundDriver_pBuffers);
    }

    if ( SoundDriver_pDevice )
    {
        SoundDriver_pDevice->close();
    }

    SoundDriver_pDevice                 = NULL;
    SoundDriver_pDirectSound            = NULL;
    SoundDriver_bOutputLost             = false;
    SoundDriver_msecNextReopen          = 0;
    SoundDriver_msecNextConfiguredCheck = 0;
    SoundDriver_aCurrentOutputDevice.clear();
    SoundDriver_outputDevices.clear();
}

void SoundDriver_SetMaxVolume(float volume)
{
    SoundDriver_maxVolume  = volume;
    SoundDriver_masterGain = SoundDriver_DecibelsToGain(SoundDriver_GetDecibelVolume(volume));

    if ( SoundDriver_pDevice )
    {
        static_cast<void>(SoundDriver_pDevice->setMasterGain(SoundDriver_masterGain));
    }
}

tSysSoundBuffer* J3DAPI SoundDriver_CreateAndPlay(size_t samplesPerSec, size_t nBitsPerSample, size_t numChannels, const uint8_t* pSoundData, size_t dataSize, tSoundChannelFlag* pFlags, tSysSoundBuffer* pSBuffer)
{
    if ( !pFlags )
    {
        return NULL;
    }

    // Added: a duplicate keeps the original sound alive and only makes a new voice, like
    // IDirectSound8::DuplicateSoundBuffer. When it cannot be made, fall through to a new sound.
    if ( pSBuffer && pSBuffer->pSlot )
    {
        SoundDriverSoundSlot* pSlot = pSBuffer->pSlot;
        babylon::audio::VoiceHandle voice;
        bool bCreated = false;

        if ( SoundDriver_pDevice )
        {
            if ( !pSlot->sound.isNull() )
            {
                voice = SoundDriver_pDevice->createVoice(pSlot->sound);
                bCreated = !voice.isNull();
            }
        }
        else
        {
            bCreated = true;
        }

        if ( bCreated )
        {
            tSysSoundBuffer* pBuffer = (tSysSoundBuffer*)STDMALLOC(sizeof(struct sSysSoundBuffer));
            if ( pBuffer )
            {
                ++pSlot->refCount;
                pBuffer->pSlot     = pSlot;
                pBuffer->voice     = voice;
                pBuffer->dbVolume  = 0; // 0 dB, a fresh DirectSound buffer
                pBuffer->pan       = SoundDriver_GetIntPan(0.0f);
                pBuffer->frequency = (int)pSlot->sampleRate;
                pBuffer->bStarted  = false;
                pBuffer->bPaused   = false;
                pBuffer->bLoop     = false;
                SoundDriver_LinkBuffer(pBuffer);

                SoundDriver_ApplyVolume(pBuffer);
                SoundDriver_ApplyPan(pBuffer);
                SoundDriver_ApplyPitch(pBuffer);

                // Added: the DirectSound duplicate path sets this flag before playing.
                *pFlags = (tSoundChannelFlag)(*pFlags | SOUND_CHANNEL_UNKNOWN_800000);

                if ( (*pFlags & SOUND_CHANNEL_PLAYING) != 0 )
                {
                    SoundDriver_StartOrResume(pBuffer, (*pFlags & (SOUND_CHANNEL_LOOP | SOUND_CHANNEL_PLAYING)) == (SOUND_CHANNEL_LOOP | SOUND_CHANNEL_PLAYING));
                }

                return pBuffer;
            }

            if ( SoundDriver_pDevice && !voice.isNull() )
            {
                SoundDriver_pDevice->destroyVoice(voice);
            }
        }
    }

    if ( SoundDriver_bUseGlobalFocusBuf || (*pFlags & SOUND_CHANNEL_GLOBALFOCUS) != 0 )
    {
        *pFlags = (tSoundChannelFlag)(*pFlags | SOUND_CHANNEL_GLOBALFOCUS);
    }

    // Added: no hardware 3D, so the buffer is always software mixed.
    *pFlags = (tSoundChannelFlag)(*pFlags & ~SOUND_CHANNEL_HWMIXING);

    babylon::audio::SampleFormat format;
    if ( nBitsPerSample == 8 )
    {
        format = babylon::audio::SampleFormat::Unsigned8;
    }
    else if ( nBitsPerSample == 16 )
    {
        format = babylon::audio::SampleFormat::Signed16;
    }
    else
    {
        return NULL;
    }

    if ( numChannels != 1 && numChannels != 2 )
    {
        return NULL;
    }

    if ( samplesPerSec == 0 || samplesPerSec > babylon::audio::kMaxSampleRate )
    {
        return NULL;
    }

    const uint8_t* pPcm      = pSoundData;
    size_t pcmSize           = dataSize;
    uint8_t* pUncompressed   = NULL;

    if ( (*pFlags & SOUND_CHANNEL_COMPRESSED) != 0 )
    {
        if ( !pSoundData )
        {
            return NULL;
        }

        const tAudioCompressedData* pCompressed = (const tAudioCompressedData*)pSoundData;
        pcmSize = pCompressed->uncompressedSize;
        if ( pcmSize == 0 )
        {
            return NULL;
        }

        pUncompressed = (uint8_t*)STDMALLOC(pcmSize);
        if ( !pUncompressed )
        {
            SoundDriver_LogError("SoundDriver: failed to allocate memory to uncompress sound data.\n");
            return NULL;
        }

        tAudioCompressorState compressor;
        AudioLib_ResetCompressor(&compressor);
        AudioLib_Uncompress(&compressor, pUncompressed, pCompressed->compressedData, (unsigned int)pcmSize);
        pPcm = pUncompressed;
    }

    const uint32_t frameSize = babylon::audio::bytesPerFrame(format, (uint32_t)numChannels);
    pcmSize -= pcmSize % frameSize;
    if ( !pPcm || pcmSize == 0 )
    {
        if ( pUncompressed )
        {
            STDFREE(pUncompressed);
        }

        return NULL;
    }

    SoundDriverSoundSlot* pSlot = (SoundDriverSoundSlot*)STDMALLOC(sizeof(SoundDriverSoundSlot));
    if ( !pSlot )
    {
        if ( pUncompressed )
        {
            STDFREE(pUncompressed);
        }

        return NULL;
    }

    pSlot->sound         = babylon::audio::SoundHandle{};
    pSlot->format        = format;
    pSlot->sampleRate    = (uint32_t)samplesPerSec;
    pSlot->bitsPerSample = (uint32_t)nBitsPerSample;
    pSlot->channels      = (uint32_t)numChannels;
    pSlot->dataSize      = pcmSize;
    pSlot->frameCount    = pcmSize / frameSize;
    pSlot->refCount      = 1;

    babylon::audio::VoiceHandle voice;
    if ( SoundDriver_pDevice )
    {
        babylon::audio::SoundDesc desc;
        desc.format     = format;
        desc.sampleRate = (uint32_t)samplesPerSec;
        desc.channels   = (uint32_t)numChannels;
        desc.data       = std::span<const std::byte>(reinterpret_cast<const std::byte*>(pPcm), pcmSize);

        pSlot->sound = SoundDriver_pDevice->createSound(desc);
        if ( pSlot->sound.isNull() )
        {
            if ( pUncompressed )
            {
                STDFREE(pUncompressed);
            }

            STDFREE(pSlot);
            return NULL;
        }

        voice = SoundDriver_pDevice->createVoice(pSlot->sound);
        if ( voice.isNull() )
        {
            SoundDriver_pDevice->destroySound(pSlot->sound);
            if ( pUncompressed )
            {
                STDFREE(pUncompressed);
            }

            STDFREE(pSlot);
            return NULL;
        }
    }

    if ( pUncompressed )
    {
        STDFREE(pUncompressed);
    }

    tSysSoundBuffer* pBuffer = (tSysSoundBuffer*)STDMALLOC(sizeof(struct sSysSoundBuffer));
    if ( !pBuffer )
    {
        if ( SoundDriver_pDevice )
        {
            SoundDriver_pDevice->destroyVoice(voice);
            SoundDriver_pDevice->destroySound(pSlot->sound);
        }

        STDFREE(pSlot);
        return NULL;
    }

    pBuffer->pSlot     = pSlot;
    pBuffer->voice     = voice;
    pBuffer->bStarted  = false;
    pBuffer->bPaused   = false;
    pBuffer->bLoop     = false;
    pBuffer->frequency = (int)pSlot->sampleRate;

    // Added: the DirectSound driver starts a RESTART buffer at DSBVOLUME_MIN scaled by the master
    // volume and every other fresh buffer at 0 dB.
    if ( (*pFlags & SOUND_CHANNEL_RESTART) != 0 )
    {
        pBuffer->dbVolume = (int)(SoundDriver_maxVolume * -10000.0f);
    }
    else
    {
        pBuffer->dbVolume = 0;
    }

    pBuffer->pan = SoundDriver_GetIntPan(0.0f);
    SoundDriver_LinkBuffer(pBuffer);
    SoundDriver_ApplyVolume(pBuffer);
    SoundDriver_ApplyPan(pBuffer);
    SoundDriver_ApplyPitch(pBuffer);

    if ( (*pFlags & SOUND_CHANNEL_PLAYING) != 0 )
    {
        SoundDriver_StartOrResume(pBuffer, (*pFlags & (SOUND_CHANNEL_LOOP | SOUND_CHANNEL_PLAYING)) == (SOUND_CHANNEL_LOOP | SOUND_CHANNEL_PLAYING));
    }

    return pBuffer;
}

void J3DAPI SoundDriver_Release(tSysSoundBuffer* pDSBuf)
{
    if ( !pDSBuf )
    {
        return;
    }

    SoundDriver_UnlinkBuffer(pDSBuf);

    if ( SoundDriver_pDevice && !pDSBuf->voice.isNull() )
    {
        SoundDriver_pDevice->destroyVoice(pDSBuf->voice);
    }

    if ( pDSBuf->pSlot )
    {
        --pDSBuf->pSlot->refCount;
        if ( pDSBuf->pSlot->refCount <= 0 )
        {
            if ( SoundDriver_pDevice && !pDSBuf->pSlot->sound.isNull() )
            {
                SoundDriver_pDevice->destroySound(pDSBuf->pSlot->sound);
            }

            STDFREE(pDSBuf->pSlot);
        }
    }

    STDFREE(pDSBuf);
}

void J3DAPI SoundDriver_Play(tSysSoundBuffer* pDSBuf, int bLoop)
{
    if ( pDSBuf )
    {
        SoundDriver_StartOrResume(pDSBuf, bLoop);
    }
}

void J3DAPI SoundDriver_Stop(tSysSoundBuffer* pDSBuf)
{
    if ( pDSBuf )
    {
        // Added: DirectSound Stop keeps the play cursor; pausing the Babylon voice does the same,
        // and Play resumes it instead of starting over.
        if ( SoundDriver_pDevice && !pDSBuf->voice.isNull() )
        {
            SoundDriver_pDevice->setPaused(pDSBuf->voice, true);
        }

        pDSBuf->bPaused = true;
    }
}

void J3DAPI SoundDriver_SetVolume(tSysSoundBuffer* pDSBuf, float volume)
{
    if ( pDSBuf )
    {
        pDSBuf->dbVolume = SoundDriver_GetDecibelVolume(volume);
        SoundDriver_ApplyVolume(pDSBuf);
    }
}

float J3DAPI SoundDriver_GetVolume(tSysSoundBuffer* pDSBuf)
{
    if ( !pDSBuf )
    {
        return 1.0f;
    }

    return SoundDriver_GetVolumeFromDecibels(pDSBuf->dbVolume);
}

void J3DAPI SoundDriver_SetPan(tSysSoundBuffer* pDSoundBuf, float pan)
{
    if ( pDSoundBuf )
    {
        pDSoundBuf->pan = SoundDriver_GetIntPan(pan);
        SoundDriver_ApplyPan(pDSoundBuf);
    }
}

float J3DAPI SoundDriver_GetPan(tSysSoundBuffer* pDSBuf)
{
    if ( !pDSBuf )
    {
        return 0.0f;
    }

    return SoundDriver_GetPanFromTable(pDSBuf->pan);
}

void J3DAPI SoundDriver_SetFrequency(tSysSoundBuffer* pDSBuf, float freq)
{
    if ( pDSBuf )
    {
        // Added: DirectSound stores the frequency as an integer; the getter reads it back that way.
        pDSBuf->frequency = (int32_t)freq;
        SoundDriver_ApplyPitch(pDSBuf);
    }
}

float J3DAPI SoundDriver_GetFrequency(tSysSoundBuffer* pDSBuf)
{
    if ( !pDSBuf )
    {
        return 22050.0f;
    }

    return (float)pDSBuf->frequency;
}

size_t J3DAPI SoundDriver_GetCurrentPosition(tSysSoundBuffer* pDSoundBuf)
{
    if ( !pDSoundBuf || !SoundDriver_pDevice || pDSoundBuf->voice.isNull() || !pDSoundBuf->pSlot )
    {
        return 0;
    }

    const std::optional<std::uint64_t> position = SoundDriver_pDevice->voicePosition(pDSoundBuf->voice);
    if ( !position )
    {
        return 0;
    }

    uint64_t bytes = *position * SoundDriver_BytesPerFrame(pDSoundBuf->pSlot);
    if ( pDSoundBuf->bLoop && pDSoundBuf->pSlot->dataSize != 0 )
    {
        // Added: like the DirectSound play cursor, a looping buffer's position wraps inside it.
        bytes %= pDSoundBuf->pSlot->dataSize;
    }

    return (size_t)bytes;
}

tSoundChannelFlag J3DAPI SoundDriver_GetStatusAndCaps(tSysSoundBuffer* pDSBuffer)
{
    if ( !pDSBuffer )
    {
        return (tSoundChannelFlag)0;
    }

    // Added: the Null driver's caps, plus PLAYING/LOOPING from the voice.
    uint32_t flags = SOUND_CHANNEL_SWMIXING | SOUND_CHANNEL_GLOBALFOCUS;

    if ( SoundDriver_pDevice && !pDSBuffer->voice.isNull() && SoundDriver_pDevice->isPlaying(pDSBuffer->voice) )
    {
        flags |= SOUND_CHANNEL_PLAYING;
        if ( pDSBuffer->bLoop )
        {
            flags |= SOUND_CHANNEL_LOOP;
        }
    }

    return (tSoundChannelFlag)flags;
}

void J3DAPI SoundDriver_SetListenerPosition(const rdVector3* pPos, const rdVector3* pVelocity, const rdVector3* pTopOrient, const rdVector3* pFrontOrient)
{
    J3D_UNUSED(pVelocity);
    J3D_UNUSED(pTopOrient);

    if ( pPos )
    {
        rdVector_Copy3(&SoundDriver_curListenerPos, pPos);
    }

    // Added: the same fallback pan-orientation math as DriverDX9.c for the software mix.
    if ( !SoundDriver_pfCalcListenerSoundMix && pFrontOrient )
    {
        SoundDriver_frontOrientATan = pFrontOrient->x == 0.0f ? 0.0f : atan2f(pFrontOrient->z, pFrontOrient->x);
    }
}

void SoundDriver_ListenerCommitDeferred(void)
{
    // Added: this is the once-per-Sound_Update call, so device events are polled once per frame and
    // a lost output is reopened here.
    SoundDriver_PollDevice();
}

void J3DAPI SoundDriver_SetPosAndVelocity(tSysSoundBuffer* pBuffer, float x, float y, float z, float velX, float velY, float velZ, float minDistance, float maxDistance)
{
    J3D_UNUSED(pBuffer);
    J3D_UNUSED(x);
    J3D_UNUSED(y);
    J3D_UNUSED(z);
    J3D_UNUSED(velX);
    J3D_UNUSED(velY);
    J3D_UNUSED(velZ);
    J3D_UNUSED(minDistance);
    J3D_UNUSED(maxDistance);
}

int J3DAPI SoundDriver_Update3DSound(tSysSoundBuffer** ppDSBuf, float x, float y, float z, float volume, float pitch, tSoundChannelFlag* pChannelFlags, float minRadius, float maxRadius, SoundEnvFlags envflags)
{
    tSysSoundBuffer* pDSBuf = ppDSBuf ? *ppDSBuf : NULL;

    // Added: no 3D hardware, so always the software mix path of DriverDX9.c Update3DSound.
    float newVolume = volume;
    float pan       = 0.0f;
    float newPitch  = pitch;

    SoundDriver_CalcListeneSoundMix(x, y, z, volume, pitch, &newVolume, &pan, &newPitch, minRadius, maxRadius, envflags);

    if ( newVolume < SOUNDDRIVER_FARVOLUMETHRESHOLD
        && (*pChannelFlags & (SOUND_CHANNEL_LOOP | SOUND_CHANNEL_PLAYING)) != (SOUND_CHANNEL_LOOP | SOUND_CHANNEL_PLAYING) )
    {
        return 0;
    }

    SoundDriver_SetVolume(pDSBuf, newVolume * SoundDriver_maxVolume);
    SoundDriver_SetPan(pDSBuf, pan);
    if ( newPitch != 1.0f )
    {
        float freq = SoundDriver_GetFrequency(pDSBuf);
        freq *= newPitch / pitch;
        SoundDriver_SetFrequency(pDSBuf, freq);
    }

    // Added: the DriverDX9.c 3D-channel stop branches all require SOUND_CHANNEL_3DSOUND, which this
    // backend never sets, so the software path ends with the far-restart check.
    if ( (*pChannelFlags & SOUND_CHANNEL_FAR) != 0 && newVolume > SOUNDDRIVER_FARVOLUMETHRESHOLD )
    {
        *pChannelFlags = (tSoundChannelFlag)(*pChannelFlags | SOUND_CHANNEL_RESTART);
        return 1;
    }

    return 1;
}

void J3DAPI SoundDriver_SetGlobals(float distanceFactor, float minDistance, float maxDistance, float rolloffFactor, float dopplerFactor)
{
    J3D_UNUSED(distanceFactor);
    J3D_UNUSED(minDistance);
    J3D_UNUSED(maxDistance);
    J3D_UNUSED(rolloffFactor);
    J3D_UNUSED(dopplerFactor);
}

int SoundDriver_Has3DHW(void)
{
    return 0;
}

int SoundDriver_Use3DCaps(void)
{
    return 0;
}

tDirectSound* SoundDriver_GetDSound(void)
{
    return SoundDriver_pDirectSound;
}

tDirectSound* J3DAPI SoundDriver_CreateDirectSound(int bNoSound3D)
{
    J3D_UNUSED(bNoSound3D);
    return NULL;
}

void J3DAPI SoundDriver_ReleaseDirectSound(tDirectSound* pDSound)
{
    J3D_UNUSED(pDSound);
}

void SoundDriver_CreateListener(void)
{
}

void SoundDriver_ReleaseListener(void)
{
}

void J3DAPI SoundDriver_SetOuputFormat(tDirectSound* pDSound, uint32_t nSamplesPerSec, uint32_t nBitsPerSample, uint32_t numChannels)
{
    J3D_UNUSED(pDSound);
    J3D_UNUSED(nSamplesPerSec);
    J3D_UNUSED(nBitsPerSample);
    J3D_UNUSED(numChannels);
}

tSysSoundBuffer* J3DAPI SoundDriver_GetPrimaryDSBuffer(tDirectSound* pDSound)
{
    J3D_UNUSED(pDSound);
    return NULL;
}

void J3DAPI SoundDriver_ReleasePrimaryDSBuffer()
{
}

int J3DAPI SoundDriver_GetDecibelVolume(float volume)
{
    int idx = (int)(volume * 255.0f);
    if ( idx < 0 )
    {
        idx = 0;
    }

    if ( idx < (int)STD_ARRAYLEN(SoundDriver_aDBTable) )
    {
        return SoundDriver_aDBTable[idx];
    }

    return 0;
}

int J3DAPI SoundDriver_GetIntPan(float pan)
{
    int idx = (int)(pan * 128.0f + 128.0f);
    if ( idx < 0 )
    {
        idx = 0;
    }

    if ( idx < (int)STD_ARRAYLEN(SoundDriver_aPanTable) )
    {
        return SoundDriver_aPanTable[idx];
    }

    return 0;
}

float J3DAPI SoundDriver_GetVolumeFromDecibels(int dB)
{
    int v1;
    int idx = (int)STD_ARRAYLEN(SoundDriver_aDBTable);
    do
    {
        v1 = idx--;
    } while ( v1 && SoundDriver_aDBTable[idx] > dB );

    return (float)idx * 1.0f / 255.0f;
}

float J3DAPI SoundDriver_GetPanFromTable(int pan)
{
    int v1;
    int idx = (int)STD_ARRAYLEN(SoundDriver_aPanTable);
    do
    {
        v1 = idx--;
    } while ( v1 && SoundDriver_aPanTable[idx] > pan );

    return (float)(idx - 128) * 1.0f / 128.0f;
}

uint32_t SoundDriver_GetTimeMsec(void)
{
    // Added: ADR-0027 - the driver's time always comes from the platform clock, never the device.
    return stdPlatform_GetTimeMsec();
}

void J3DAPI SoundDriver_Sleep(unsigned int dwMilliseconds)
{
    stdPlatform_Sleep(dwMilliseconds);
}

// Added: the output-device selection the Sound settings screen uses. This is the only adapter
// that can enumerate outputs; the list is cached here for the index-based read below.
size_t SoundDriver_GetNumOutputDevices(void)
{
    SoundDriver_outputDevices.clear();
    if ( SoundDriver_pDevice )
    {
        SoundDriver_outputDevices = SoundDriver_pDevice->listOutputDevices();
    }

    return SoundDriver_outputDevices.size();
}

// Added: reads one cached output. Both strings are truncated to fit and always terminated.
int SoundDriver_GetOutputDevice(size_t index, char* pId, size_t idSize, char* pName, size_t nameSize)
{
    if ( index >= SoundDriver_outputDevices.size() )
    {
        return 0;
    }

    const babylon::audio::OutputDevice& rDevice = SoundDriver_outputDevices[index];
    SoundDriver_CopyDeviceString(pId, idSize, rDevice.id);
    SoundDriver_CopyDeviceString(pName, nameSize, rDevice.name);
    return 1;
}

// Added: switches the output on the chosen device. Babylon keeps the sounds and voices across a
// close/open, so every voice keeps its state and position. NULL or an empty id selects the default.
int SoundDriver_SetOutputDevice(const char* pId)
{
    const std::string desired = pId ? pId : "";

    if ( !SoundDriver_pDevice )
    {
        return 0;
    }

    const std::string previous = SoundDriver_aCurrentOutputDevice;
    if ( SoundDriver_pDevice->open(desired) )
    {
        SoundDriver_aCurrentOutputDevice   = desired;
        SoundDriver_aConfiguredOutputDevice = desired;
        SoundDriver_bOutputLost            = false;
        SoundDriver_msecNextReopen         = 0;
        return 1;
    }

    // Added: a refused switch keeps the previous output, or opens the default when that one is
    // gone too, exactly as SetOutputDevice promises.
    if ( !previous.empty() && SoundDriver_IsOutputDeviceAvailable(previous)
        && SoundDriver_pDevice->open(previous) )
    {
        SoundDriver_aCurrentOutputDevice = previous;
    }
    else
    {
        static_cast<void>(SoundDriver_pDevice->open());
        SoundDriver_aCurrentOutputDevice.clear();
    }

    SoundDriver_bOutputLost    = false;
    SoundDriver_msecNextReopen = 0;
    return 0;
}

// Added: the id of the output in use, borrowed until the next call that changes it.
const char* SoundDriver_GetCurrentOutputDevice(void)
{
    return SoundDriver_aCurrentOutputDevice.c_str();
}

// Added: the startup config read hands the persisted choice to the driver through this before
// SoundDriver_Open(). The driver reads the key elsewhere; it never writes it.
void SoundDriver_SetConfiguredOutputDevice(const char* pId)
{
    SoundDriver_aConfiguredOutputDevice = pId ? pId : "";
}

} // extern "C"

static float SoundDriver_DecibelsToGain(int db)
{
    // DirectSound attenuation is in hundredths of a dB: 20 * log10(gain) = dB / 100.
    return (float)pow(10.0, (double)db / 2000.0);
}

static uint32_t SoundDriver_BytesPerFrame(const SoundDriverSoundSlot* pSlot)
{
    return babylon::audio::bytesPerFrame(pSlot->format, pSlot->channels);
}

static void SoundDriver_ApplyVolume(tSysSoundBuffer* pBuffer)
{
    if ( !SoundDriver_pDevice || pBuffer->voice.isNull() )
    {
        return;
    }

    float gain = SoundDriver_DecibelsToGain(pBuffer->dbVolume);
    if ( SoundDriver_masterGain > 0.0f )
    {
        // Added: Sound_SetMaxVolume applies the master volume to every channel itself, so the voice
        // gain is divided by the master gain to keep the effective gain exactly the DX9 buffer gain.
        gain /= SoundDriver_masterGain;
    }
    else
    {
        gain = 0.0f;
    }

    static_cast<void>(SoundDriver_pDevice->setVoiceGain(pBuffer->voice, gain));
}

static void SoundDriver_ApplyPan(tSysSoundBuffer* pBuffer)
{
    if ( !SoundDriver_pDevice || pBuffer->voice.isNull() )
    {
        return;
    }

    static_cast<void>(SoundDriver_pDevice->setVoicePan(pBuffer->voice, SoundDriver_GetPanFromTable(pBuffer->pan)));
}

static void SoundDriver_ApplyPitch(tSysSoundBuffer* pBuffer)
{
    if ( !SoundDriver_pDevice || pBuffer->voice.isNull() || !pBuffer->pSlot || pBuffer->pSlot->sampleRate == 0 )
    {
        return;
    }

    float pitch = (float)pBuffer->frequency / (float)pBuffer->pSlot->sampleRate;
    if ( pitch < babylon::audio::kMinPitch )
    {
        pitch = babylon::audio::kMinPitch;
    }
    else if ( pitch > babylon::audio::kMaxPitch )
    {
        pitch = babylon::audio::kMaxPitch;
    }

    static_cast<void>(SoundDriver_pDevice->setVoicePitch(pBuffer->voice, pitch));
}

static void SoundDriver_LinkBuffer(tSysSoundBuffer* pBuffer)
{
    pBuffer->pNext = SoundDriver_pBuffers;
    SoundDriver_pBuffers = pBuffer;
}

static void SoundDriver_UnlinkBuffer(tSysSoundBuffer* pBuffer)
{
    tSysSoundBuffer** ppCur = &SoundDriver_pBuffers;
    while ( *ppCur )
    {
        if ( *ppCur == pBuffer )
        {
            *ppCur = pBuffer->pNext;
            pBuffer->pNext = NULL;
            return;
        }

        ppCur = &(*ppCur)->pNext;
    }
}

static void SoundDriver_StartOrResume(tSysSoundBuffer* pBuffer, int bLoop)
{
    if ( !SoundDriver_pDevice || pBuffer->voice.isNull() )
    {
        pBuffer->bLoop = bLoop != 0;
        return;
    }

    bool bFinished = false;
    if ( pBuffer->pSlot && !pBuffer->bLoop )
    {
        const std::optional<std::uint64_t> position = SoundDriver_pDevice->voicePosition(pBuffer->voice);
        bFinished = position && *position >= pBuffer->pSlot->frameCount;
    }

    if ( !pBuffer->bStarted )
    {
        static_cast<void>(SoundDriver_pDevice->play(pBuffer->voice, bLoop != 0));
        pBuffer->bStarted = true;
        pBuffer->bPaused  = false;
    }
    else if ( pBuffer->bPaused )
    {
        // Added: DirectSound Play continues a stopped buffer, but a non-looping buffer that already
        // reached its end starts over.
        if ( bFinished )
        {
            static_cast<void>(SoundDriver_pDevice->play(pBuffer->voice, bLoop != 0));
        }
        else
        {
            SoundDriver_pDevice->setPaused(pBuffer->voice, false);
        }

        pBuffer->bPaused = false;
    }
    else if ( bFinished )
    {
        static_cast<void>(SoundDriver_pDevice->play(pBuffer->voice, bLoop != 0));
    }
    else if ( !SoundDriver_pDevice->isPlaying(pBuffer->voice) )
    {
        static_cast<void>(SoundDriver_pDevice->play(pBuffer->voice, bLoop != 0));
        pBuffer->bStarted = true;
    }

    pBuffer->bLoop = bLoop != 0;
}

static void SoundDriver_PollDevice(void)
{
    if ( !SoundDriver_pDevice )
    {
        return;
    }

    // Added: drain the event queue once per Sound_Update; device loss freezes the voices but keeps
    // their handles, so only the output has to be reopened.
    while ( const std::optional<babylon::audio::DeviceEvent> event = SoundDriver_pDevice->pollEvent() )
    {
        if ( *event == babylon::audio::DeviceEvent::DeviceLost )
        {
            SoundDriver_bOutputLost    = true;
            SoundDriver_msecNextReopen = SoundDriver_GetTimeMsec();
        }
    }

    const uint32_t now = SoundDriver_GetTimeMsec();

    if ( SoundDriver_bOutputLost )
    {
        if ( (int32_t)(now - SoundDriver_msecNextReopen) < 0 )
        {
            return;
        }

        // Added: reopen the configured device when it is available again, otherwise the default.
        const bool bUseConfigured = !SoundDriver_aConfiguredOutputDevice.empty()
            && SoundDriver_IsOutputDeviceAvailable(SoundDriver_aConfiguredOutputDevice);
        if ( bUseConfigured && SoundDriver_pDevice->open(SoundDriver_aConfiguredOutputDevice) )
        {
            SoundDriver_aCurrentOutputDevice  = SoundDriver_aConfiguredOutputDevice;
            SoundDriver_bOutputLost           = false;
            SoundDriver_msecNextConfiguredCheck = now + 1000u;
        }
        else if ( SoundDriver_pDevice->open() )
        {
            SoundDriver_aCurrentOutputDevice    = "";
            SoundDriver_bOutputLost             = false;
            SoundDriver_msecNextConfiguredCheck = now + 1000u;
        }
        else
        {
            // Added: try again in one second of platform time, never more often.
            SoundDriver_msecNextReopen = now + 1000u;
        }

        return;
    }

    // Added: after a fallback to the default, switch back once the configured device returns.
    // Checked once a second and never while it is the output already in use.
    if ( !SoundDriver_aConfiguredOutputDevice.empty()
        && SoundDriver_aConfiguredOutputDevice != SoundDriver_aCurrentOutputDevice
        && (int32_t)(now - SoundDriver_msecNextConfiguredCheck) >= 0 )
    {
        SoundDriver_msecNextConfiguredCheck = now + 1000u;
        if ( SoundDriver_IsOutputDeviceAvailable(SoundDriver_aConfiguredOutputDevice)
            && SoundDriver_pDevice->open(SoundDriver_aConfiguredOutputDevice) )
        {
            SoundDriver_aCurrentOutputDevice = SoundDriver_aConfiguredOutputDevice;
        }
    }
}

static void SoundDriver_OpenConfiguredOutput(void)
{
    SoundDriver_aCurrentOutputDevice.clear();
    if ( !SoundDriver_pDevice )
    {
        return;
    }

    if ( SoundDriver_aConfiguredOutputDevice.empty() )
    {
        // Opening failure logs one warning inside Device::open(); no second line is added here.
        static_cast<void>(SoundDriver_pDevice->open());
        return;
    }

    if ( !SoundDriver_IsOutputDeviceAvailable(SoundDriver_aConfiguredOutputDevice) )
    {
        // Added: the configured device was unplugged since the setting was written; use the
        // default and keep the id so it is used again when it returns.
        SoundDriver_LogError("SoundDriver: configured output device is not available; using the default.\n");
        static_cast<void>(SoundDriver_pDevice->open());
        return;
    }

    if ( SoundDriver_pDevice->open(SoundDriver_aConfiguredOutputDevice) )
    {
        SoundDriver_aCurrentOutputDevice = SoundDriver_aConfiguredOutputDevice;
    }
    else
    {
        static_cast<void>(SoundDriver_pDevice->open());
    }
}

static bool SoundDriver_IsOutputDeviceAvailable(const std::string& id)
{
    if ( !SoundDriver_pDevice || id.empty() )
    {
        return false;
    }

    for ( const babylon::audio::OutputDevice& rDevice : SoundDriver_pDevice->listOutputDevices() )
    {
        if ( rDevice.id == id )
        {
            return true;
        }
    }

    return false;
}

static void SoundDriver_CopyDeviceString(char* pDst, size_t size, const std::string& src)
{
    if ( !pDst || size == 0 )
    {
        return;
    }

    size_t length = src.size();
    if ( length >= size )
    {
        length = size - 1;
    }

    if ( length > 0 )
    {
        std::memcpy(pDst, src.data(), length);
    }

    pDst[length] = '\0';
}

static void SoundDriver_InitPanTable(void)
{
    for ( size_t i = 0; i < (STD_ARRAYLEN(SoundDriver_aPanTable) / 2); ++i )
    {
        SoundDriver_aPanTable[i + 128]        = -SoundDriver_aDBTable[255 - 2 * i];
        SoundDriver_aPanTable[127 - i]        = SoundDriver_aDBTable[255 - 2 * i];
    }
}

static void SoundDriver_LogError(const char* pText)
{
    if ( SoundDriver_pfLogError )
    {
        SoundDriver_pfLogError("%s", pText);
    }
}

static void J3DAPI SoundDriver_CalcListeneSoundMix(float x, float y, float z, float volume, float pitch, float* newVolume, float* pan, float* newPitch, float minRadius, float maxRadius, SoundEnvFlags envflags)
{
    J3D_UNUSED(y);
    J3D_UNUSED(SoundDriver_frontOrientATan);

    if ( !newVolume || !pan || !newPitch )
    {
        return;
    }

    *newVolume = volume;
    *pan       = 0.0f;
    *newPitch  = pitch;

    if ( !SoundDriver_pfCalcListenerSoundMix )
    {
        return;
    }

    SoundSpatialInfo spatialInfo = { 0 };
    spatialInfo.pos.x     = x;
    spatialInfo.pos.y     = y;
    spatialInfo.pos.z     = z;
    spatialInfo.volume    = volume;
    spatialInfo.pitch     = pitch;
    spatialInfo.minRadius = minRadius;
    spatialInfo.maxRadius = maxRadius;
    spatialInfo.flags     = envflags;

    SoundDriver_pfCalcListenerSoundMix(&spatialInfo, newVolume, pan, newPitch);
}

#ifdef J3D_TEST
extern "C" uint32_t SoundDriver_TestGetVoiceIndex(const tSysSoundBuffer* pBuffer)
{
    return pBuffer ? pBuffer->voice.index : 0;
}

extern "C" uint32_t SoundDriver_TestGetVoiceGeneration(const tSysSoundBuffer* pBuffer)
{
    return pBuffer ? pBuffer->voice.generation : 0;
}
#endif
