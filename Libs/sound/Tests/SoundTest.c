#include <unity_fixture.h>

#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <sound/AudioLib.h>
#include <sound/Sound.h>
#include <sound/DriverOutput.h>
#include <std/General/std.h>
#include <std/General/stdUtil.h>
#include <std/types.h>

#define SOUND_TEST_SOUND_HANDLE       1234u
#define SOUND_TEST_CHANNEL_HANDLE     1235u
#define SOUND_TEST_DATA_OFFSET        64u
#define SOUND_TEST_PCM_SIZE           1600u
#define SOUND_TEST_FRAME_SIZE         266u
#define SOUND_TEST_LARGE_FRAME_COUNT  2048u
#define SOUND_TEST_CACHE_SIZE         600000u
#define SOUND_TEST_MAX_ALLOCATIONS    8u
#define SOUND_TEST_NORMAL_BANK        1u
#define SOUND_TEST_BANK_CACHE_SIZE    8u
#define SOUND_TEST_BANK_MAX_SIZE      256u
#define SOUND_TEST_BANK_RECORD_SIZE   48u
#define SOUND_TEST_BANK_ENTROPY       0x12345678u
#define SOUND_TEST_IMPORTED_HANDLE    (SOUND_TEST_SOUND_HANDLE + 2u)
#define SOUND_TEST_ROUNDTRIP_HANDLE   (SOUND_TEST_SOUND_HANDLE + 4u)
#define SOUND_TEST_LIPSYNC_POINTER    0xDEADBEEFu

typedef struct SoundTestAllocation
{
    void* pData;
    size_t size;
} SoundTestAllocation;

typedef struct SoundTestBankFile
{
    uint8_t aData[SOUND_TEST_BANK_MAX_SIZE];
    size_t size;
    size_t position;
} SoundTestBankFile;

static tHostServices SoundTest_hostServices;
static SoundInfo SoundTest_soundInfo;
static tSoundChannel SoundTest_channel;
static uint8_t SoundTest_aSoundCache[SOUND_TEST_CACHE_SIZE];
static SoundTestAllocation SoundTest_aAllocations[SOUND_TEST_MAX_ALLOCATIONS];
static SoundTestBankFile SoundTest_bankFile;
static size_t SoundTest_numMallocCalls;
static size_t SoundTest_numFreeCalls;
static size_t SoundTest_numActiveAllocations;
static size_t SoundTest_failMallocCall;
static size_t SoundTest_maxAllocationSize;
static size_t SoundTest_numReallocCalls;
static size_t SoundTest_numUncompressCalls;
static unsigned int SoundTest_lastUncompressSize;
static uint32_t SoundTest_timeMsec;

TEST_GROUP(Sound);

static int J3DAPI SoundTest_Print(const char* pFormat, ...)
{
    J3D_UNUSED(pFormat);
    return 0;
}

static void J3DAPI SoundTest_Assert(const char* pText, const char* pFile, int line)
{
    J3D_UNUSED(pText);
    J3D_UNUSED(pFile);
    J3D_UNUSED(line);
}

static void* J3DAPI SoundTest_Malloc(size_t size)
{
    ++SoundTest_numMallocCalls;
    if ( SoundTest_numMallocCalls == SoundTest_failMallocCall )
    {
        return NULL;
    }

    void* pData = malloc(size);
    TEST_ASSERT_NOT_NULL(pData);
    TEST_ASSERT_TRUE(SoundTest_numActiveAllocations < STD_ARRAYLEN(SoundTest_aAllocations));

    SoundTest_aAllocations[SoundTest_numActiveAllocations].pData = pData;
    SoundTest_aAllocations[SoundTest_numActiveAllocations].size  = size;
    ++SoundTest_numActiveAllocations;

    if ( size > SoundTest_maxAllocationSize )
    {
        SoundTest_maxAllocationSize = size;
    }

    return pData;
}

static void J3DAPI SoundTest_Free(void* pData)
{
    if ( !pData )
    {
        return;
    }

    size_t allocationIndex = SoundTest_numActiveAllocations;
    for ( size_t i = 0; i < SoundTest_numActiveAllocations; ++i )
    {
        if ( SoundTest_aAllocations[i].pData == pData )
        {
            allocationIndex = i;
            break;
        }
    }

    TEST_ASSERT_TRUE(allocationIndex < SoundTest_numActiveAllocations);
    free(pData);
    ++SoundTest_numFreeCalls;

    --SoundTest_numActiveAllocations;
    SoundTest_aAllocations[allocationIndex] = SoundTest_aAllocations[SoundTest_numActiveAllocations];
    SoundTest_aAllocations[SoundTest_numActiveAllocations].pData = NULL;
    SoundTest_aAllocations[SoundTest_numActiveAllocations].size  = 0;
}

static void* J3DAPI SoundTest_Realloc(void* pData, size_t size)
{
    J3D_UNUSED(pData);
    J3D_UNUSED(size);
    ++SoundTest_numReallocCalls;
    return NULL;
}

static void SoundTest_WriteU32LE(uint8_t* pData, uint32_t value)
{
    pData[0] = (uint8_t)value;
    pData[1] = (uint8_t)(value >> 8);
    pData[2] = (uint8_t)(value >> 16);
    pData[3] = (uint8_t)(value >> 24);
}

static uint32_t SoundTest_ReadU32LE(const uint8_t* pData)
{
    return (uint32_t)pData[0]
        | ((uint32_t)pData[1] << 8)
        | ((uint32_t)pData[2] << 16)
        | ((uint32_t)pData[3] << 24);
}

static void SoundTest_SetLipSyncEntry(uint8_t* pData, int index, uint32_t timeKey, uint8_t mouthX, uint8_t mouthY)
{
    uint32_t entry = (timeKey & 0xFFFF0000u) | ((uint32_t)(mouthX & 0x7Fu) << 8) | (mouthY & 0x7Fu);
    SoundTest_WriteU32LE(pData + 8 + index * 4, entry);
}

static size_t J3DAPI SoundTest_FileRead(tFileHandle fh, void* pData, size_t size)
{
    SoundTestBankFile* pFile = (SoundTestBankFile*)(uintptr_t)fh;
    size_t remaining = pFile->size - pFile->position;

    if ( size > remaining )
    {
        size = remaining;
    }

    if ( size > 0 )
    {
        memcpy(pData, &pFile->aData[pFile->position], size);
        pFile->position += size;
    }

    return size;
}

static size_t J3DAPI SoundTest_FileWrite(tFileHandle fh, const void* pData, size_t size)
{
    SoundTestBankFile* pFile = (SoundTestBankFile*)(uintptr_t)fh;

    if ( pFile->position + size > sizeof(pFile->aData) )
    {
        return 0;
    }

    memcpy(&pFile->aData[pFile->position], pData, size);
    pFile->position += size;

    if ( pFile->position > pFile->size )
    {
        pFile->size = pFile->position;
    }

    return size;
}

