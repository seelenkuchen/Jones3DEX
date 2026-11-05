// Added: M6.0c render capture replay tests. A capture written through the forwarding layer is
// replayed into a second capture and the two files must match byte for byte; malformed captures
// must fail cleanly. Expected files are produced by the forwarders on the Null backend, never by
// the replay code under test.

#include <unity_fixture.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <std/General/stdRenderCapture.h>
#include <std/General/stdRenderCaptureRecord.h>
#include <std/General/stdUtil.h>

#include <std/General/std3D.h>
#include <std/General/stdDisplay.h>
#include <std/General/stdDisplayCapture.h>
#include <std/General/stdRenderCaptureIds.h>
#include <std/General/stdRenderReplay.h>

#include "stdGeneralTest.h"

#define STDRENDERREPLAYTEST_INPUT  "stdRenderReplay_Input.rcap"
#define STDRENDERREPLAYTEST_OUTPUT "stdRenderReplay_Output.rcap"
#define STDRENDERREPLAYTEST_BAD    "stdRenderReplay_Bad.rcap"

static void StdRenderReplayTest_RemoveFiles(void)
{
    remove(STDRENDERREPLAYTEST_INPUT);
    remove(STDRENDERREPLAYTEST_OUTPUT);
    remove(STDRENDERREPLAYTEST_BAD);
}

static void StdRenderReplayTest_PutU16(uint8_t* pData, uint16_t value)
{
    pData[0] = (uint8_t)value;
    pData[1] = (uint8_t)(value >> 8);
}

static void StdRenderReplayTest_PutU32(uint8_t* pData, uint32_t value)
{
    pData[0] = (uint8_t)value;
    pData[1] = (uint8_t)(value >> 8);
    pData[2] = (uint8_t)(value >> 16);
    pData[3] = (uint8_t)(value >> 24);
}

static void StdRenderReplayTest_PutU64(uint8_t* pData, uint64_t value)
{
    StdRenderReplayTest_PutU32(pData, (uint32_t)value);
    StdRenderReplayTest_PutU32(pData + 4, (uint32_t)(value >> 32));
}

static size_t StdRenderReplayTest_WriteHeader(uint8_t* pData, const char aSignature[8], uint32_t version, uint32_t columnLength, const char* pColumn)
{
    memcpy(pData, aSignature, 8u);
    StdRenderReplayTest_PutU32(pData + 8, version);
    StdRenderReplayTest_PutU32(pData + 12, columnLength);
    if ( columnLength > 0u )
    {
        memcpy(pData + 16, pColumn, columnLength);
    }

    return 16u + columnLength;
}

static size_t StdRenderReplayTest_WriteRecordHeader(uint8_t* pData, uint16_t id, uint32_t payloadSize)
{
    StdRenderReplayTest_PutU16(pData, id);
    StdRenderReplayTest_PutU16(pData + 2, 0u);
    StdRenderReplayTest_PutU32(pData + 4, payloadSize);
    return 8u;
}

static void StdRenderReplayTest_WriteFile(const char* pPath, const uint8_t* pData, size_t size)
{
    FILE* pFile = fopen(pPath, "wb");

    TEST_ASSERT_NOT_NULL(pFile);
    TEST_ASSERT_EQUAL_size_t(size, fwrite(pData, 1u, size, pFile));
    TEST_ASSERT_EQUAL_INT(0, fclose(pFile));
}

static void StdRenderReplayTest_AssertReplayFails(const char* pPath)
{
    TEST_ASSERT_EQUAL_INT(1, stdRenderReplay_Run(pPath, NULL));
    TEST_ASSERT_FALSE(stdRenderCapture_IsOpen());
    remove(pPath);
}

static uint8_t* StdRenderReplayTest_ReadFile(const char* pPath, size_t* pSize)
{
    FILE* pFile = fopen(pPath, "rb");
    long size;
    uint8_t* pData;

    TEST_ASSERT_NOT_NULL(pFile);
    TEST_ASSERT_EQUAL_INT(0, fseek(pFile, 0, SEEK_END));
    size = ftell(pFile);
    TEST_ASSERT_TRUE(size >= 0);
    TEST_ASSERT_EQUAL_INT(0, fseek(pFile, 0, SEEK_SET));

    pData = (uint8_t*)malloc(size > 0 ? (size_t)size : 1u);
    TEST_ASSERT_NOT_NULL(pData);
    TEST_ASSERT_EQUAL_size_t((size_t)size, fread(pData, 1u, (size_t)size, pFile));
    TEST_ASSERT_EQUAL_INT(0, fclose(pFile));

    *pSize = (size_t)size;
    return pData;
}