static tFileHandle SoundTest_BankFileHandle(void)
{
    return (tFileHandle)(uintptr_t)&SoundTest_bankFile;
}

static void SoundTest_ResetBankFile(void)
{
    SoundTest_bankFile.size     = 0;
    SoundTest_bankFile.position = 0;
}

static void SoundTest_SetBankImage(const uint8_t* pImage, size_t size)
{
    TEST_ASSERT_TRUE(size <= sizeof(SoundTest_bankFile.aData));

    SoundTest_bankFile.position = 0;
    SoundTest_bankFile.size     = size;
    memcpy(SoundTest_bankFile.aData, pImage, size);
}

static void SoundTest_WriteBankRecord(uint8_t* pData, const SoundInfo* pInfo, uint32_t lipSyncSlot)
{
    SoundTest_WriteU32LE(pData + 0, pInfo->hSnd);
    SoundTest_WriteU32LE(pData + 4, pInfo->bankNum);
    SoundTest_WriteU32LE(pData + 8, pInfo->filePathOffset);
    SoundTest_WriteU32LE(pData + 12, pInfo->nameOffset);
    SoundTest_WriteU32LE(pData + 16, pInfo->dataOffset);
    SoundTest_WriteU32LE(pData + 20, lipSyncSlot);
    SoundTest_WriteU32LE(pData + 24, pInfo->sampleRate);
    SoundTest_WriteU32LE(pData + 28, pInfo->sempleBitSize);
    SoundTest_WriteU32LE(pData + 32, pInfo->numChannels);
    SoundTest_WriteU32LE(pData + 36, pInfo->dataSize);
    SoundTest_WriteU32LE(pData + 40, pInfo->bCompressed);
    SoundTest_WriteU32LE(pData + 44, pInfo->idx);
}

static void SoundTest_AssertSoundInfoFields(const SoundInfo* pExpected, const SoundInfo* pActual)
{
    TEST_ASSERT_EQUAL_UINT32(pExpected->hSnd, pActual->hSnd);
    TEST_ASSERT_EQUAL_UINT32(pExpected->bankNum, pActual->bankNum);
    TEST_ASSERT_EQUAL_UINT32(pExpected->filePathOffset, pActual->filePathOffset);
    TEST_ASSERT_EQUAL_UINT32(pExpected->nameOffset, pActual->nameOffset);
    TEST_ASSERT_EQUAL_UINT32(pExpected->dataOffset, pActual->dataOffset);
    TEST_ASSERT_EQUAL_UINT32(pExpected->sampleRate, pActual->sampleRate);
    TEST_ASSERT_EQUAL_UINT32(pExpected->sempleBitSize, pActual->sempleBitSize);
    TEST_ASSERT_EQUAL_UINT32(pExpected->numChannels, pActual->numChannels);
    TEST_ASSERT_EQUAL_UINT32(pExpected->dataSize, pActual->dataSize);
    TEST_ASSERT_EQUAL_UINT32(pExpected->bCompressed, pActual->bCompressed);
    TEST_ASSERT_EQUAL_UINT32(pExpected->idx, pActual->idx);
}

static void SoundTest_ResetAllocationState(void)
{
    TEST_ASSERT_EQUAL_size_t(0u, SoundTest_numActiveAllocations);

    STD_ZEROMEM(SoundTest_aAllocations, sizeof(SoundTest_aAllocations));
    SoundTest_numMallocCalls       = 0;
    SoundTest_numFreeCalls         = 0;
    SoundTest_numActiveAllocations = 0;
    SoundTest_failMallocCall       = SIZE_MAX;
    SoundTest_maxAllocationSize    = 0;
}

static void SoundTest_ConfigureDefaultFixture(void)
{
    STD_ZEROMEM(&SoundTest_soundInfo, sizeof(SoundTest_soundInfo));
    STD_ZEROMEM(&SoundTest_channel, sizeof(SoundTest_channel));
    STD_ZEROMEM(SoundTest_aSoundCache, sizeof(SoundTest_aSoundCache));

    TEST_ASSERT_TRUE(stdUtil_StringCopy(
        (char*)SoundTest_aSoundCache,
        STD_ARRAYLEN(SoundTest_aSoundCache),
        "voice_test.wav"
    ));
    STD_ZEROMEM(&SoundTest_aSoundCache[SOUND_TEST_DATA_OFFSET], SOUND_TEST_PCM_SIZE + sizeof(int16_t));
    SoundTest_aSoundCache[SOUND_TEST_DATA_OFFSET] = 0xA5;

    SoundTest_soundInfo.hSnd           = SOUND_TEST_SOUND_HANDLE;
    SoundTest_soundInfo.bankNum        = 0;
    SoundTest_soundInfo.filePathOffset = 0;
    SoundTest_soundInfo.dataOffset     = SOUND_TEST_DATA_OFFSET;
    SoundTest_soundInfo.sampleRate     = 8000;
    SoundTest_soundInfo.sempleBitSize  = 16;
    SoundTest_soundInfo.numChannels    = 1;
    SoundTest_soundInfo.dataSize       = SOUND_TEST_PCM_SIZE;
    SoundTest_soundInfo.bCompressed    = 0;

    SoundTest_channel.handle        = SOUND_TEST_CHANNEL_HANDLE;
    SoundTest_channel.hSnd          = SOUND_TEST_SOUND_HANDLE;
    SoundTest_channel.pDSoundBuffer = (tSysSoundBuffer*)(uintptr_t)1;

    Sound_TestSetChannels(&SoundTest_channel, 1);
    Sound_TestSetSoundBank(0, &SoundTest_soundInfo, 1, SoundTest_aSoundCache, sizeof(SoundTest_aSoundCache));
    Sound_TestSetCurrentPosition(0);
}

static void SoundTest_ConfigureCompressedFixture(void)
{
    SoundTest_soundInfo.bCompressed = 1;
    SoundTest_soundInfo.dataSize    = 16;
    SoundTest_WriteU32LE(&SoundTest_aSoundCache[SOUND_TEST_DATA_OFFSET], SOUND_TEST_PCM_SIZE);
}

// Altered: ADR-0027 - start a channel whose completion is driven by the sound layer's clock.
static void SoundTest_ConfigurePlayingChannel(tSoundChannelFlag flags, float pitch)
{
    STD_ZEROMEM(&SoundTest_channel, sizeof(SoundTest_channel));
    SoundTest_channel.handle         = SOUND_TEST_CHANNEL_HANDLE;
    SoundTest_channel.hSnd           = SOUND_TEST_SOUND_HANDLE;
    SoundTest_channel.pDSoundBuffer  = NULL;
    SoundTest_channel.volume         = 1.0f;
    SoundTest_channel.pitch          = pitch;
    SoundTest_channel.sampleRate     = (float)SoundTest_soundInfo.sampleRate;
    SoundTest_channel.flags          = flags;
    SoundTest_channel.msecLastUpdate = 0; // the virtual clock and the layer's clock both start at 0
    Sound_TestSetChannels(&SoundTest_channel, 1);
}

static void SoundTest_AdvanceTime(uint32_t msec)
{
    // Added: advance the sound layer's own test clock, not the platform clock, so the suite is
    // independent of the column stdPlatform_GetTimeMsec reads.
    SoundTest_timeMsec += msec;
    Sound_TestSetTimeMsec(SoundTest_timeMsec);
    Sound_Update(NULL, NULL, NULL, NULL);
}

static void J3DAPI SoundTest_Uncompress(tAudioCompressorState* pCompressorState, uint8_t* pOutSndData, const uint8_t* pCompressedData, unsigned int size)
{
    J3D_UNUSED(pCompressorState);
    J3D_UNUSED(pCompressedData);

    ++SoundTest_numUncompressCalls;
    SoundTest_lastUncompressSize = size;
    STD_ZEROMEM(pOutSndData, size);
}

static int SoundTest_IsTrackedAllocation(const void* pData)
{
    for ( size_t i = 0; i < SoundTest_numActiveAllocations; ++i )
    {
        if ( SoundTest_aAllocations[i].pData == pData )
        {
            return 1;
        }
    }

    return 0;
}

static void SoundTest_AssertLipSyncFailure(int msecLipSyncOffset)
{
    uint8_t mouthX = UINT8_MAX;
    uint8_t mouthY = UINT8_MAX;

    TEST_ASSERT_EQUAL_INT(0, Sound_GenerateLipSync(SOUND_TEST_CHANNEL_HANDLE, &mouthX, &mouthY, msecLipSyncOffset));
    TEST_ASSERT_EQUAL_UINT8(0, mouthX);
    TEST_ASSERT_EQUAL_UINT8(0, mouthY);
}

TEST_SETUP(Sound)
{
    // Altered: ADR-0027 - drive the sound layer's clock explicitly instead of reading wall time.
    SoundTest_timeMsec = 0;

    STD_ZEROMEM(&SoundTest_hostServices, sizeof(SoundTest_hostServices));
    SoundTest_hostServices.pStatusPrint  = SoundTest_Print;
    SoundTest_hostServices.pMessagePrint = SoundTest_Print;
    SoundTest_hostServices.pWarningPrint = SoundTest_Print;
    SoundTest_hostServices.pErrorPrint   = SoundTest_Print;
    SoundTest_hostServices.pDebugPrint   = SoundTest_Print;
    SoundTest_hostServices.pAssert       = SoundTest_Assert;
    SoundTest_hostServices.pMalloc       = SoundTest_Malloc;
    SoundTest_hostServices.pFree         = SoundTest_Free;
    SoundTest_hostServices.pRealloc      = SoundTest_Realloc;
    SoundTest_hostServices.pFileRead     = SoundTest_FileRead;
    SoundTest_hostServices.pFileWrite    = SoundTest_FileWrite;

    SoundTest_ResetBankFile();

    // Added: Sound_Play allocates the Null driver's buffers through the std memory layer.
    stdStartup(&SoundTest_hostServices);

    SoundTest_numActiveAllocations = 0;
    SoundTest_ResetAllocationState();
    SoundTest_numReallocCalls    = 0;
    SoundTest_numUncompressCalls = 0;
    SoundTest_lastUncompressSize = 0;

    Sound_TestResetLipSyncState();
    // Added: the override has to be set after the reset, which clears it.
    Sound_TestSetTimeMsec(SoundTest_timeMsec);
    TEST_ASSERT_EQUAL_INT(0, Sound_Initialize(&SoundTest_hostServices));
    Sound_TestSetOpenState();
    SoundTest_ConfigureDefaultFixture();
    Sound_TestSetUncompressFunc(SoundTest_Uncompress);
}

TEST_TEAR_DOWN(Sound)
{
    if ( SoundTest_soundInfo.pLipSyncData && SoundTest_IsTrackedAllocation(SoundTest_soundInfo.pLipSyncData) )
    {
        SoundTest_Free(SoundTest_soundInfo.pLipSyncData);
        SoundTest_soundInfo.pLipSyncData = NULL;
    }

    size_t numLeakedAllocations = SoundTest_numActiveAllocations;
    while ( SoundTest_numActiveAllocations > 0 )
    {
        SoundTest_Free(SoundTest_aAllocations[SoundTest_numActiveAllocations - 1].pData);
    }

    Sound_Uninitialize();
    stdShutdown();
    Sound_TestResetLipSyncState();
    TEST_ASSERT_EQUAL_size_t(0u, SoundTest_numReallocCalls);
    TEST_ASSERT_EQUAL_size_t(0u, numLeakedAllocations);
}

TEST(Sound, TestFailures)
{
    uint8_t mouthX = UINT8_MAX;
    uint8_t mouthY = UINT8_MAX;

    TEST_ASSERT_EQUAL_INT(0, Sound_GenerateLipSync(SOUND_INVALIDHANDLE, &mouthX, &mouthY, 0));
    TEST_ASSERT_EQUAL_UINT8(0, mouthX);
    TEST_ASSERT_EQUAL_UINT8(0, mouthY);

    SoundTest_channel.hSnd = SOUND_TEST_SOUND_HANDLE + 2u;
    mouthX = UINT8_MAX;
    mouthY = UINT8_MAX;
    TEST_ASSERT_EQUAL_INT(0, Sound_GenerateLipSync(SOUND_TEST_CHANNEL_HANDLE, &mouthX, &mouthY, 0));
    TEST_ASSERT_EQUAL_UINT8(0, mouthX);
    TEST_ASSERT_EQUAL_UINT8(0, mouthY);
    SoundTest_channel.hSnd = SOUND_TEST_SOUND_HANDLE;

    static const struct
    {
        uint32_t sampleRate;
        uint32_t bitsPerSample;
        uint32_t numChannels;
    } aInvalidFormats[] = {
        { 0, 16, 1 },
        { 8000, 8, 1 },
        { 8000, 24, 1 },
        { 8000, 16, 0 },
        { 8000, 16, 2 }
    };

    for ( size_t caseIndex = 0; caseIndex < STD_ARRAYLEN(aInvalidFormats); ++caseIndex )
    {
        SoundTest_soundInfo.sampleRate    = aInvalidFormats[caseIndex].sampleRate;
        SoundTest_soundInfo.sempleBitSize = aInvalidFormats[caseIndex].bitsPerSample;
        SoundTest_soundInfo.numChannels   = aInvalidFormats[caseIndex].numChannels;
        mouthX = UINT8_MAX;
        mouthY = UINT8_MAX;

        TEST_ASSERT_EQUAL_INT(0, Sound_GenerateLipSync(SOUND_TEST_CHANNEL_HANDLE, &mouthX, &mouthY, 0));
        TEST_ASSERT_EQUAL_UINT8(0, mouthX);
        TEST_ASSERT_EQUAL_UINT8(0, mouthY);
    }

    SoundTest_soundInfo.sampleRate    = 8000;
    SoundTest_soundInfo.sempleBitSize = 16;
    SoundTest_soundInfo.numChannels   = 1;
    Sound_TestSetNoLipSync(1);
    TEST_ASSERT_EQUAL_INT(0, Sound_GenerateLipSync(SOUND_TEST_CHANNEL_HANDLE, &mouthX, &mouthY, 0));
    TEST_ASSERT_EQUAL_size_t(0u, SoundTest_numMallocCalls);

}