static void StdRenderReplayTest_FillColorInfo32(ColorInfo* pColorInfo)
{
    STD_ZEROMEM(pColorInfo, sizeof(*pColorInfo));
    pColorInfo->colorMode     = STDCOLOR_RGB;
    pColorInfo->bpp           = 32u;
    pColorInfo->redBPP        = 8u;
    pColorInfo->greenBPP      = 8u;
    pColorInfo->blueBPP       = 8u;
    pColorInfo->redPosShift   = 16;
    pColorInfo->greenPosShift = 8;
    pColorInfo->bluePosShift  = 0;
}

static void StdRenderReplayTest_FillRaster(tRasterInfo* pRasterInfo, uint32_t width, uint32_t height)
{
    STD_ZEROMEM(pRasterInfo, sizeof(*pRasterInfo));
    pRasterInfo->width  = width;
    pRasterInfo->height = height;
    StdRenderReplayTest_FillColorInfo32(&pRasterInfo->colorInfo);
}

static void StdRenderReplayTest_FillColorInfo16(ColorInfo* pColorInfo)
{
    STD_ZEROMEM(pColorInfo, sizeof(*pColorInfo));
    pColorInfo->colorMode          = STDCOLOR_RGB;
    pColorInfo->bpp                = 16u;
    pColorInfo->redBPP             = 5u;
    pColorInfo->greenBPP           = 6u;
    pColorInfo->blueBPP            = 5u;
    pColorInfo->redPosShift        = 11;
    pColorInfo->greenPosShift      = 5;
    pColorInfo->bluePosShift       = 0;
    pColorInfo->redPosShiftRight   = 3;
    pColorInfo->greenPosShiftRight = 2;
    pColorInfo->bluePosShiftRight  = 3;
}

TEST_GROUP(stdRenderReplay);

TEST_SETUP(stdRenderReplay)
{
    StdGeneralTest_Startup();
    stdRenderCapture_ResetGlobals();
    std3D_ResetGlobals();
    stdDisplay_ResetGlobals();
    StdRenderReplayTest_RemoveFiles();
}

TEST_TEAR_DOWN(stdRenderReplay)
{
    stdDisplayCapture_Close();
    stdRenderCapture_ResetGlobals();
    std3D_Shutdown();
    stdDisplay_Shutdown();
    std3D_ResetGlobals();
    stdDisplay_ResetGlobals();
    StdRenderReplayTest_RemoveFiles();
    StdGeneralTest_Shutdown();
}