TEST(Sound, TestUncompressedCache)
{
    uint8_t aOriginalPcm[SOUND_TEST_PCM_SIZE];
    memcpy(aOriginalPcm, &SoundTest_aSoundCache[SOUND_TEST_DATA_OFFSET + 1u], sizeof(aOriginalPcm));
    TEST_ASSERT_EACH_EQUAL_UINT8(0, aOriginalPcm, sizeof(aOriginalPcm));

    uint8_t mouthX = UINT8_MAX;
    uint8_t mouthY = UINT8_MAX;

    TEST_ASSERT_EQUAL_INT(1, Sound_GenerateLipSync(SOUND_TEST_CHANNEL_HANDLE, &mouthX, &mouthY, 0));
    TEST_ASSERT_NOT_NULL(SoundTest_soundInfo.pLipSyncData);
    TEST_ASSERT_EQUAL_MEMORY("SYNC", SoundTest_soundInfo.pLipSyncData, 4);
    TEST_ASSERT_EQUAL_HEX32(0x00002000u, SoundTest_ReadU32LE(SoundTest_soundInfo.pLipSyncData + 8));
    TEST_ASSERT_EQUAL_UINT8(32, mouthX);
    TEST_ASSERT_EQUAL_UINT8(0, mouthY);
    TEST_ASSERT_EQUAL_size_t(1u, SoundTest_numMallocCalls);
    TEST_ASSERT_EQUAL_size_t(1u, SoundTest_numActiveAllocations);
    TEST_ASSERT_EQUAL_MEMORY(aOriginalPcm, &SoundTest_aSoundCache[SOUND_TEST_DATA_OFFSET + 1u], sizeof(aOriginalPcm));

    uint8_t* pCachedData = SoundTest_soundInfo.pLipSyncData;
    Sound_TestSetNoLipSync(1);
    Sound_TestSetCurrentPosition(532);

    TEST_ASSERT_EQUAL_INT(1, Sound_GenerateLipSync(SOUND_TEST_CHANNEL_HANDLE, &mouthX, &mouthY, 0));
    TEST_ASSERT_EQUAL_PTR(pCachedData, SoundTest_soundInfo.pLipSyncData);
    TEST_ASSERT_EQUAL_size_t(1u, SoundTest_numMallocCalls);
}

TEST(Sound, TestPlaybackPosition)
{
    uint8_t aLipSyncData[8 + 3 * 4];
    memcpy(aLipSyncData, "SYNC", 4);
    SoundTest_WriteU32LE(aLipSyncData + 4, 3);
    SoundTest_SetLipSyncEntry(aLipSyncData, 0, 0x00000000u, 1, 2);
    SoundTest_SetLipSyncEntry(aLipSyncData, 1, 0x00020000u, 3, 4);
    SoundTest_SetLipSyncEntry(aLipSyncData, 2, 0x00030000u, 5, 6);
    SoundTest_soundInfo.pLipSyncData = aLipSyncData;

    uint8_t mouthX = UINT8_MAX;
    uint8_t mouthY = UINT8_MAX;

    Sound_TestSetCurrentPosition(0);
    TEST_ASSERT_EQUAL_INT(1, Sound_GenerateLipSync(SOUND_TEST_CHANNEL_HANDLE, &mouthX, &mouthY, 0));
    TEST_ASSERT_EQUAL_UINT8(1, mouthX);
    TEST_ASSERT_EQUAL_UINT8(2, mouthY);

    Sound_TestSetCurrentPosition(512);
    TEST_ASSERT_EQUAL_INT(1, Sound_GenerateLipSync(SOUND_TEST_CHANNEL_HANDLE, &mouthX, &mouthY, 0));
    TEST_ASSERT_EQUAL_UINT8(3, mouthX);
    TEST_ASSERT_EQUAL_UINT8(4, mouthY);

    Sound_TestSetCurrentPosition(0);
    TEST_ASSERT_EQUAL_INT(1, Sound_GenerateLipSync(SOUND_TEST_CHANNEL_HANDLE, NULL, &mouthY, 16));
    TEST_ASSERT_EQUAL_UINT8(2, mouthY);
    TEST_ASSERT_EQUAL_INT(1, Sound_GenerateLipSync(SOUND_TEST_CHANNEL_HANDLE, &mouthX, NULL, 16));
    TEST_ASSERT_EQUAL_UINT8(1, mouthX);

    Sound_TestSetCurrentPosition(SOUND_TEST_PCM_SIZE - 400u);
    mouthX = UINT8_MAX;
    mouthY = UINT8_MAX;
    TEST_ASSERT_EQUAL_INT(0, Sound_GenerateLipSync(SOUND_TEST_CHANNEL_HANDLE, &mouthX, &mouthY, 0));
    TEST_ASSERT_EQUAL_UINT8(0, mouthX);
    TEST_ASSERT_EQUAL_UINT8(0, mouthY);

    Sound_TestSetCurrentPosition(SOUND_TEST_PCM_SIZE - 401u);
    TEST_ASSERT_EQUAL_INT(1, Sound_GenerateLipSync(SOUND_TEST_CHANNEL_HANDLE, &mouthX, &mouthY, 0));
    TEST_ASSERT_EQUAL_UINT8(5, mouthX);
    TEST_ASSERT_EQUAL_UINT8(6, mouthY);

    Sound_TestSetCurrentPosition(0);
    TEST_ASSERT_EQUAL_INT(1, Sound_GenerateLipSync(SOUND_TEST_CHANNEL_HANDLE, NULL, NULL, 0));

    SoundTest_AssertLipSyncFailure(-1);

    Sound_TestSetCurrentPosition(512);
    SoundTest_AssertLipSyncFailure(INT_MAX);

    Sound_TestSetCurrentPosition(SOUND_TEST_PCM_SIZE);
    SoundTest_AssertLipSyncFailure(0);

    aLipSyncData[0] = 'X';
    Sound_TestSetCurrentPosition(0);
    SoundTest_AssertLipSyncFailure(0);
}

TEST(Sound, TestCompressedCache)
{
    SoundTest_ConfigureCompressedFixture();

    uint8_t mouthX = UINT8_MAX;
    uint8_t mouthY = UINT8_MAX;

    TEST_ASSERT_EQUAL_INT(1, Sound_GenerateLipSync(SOUND_TEST_CHANNEL_HANDLE, &mouthX, &mouthY, 0));
    TEST_ASSERT_EQUAL_UINT8(32, mouthX);
    TEST_ASSERT_EQUAL_UINT8(0, mouthY);
    TEST_ASSERT_EQUAL_size_t(1u, SoundTest_numUncompressCalls);
    TEST_ASSERT_EQUAL_UINT32(SOUND_TEST_PCM_SIZE, SoundTest_lastUncompressSize);
    TEST_ASSERT_EQUAL_size_t(2u, SoundTest_numMallocCalls);
    TEST_ASSERT_EQUAL_size_t(1u, SoundTest_numFreeCalls);
    TEST_ASSERT_EQUAL_size_t(1u, SoundTest_numActiveAllocations);
    TEST_ASSERT_EQUAL_size_t(SOUND_TEST_PCM_SIZE + sizeof(int16_t), SoundTest_maxAllocationSize);

    TEST_ASSERT_EQUAL_INT(1, Sound_GenerateLipSync(SOUND_TEST_CHANNEL_HANDLE, &mouthX, &mouthY, 0));
    TEST_ASSERT_EQUAL_size_t(1u, SoundTest_numUncompressCalls);
    TEST_ASSERT_EQUAL_size_t(2u, SoundTest_numMallocCalls);
}

TEST(Sound, TestAllocationFailures)
{
    uint8_t mouthX = UINT8_MAX;
    uint8_t mouthY = UINT8_MAX;

    SoundTest_failMallocCall = 1;
    TEST_ASSERT_EQUAL_INT(0, Sound_GenerateLipSync(SOUND_TEST_CHANNEL_HANDLE, &mouthX, &mouthY, 0));
    TEST_ASSERT_NULL(SoundTest_soundInfo.pLipSyncData);
    TEST_ASSERT_EQUAL_size_t(0u, SoundTest_numActiveAllocations);

    SoundTest_ResetAllocationState();
    SoundTest_ConfigureCompressedFixture();
    SoundTest_failMallocCall = 1;
    TEST_ASSERT_EQUAL_INT(0, Sound_GenerateLipSync(SOUND_TEST_CHANNEL_HANDLE, &mouthX, &mouthY, 0));
    TEST_ASSERT_EQUAL_size_t(0u, SoundTest_numUncompressCalls);
    TEST_ASSERT_EQUAL_size_t(0u, SoundTest_numActiveAllocations);

    SoundTest_ResetAllocationState();
    SoundTest_failMallocCall = 2;
    TEST_ASSERT_EQUAL_INT(0, Sound_GenerateLipSync(SOUND_TEST_CHANNEL_HANDLE, &mouthX, &mouthY, 0));
    TEST_ASSERT_EQUAL_size_t(1u, SoundTest_numUncompressCalls);
    TEST_ASSERT_EQUAL_size_t(1u, SoundTest_numFreeCalls);
    TEST_ASSERT_EQUAL_size_t(0u, SoundTest_numActiveAllocations);
    TEST_ASSERT_NULL(SoundTest_soundInfo.pLipSyncData);
}

TEST(Sound, TestLargeLipSyncBuffer)
{
    SoundTest_soundInfo.dataSize = SOUND_TEST_LARGE_FRAME_COUNT * SOUND_TEST_FRAME_SIZE;
    STD_ZEROMEM(&SoundTest_aSoundCache[SOUND_TEST_DATA_OFFSET + 1u], SoundTest_soundInfo.dataSize);

    uint8_t mouthX = UINT8_MAX;
    uint8_t mouthY = UINT8_MAX;

    TEST_ASSERT_EQUAL_INT(1, Sound_GenerateLipSync(SOUND_TEST_CHANNEL_HANDLE, &mouthX, &mouthY, 0));
    TEST_ASSERT_GREATER_THAN_size_t(8192u, SoundTest_maxAllocationSize);
    TEST_ASSERT_NOT_NULL(SoundTest_soundInfo.pLipSyncData);
    TEST_ASSERT_EQUAL_MEMORY("SYNC", SoundTest_soundInfo.pLipSyncData, 4);
}

TEST(Sound, TestMouthLevelMacro)
{
    for ( uint32_t mouthPosition = 0; mouthPosition < AUDIOLIB_LIPSYNC_MOUTH_POSITION_COUNT; ++mouthPosition )
    {
        uint32_t expectedLevel = mouthPosition / SOUND_LIPSYNC_MOUTH_LEVEL_STEP;
        TEST_ASSERT_EQUAL_UINT32(expectedLevel, SOUND_LIPSYNC_GETMOUTHLEVEL(mouthPosition));
    }
}

// Regression: with all channels busy and none of lower priority Sound_Play scanned past the end of the channel array
TEST(Sound, TestPlayAllChannelsBusy)
{
    // The second element is outside of the channel list (numChannels == 1) and looks like a free channel.
    tSoundChannel aChannels[2];
    STD_ZEROMEM(aChannels, sizeof(aChannels));
    aChannels[0]               = SoundTest_channel;
    aChannels[0].priority      = 5;
    Sound_TestSetChannels(aChannels, 1);
    Sound_TestSetOpen(1);

    TEST_ASSERT_EQUAL_UINT32(SOUND_INVALIDHANDLE, Sound_Play(SOUND_TEST_SOUND_HANDLE, 1.0f, 0.0f, 3, SOUND_CHANNEL_STARTFAR, 0)); // STARTFAR skips the driver, so a write outside the list fails the test instead of crashing
    TEST_ASSERT_EQUAL_UINT32(SOUND_INVALIDHANDLE, aChannels[1].handle);
    TEST_ASSERT_EQUAL_UINT32(SOUND_INVALIDHANDLE, aChannels[1].hSnd);
    TEST_ASSERT_EQUAL_UINT32(SOUND_TEST_CHANNEL_HANDLE, aChannels[0].handle);

    Sound_TestSetOpen(0);
    Sound_TestSetChannels(&SoundTest_channel, 1);
}