TEST(stdRenderReplay, TestRoundTripMatchesByteForByte)
{
    static StdVertexTL aVertices[3] = {
        {  1.0f,  2.0f, 4.0f, 0.5f, 0x11223344u, 0x55667788u,  0.25f, -0.5f },
        {  0.0f,  0.0f, 0.0f, 0.0f, 0xFFFFFFFFu, 0x00000000u,  0.0f,   0.0f },
        { -1.0f,  1.5f, 0.0f, 1.0f, 0x00FF00FFu, 0xDEADBEEFu,  1.0f,   2.0f },
    };
    static const uint16_t aIndices[3] = { 0u, 1u, 2u };
    static tSystemTexture texture;
    tRasterInfo raster0;
    tRasterInfo raster1;
    tVBuffer* apMipmaps[2];
    tVBuffer* pMip0;
    tVBuffer* pMip1;
    uint8_t* pInput = NULL;
    uint8_t* pOutput = NULL;
    size_t inputSize = 0u;
    size_t outputSize = 0u;
    uint32_t width;
    uint32_t height;
    void* pSurface = NULL;
    uint32_t lockWidth = 0u;
    uint32_t lockHeight = 0u;
    int32_t lockPitch = 0;
    const StdRect rect = { 0, 0, 4, 4 };

    StdRenderReplayTest_FillRaster(&raster0, 4u, 4u);
    StdRenderReplayTest_FillRaster(&raster1, 2u, 2u);
    STD_ZEROMEM(&texture, sizeof(texture));

    TEST_ASSERT_TRUE(stdDisplayCapture_Open(STDRENDERREPLAYTEST_INPUT, "null"));

    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Startup());
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Open(0u));
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_SetMode(0u, 0, 1u));
    TEST_ASSERT_EQUAL_INT(1, std3D_Startup());
    TEST_ASSERT_EQUAL_INT(1, std3D_Open(0u));

    pMip0 = stdDisplay_VBufferNew(&raster0, 0, 0);
    pMip1 = stdDisplay_VBufferNew(&raster1, 0, 0);
    TEST_ASSERT_NOT_NULL(pMip0);
    TEST_ASSERT_NOT_NULL(pMip1);

    TEST_ASSERT_EQUAL_INT(0, stdDisplay_VBufferFill(pMip0, 0x11223344u, &rect));
    // Added: change the pixels after the fill, so only the VBufferUnlock blob restores the
    // recorded content during the replay; a missing restore leaves the filled bytes behind.
    {
        size_t i;
        for ( i = 0; i < pMip0->rasterInfo.size; ++i )
        {
            pMip0->pPixels[i] = (uint8_t)(0x80u + i);
        }
    }
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferUnlock(pMip0));
    // Added: a same-format conversion returns its source, so the record names the existing handle.
    TEST_ASSERT_EQUAL_PTR(pMip0, stdDisplay_VBufferConvertColorFormat(&raster0.colorInfo, pMip0, 0, NULL));
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_VBufferFill(pMip1, 0xAABBCCDDu, NULL));
    {
        size_t i;
        for ( i = 0; i < pMip1->rasterInfo.size; ++i )
        {
            pMip1->pPixels[i] = (uint8_t)(0x40u + i);
        }
    }
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferUnlock(pMip1));

    apMipmaps[0] = pMip0;
    apMipmaps[1] = pMip1;
    std3D_AllocSystemTexture(&texture, apMipmaps, 2u, STDCOLOR_FORMAT_RGB);
    TEST_ASSERT_EQUAL_size_t(2u, texture.numMipLevels);
    std3D_AddToTextureCache(&texture, STDCOLOR_FORMAT_RGB);

    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_DrawRenderList(texture.pCachedTexture, STD3D_RS_ZWRITE_DISABLED, aVertices, 3u, (uint16_t*)aIndices, 3u);
    std3D_EndScene();

    std3D_EnableFog(1, 2.0f);
    std3D_SetFog(0.1f, 0.2f, 0.3f, 4.0f, 100.0f);

    // A frames-off window: the frame-class calls in it are forwarded but not recorded.
    stdRenderCapture_SetFramesEnabled(false);
    std3D_StartScene();
    stdDisplay_GetBackBufferSize(&width, &height);
    std3D_EndScene();
    stdRenderCapture_SetFramesEnabled(true);

    TEST_ASSERT_EQUAL_INT(0, stdDisplay_LockBackBuffer(&pSurface, &lockWidth, &lockHeight, &lockPitch));
    TEST_ASSERT_NOT_NULL(pSurface);
    memset(pSurface, 0x5A, (size_t)lockHeight * (size_t)lockPitch);
    stdDisplay_UnlockBackBuffer();

    std3D_ClearSystemTexture(&texture);
    stdDisplay_VBufferFree(pMip0);
    stdDisplay_VBufferFree(pMip1);

    stdDisplayCapture_Close();
    TEST_ASSERT_FALSE(stdRenderCapture_IsOpen());

    std3D_Shutdown();
    stdDisplay_Shutdown();
    std3D_ResetGlobals();
    stdDisplay_ResetGlobals();

    TEST_ASSERT_EQUAL_INT(0, stdRenderReplay_Run(STDRENDERREPLAYTEST_INPUT, STDRENDERREPLAYTEST_OUTPUT));
    TEST_ASSERT_FALSE(stdRenderCapture_IsOpen());

    pInput  = StdRenderReplayTest_ReadFile(STDRENDERREPLAYTEST_INPUT, &inputSize);
    pOutput = StdRenderReplayTest_ReadFile(STDRENDERREPLAYTEST_OUTPUT, &outputSize);

    TEST_ASSERT_EQUAL_size_t(inputSize, outputSize);
    TEST_ASSERT_EQUAL_MEMORY(pInput, pOutput, inputSize);

    free(pInput);
    free(pOutput);
}