// Regression: Sound_Load dropped the first char of a path without separator, so the sound in the bank was not found
TEST(Sound, TestLoadFindsBankedSound)
{
    uint32_t sndIdx = UINT32_MAX;
    SoundTest_soundInfo.idx = 5u;

    TEST_ASSERT_EQUAL_UINT32(SOUND_TEST_SOUND_HANDLE, Sound_Load("voice_test.wav", &sndIdx));
    TEST_ASSERT_EQUAL_UINT32(5u, sndIdx);

    sndIdx = UINT32_MAX;
    TEST_ASSERT_EQUAL_UINT32(SOUND_TEST_SOUND_HANDLE, Sound_Load("sound\\voice_test.wav", &sndIdx));
    TEST_ASSERT_EQUAL_UINT32(5u, sndIdx);

    sndIdx = UINT32_MAX;
    TEST_ASSERT_EQUAL_UINT32(SOUND_TEST_SOUND_HANDLE, Sound_Load("/res/sound/voice_test.wav", &sndIdx));
    TEST_ASSERT_EQUAL_UINT32(5u, sndIdx);

    // Leading separator
    TEST_ASSERT_EQUAL_UINT32(SOUND_TEST_SOUND_HANDLE, Sound_Load("\\voice_test.wav", NULL));
}

// Regression: Sound_GetSoundIndex matched the invalid handle (0) with unused sound infos
TEST(Sound, TestGetSoundIndexInvalidHandle)
{
    SoundInfo aInfos[2];
    aInfos[0]     = SoundTest_soundInfo;
    aInfos[0].idx = 3u;
    STD_ZEROMEM(&aInfos[1], sizeof(aInfos[1]));
    aInfos[1].idx = 7u;
    Sound_TestSetSoundBank(0, aInfos, STD_ARRAYLEN(aInfos), SoundTest_aSoundCache, sizeof(SoundTest_aSoundCache));

    TEST_ASSERT_EQUAL_UINT32(SOUND_INVALISOUNDINFOINDEX, Sound_GetSoundIndex(SOUND_INVALIDHANDLE));
    TEST_ASSERT_EQUAL_UINT32(3u, Sound_GetSoundIndex(SOUND_TEST_SOUND_HANDLE));
    TEST_ASSERT_EQUAL_UINT32(SOUND_INVALISOUNDINFOINDEX, Sound_GetSoundIndex(SOUND_TEST_SOUND_HANDLE + 1u));
}

TEST(Sound, TestNonLoopingFinishesAtLength)
{
    SoundTest_ConfigurePlayingChannel(SOUND_CHANNEL_PLAYING, 1.0f);

    SoundTest_AdvanceTime(99);
    TEST_ASSERT_NOT_NULL(Sound_GetChannel(SOUND_TEST_CHANNEL_HANDLE));
    TEST_ASSERT_EQUAL_UINT32(SOUND_CHANNEL_PLAYING, (uint32_t)(Sound_GetChannelFlags(SOUND_TEST_CHANNEL_HANDLE) & SOUND_CHANNEL_PLAYING));

    SoundTest_AdvanceTime(1);
    TEST_ASSERT_NULL(Sound_GetChannel(SOUND_TEST_CHANNEL_HANDLE));
}

TEST(Sound, TestPitchScalesPlayPosition)
{
    float progress = 0.0f;

    SoundTest_ConfigurePlayingChannel(SOUND_CHANNEL_PLAYING, 2.0f);

    SoundTest_AdvanceTime(25);
    TEST_ASSERT_TRUE(Sound_GetChannelPlayProgress(SOUND_TEST_CHANNEL_HANDLE, &progress));
    TEST_ASSERT_EQUAL_FLOAT(50.0f, progress);

    Sound_SetPitch(SOUND_TEST_CHANNEL_HANDLE, 1.0f);
    SoundTest_AdvanceTime(49);
    TEST_ASSERT_TRUE(Sound_GetChannelPlayProgress(SOUND_TEST_CHANNEL_HANDLE, &progress));
    TEST_ASSERT_EQUAL_FLOAT(99.0f, progress);

    SoundTest_AdvanceTime(1);
    TEST_ASSERT_NULL(Sound_GetChannel(SOUND_TEST_CHANNEL_HANDLE));
}

TEST(Sound, TestLoopingSoundNeverFinishes)
{
    float progress = 0.0f;

    SoundTest_ConfigurePlayingChannel((tSoundChannelFlag)(SOUND_CHANNEL_LOOP | SOUND_CHANNEL_PLAYING), 1.0f);

    SoundTest_AdvanceTime(100);
    TEST_ASSERT_NOT_NULL(Sound_GetChannel(SOUND_TEST_CHANNEL_HANDLE));

    SoundTest_AdvanceTime(5000);
    TEST_ASSERT_NOT_NULL(Sound_GetChannel(SOUND_TEST_CHANNEL_HANDLE));
    TEST_ASSERT_TRUE(Sound_GetChannelPlayProgress(SOUND_TEST_CHANNEL_HANDLE, &progress));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, progress);

    SoundTest_AdvanceTime(25);
    TEST_ASSERT_TRUE(Sound_GetChannelPlayProgress(SOUND_TEST_CHANNEL_HANDLE, &progress));
    TEST_ASSERT_EQUAL_FLOAT(25.0f, progress);
}

TEST(Sound, TestLayerPauseStopsPlayClock)
{
    float progress = 0.0f;

    SoundTest_ConfigurePlayingChannel(SOUND_CHANNEL_PLAYING, 1.0f);

    SoundTest_AdvanceTime(10);
    TEST_ASSERT_TRUE(Sound_GetChannelPlayProgress(SOUND_TEST_CHANNEL_HANDLE, &progress));
    TEST_ASSERT_EQUAL_FLOAT(10.0f, progress);

    Sound_Pause();
    SoundTest_AdvanceTime(50);
    TEST_ASSERT_TRUE(Sound_GetChannelPlayProgress(SOUND_TEST_CHANNEL_HANDLE, &progress));
    TEST_ASSERT_EQUAL_FLOAT(10.0f, progress);

    Sound_Resume();
    SoundTest_AdvanceTime(0);
    TEST_ASSERT_TRUE(Sound_GetChannelPlayProgress(SOUND_TEST_CHANNEL_HANDLE, &progress));
    TEST_ASSERT_EQUAL_FLOAT(10.0f, progress);

    SoundTest_AdvanceTime(10);
    TEST_ASSERT_TRUE(Sound_GetChannelPlayProgress(SOUND_TEST_CHANNEL_HANDLE, &progress));
    TEST_ASSERT_EQUAL_FLOAT(20.0f, progress);

    SoundTest_AdvanceTime(80);
    TEST_ASSERT_NULL(Sound_GetChannel(SOUND_TEST_CHANNEL_HANDLE));
}