TEST(stdRenderReplay, TestRoundTripAfterConvertFreesSource)
{
    tRasterInfo raster;
    ColorInfo colorInfo16;
    tVBuffer* pSrc;
    tVBuffer* pConverted;
    tVBuffer* pNext;
    tVBuffer* pAfter;
    uint8_t* pInput = NULL;
    uint8_t* pOutput = NULL;
    size_t inputSize = 0u;
    size_t outputSize = 0u;

    StdRenderReplayTest_FillRaster(&raster, 2u, 2u);
    StdRenderReplayTest_FillColorInfo16(&colorInfo16);

    TEST_ASSERT_TRUE(stdDisplayCapture_Open(STDRENDERREPLAYTEST_INPUT, "null"));
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Startup());
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Open(0u));

    pSrc = stdDisplay_VBufferNew(&raster, 0, 0);
    TEST_ASSERT_NOT_NULL(pSrc);
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_VBufferFill(pSrc, 0xAABBCCDDu, NULL));

    pConverted = stdDisplay_VBufferConvertColorFormat(&colorInfo16, pSrc, 0, NULL);
    TEST_ASSERT_NOT_NULL(pConverted);
    TEST_ASSERT_TRUE(pConverted != pSrc);

    pNext = stdDisplay_VBufferNew(&raster, 0, 0);
    pAfter = stdDisplay_VBufferNew(&raster, 0, 0);
    TEST_ASSERT_NOT_NULL(pNext);
    TEST_ASSERT_NOT_NULL(pAfter);

    stdDisplay_VBufferFree(pAfter);
    stdDisplay_VBufferFree(pNext);
    stdDisplay_VBufferFree(pConverted);

    stdDisplayCapture_Close();
    TEST_ASSERT_FALSE(stdRenderCapture_IsOpen());

    stdDisplay_Shutdown();
    stdDisplay_ResetGlobals();

    TEST_ASSERT_EQUAL_INT(0, stdRenderReplay_Run(STDRENDERREPLAYTEST_INPUT, STDRENDERREPLAYTEST_OUTPUT));
    TEST_ASSERT_FALSE(stdRenderCapture_IsOpen());

    pInput  = StdRenderReplayTest_ReadFile(STDRENDERREPLAYTEST_INPUT, &inputSize);
    pOutput = StdRenderReplayTest_ReadFile(STDRENDERREPLAYTEST_OUTPUT, &outputSize);

    TEST_ASSERT_EQUAL_size_t(inputSize, outputSize);
    TEST_ASSERT_EQUAL_MEMORY(pInput, pOutput, inputSize);

    free(pInput);
    free(pOutput);
}

TEST(stdRenderReplay, TestRoundTripReproducesSithMaterialRasterMutation)
{
    tRasterInfo raster;
    ColorInfo colorInfo16;
    ColorInfo colorInfo32;
    tVBuffer* pSrc;
    tVBuffer* pConverted;
    tVBuffer* pConvertedAgain;
    uint8_t* pInput = NULL;
    uint8_t* pOutput = NULL;
    size_t inputSize = 0u;
    size_t outputSize = 0u;

    StdRenderReplayTest_FillRaster(&raster, 2u, 2u);
    StdRenderReplayTest_FillColorInfo16(&raster.colorInfo);
    StdRenderReplayTest_FillColorInfo16(&colorInfo16);
    StdRenderReplayTest_FillColorInfo32(&colorInfo32);

    TEST_ASSERT_TRUE(stdDisplayCapture_Open(STDRENDERREPLAYTEST_INPUT, "null"));
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Startup());
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Open(0u));

    pSrc = stdDisplay_VBufferNew(&raster, 0, 0);
    TEST_ASSERT_NOT_NULL(pSrc);
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferLock(pSrc));
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_VBufferFill(pSrc, 0xAABBCCDDu, NULL));
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferUnlock(pSrc));
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferUnlock(pSrc));

    pConverted = stdDisplay_VBufferConvertColorFormat(&colorInfo32, pSrc, 0, NULL);
    TEST_ASSERT_NOT_NULL(pConverted);
    TEST_ASSERT_TRUE(pConverted != pSrc);

    // The sithMaterial pattern: change the converted buffer back to a 16-bit raster by hand.
    pConverted->rasterInfo.colorInfo = colorInfo16;
    pConverted->rasterInfo.rowSize   = 4u;
    pConverted->rasterInfo.size      = 8u;
    pConverted->rasterInfo.rowWidth  = 2u;

    TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferLock(pConverted));
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferUnlock(pConverted));

    pConvertedAgain = stdDisplay_VBufferConvertColorFormat(&colorInfo32, pConverted, 0, NULL);
    TEST_ASSERT_NOT_NULL(pConvertedAgain);
    TEST_ASSERT_TRUE(pConvertedAgain != pConverted);
    stdDisplay_VBufferFree(pConvertedAgain);


    stdDisplayCapture_Close();
    TEST_ASSERT_FALSE(stdRenderCapture_IsOpen());

    stdDisplay_Shutdown();
    stdDisplay_ResetGlobals();

    TEST_ASSERT_EQUAL_INT(0, stdRenderReplay_Run(STDRENDERREPLAYTEST_INPUT, STDRENDERREPLAYTEST_OUTPUT));
    TEST_ASSERT_FALSE(stdRenderCapture_IsOpen());

    pInput  = StdRenderReplayTest_ReadFile(STDRENDERREPLAYTEST_INPUT, &inputSize);
    pOutput = StdRenderReplayTest_ReadFile(STDRENDERREPLAYTEST_OUTPUT, &outputSize);

    TEST_ASSERT_EQUAL_size_t(inputSize, outputSize);
    TEST_ASSERT_EQUAL_MEMORY(pInput, pOutput, inputSize);

    free(pInput);
    free(pOutput);
}

TEST(stdRenderReplay, TestVBufferStateBeyondAllocation)
{
    static const char aSignature[8] = { 'J', '3', 'D', 'R', 'C', 'A', 'P', 0 };
    static const uint32_t aColorInfo32Fields[14] = { 1u, 32u, 8u, 8u, 8u, 16u, 8u, 0u, 0u, 0u, 0u, 0u, 0u, 0u };
    uint8_t aFile[256];
    size_t size = StdRenderReplayTest_WriteHeader(aFile, aSignature, 1u, 4u, "null");
    size_t payloadStart;
    size_t i;

    size += StdRenderReplayTest_WriteRecordHeader(&aFile[size], STDRENDERCAPTURE_ID_STDDISPLAY_VBUFFERNEW, 77u);
    payloadStart = size;
    aFile[size++] = 1u;
    StdRenderReplayTest_PutU32(&aFile[size], 2u);
    size += 4u;
    StdRenderReplayTest_PutU32(&aFile[size], 2u);
    size += 4u;
    for ( i = 0; i < 14u; ++i )
    {
        StdRenderReplayTest_PutU32(&aFile[size], aColorInfo32Fields[i]);
        size += 4u;
    }
    StdRenderReplayTest_PutU32(&aFile[size], 0u);
    size += 4u;
    StdRenderReplayTest_PutU32(&aFile[size], 0u);
    size += 4u;
    StdRenderReplayTest_PutU32(&aFile[size], 2u);
    size += 4u;
    TEST_ASSERT_EQUAL_size_t(77u, size - payloadStart);

    size += StdRenderReplayTest_WriteRecordHeader(&aFile[size], STDRENDERCAPTURE_RECORD_VBSTATE, 92u);
    payloadStart = size;
    StdRenderReplayTest_PutU32(&aFile[size], 2u);
    size += 4u;
    StdRenderReplayTest_PutU32(&aFile[size], 2u);
    size += 4u;
    StdRenderReplayTest_PutU32(&aFile[size], 4u);
    size += 4u;
    StdRenderReplayTest_PutU64(&aFile[size], 32u);
    size += 8u;
    StdRenderReplayTest_PutU64(&aFile[size], 8u);
    size += 8u;
    StdRenderReplayTest_PutU64(&aFile[size], 2u);
    size += 8u;
    for ( i = 0; i < 14u; ++i )
    {
        StdRenderReplayTest_PutU32(&aFile[size], aColorInfo32Fields[i]);
        size += 4u;
    }
    TEST_ASSERT_EQUAL_size_t(92u, size - payloadStart);

    StdRenderReplayTest_WriteFile(STDRENDERREPLAYTEST_BAD, aFile, size);
    StdRenderReplayTest_AssertReplayFails(STDRENDERREPLAYTEST_BAD);
}