TEST(Sound, TestChannelPauseStopsPlayClock)
{
    float progress = 0.0f;

    SoundTest_ConfigurePlayingChannel((tSoundChannelFlag)(SOUND_CHANNEL_PLAYING | SOUND_CHANNEL_PAUSED), 1.0f);

    SoundTest_AdvanceTime(30);
    TEST_ASSERT_TRUE(Sound_GetChannelPlayProgress(SOUND_TEST_CHANNEL_HANDLE, &progress));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, progress);

    SoundTest_channel.flags &= ~SOUND_CHANNEL_PAUSED;
    SoundTest_AdvanceTime(10);
    TEST_ASSERT_TRUE(Sound_GetChannelPlayProgress(SOUND_TEST_CHANNEL_HANDLE, &progress));
    TEST_ASSERT_EQUAL_FLOAT(10.0f, progress);
}

TEST(Sound, TestSoundStartedWhilePausedPlays)
{
    float progress = 0.0f;

    SoundTest_channel.pDSoundBuffer = NULL;
    Sound_StopChannel(SOUND_TEST_CHANNEL_HANDLE);
    Sound_Pause();

    tSoundChannelHandle hChannel = Sound_Play(SOUND_TEST_SOUND_HANDLE, 1.0f, 0.0f, 0, SOUND_CHANNEL_PLAYING, 0);
    TEST_ASSERT_TRUE(hChannel != SOUND_INVALIDHANDLE);
    TEST_ASSERT_NOT_NULL(Sound_GetChannel(hChannel));

    SoundTest_AdvanceTime(99);
    TEST_ASSERT_NOT_NULL(Sound_GetChannel(hChannel));
    TEST_ASSERT_TRUE(Sound_GetChannelPlayProgress(hChannel, &progress));
    TEST_ASSERT_EQUAL_FLOAT(99.0f, progress);

    SoundTest_AdvanceTime(1);
    TEST_ASSERT_NULL(Sound_GetChannel(hChannel));

    Sound_Resume();
}

TEST(Sound, TestPlayAcrossPauseResume)
{
    float progress = 0.0f;

    SoundTest_ConfigurePlayingChannel(SOUND_CHANNEL_PLAYING, 1.0f);

    SoundTest_AdvanceTime(40);
    TEST_ASSERT_TRUE(Sound_GetChannelPlayProgress(SOUND_TEST_CHANNEL_HANDLE, &progress));
    TEST_ASSERT_EQUAL_FLOAT(40.0f, progress);

    Sound_Pause();
    SoundTest_AdvanceTime(60);
    TEST_ASSERT_TRUE(Sound_GetChannelPlayProgress(SOUND_TEST_CHANNEL_HANDLE, &progress));
    TEST_ASSERT_EQUAL_FLOAT(40.0f, progress);

    Sound_Resume();
    SoundTest_AdvanceTime(59);
    TEST_ASSERT_TRUE(Sound_GetChannelPlayProgress(SOUND_TEST_CHANNEL_HANDLE, &progress));
    TEST_ASSERT_EQUAL_FLOAT(99.0f, progress);

    SoundTest_AdvanceTime(1);
    TEST_ASSERT_NULL(Sound_GetChannel(SOUND_TEST_CHANNEL_HANDLE));
}

TEST(Sound, TestPlayProgressUsesComputedPosition)
{
    float progress = 0.0f;

    SoundTest_ConfigurePlayingChannel(SOUND_CHANNEL_PLAYING, 1.0f);

    SoundTest_AdvanceTime(25);
    TEST_ASSERT_TRUE(Sound_GetChannelPlayProgress(SOUND_TEST_CHANNEL_HANDLE, &progress));
    TEST_ASSERT_EQUAL_FLOAT(25.0f, progress);

    SoundTest_AdvanceTime(25);
    TEST_ASSERT_TRUE(Sound_GetChannelPlayProgress(SOUND_TEST_CHANNEL_HANDLE, &progress));
    TEST_ASSERT_EQUAL_FLOAT(50.0f, progress);

    SoundTest_AdvanceTime(49);
    TEST_ASSERT_TRUE(Sound_GetChannelPlayProgress(SOUND_TEST_CHANNEL_HANDLE, &progress));
    TEST_ASSERT_EQUAL_FLOAT(99.0f, progress);

    SoundTest_AdvanceTime(1);
    TEST_ASSERT_FALSE(Sound_GetChannelPlayProgress(SOUND_TEST_CHANNEL_HANDLE, &progress));
}

TEST(Sound, OutputDeviceStubsReportNoDevicesAndRefuseSwitching)
{
    char aId[16]   = "keep";
    char aName[16] = "keep";

    TEST_ASSERT_EQUAL_size_t(0u, SoundDriver_GetNumOutputDevices());
    TEST_ASSERT_EQUAL_INT(0, SoundDriver_GetOutputDevice(0, aId, sizeof(aId), aName, sizeof(aName)));
    TEST_ASSERT_EQUAL_STRING("keep", aId);
    TEST_ASSERT_EQUAL_STRING("keep", aName);

    TEST_ASSERT_EQUAL_INT(0, SoundDriver_SetOutputDevice("null"));
    TEST_ASSERT_EQUAL_INT(0, SoundDriver_SetOutputDevice(NULL));
    TEST_ASSERT_EQUAL_STRING("", SoundDriver_GetCurrentOutputDevice());
}

TEST(Sound, TestImportBankDropsStoredLipSyncPointer)
{
    SoundInfo expected;
    STD_ZEROMEM(&expected, sizeof(expected));
    expected.hSnd           = SOUND_TEST_IMPORTED_HANDLE;
    expected.bankNum        = SOUND_TEST_NORMAL_BANK;
    expected.filePathOffset = 0x11u;
    expected.nameOffset     = 0x22u;
    expected.dataOffset     = 0x33u;
    expected.sampleRate     = 8000u;
    expected.sempleBitSize  = 16u;
    expected.numChannels    = 1u;
    expected.dataSize       = 64u;
    expected.bCompressed    = 1u;
    expected.idx            = 7u;

    uint8_t aImage[SOUND_TEST_BANK_MAX_SIZE];
    size_t offset = 0;

    SoundTest_WriteU32LE(aImage + offset, 1u); // number of sound infos
    offset += 4;
    SoundTest_WriteU32LE(aImage + offset, SOUND_TEST_BANK_CACHE_SIZE);
    offset += 4;
    SoundTest_WriteBankRecord(aImage + offset, &expected, SOUND_TEST_LIPSYNC_POINTER);
    offset += SOUND_TEST_BANK_RECORD_SIZE;

    for ( size_t i = 0; i < SOUND_TEST_BANK_CACHE_SIZE; ++i )
    {
        aImage[offset + i] = (uint8_t)(0xA0u + i);
    }
    offset += SOUND_TEST_BANK_CACHE_SIZE;

    SoundTest_WriteU32LE(aImage + offset, 0u); // next handle entropy
    offset += 4;

    SoundTest_SetBankImage(aImage, offset);

    TEST_ASSERT_EQUAL_INT(1, Sound_ImportNormalBank(SoundTest_BankFileHandle()));

    SoundInfo* pImported = Sound_GetSoundInfo(SOUND_TEST_IMPORTED_HANDLE);
    TEST_ASSERT_NOT_NULL(pImported);
    TEST_ASSERT_NULL(pImported->pLipSyncData);
    SoundTest_AssertSoundInfoFields(&expected, pImported);

    // Old code restored 0xDEADBEEF as the pointer; Sound_Reset would free an untracked address.
    Sound_Reset(0);
}