TEST(stdRenderReplay, TestBadSignature)
{
    static const char aBadSignature[8] = { 'J', '3', 'D', 'R', 'C', 'A', 'X', 0 };
    uint8_t aFile[64];
    size_t size = StdRenderReplayTest_WriteHeader(aFile, aBadSignature, 1u, 4u, "null");

    StdRenderReplayTest_WriteFile(STDRENDERREPLAYTEST_BAD, aFile, size);
    StdRenderReplayTest_AssertReplayFails(STDRENDERREPLAYTEST_BAD);
}

TEST(stdRenderReplay, TestBadVersion)
{
    static const char aSignature[8] = { 'J', '3', 'D', 'R', 'C', 'A', 'P', 0 };
    uint8_t aFile[64];
    size_t size = StdRenderReplayTest_WriteHeader(aFile, aSignature, 2u, 4u, "null");

    StdRenderReplayTest_WriteFile(STDRENDERREPLAYTEST_BAD, aFile, size);
    StdRenderReplayTest_AssertReplayFails(STDRENDERREPLAYTEST_BAD);
}

TEST(stdRenderReplay, TestZeroColumnLength)
{
    static const char aSignature[8] = { 'J', '3', 'D', 'R', 'C', 'A', 'P', 0 };
    uint8_t aFile[64];
    size_t size = StdRenderReplayTest_WriteHeader(aFile, aSignature, 1u, 0u, "");

    StdRenderReplayTest_WriteFile(STDRENDERREPLAYTEST_BAD, aFile, size);
    StdRenderReplayTest_AssertReplayFails(STDRENDERREPLAYTEST_BAD);
}

TEST(stdRenderReplay, TestTruncatedRecordHeader)
{
    static const char aSignature[8] = { 'J', '3', 'D', 'R', 'C', 'A', 'P', 0 };
    uint8_t aFile[64];
    size_t size = StdRenderReplayTest_WriteHeader(aFile, aSignature, 1u, 4u, "null");

    StdRenderReplayTest_PutU16(&aFile[size], STDRENDERCAPTURE_ID_STD3D_STARTSCENE);
    size += 2u;
    StdRenderReplayTest_WriteFile(STDRENDERREPLAYTEST_BAD, aFile, size);
    StdRenderReplayTest_AssertReplayFails(STDRENDERREPLAYTEST_BAD);
}

TEST(stdRenderReplay, TestPayloadPastEndOfFile)
{
    static const char aSignature[8] = { 'J', '3', 'D', 'R', 'C', 'A', 'P', 0 };
    uint8_t aFile[64];
    size_t size = StdRenderReplayTest_WriteHeader(aFile, aSignature, 1u, 4u, "null");

    size += StdRenderReplayTest_WriteRecordHeader(&aFile[size], STDRENDERCAPTURE_ID_STD3D_STARTSCENE, 4u);
    aFile[size++] = 0u;
    aFile[size++] = 0u;
    StdRenderReplayTest_WriteFile(STDRENDERREPLAYTEST_BAD, aFile, size);
    StdRenderReplayTest_AssertReplayFails(STDRENDERREPLAYTEST_BAD);
}