TEST(Sound, TestExportBankWritesNoLipSyncPointer)
{
    // Load an empty bank first so the exported next-handle entropy is deterministic.
    uint8_t aEmptyImage[12];
    SoundTest_WriteU32LE(aEmptyImage + 0, 0u);
    SoundTest_WriteU32LE(aEmptyImage + 4, 0u);
    SoundTest_WriteU32LE(aEmptyImage + 8, SOUND_TEST_BANK_ENTROPY);
    SoundTest_SetBankImage(aEmptyImage, sizeof(aEmptyImage));
    TEST_ASSERT_EQUAL_INT(1, Sound_ImportNormalBank(SoundTest_BankFileHandle()));

    SoundInfo record = SoundTest_soundInfo;
    record.pLipSyncData = (uint8_t*)SoundTest_Malloc(4);
    TEST_ASSERT_NOT_NULL(record.pLipSyncData);
    record.pLipSyncData[0] = 0x5A;

    Sound_TestSetSoundBank(SOUND_TEST_NORMAL_BANK, &record, 1, SoundTest_aSoundCache, SOUND_TEST_BANK_CACHE_SIZE);

    uint8_t aExpected[SOUND_TEST_BANK_MAX_SIZE];
    size_t offset = 0;

    SoundTest_WriteU32LE(aExpected + offset, 1u); // number of sound infos
    offset += 4;
    SoundTest_WriteU32LE(aExpected + offset, SOUND_TEST_BANK_CACHE_SIZE);
    offset += 4;
    SoundTest_WriteBankRecord(aExpected + offset, &record, 0u);
    offset += SOUND_TEST_BANK_RECORD_SIZE;

    memcpy(aExpected + offset, SoundTest_aSoundCache, SOUND_TEST_BANK_CACHE_SIZE);
    offset += SOUND_TEST_BANK_CACHE_SIZE;

    SoundTest_WriteU32LE(aExpected + offset, SOUND_TEST_BANK_ENTROPY);
    offset += 4;

    SoundTest_ResetBankFile();
    TEST_ASSERT_EQUAL_INT(1, Sound_ExportNormalBank(SoundTest_BankFileHandle()));
    TEST_ASSERT_EQUAL_size_t(offset, SoundTest_bankFile.size);
    TEST_ASSERT_EQUAL_MEMORY(aExpected, SoundTest_bankFile.aData, offset);

    Sound_TestSetSoundBank(SOUND_TEST_NORMAL_BANK, NULL, 0, NULL, 0);
    SoundTest_Free(record.pLipSyncData);
}

TEST(Sound, TestBankRoundTripKeepsFields)
{
    SoundInfo record = SoundTest_soundInfo;
    record.hSnd         = SOUND_TEST_ROUNDTRIP_HANDLE;
    record.pLipSyncData = (uint8_t*)SoundTest_Malloc(4);
    TEST_ASSERT_NOT_NULL(record.pLipSyncData);
    record.pLipSyncData[0] = 0xA5;

    Sound_TestSetSoundBank(SOUND_TEST_NORMAL_BANK, &record, 1, SoundTest_aSoundCache, SOUND_TEST_BANK_CACHE_SIZE);

    SoundTest_ResetBankFile();
    TEST_ASSERT_EQUAL_INT(1, Sound_ExportNormalBank(SoundTest_BankFileHandle()));
    TEST_ASSERT_GREATER_THAN_size_t(0u, SoundTest_bankFile.size);

    // Detach the source record before importing so the import reset does not free test-owned memory.
    Sound_TestSetSoundBank(SOUND_TEST_NORMAL_BANK, NULL, 0, NULL, 0);
    SoundTest_bankFile.position = 0;

    TEST_ASSERT_EQUAL_INT(1, Sound_ImportNormalBank(SoundTest_BankFileHandle()));

    SoundInfo* pImported = Sound_GetSoundInfo(SOUND_TEST_ROUNDTRIP_HANDLE);
    TEST_ASSERT_NOT_NULL(pImported);
    TEST_ASSERT_NULL(pImported->pLipSyncData);
    SoundTest_AssertSoundInfoFields(&record, pImported);

    Sound_Reset(0);
    SoundTest_Free(record.pLipSyncData);
}

TEST_GROUP_RUNNER(Sound)
{
    RUN_TEST_CASE(Sound, TestFailures);
    RUN_TEST_CASE(Sound, TestUncompressedCache);
    RUN_TEST_CASE(Sound, TestPlaybackPosition);
    RUN_TEST_CASE(Sound, TestCompressedCache);
    RUN_TEST_CASE(Sound, TestAllocationFailures);
    RUN_TEST_CASE(Sound, TestLargeLipSyncBuffer);
    RUN_TEST_CASE(Sound, TestMouthLevelMacro);
    RUN_TEST_CASE(Sound, TestPlayAllChannelsBusy);
    RUN_TEST_CASE(Sound, TestLoadFindsBankedSound);
    RUN_TEST_CASE(Sound, TestGetSoundIndexInvalidHandle);
    RUN_TEST_CASE(Sound, TestNonLoopingFinishesAtLength);
    RUN_TEST_CASE(Sound, TestPitchScalesPlayPosition);
    RUN_TEST_CASE(Sound, TestLoopingSoundNeverFinishes);
    RUN_TEST_CASE(Sound, TestLayerPauseStopsPlayClock);
    RUN_TEST_CASE(Sound, TestChannelPauseStopsPlayClock);
    RUN_TEST_CASE(Sound, TestSoundStartedWhilePausedPlays);
    RUN_TEST_CASE(Sound, TestPlayAcrossPauseResume);
    RUN_TEST_CASE(Sound, TestPlayProgressUsesComputedPosition);
    RUN_TEST_CASE(Sound, OutputDeviceStubsReportNoDevicesAndRefuseSwitching);
    RUN_TEST_CASE(Sound, TestImportBankDropsStoredLipSyncPointer);
    RUN_TEST_CASE(Sound, TestExportBankWritesNoLipSyncPointer);
    RUN_TEST_CASE(Sound, TestBankRoundTripKeepsFields);
}