TEST(stdRenderReplay, TestPayloadWithLeftoverBytes)
{
    static const char aSignature[8] = { 'J', '3', 'D', 'R', 'C', 'A', 'P', 0 };
    uint8_t aFile[64];
    size_t size = StdRenderReplayTest_WriteHeader(aFile, aSignature, 1u, 4u, "null");

    size += StdRenderReplayTest_WriteRecordHeader(&aFile[size], STDRENDERCAPTURE_ID_STD3D_SHUTDOWN, 1u);
    aFile[size++] = 0u;
    StdRenderReplayTest_WriteFile(STDRENDERREPLAYTEST_BAD, aFile, size);
    StdRenderReplayTest_AssertReplayFails(STDRENDERREPLAYTEST_BAD);
}

TEST(stdRenderReplay, TestVertexCountLargerThanPayload)
{
    static const char aSignature[8] = { 'J', '3', 'D', 'R', 'C', 'A', 'P', 0 };
    uint8_t aFile[128];
    size_t size = StdRenderReplayTest_WriteHeader(aFile, aSignature, 1u, 4u, "null");
    size_t i;

    size += StdRenderReplayTest_WriteRecordHeader(
        &aFile[size],
        STDRENDERCAPTURE_ID_STD3D_DRAWLINESTRIP,
        4u + (uint32_t)sizeof(StdVertexTL)
    );
    StdRenderReplayTest_PutU32(&aFile[size], 2u);
    size += 4u;
    for ( i = 0; i < sizeof(StdVertexTL); ++i )
    {
        aFile[size++] = 0u;
    }

    StdRenderReplayTest_WriteFile(STDRENDERREPLAYTEST_BAD, aFile, size);
    StdRenderReplayTest_AssertReplayFails(STDRENDERREPLAYTEST_BAD);
}

TEST(stdRenderReplay, TestUnknownVBufferHandle)
{
    static const char aSignature[8] = { 'J', '3', 'D', 'R', 'C', 'A', 'P', 0 };
    uint8_t aFile[64];
    size_t size = StdRenderReplayTest_WriteHeader(aFile, aSignature, 1u, 4u, "null");

    size += StdRenderReplayTest_WriteRecordHeader(&aFile[size], STDRENDERCAPTURE_ID_STDDISPLAY_VBUFFERLOCK, 8u);
    StdRenderReplayTest_PutU32(&aFile[size], 99u);
    size += 4u;
    StdRenderReplayTest_PutU32(&aFile[size], 1u);
    size += 4u;
    StdRenderReplayTest_WriteFile(STDRENDERREPLAYTEST_BAD, aFile, size);
    StdRenderReplayTest_AssertReplayFails(STDRENDERREPLAYTEST_BAD);
}

TEST(stdRenderReplay, TestUnknownBlobId)
{
    static const char aSignature[8] = { 'J', '3', 'D', 'R', 'C', 'A', 'P', 0 };
    uint8_t aFile[64];
    size_t size = StdRenderReplayTest_WriteHeader(aFile, aSignature, 1u, 4u, "null");

    size += StdRenderReplayTest_WriteRecordHeader(&aFile[size], STDRENDERCAPTURE_ID_STDDISPLAY_VBUFFERUNLOCK, 12u);
    StdRenderReplayTest_PutU32(&aFile[size], 1u);
    size += 4u;
    StdRenderReplayTest_PutU32(&aFile[size], 5u);
    size += 4u;
    StdRenderReplayTest_PutU32(&aFile[size], 1u);
    size += 4u;
    StdRenderReplayTest_WriteFile(STDRENDERREPLAYTEST_BAD, aFile, size);
    StdRenderReplayTest_AssertReplayFails(STDRENDERREPLAYTEST_BAD);
}

TEST(stdRenderReplay, TestBlobIdsOutOfOrder)
{
    static const char aSignature[8] = { 'J', '3', 'D', 'R', 'C', 'A', 'P', 0 };
    uint8_t aFile[64];
    size_t size = StdRenderReplayTest_WriteHeader(aFile, aSignature, 1u, 4u, "null");

    size += StdRenderReplayTest_WriteRecordHeader(&aFile[size], STDRENDERCAPTURE_RECORD_BLOB, 12u);
    StdRenderReplayTest_PutU32(&aFile[size], 2u);
    size += 4u;
    StdRenderReplayTest_PutU64(&aFile[size], 0u);
    size += 8u;
    StdRenderReplayTest_WriteFile(STDRENDERREPLAYTEST_BAD, aFile, size);
    StdRenderReplayTest_AssertReplayFails(STDRENDERREPLAYTEST_BAD);
}

TEST(stdRenderReplay, TestFramesValueOutOfRange)
{
    static const char aSignature[8] = { 'J', '3', 'D', 'R', 'C', 'A', 'P', 0 };
    uint8_t aFile[64];
    size_t size = StdRenderReplayTest_WriteHeader(aFile, aSignature, 1u, 4u, "null");

    size += StdRenderReplayTest_WriteRecordHeader(&aFile[size], STDRENDERCAPTURE_RECORD_FRAMES, 1u);
    aFile[size++] = 2u;
    StdRenderReplayTest_WriteFile(STDRENDERREPLAYTEST_BAD, aFile, size);
    StdRenderReplayTest_AssertReplayFails(STDRENDERREPLAYTEST_BAD);
}

TEST(stdRenderReplay, TestUnknownRecordIdIsSkipped)
{
    static const char aSignature[8] = { 'J', '3', 'D', 'R', 'C', 'A', 'P', 0 };
    uint8_t aFile[64];
    size_t size = StdRenderReplayTest_WriteHeader(aFile, aSignature, 1u, 4u, "null");

    size += StdRenderReplayTest_WriteRecordHeader(&aFile[size], 999u, 3u);
    aFile[size++] = 1u;
    aFile[size++] = 2u;
    aFile[size++] = 3u;

    StdRenderReplayTest_WriteFile(STDRENDERREPLAYTEST_BAD, aFile, size);
    TEST_ASSERT_EQUAL_INT(0, stdRenderReplay_Run(STDRENDERREPLAYTEST_BAD, NULL));
    TEST_ASSERT_FALSE(stdRenderCapture_IsOpen());
    remove(STDRENDERREPLAYTEST_BAD);
}

TEST(stdRenderReplay, TestNullInputPath)
{
    TEST_ASSERT_EQUAL_INT(2, stdRenderReplay_Run(NULL, NULL));
    TEST_ASSERT_FALSE(stdRenderCapture_IsOpen());
}

TEST(stdRenderReplay, TestMissingFile)
{
    TEST_ASSERT_EQUAL_INT(1, stdRenderReplay_Run("stdRenderReplay_DoesNotExist.rcap", NULL));
    TEST_ASSERT_FALSE(stdRenderCapture_IsOpen());
}

TEST_GROUP_RUNNER(stdRenderReplay)
{
    RUN_TEST_CASE(stdRenderReplay, TestRoundTripMatchesByteForByte);
    RUN_TEST_CASE(stdRenderReplay, TestRoundTripAfterConvertFreesSource);
    RUN_TEST_CASE(stdRenderReplay, TestRoundTripReproducesSithMaterialRasterMutation);
    RUN_TEST_CASE(stdRenderReplay, TestVBufferStateBeyondAllocation);
    RUN_TEST_CASE(stdRenderReplay, TestBadSignature);
    RUN_TEST_CASE(stdRenderReplay, TestBadVersion);
    RUN_TEST_CASE(stdRenderReplay, TestZeroColumnLength);
    RUN_TEST_CASE(stdRenderReplay, TestTruncatedRecordHeader);
    RUN_TEST_CASE(stdRenderReplay, TestPayloadPastEndOfFile);
    RUN_TEST_CASE(stdRenderReplay, TestPayloadWithLeftoverBytes);
    RUN_TEST_CASE(stdRenderReplay, TestVertexCountLargerThanPayload);
    RUN_TEST_CASE(stdRenderReplay, TestUnknownVBufferHandle);
    RUN_TEST_CASE(stdRenderReplay, TestUnknownBlobId);
    RUN_TEST_CASE(stdRenderReplay, TestBlobIdsOutOfOrder);
    RUN_TEST_CASE(stdRenderReplay, TestFramesValueOutOfRange);
    RUN_TEST_CASE(stdRenderReplay, TestUnknownRecordIdIsSkipped);
    RUN_TEST_CASE(stdRenderReplay, TestNullInputPath);
    RUN_TEST_CASE(stdRenderReplay, TestMissingFile);
}
